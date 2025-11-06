package benchmarks

import (
	"encoding/csv"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"lsrp_test/http"

	"github.com/Pavelavl/go-lsrp"
)

const (
	DefaultRequests    = 10000
	DefaultConcurrency = 1
	DefaultDuration    = 60 * time.Second
	MetricsInterval    = 1 * time.Second
	WarmupRequests     = 50
	WarmupDelay        = 5 * time.Second
)

type BenchmarkConfig struct {
	Requests      int
	Concurrency   int
	Duration      time.Duration
	RRDFile       string
	OutputDir     string
	LogDir        string
	ConfigFile    string
	UseRRDCached  bool
	MetricsFile   string
	config        Config
	collectorCmd  *exec.Cmd
	collectorStop chan struct{}
}

type Config struct {
	Server struct {
		TCPPort       int    `json:"tcp_port"`
		AllowedIps    string `json:"allowed_ips"`
		RRDCachedAddr string `json:"rrdcached_addr"`
	} `json:"server"`
	RRD struct {
		BasePath string `json:"base_path"`
	} `json:"rrd"`
	JS struct {
		ScriptPath string `json:"script_path"`
	} `json:"js"`
	Metrics []struct {
		Endpoint         string `json:"endpoint"`
		RrdPath          string `json:"rrd_path"`
		RequiresParam    bool   `json:"requires_param"`
		Title            string `json:"title"`
		YLabel           string `json:"y_label"`
		IsPercentage     bool   `json:"is_percentage"`
		ValueFormat      string `json:"value_format"`
		ParamName        string `json:"param_name,omitempty"`
		TransformType    string `json:"transform_type,omitempty"`
		TransformDivisor int    `json:"transform_divisor,omitempty"`
	} `json:"metrics"`
}

type RequestResult struct {
	RequestNum int
	Latency    time.Duration
	Success    bool
	Error      error
}

type BenchmarkResult struct {
	TestName      string
	TotalRequests int
	SuccessCount  int
	FailCount     int
	SuccessRate   float64
	AvgLatency    time.Duration
	MedianLatency time.Duration
	MinLatency    time.Duration
	MaxLatency    time.Duration
	P95Latency    time.Duration
	P99Latency    time.Duration
	ThroughputRPS float64
	Duration      time.Duration
	Latencies     []time.Duration
}

type MetricsSummary struct {
	CPUAvg                 float64
	CPUMedian              float64
	CPUMax                 float64
	MemAvgMB               float64
	MemMedianMB            float64
	MemMaxMB               float64
	IOReadMB               float64
	IOWriteMB              float64
	IOReadOpsPS            float64
	IOWriteOpsPS           float64
	ThreadsAvg             float64
	ThreadsMax             int
	FDsAvg                 float64
	FDsMax                 int
	CtxSwitchVoluntaryPS   float64
	CtxSwitchInvoluntaryPS float64
	PageFaultsMinorPS      float64
	PageFaultsMajorPS      float64
}

func NewBenchmarkConfig() *BenchmarkConfig {
	return &BenchmarkConfig{
		Requests:    DefaultRequests,
		Concurrency: DefaultConcurrency,
		Duration:    DefaultDuration,
		RRDFile:     "/opt/collectd/var/lib/collectd/rrd/localhost/cpu-total/percent-active.rrd",
		OutputDir:   "results",
		LogDir:      "logs",
		ConfigFile:  "../../config.json",
	}
}

func (bc *BenchmarkConfig) LoadConfig() error {
	data, err := os.ReadFile(bc.ConfigFile)
	if err != nil {
		return fmt.Errorf("failed to read config: %w", err)
	}

	if err := json.Unmarshal(data, &bc.config); err != nil {
		return fmt.Errorf("failed to parse config: %w", err)
	}

	return nil
}

func (bc *BenchmarkConfig) UpdateConfig() error {
	if bc.UseRRDCached {
		bc.config.Server.RRDCachedAddr = "unix:/var/run/rrdcached.sock"
	} else {
		bc.config.Server.RRDCachedAddr = ""
	}

	data, err := json.MarshalIndent(bc.config, "", "\t")
	if err != nil {
		return fmt.Errorf("failed to marshal config: %w", err)
	}

	return os.WriteFile(bc.ConfigFile, data, 0644)
}

