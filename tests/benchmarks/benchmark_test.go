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

	"github.com/Pavelavl/go-lsrp"
)

const (
	DefaultRequests    = 1000
	DefaultConcurrency = 10
	DefaultDuration    = 60 * time.Second
	MetricsInterval    = 1 * time.Second
)

type BenchmarkConfig struct {
	Requests      int
	Concurrency   int
	Duration      time.Duration
	Endpoint      string
	RRDFile       string
	OutputDir     string
	LogDir        string
	ConfigFile    string
	BinaryPath    string
	UseRRDCached  bool
	MetricsFile   string
	config        Config
	collectorCmd  *exec.Cmd
	collectorStop chan struct{}
}

type Config struct {
	Server struct {
		TcpPort       int    `json:"tcp_port"`
		AllowedIPs    string `json:"allowed_ips"`
		RRDCachedAddr string `json:"rrdcached_addr"`
	} `json:"server"`
	RRD struct {
		BasePath              string `json:"base_path"`
		CpuTotal              string `json:"cpu_total"`
		CpuProcess            string `json:"cpu_process"`
		RamTotal              string `json:"ram_total"`
		RamProcess            string `json:"ram_process"`
		Network               string `json:"network"`
		Disk                  string `json:"disk"`
		PostgresqlConnections string `json:"postgresql_connections"`
	} `json:"rrd"`
	JS struct {
		ScriptPath string `json:"script_path"`
	} `json:"js"`
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
	CPUAvg       float64
	CPUMedian    float64
	CPUMax       float64
	MemAvgMB     float64
	MemMedianMB  float64
	MemMaxMB     float64
	IOReadMB     float64
	IOWriteMB    float64
	IOReadOpsPS  float64
	IOWriteOpsPS float64
	ThreadsAvg   float64
	ThreadsMax   int
	FDsAvg       float64
	FDsMax       int
}