func (bc *BenchmarkConfig) StartDaemon(bin string) (*exec.Cmd, error) {
	logFile := filepath.Join(bc.LogDir, "daemon.log")
	log, err := os.Create(logFile)
	if err != nil {
		return nil, fmt.Errorf("failed to create log file: %w", err)
	}

	cmd := exec.Command(bin, bc.ConfigFile)
	cmd.Stdout = log
	cmd.Stderr = log

	if err := cmd.Start(); err != nil {
		log.Close()
		return nil, fmt.Errorf("failed to start daemon: %w", err)
	}

	fmt.Printf("Daemon starting with PID %d...\n", cmd.Process.Pid)
	time.Sleep(2 * time.Second)

	if _, err := os.Stat(fmt.Sprintf("/proc/%d", cmd.Process.Pid)); os.IsNotExist(err) {
		return nil, fmt.Errorf("daemon process not found after start")
	}

	maxRetries := 10
	retryDelay := 500 * time.Millisecond
	for i := 0; i < maxRetries; i++ {
		testClient, err := lsrp.NewClient("localhost", bc.config.Server.TCPPort)
		if err == nil {
			testClient.Close()
			fmt.Printf("Daemon ready on port %d (attempt %d/%d)\n", bc.config.Server.TCPPort, i+1, maxRetries)
			return cmd, nil
		}
		if i < maxRetries-1 {
			time.Sleep(retryDelay)
		}
	}

	if err := cmd.Process.Kill(); err != nil {
		fmt.Printf("Warning: failed to kill daemon: %v\n", err)
	}
	return nil, fmt.Errorf("daemon not accepting connections on port %d after %d attempts", bc.config.Server.TCPPort, maxRetries)
}

func (bc *BenchmarkConfig) StartMetricsCollector(daemonPID int) error {
	bc.collectorStop = make(chan struct{})

	collectorPath := "../bin/metrics_collector"
	if _, err := os.Stat(collectorPath); os.IsNotExist(err) {
		fmt.Println("Building metrics_collector...")
		buildCmd := exec.Command("gcc", "-o", collectorPath, "/home/claude/metrics_collector.c", "-pthread")
		if output, err := buildCmd.CombinedOutput(); err != nil {
			return fmt.Errorf("failed to build metrics_collector: %w\n%s", err, output)
		}
	}

	if _, err := os.Stat(fmt.Sprintf("/proc/%d", daemonPID)); os.IsNotExist(err) {
		return fmt.Errorf("target process %d does not exist", daemonPID)
	}

	cmd := exec.Command(collectorPath,
		strconv.Itoa(daemonPID),
		bc.MetricsFile,
		"1")

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start metrics collector: %w", err)
	}

	bc.collectorCmd = cmd
	fmt.Printf("Metrics collector started with PID %d\n", cmd.Process.Pid)

	time.Sleep(1500 * time.Millisecond)

	if _, err := os.Stat(bc.MetricsFile); os.IsNotExist(err) {
		return fmt.Errorf("metrics file not created after 1.5s")
	}

	return nil
}

func (bc *BenchmarkConfig) StopMetricsCollector() error {
	if bc.collectorCmd != nil && bc.collectorCmd.Process != nil {
		if err := bc.collectorCmd.Process.Signal(os.Interrupt); err != nil {
			fmt.Printf("Warning: failed to send interrupt signal: %v\n", err)
		}
		time.Sleep(2 * time.Second)

		if _, err := os.Stat(fmt.Sprintf("/proc/%d", bc.collectorCmd.Process.Pid)); err == nil {
			if err := bc.collectorCmd.Process.Kill(); err != nil {
				fmt.Printf("Warning: failed to kill collector: %v\n", err)
			}
		}
	}
	return nil
}

func (bc *BenchmarkConfig) RunLoadTest(client any, p proto) (*BenchmarkResult, error) {
	results := make(chan RequestResult, bc.Requests)
	var wg sync.WaitGroup

	startTime := time.Now()

	sem := make(chan struct{}, bc.Concurrency)

	for i := 0; i < bc.Requests; i++ {
		wg.Add(1)
		sem <- struct{}{}

		go func(reqNum int) {
			defer wg.Done()
			defer func() { <-sem }()

			reqStart := time.Now()
			var (
				resp any
				err  error
			)
			if p == protoLSRP {
				resp, err = client.(*lsrp.Client).Send("endpoint=cpu&period=3600")
			} else {
				resp, err = client.(*http.Client).Send("cpu?period=3600")
			}
			latency := time.Since(reqStart)

			success := false
			if p == protoLSRP {
				if err == nil && resp.(*lsrp.Response).Status == 0 {
					success = true
				}
			} else {
				if err == nil && resp.(*http.Response).Status == 0 {
					success = true
				}
			}

			results <- RequestResult{
				RequestNum: reqNum,
				Latency:    latency,
				Success:    success,
				Error:      err,
			}
		}(i)
	}

	wg.Wait()
	close(results)

	duration := time.Since(startTime)

	return analyzeResults(results, duration)
}

func analyzeResults(results chan RequestResult, duration time.Duration) (*BenchmarkResult, error) {
	br := &BenchmarkResult{
		Duration:   duration,
		MinLatency: time.Duration(math.MaxInt64),
	}

	latencies := make([]time.Duration, 0)

	for result := range results {
		br.TotalRequests++

		if result.Success {
			br.SuccessCount++
			latencies = append(latencies, result.Latency)

			if result.Latency < br.MinLatency {
				br.MinLatency = result.Latency
			}
			if result.Latency > br.MaxLatency {
				br.MaxLatency = result.Latency
			}
		} else {
			br.FailCount++
		}
	}

	if br.SuccessCount == 0 {
		return br, fmt.Errorf("no successful requests")
	}

	br.SuccessRate = float64(br.SuccessCount) / float64(br.TotalRequests) * 100
	br.ThroughputRPS = float64(br.SuccessCount) / duration.Seconds()
	br.Latencies = latencies

	sort.Slice(latencies, func(i, j int) bool {
		return latencies[i] < latencies[j]
	})

	var totalLatency time.Duration
	for _, l := range latencies {
		totalLatency += l
	}
	br.AvgLatency = totalLatency / time.Duration(len(latencies))
	br.MedianLatency = latencies[len(latencies)/2]

	if len(latencies) > 0 {
		p95Idx := int(float64(len(latencies)) * 0.95)
		p99Idx := int(float64(len(latencies)) * 0.99)
		if p95Idx >= len(latencies) {
			p95Idx = len(latencies) - 1
		}
		if p99Idx >= len(latencies) {
			p99Idx = len(latencies) - 1
		}
		br.P95Latency = latencies[p95Idx]
		br.P99Latency = latencies[p99Idx]
	}

	return br, nil
}