func NewBenchmarkConfig() *BenchmarkConfig {
	return &BenchmarkConfig{
		Requests:    DefaultRequests,
		Concurrency: DefaultConcurrency,
		Duration:    DefaultDuration,
		Endpoint:    "endpoint=cpu&period=3600",
		RRDFile:     "/opt/collectd/var/lib/collectd/rrd/localhost/cpu-total/percent-active.rrd",
		OutputDir:   "results",
		LogDir:      "logs",
		ConfigFile:  "../../config.json",
		BinaryPath:  "../bin/lsrp",
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

func (bc *BenchmarkConfig) StartDaemon() (*exec.Cmd, error) {
	logFile := filepath.Join(bc.LogDir, "daemon.log")
	log, err := os.Create(logFile)
	if err != nil {
		return nil, fmt.Errorf("failed to create log file: %w", err)
	}

	cmd := exec.Command(bc.BinaryPath, bc.ConfigFile)
	cmd.Stdout = log
	cmd.Stderr = log

	if err := cmd.Start(); err != nil {
		log.Close()
		return nil, fmt.Errorf("failed to start daemon: %w", err)
	}

	// Wait for daemon to start
	time.Sleep(2 * time.Second)

	// Check if daemon is still running by checking /proc
	if _, err := os.Stat(fmt.Sprintf("/proc/%d", cmd.Process.Pid)); os.IsNotExist(err) {
		return nil, fmt.Errorf("daemon process not found after start")
	}

	// Try to connect to verify daemon is listening
	maxRetries := 5
	for i := 0; i < maxRetries; i++ {
		testClient, err := lsrp.NewClient("localhost", bc.config.Server.TcpPort)
		if err == nil {
			testClient.Close()
			fmt.Printf("Daemon started with PID %d on port %d\n", cmd.Process.Pid, bc.config.Server.TcpPort)
			return cmd, nil
		}
		time.Sleep(500 * time.Millisecond)
	}

	cmd.Process.Kill()
	return nil, fmt.Errorf("daemon not accepting connections on port %d", bc.config.Server.TcpPort)
}

func (bc *BenchmarkConfig) StartMetricsCollector(daemonPID int) error {
	bc.collectorStop = make(chan struct{})

	// Build metrics collector if needed
	collectorPath := "../bin/metrics_collector"
	if _, err := os.Stat(collectorPath); os.IsNotExist(err) {
		fmt.Println("Building metrics_collector...")
		buildCmd := exec.Command("go", "build", "-o", collectorPath, "metrics_collector.go")
		if err := buildCmd.Run(); err != nil {
			return fmt.Errorf("failed to build metrics_collector: %w", err)
		}
	}

	cmd := exec.Command(collectorPath,
		strconv.Itoa(daemonPID),
		bc.MetricsFile,
		"1") // 1 second interval

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start metrics collector: %w", err)
	}

	bc.collectorCmd = cmd
	fmt.Printf("Metrics collector started with PID %d\n", cmd.Process.Pid)

	// Wait a bit for collector to start
	time.Sleep(500 * time.Millisecond)

	return nil
}

func (bc *BenchmarkConfig) StopMetricsCollector() error {
	if bc.collectorCmd != nil && bc.collectorCmd.Process != nil {
		bc.collectorCmd.Process.Signal(os.Interrupt)
		time.Sleep(1 * time.Second)
		bc.collectorCmd.Process.Kill()
	}
	return nil
}

func (bc *BenchmarkConfig) RunLoadTest(client *lsrp.Client) (*BenchmarkResult, error) {
	results := make(chan RequestResult, bc.Requests)
	var wg sync.WaitGroup

	startTime := time.Now()

	// Semaphore for concurrency control
	sem := make(chan struct{}, bc.Concurrency)

	for i := 0; i < bc.Requests; i++ {
		wg.Add(1)
		sem <- struct{}{} // Acquire

		go func(reqNum int) {
			defer wg.Done()
			defer func() { <-sem }() // Release

			reqStart := time.Now()
			resp, err := client.Send(bc.Endpoint)
			latency := time.Since(reqStart)

			success := false
			if err == nil && resp.Status == 0 {
				success = true
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

	// Analyze results
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

	// Sort latencies for percentile calculation
	sort.Slice(latencies, func(i, j int) bool {
		return latencies[i] < latencies[j]
	})

	// Calculate statistics
	var totalLatency time.Duration
	for _, l := range latencies {
		totalLatency += l
	}
	br.AvgLatency = totalLatency / time.Duration(len(latencies))
	br.MedianLatency = latencies[len(latencies)/2]

	if len(latencies) > 0 {
		p95Idx := int(float64(len(latencies)) * 0.95)
		p99Idx := int(float64(len(latencies)) * 0.99)
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
		return nil, fmt.Errorf("insufficient metrics data")
	}

	// Skip header
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

	for i, record := range records {
		if len(record) < 12 {
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

		if i > 0 { // Skip first CPU reading (always 0)
			cpuValues = append(cpuValues, cpu)
			if cpu > summary.CPUMax {
				summary.CPUMax = cpu
			}
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
		}
		lastIORead = ioReadBytes
		lastIOWrite = ioWriteBytes
		lastIOReadOps = ioReadOps
		lastIOWriteOps = ioWriteOps
	}

	// Calculate averages
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

	// Calculate IO totals
	summary.IOReadMB = float64(lastIORead-firstIORead) / (1024 * 1024)
	summary.IOWriteMB = float64(lastIOWrite-firstIOWrite) / (1024 * 1024)

	duration := float64(len(records))
	if duration > 0 {
		summary.IOReadOpsPS = float64(lastIOReadOps-firstIOReadOps) / duration
		summary.IOWriteOpsPS = float64(lastIOWriteOps-firstIOWriteOps) / duration
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

		fmt.Println("\n🔧 Process Statistics:")
		fmt.Printf("  Threads (avg):     %.1f\n", ms.ThreadsAvg)
		fmt.Printf("  Threads (max):     %d\n", ms.ThreadsMax)
		fmt.Printf("  FDs (avg):         %.1f\n", ms.FDsAvg)
		fmt.Printf("  FDs (max):         %d\n", ms.FDsMax)
	}

	fmt.Println(strings.Repeat("=", 70))
}

func compareResults(direct, cached *BenchmarkResult, directMetrics, cachedMetrics *MetricsSummary) {
	fmt.Println("\n" + strings.Repeat("=", 70))
	fmt.Println("COMPARISON: Direct vs RRDCached")
	fmt.Println(strings.Repeat("=", 70))

	calcDiff := func(direct, cached float64) (float64, string) {
		diff := ((cached - direct) / direct) * 100
		sign := "+"
		if diff < 0 {
			sign = ""
		}
		return diff, sign
	}

	fmt.Println("\n📊 Request Performance:")
	if direct.ThroughputRPS > 0 {
		diff, sign := calcDiff(direct.ThroughputRPS, cached.ThroughputRPS)
		fmt.Printf("  Throughput:        Direct: %.2f req/s  |  Cached: %.2f req/s  (%s%.2f%%)\n",
			direct.ThroughputRPS, cached.ThroughputRPS, sign, diff)
	}

	avgDiff := float64(cached.AvgLatency-direct.AvgLatency) / float64(direct.AvgLatency) * 100
	avgSign := "+"
	if avgDiff < 0 {
		avgSign = ""
	}
	fmt.Printf("  Avg Latency:       Direct: %v  |  Cached: %v  (%s%.2f%%)\n",
		direct.AvgLatency, cached.AvgLatency, avgSign, avgDiff)

	p95Diff := float64(cached.P95Latency-direct.P95Latency) / float64(direct.P95Latency) * 100
	p95Sign := "+"
	if p95Diff < 0 {
		p95Sign = ""
	}
	fmt.Printf("  P95 Latency:       Direct: %v  |  Cached: %v  (%s%.2f%%)\n",
		direct.P95Latency, cached.P95Latency, p95Sign, p95Diff)

	if directMetrics != nil && cachedMetrics != nil {
		fmt.Println("\n💻 CPU Usage:")
		cpuDiff, cpuSign := calcDiff(directMetrics.CPUAvg, cachedMetrics.CPUAvg)
		fmt.Printf("  Average:           Direct: %.2f%%  |  Cached: %.2f%%  (%s%.2f%%)\n",
			directMetrics.CPUAvg, cachedMetrics.CPUAvg, cpuSign, cpuDiff)

		fmt.Println("\n🧠 Memory Usage:")
		memDiff, memSign := calcDiff(directMetrics.MemAvgMB, cachedMetrics.MemAvgMB)
		fmt.Printf("  Average:           Direct: %.2f MB  |  Cached: %.2f MB  (%s%.2f%%)\n",
			directMetrics.MemAvgMB, cachedMetrics.MemAvgMB, memSign, memDiff)

		fmt.Println("\n💾 I/O Operations:")
		ioDiff, ioSign := calcDiff(directMetrics.IOReadOpsPS+directMetrics.IOWriteOpsPS,
			cachedMetrics.IOReadOpsPS+cachedMetrics.IOWriteOpsPS)
		fmt.Printf("  Total IOPS:        Direct: %.2f  |  Cached: %.2f  (%s%.2f%%)\n",
			directMetrics.IOReadOpsPS+directMetrics.IOWriteOpsPS,
			cachedMetrics.IOReadOpsPS+cachedMetrics.IOWriteOpsPS, ioSign, ioDiff)

		fmt.Printf("  Read MB:           Direct: %.2f  |  Cached: %.2f\n",
			directMetrics.IOReadMB, cachedMetrics.IOReadMB)
		fmt.Printf("  Write MB:          Direct: %.2f  |  Cached: %.2f\n",
			directMetrics.IOWriteMB, cachedMetrics.IOWriteMB)
	}

	fmt.Println(strings.Repeat("=", 70))
}

func Test_BenchmarkDirectVsCached(t *testing.T) {
	// Create directories
	os.MkdirAll("results", 0755)
	os.MkdirAll("logs", 0755)

	// Test 1: Direct (without rrdcached)
	t.Run("Direct", func(t *testing.T) {
		bc := NewBenchmarkConfig()
		bc.UseRRDCached = false
		bc.MetricsFile = filepath.Join(bc.OutputDir, "direct_metrics.csv")

		if err := bc.LoadConfig(); err != nil {
			t.Fatalf("Failed to load config: %v", err)
		}

		// Increment port to avoid conflicts
		bc.config.Server.TcpPort += 1

		if err := bc.UpdateConfig(); err != nil {
			t.Fatalf("Failed to update config: %v", err)
		}

		// Start daemon
		daemonCmd, err := bc.StartDaemon()
		if err != nil {
			t.Fatalf("Failed to start daemon: %v", err)
		}
		defer daemonCmd.Process.Kill()

		// Start metrics collector
		if err := bc.StartMetricsCollector(daemonCmd.Process.Pid); err != nil {
			t.Fatalf("Failed to start metrics collector: %v", err)
		}
		defer bc.StopMetricsCollector()

		// Create client
		client, err := lsrp.NewClient("localhost", bc.config.Server.TcpPort)
		if err != nil {
			t.Fatalf("Failed to create client: %v", err)
		}
		defer client.Close()

		// Warmup
		fmt.Println("Warming up...")
		for i := 0; i < 10; i++ {
			client.Send(bc.Endpoint)
		}
		time.Sleep(2 * time.Second)

		// Run load test
		fmt.Printf("\nRunning load test: %d requests, concurrency: %d\n", bc.Requests, bc.Concurrency)
		result, err := bc.RunLoadTest(client)
		if err != nil {
			t.Fatalf("Load test failed: %v", err)
		}

		// Stop collector and analyze metrics
		bc.StopMetricsCollector()
		time.Sleep(2 * time.Second)

		metrics, err := bc.AnalyzeMetrics()
		if err != nil {
			t.Logf("Warning: Failed to analyze metrics: %v", err)
		}

		result.TestName = "Direct (without rrdcached)"
		printResults(result.TestName, result, metrics)

		// Save results to file
		saveResultsToJSON(filepath.Join(bc.OutputDir, "direct_results.json"), result, metrics)
	})

	// Test 2: With rrdcached
	t.Run("Cached", func(t *testing.T) {
		bc := NewBenchmarkConfig()
		bc.UseRRDCached = true
		bc.MetricsFile = filepath.Join(bc.OutputDir, "cached_metrics.csv")

		if err := bc.LoadConfig(); err != nil {
			t.Fatalf("Failed to load config: %v", err)
		}

		// Increment port to avoid conflicts
		bc.config.Server.TcpPort += 2

		if err := bc.UpdateConfig(); err != nil {
			t.Fatalf("Failed to update config: %v", err)
		}

		// Start daemon
		daemonCmd, err := bc.StartDaemon()
		if err != nil {
			t.Fatalf("Failed to start daemon: %v", err)
		}
		defer daemonCmd.Process.Kill()

		// Start metrics collector
		if err := bc.StartMetricsCollector(daemonCmd.Process.Pid); err != nil {
			t.Fatalf("Failed to start metrics collector: %v", err)
		}
		defer bc.StopMetricsCollector()

		// Create client
		client, err := lsrp.NewClient("localhost", bc.config.Server.TcpPort)
		if err != nil {
			t.Fatalf("Failed to create client: %v", err)
		}
		defer client.Close()

		// Warmup
		fmt.Println("Warming up...")
		for i := 0; i < 10; i++ {
			client.Send(bc.Endpoint)
		}
		time.Sleep(2 * time.Second)

		// Run load test
		fmt.Printf("\nRunning load test: %d requests, concurrency: %d\n", bc.Requests, bc.Concurrency)
		result, err := bc.RunLoadTest(client)
		if err != nil {
			t.Fatalf("Load test failed: %v", err)
		}

		// Stop collector and analyze metrics
		bc.StopMetricsCollector()
		time.Sleep(2 * time.Second)

		metrics, err := bc.AnalyzeMetrics()
		if err != nil {
			t.Logf("Warning: Failed to analyze metrics: %v", err)
		}

		result.TestName = "Cached (with rrdcached)"
		printResults(result.TestName, result, metrics)

		// Save results to file
		saveResultsToJSON(filepath.Join(bc.OutputDir, "cached_results.json"), result, metrics)
	})

	// Compare results
	directResult, directMetrics := loadResultsFromJSON(filepath.Join("benchmark_results", "direct_results.json"))
	cachedResult, cachedMetrics := loadResultsFromJSON(filepath.Join("benchmark_results", "cached_results.json"))

	if directResult != nil && cachedResult != nil {
		compareResults(directResult, cachedResult, directMetrics, cachedMetrics)
	}
}

type SavedResults struct {
	Benchmark *BenchmarkResult `json:"benchmark"`
	Metrics   *MetricsSummary  `json:"metrics"`
}

func saveResultsToJSON(filename string, br *BenchmarkResult, ms *MetricsSummary) error {
	saved := SavedResults{
		Benchmark: br,
		Metrics:   ms,
	}

	data, err := json.MarshalIndent(saved, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(filename, data, 0644)
}

func loadResultsFromJSON(filename string) (*BenchmarkResult, *MetricsSummary) {
	data, err := os.ReadFile(filename)
	if err != nil {
		return nil, nil
	}

	var saved SavedResults
	if err := json.Unmarshal(data, &saved); err != nil {
		return nil, nil
	}

	return saved.Benchmark, saved.Metrics
}

// Helper function
func Repeat(s string, count int) string {
	result := ""
	for i := 0; i < count; i++ {
		result += s
	}
	return result
}