func (bc *BenchmarkConfig) AnalyzeMetrics() (*MetricsSummary, error) {
	file, err := os.Open(bc.MetricsFile)
	if err != nil {
		return nil, fmt.Errorf("failed to open metrics file: %w", err)
	}
	defer file.Close()

	reader := csv.NewReader(file)
	records, err := reader.ReadAll()
	if err != nil {
		return nil, fmt.Errorf("failed to read metrics: %w", err)
	}

	if len(records) < 2 {
		return nil, fmt.Errorf("insufficient metrics data: only %d records", len(records))
	}

	records = records[1:]

	summary := &MetricsSummary{}
	cpuValues := make([]float64, 0)
	memValues := make([]float64, 0)
	threadValues := make([]int, 0)
	fdValues := make([]int, 0)

	var firstIORead, lastIORead uint64
	var firstIOWrite, lastIOWrite uint64
	var firstIOReadOps, lastIOReadOps uint64
	var firstIOWriteOps, lastIOWriteOps uint64
	var firstCtxVoluntary, lastCtxVoluntary uint64
	var firstCtxInvoluntary, lastCtxInvoluntary uint64
	var firstPageFaultsMinor, lastPageFaultsMinor uint64
	var firstPageFaultsMajor, lastPageFaultsMajor uint64

	for i, record := range records {
		if len(record) < 16 {
			continue
		}

		cpu, _ := strconv.ParseFloat(record[1], 64)
		memRSS, _ := strconv.ParseUint(record[4], 10, 64)
		ioReadBytes, _ := strconv.ParseUint(record[6], 10, 64)
		ioWriteBytes, _ := strconv.ParseUint(record[7], 10, 64)
		ioReadOps, _ := strconv.ParseUint(record[8], 10, 64)
		ioWriteOps, _ := strconv.ParseUint(record[9], 10, 64)
		threads, _ := strconv.Atoi(record[10])
		fds, _ := strconv.Atoi(record[11])
		ctxVoluntary, _ := strconv.ParseUint(record[12], 10, 64)
		ctxInvoluntary, _ := strconv.ParseUint(record[13], 10, 64)
		pageFaultsMinor, _ := strconv.ParseUint(record[14], 10, 64)
		pageFaultsMajor, _ := strconv.ParseUint(record[15], 10, 64)

		cpuValues = append(cpuValues, cpu)
		if cpu > summary.CPUMax {
			summary.CPUMax = cpu
		}

		memMB := float64(memRSS) / 1024.0
		memValues = append(memValues, memMB)
		if memMB > summary.MemMaxMB {
			summary.MemMaxMB = memMB
		}

		threadValues = append(threadValues, threads)
		if threads > summary.ThreadsMax {
			summary.ThreadsMax = threads
		}

		fdValues = append(fdValues, fds)
		if fds > summary.FDsMax {
			summary.FDsMax = fds
		}

		if i == 0 {
			firstIORead = ioReadBytes
			firstIOWrite = ioWriteBytes
			firstIOReadOps = ioReadOps
			firstIOWriteOps = ioWriteOps
			firstCtxVoluntary = ctxVoluntary
			firstCtxInvoluntary = ctxInvoluntary
			firstPageFaultsMinor = pageFaultsMinor
			firstPageFaultsMajor = pageFaultsMajor
		}
		lastIORead = ioReadBytes
		lastIOWrite = ioWriteBytes
		lastIOReadOps = ioReadOps
		lastIOWriteOps = ioWriteOps
		lastCtxVoluntary = ctxVoluntary
		lastCtxInvoluntary = ctxInvoluntary
		lastPageFaultsMinor = pageFaultsMinor
		lastPageFaultsMajor = pageFaultsMajor
	}

	if len(cpuValues) > 0 {
		for _, v := range cpuValues {
			summary.CPUAvg += v
		}
		summary.CPUAvg /= float64(len(cpuValues))

		sort.Float64s(cpuValues)
		summary.CPUMedian = cpuValues[len(cpuValues)/2]
	}

	if len(memValues) > 0 {
		for _, v := range memValues {
			summary.MemAvgMB += v
		}
		summary.MemAvgMB /= float64(len(memValues))

		sort.Float64s(memValues)
		summary.MemMedianMB = memValues[len(memValues)/2]
	}

	if len(threadValues) > 0 {
		var total int
		for _, v := range threadValues {
			total += v
		}
		summary.ThreadsAvg = float64(total) / float64(len(threadValues))
	}

	if len(fdValues) > 0 {
		var total int
		for _, v := range fdValues {
			total += v
		}
		summary.FDsAvg = float64(total) / float64(len(fdValues))
	}

	summary.IOReadMB = float64(lastIORead-firstIORead) / (1024 * 1024)
	summary.IOWriteMB = float64(lastIOWrite-firstIOWrite) / (1024 * 1024)

	duration := float64(len(records))
	if duration > 0 {
		summary.IOReadOpsPS = float64(lastIOReadOps-firstIOReadOps) / duration
		summary.IOWriteOpsPS = float64(lastIOWriteOps-firstIOWriteOps) / duration
		summary.CtxSwitchVoluntaryPS = float64(lastCtxVoluntary-firstCtxVoluntary) / duration
		summary.CtxSwitchInvoluntaryPS = float64(lastCtxInvoluntary-firstCtxInvoluntary) / duration
		summary.PageFaultsMinorPS = float64(lastPageFaultsMinor-firstPageFaultsMinor) / duration
		summary.PageFaultsMajorPS = float64(lastPageFaultsMajor-firstPageFaultsMajor) / duration
	}

	return summary, nil
}

func printResults(testName string, br *BenchmarkResult, ms *MetricsSummary) {
	fmt.Println("\n" + strings.Repeat("=", 70))
	fmt.Printf("BENCHMARK RESULTS: %s\n", testName)
	fmt.Println(strings.Repeat("=", 70))

	fmt.Println("\n📊 Request Statistics:")
	fmt.Printf("  Total Requests:    %d\n", br.TotalRequests)
	fmt.Printf("  Successful:        %d (%.2f%%)\n", br.SuccessCount, br.SuccessRate)
	fmt.Printf("  Failed:            %d\n", br.FailCount)
	fmt.Printf("  Duration:          %v\n", br.Duration)
	fmt.Printf("  Throughput:        %.2f req/s\n", br.ThroughputRPS)

	fmt.Println("\n⏱️  Latency Statistics:")
	fmt.Printf("  Average:           %v\n", br.AvgLatency)
	fmt.Printf("  Median:            %v\n", br.MedianLatency)
	fmt.Printf("  Min:               %v\n", br.MinLatency)
	fmt.Printf("  Max:               %v\n", br.MaxLatency)
	fmt.Printf("  P95:               %v\n", br.P95Latency)
	fmt.Printf("  P99:               %v\n", br.P99Latency)

	if ms != nil {
		fmt.Println("\n💻 CPU Statistics:")
		fmt.Printf("  Average:           %.2f%%\n", ms.CPUAvg)
		fmt.Printf("  Median:            %.2f%%\n", ms.CPUMedian)
		fmt.Printf("  Max:               %.2f%%\n", ms.CPUMax)

		fmt.Println("\n🧠 Memory Statistics:")
		fmt.Printf("  Average:           %.2f MB\n", ms.MemAvgMB)
		fmt.Printf("  Median:            %.2f MB\n", ms.MemMedianMB)
		fmt.Printf("  Max:               %.2f MB\n", ms.MemMaxMB)

		fmt.Println("\n💾 I/O Statistics:")
		fmt.Printf("  Read:              %.2f MB\n", ms.IOReadMB)
		fmt.Printf("  Write:             %.2f MB\n", ms.IOWriteMB)
		fmt.Printf("  Read Ops/s:        %.2f\n", ms.IOReadOpsPS)
		fmt.Printf("  Write Ops/s:       %.2f\n", ms.IOWriteOpsPS)

		fmt.Println("\n🔄 Context Switches:")
		fmt.Printf("  Voluntary/s:       %.2f\n", ms.CtxSwitchVoluntaryPS)
		fmt.Printf("  Involuntary/s:     %.2f\n", ms.CtxSwitchInvoluntaryPS)

		fmt.Println("\n📄 Page Faults:")
		fmt.Printf("  Minor/s:           %.2f\n", ms.PageFaultsMinorPS)
		fmt.Printf("  Major/s:           %.2f\n", ms.PageFaultsMajorPS)

		fmt.Println("\n🔧 Process Statistics:")
		fmt.Printf("  Threads (avg):     %.1f\n", ms.ThreadsAvg)
		fmt.Printf("  Threads (max):     %d\n", ms.ThreadsMax)
		fmt.Printf("  FDs (avg):         %.1f\n", ms.FDsAvg)
		fmt.Printf("  FDs (max):         %d\n", ms.FDsMax)
	}

	fmt.Println(strings.Repeat("=", 70))
}

type proto string

const (
	protoLSRP proto = "lsrp"
	protoHTTP proto = "http"
)

type ComparisonRow struct {
	TestName   string
	Protocol   string
	RRDCached  bool
	Throughput float64
	AvgLatency time.Duration
	P95Latency time.Duration
	CPUAvg     float64
	MemAvgMB   float64
	IOReadMB   float64
	IOWriteMB  float64
}

func printBenchmarkComparisonTable(rows []*ComparisonRow) {
	fmt.Println("\n" + strings.Repeat("=", 150))
	fmt.Println("# BENCHMARK COMPARISON TABLE")
	fmt.Println(strings.Repeat("=", 150))
	fmt.Println()
	fmt.Println("| Protocol | RRDCached | Throughput (req/s) | Avg Latency | P95 Latency | CPU Avg (%) | Mem Avg (MB) | IO Read (MB) | IO Write (MB) |")
	fmt.Println("|----------|-----------|-------------------|-------------|-------------|-------------|--------------|--------------|---------------|")

	for _, row := range rows {
		cached := "No"
		if row.RRDCached {
			cached = "Yes"
		}
		fmt.Printf("| %-8s | %-9s | %17.2f | %11v | %11v | %11.2f | %12.2f | %12.2f | %13.2f |\n",
			row.Protocol,
			cached,
			row.Throughput,
			row.AvgLatency,
			row.P95Latency,
			row.CPUAvg,
			row.MemAvgMB,
			row.IOReadMB,
			row.IOWriteMB,
		)
	}

	fmt.Println()
	fmt.Println(strings.Repeat("=", 150))
}

func Test_BenchmarkDirectVsCached(t *testing.T) {
	// t.Skip()

	os.MkdirAll("results", 0755)
	os.MkdirAll("logs", 0755)

	tests := []struct {
		name             string
		usecache         bool
		metricFilePrefix string
		p                proto
	}{
		{
			name:             "direct LSRP",
			usecache:         false,
			metricFilePrefix: "direct",
			p:                protoLSRP,
		},
		{
			name:             "rrdcached LSRP",
			usecache:         true,
			metricFilePrefix: "cached",
			p:                protoLSRP,
		},
		{
			name:             "direct HTTP",
			usecache:         false,
			metricFilePrefix: "direct",
			p:                protoHTTP,
		},
		{
			name:             "rrdcached HTTP",
			usecache:         true,
			metricFilePrefix: "cached",
			p:                protoHTTP,
		},
	}

	var comparisonRows []*ComparisonRow

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			bc := NewBenchmarkConfig()
			bc.UseRRDCached = tt.usecache
			bc.MetricsFile = filepath.Join(bc.OutputDir, fmt.Sprintf("%s_%s_metrics.csv", tt.metricFilePrefix, tt.p))

			if err := bc.LoadConfig(); err != nil {
				t.Fatalf("Failed to load config: %v", err)
			}

			bc.config.Server.TCPPort++

			if err := bc.UpdateConfig(); err != nil {
				t.Fatalf("Failed to update config: %v", err)
			}

			daemonCmd, err := bc.StartDaemon(fmt.Sprintf("../bin/%s", tt.p))
			if err != nil {
				t.Fatalf("Failed to start daemon: %v", err)
			}
			defer func() {
				if daemonCmd != nil && daemonCmd.Process != nil {
					if err := daemonCmd.Process.Kill(); err != nil {
						t.Logf("Warning: failed to kill daemon: %v", err)
					}
				}
			}()

			if err := bc.StartMetricsCollector(daemonCmd.Process.Pid); err != nil {
				t.Fatalf("Failed to start metrics collector: %v", err)
			}
			defer bc.StopMetricsCollector()

			var client any
			if tt.p == protoLSRP {
				c, err := lsrp.NewClient("localhost", bc.config.Server.TCPPort)
				if err != nil {
					t.Fatalf("Failed to create lsrp client: %v", err)
				}
				defer c.Close()
				client = c
			} else {
				c, err := http.NewClient("localhost", bc.config.Server.TCPPort)
				if err != nil {
					t.Fatalf("Failed to create http client: %v", err)
				}
				defer c.Close()
				client = c
			}

			fmt.Printf("Warming up with %d requests...\n", WarmupRequests)
			successCount := 0
			for i := 0; i < WarmupRequests; i++ {
				if tt.p == protoLSRP {
					if _, err = client.(*lsrp.Client).Send("endpoint=cpu&period=3600"); err == nil {
						successCount++
					}
				} else {
					if _, err = client.(*http.Client).Send("cpu?period=3600"); err == nil {
						successCount++
					}
				}
			}
			fmt.Printf("Warmup completed: %d/%d successful\n", successCount, WarmupRequests)

			if successCount < WarmupRequests/2 {
				t.Fatalf("Warmup failed: only %d/%d requests succeeded", successCount, WarmupRequests)
			}

			fmt.Printf("Waiting %v before starting test...\n", WarmupDelay)
			time.Sleep(WarmupDelay)

			fmt.Printf("\nRunning load test: %d requests, concurrency: %d\n", bc.Requests, bc.Concurrency)
			result, err := bc.RunLoadTest(client, tt.p)
			if err != nil {
				t.Fatalf("Load test failed: %v", err)
			}

			bc.StopMetricsCollector()
			time.Sleep(2 * time.Second)

			metrics, err := bc.AnalyzeMetrics()
			if err != nil {
				t.Logf("Warning: Failed to analyze metrics: %v", err)
			}

			result.TestName = tt.name
			printResults(result.TestName, result, metrics)

			row := &ComparisonRow{
				TestName:   tt.name,
				Protocol:   string(tt.p),
				RRDCached:  tt.usecache,
				Throughput: result.ThroughputRPS,
				AvgLatency: result.AvgLatency,
				P95Latency: result.P95Latency,
			}
			if metrics != nil {
				row.CPUAvg = metrics.CPUAvg
				row.MemAvgMB = metrics.MemAvgMB
				row.IOReadMB = metrics.IOReadMB
				row.IOWriteMB = metrics.IOWriteMB
			}
			comparisonRows = append(comparisonRows, row)
		})
	}

	printBenchmarkComparisonTable(comparisonRows)
}

type SavedResults struct {
	Benchmark *BenchmarkResult `json:"benchmark"`
	Metrics   *MetricsSummary  `json:"metrics"`
}

func Repeat(s string, count int) string {
	result := ""
	for i := 0; i < count; i++ {
		result += s
	}
	return result
}
