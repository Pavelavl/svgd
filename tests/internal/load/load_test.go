package load

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"tests/pkg/benchmark"
	"tests/pkg/http"
	"tests/pkg/system"

	"github.com/Pavelavl/go-lsrp"
)

const (
	DefaultRequests    = 1000
	DefaultConcurrency = 1
	DefaultDuration    = 60 * time.Second
	MetricsInterval    = 1 * time.Second
	WarmupRequests     = 50
	WarmupDelay        = 5 * time.Second
)

var repoRoot string

func init() {
	// Get repository root (3 levels up from this file)
	// tests/load/load_test.go -> tests/load -> tests -> svgd (repo root)
	_, filename, _, _ := runtime.Caller(0)
	repoRoot = filepath.Dir(filepath.Dir(filepath.Dir(filename)))
}

// binPath returns absolute path to binary
func binPath(name string) string {
	return filepath.Join(repoRoot, "bin", name)
}

type BenchmarkConfig struct {
	Requests         int
	Concurrency      int
	Duration         time.Duration
	RRDFile          string
	OutputDir        string
	LogDir           string
	ConfigFile       string
	UseRRDCached     bool
	config           Config
	metricsCollector *benchmark.MetricsCollector
}

type Config struct {
	Server struct {
		TCPPort         int    `json:"tcp_port"`
		AllowedIps      string `json:"allowed_ips"`
		RRDCachedAddr   string `json:"rrdcached_addr"`
		Protocol        string `json:"protocol"`
		ThreadPoolSize  int    `json:"thread_pool_size"`
		CacheTTLMetrics int    `json:"cache_ttl_seconds"`
		Verbose         int    `json:"verbose"`
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

// RequestResult, BenchmarkResult, and MetricsSummary are imported from svgd/tests/internal/benchmark

func NewBenchmarkConfig() *BenchmarkConfig {
	bc := &BenchmarkConfig{
		Requests:    1000, // Reduced for faster testing
		Concurrency: DefaultConcurrency,
		Duration:    DefaultDuration,
		RRDFile:     "/opt/collectd/var/lib/collectd/rrd/localhost/cpu-total/percent-active.rrd",
		OutputDir:   filepath.Join(repoRoot, "tests", "load", "results"),
		LogDir:      filepath.Join(repoRoot, "tests", "load", "logs"),
		ConfigFile:  filepath.Join(repoRoot, "config.json"),
	}

	// Create directories if they don't exist
	if err := os.MkdirAll(bc.OutputDir, 0755); err != nil {
		panic(fmt.Sprintf("Failed to create output directory: %v", err))
	}
	if err := os.MkdirAll(bc.LogDir, 0755); err != nil {
		panic(fmt.Sprintf("Failed to create log directory: %v", err))
	}

	// Load config file to get server port
	if err := bc.loadConfig(); err != nil {
		panic(fmt.Sprintf("Failed to load config: %v", err))
	}
	return bc
}

func (bc *BenchmarkConfig) loadConfig() error {
	data, err := os.ReadFile(bc.ConfigFile)
	if err != nil {
		return fmt.Errorf("failed to read config file: %w", err)
	}
	if err := json.Unmarshal(data, &bc.config); err != nil {
		return fmt.Errorf("failed to parse config file: %w", err)
	}
	return nil
}

func (bc *BenchmarkConfig) StartDaemon(binaryName string) (*exec.Cmd, error) {
	logFile := filepath.Join(bc.LogDir, "daemon.log")
	log, err := os.Create(logFile)
	if err != nil {
		return nil, fmt.Errorf("failed to create log file: %w", err)
	}

	binaryPath := binPath(binaryName)

	var cmd *exec.Cmd
	if binaryName == "svgd-gate" {
		// svgd-gate needs: host, backend_port, http_port, static_path
		staticPath := filepath.Join(repoRoot, "gate", "static")
		cmd = exec.Command(binaryPath, "127.0.0.1", strconv.Itoa(bc.config.Server.TCPPort), "8080", staticPath)
	} else {
		// svgd needs config file
		cmd = exec.Command(binaryPath, bc.ConfigFile)
	}
	cmd.Dir = repoRoot // Set working directory to repo root for relative paths in config
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

// StartDaemonWithConfig starts a daemon with a custom config file
func (bc *BenchmarkConfig) StartDaemonWithConfig(binaryName, configFile string) (*exec.Cmd, error) {
	logFile := filepath.Join(bc.LogDir, binaryName+".log")
	log, err := os.Create(logFile)
	if err != nil {
		return nil, fmt.Errorf("failed to create log file: %w", err)
	}

	binaryPath := binPath(binaryName)
	if _, err := os.Stat(binaryPath); os.IsNotExist(err) {
		log.Close()
		return nil, fmt.Errorf("binary not found: %s", binaryPath)
	}

	var cmd *exec.Cmd
	if binaryName == "svgd-gate" {
		// svgd-gate needs: host, backend_port, http_port, static_path
		staticPath := filepath.Join(repoRoot, "gate", "static")
		cmd = exec.Command(binaryPath, "127.0.0.1", strconv.Itoa(bc.config.Server.TCPPort), "8080", staticPath)
	} else {
		// svgd needs config file
		cmd = exec.Command(binaryPath, configFile)
	}
	cmd.Dir = repoRoot
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

	return cmd, nil
}

// StartHTTPGateway starts the svgd-gate HTTP gateway with specified number of workers
func (bc *BenchmarkConfig) StartHTTPGateway(workers int) (*exec.Cmd, error) {
	logFile := filepath.Join(bc.LogDir, "gateway.log")
	log, err := os.Create(logFile)
	if err != nil {
		return nil, fmt.Errorf("failed to create gateway log file: %w", err)
	}

	gatePath := binPath("svgd-gate")
	staticPath := filepath.Join(repoRoot, "gate", "static")
	httpPort := 8080 // Gateway listens on 8080, backend is on config port

	// Command: svgd-gate <host> <backend_port> <http_port> <static_path> [workers]
	cmd := exec.Command(gatePath, "127.0.0.1", strconv.Itoa(bc.config.Server.TCPPort), strconv.Itoa(httpPort), staticPath, strconv.Itoa(workers))
	cmd.Dir = repoRoot
	cmd.Stdout = log
	cmd.Stderr = log

	if err := cmd.Start(); err != nil {
		log.Close()
		return nil, fmt.Errorf("failed to start gateway: %w", err)
	}

	fmt.Printf("Gateway starting with PID %d on port %d with %d workers...\n", cmd.Process.Pid, httpPort, workers)
	time.Sleep(2 * time.Second)

	// Check if gateway is responding
	maxRetries := 10
	retryDelay := 500 * time.Millisecond
	for i := 0; i < maxRetries; i++ {
		testClient, err := http.NewClient("localhost", httpPort)
		if err == nil {
			testClient.Close()
			fmt.Printf("Gateway ready on port %d (attempt %d/%d)\n", httpPort, i+1, maxRetries)
			return cmd, nil
		}
		if i < maxRetries-1 {
			time.Sleep(retryDelay)
		}
	}

	if err := cmd.Process.Kill(); err != nil {
		fmt.Printf("Warning: failed to kill gateway: %v\n", err)
	}
	return nil, fmt.Errorf("gateway not accepting connections on port %d after %d attempts", httpPort, maxRetries)
}

func (bc *BenchmarkConfig) StartMetricsCollector(daemonPID int) error {
	if _, err := os.Stat(fmt.Sprintf("/proc/%d", daemonPID)); os.IsNotExist(err) {
		return fmt.Errorf("target process %d does not exist", daemonPID)
	}

	fmt.Printf("Starting metrics collector for PID %d\n", daemonPID)

	bc.metricsCollector = benchmark.NewMetricsCollector(daemonPID)

	// Start collector in background
	ctx := context.Background()
	if err := bc.metricsCollector.Start(ctx); err != nil {
		return fmt.Errorf("failed to start metrics collector: %w", err)
	}

	// Wait a bit to ensure first sample is collected
	time.Sleep(500 * time.Millisecond)

	return nil
}

func (bc *BenchmarkConfig) StopMetricsCollector() error {
	if bc.metricsCollector != nil {
		bc.metricsCollector.Stop()
		bc.metricsCollector = nil
	}
	return nil
}

func (bc *BenchmarkConfig) RunLoadTest(client any, p proto) (*benchmark.BenchmarkResult, error) {
	results := make(chan benchmark.RequestResult, bc.Requests)
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

			results <- benchmark.RequestResult{
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

	return benchmark.AnalyzeResults(results, duration)
}

func (bc *BenchmarkConfig) AnalyzeMetrics() (*benchmark.MetricsSummary, error) {
	if bc.metricsCollector == nil {
		return nil, fmt.Errorf("metrics collector not started")
	}
	return bc.metricsCollector.Analyze()
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

	// Extended fields from BenchmarkResult
	TotalRequests int
	SuccessCount  int
	FailCount     int
	SuccessRate   float64
	MedianLatency time.Duration
	MinLatency    time.Duration
	MaxLatency    time.Duration
	P99Latency    time.Duration
	TestDuration  time.Duration

	// Extended fields from MetricsSummary
	CPUMedian              float64
	CPUMax                 float64
	MemMedianMB            float64
	MemMaxMB               float64
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

func writeBenchmarkResults(rows []*ComparisonRow) error {
	if len(rows) == 0 {
		return nil
	}

	reporter, err := system.NewReporter("load")
	if err != nil {
		return fmt.Errorf("failed to create reporter: %w", err)
	}
	defer reporter.Close()

	for _, row := range rows {
		record := map[string]any{
			"test_name":            row.TestName,
			"protocol":             row.Protocol,
			"rrd_cached":           row.RRDCached,
			"throughput_rps":       row.Throughput,
			"total_requests":       row.TotalRequests,
			"success_count":        row.SuccessCount,
			"fail_count":           row.FailCount,
			"success_rate":         row.SuccessRate,
			"avg_latency_ms":       float64(row.AvgLatency.Microseconds()) / 1000,
			"median_latency_ms":    float64(row.MedianLatency.Microseconds()) / 1000,
			"min_latency_ms":       float64(row.MinLatency.Microseconds()) / 1000,
			"max_latency_ms":       float64(row.MaxLatency.Microseconds()) / 1000,
			"p95_latency_ms":       float64(row.P95Latency.Microseconds()) / 1000,
			"p99_latency_ms":       float64(row.P99Latency.Microseconds()) / 1000,
			"test_duration_sec":    row.TestDuration.Seconds(),
			"cpu_avg_pct":          row.CPUAvg,
			"cpu_median_pct":       row.CPUMedian,
			"cpu_max_pct":          row.CPUMax,
			"mem_avg_mb":           row.MemAvgMB,
			"mem_median_mb":        row.MemMedianMB,
			"mem_max_mb":           row.MemMaxMB,
			"io_read_mb":           row.IOReadMB,
			"io_write_mb":          row.IOWriteMB,
			"io_read_ops_ps":       row.IOReadOpsPS,
			"io_write_ops_ps":      row.IOWriteOpsPS,
			"threads_avg":          row.ThreadsAvg,
			"threads_max":          row.ThreadsMax,
			"fds_avg":              row.FDsAvg,
			"fds_max":              row.FDsMax,
			"ctx_switch_vol_ps":    row.CtxSwitchVoluntaryPS,
			"ctx_switch_invol_ps":  row.CtxSwitchInvoluntaryPS,
			"page_faults_minor_ps": row.PageFaultsMinorPS,
			"page_faults_major_ps": row.PageFaultsMajorPS,
		}
		if err := reporter.Record(record); err != nil {
			return fmt.Errorf("failed to record result: %w", err)
		}
	}

	fmt.Println("Benchmark results written to tests/results/load.csv")
	return nil
}

// createTestConfig creates a temporary config file with specified protocol
func createTestConfig(baseConfigPath, protocol string, useCache bool, port int) (string, error) {
	// Read base config
	data, err := os.ReadFile(baseConfigPath)
	if err != nil {
		return "", fmt.Errorf("failed to read base config: %w", err)
	}

	// Parse JSON
	var config map[string]interface{}
	if err := json.Unmarshal(data, &config); err != nil {
		return "", fmt.Errorf("failed to parse config: %w", err)
	}

	// Modify server settings
	if server, ok := config["server"].(map[string]interface{}); ok {
		server["protocol"] = protocol
		server["tcp_port"] = port
		if useCache {
			server["rrdcached_addr"] = "unix:/var/run/rrdcached.sock"
		} else {
			server["rrdcached_addr"] = ""
		}
	}

	// Write to temp file
	tmpFile := filepath.Join(os.TempDir(), fmt.Sprintf("svgd-test-%s-%d.json", protocol, port))
	data, err = json.MarshalIndent(config, "", "  ")
	if err != nil {
		return "", fmt.Errorf("failed to marshal config: %w", err)
	}
	if err := os.WriteFile(tmpFile, data, 0644); err != nil {
		return "", fmt.Errorf("failed to write temp config: %w", err)
	}

	return tmpFile, nil
}

func Test_BenchmarkDirectVsCached(t *testing.T) {
	os.MkdirAll(filepath.Join(repoRoot, "tests", "load", "results"), 0755)
	os.MkdirAll(filepath.Join(repoRoot, "tests", "load", "logs"), 0755)

	tests := []struct {
		name             string
		usecache         bool
		metricFilePrefix string
		p                proto
		concurrency      int
	}{
		// Direct (no rrdcached)
		{
			name:             "direct LSRP (c=1)",
			usecache:         false,
			metricFilePrefix: "direct",
			p:                protoLSRP,
			concurrency:      1,
		},
		{
			name:             "direct HTTP (c=1)",
			usecache:         false,
			metricFilePrefix: "direct",
			p:                protoHTTP,
			concurrency:      1,
		},
		{
			name:             "direct LSRP (c=10)",
			usecache:         false,
			metricFilePrefix: "direct_c10",
			p:                protoLSRP,
			concurrency:      10,
		},
		{
			name:             "direct HTTP (c=10)",
			usecache:         false,
			metricFilePrefix: "direct_c10",
			p:                protoHTTP,
			concurrency:      10,
		},
		// With rrdcached
		{
			name:             "cached LSRP (c=1)",
			usecache:         true,
			metricFilePrefix: "cached",
			p:                protoLSRP,
			concurrency:      1,
		},
		{
			name:             "cached HTTP (c=1)",
			usecache:         true,
			metricFilePrefix: "cached",
			p:                protoHTTP,
			concurrency:      1,
		},
		{
			name:             "cached LSRP (c=10)",
			usecache:         true,
			metricFilePrefix: "cached_c10",
			p:                protoLSRP,
			concurrency:      10,
		},
		{
			name:             "cached HTTP (c=10)",
			usecache:         true,
			metricFilePrefix: "cached_c10",
			p:                protoHTTP,
			concurrency:      10,
		},
	}

	var comparisonRows []*ComparisonRow
	basePort := 8090 // Use different ports for each test to avoid conflicts

	for i, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			bc := NewBenchmarkConfig()
			bc.UseRRDCached = tt.usecache
			bc.Concurrency = tt.concurrency
			// Reduce requests for higher concurrency tests
			if tt.concurrency > 1 {
				bc.Requests = 1000
			}
			testPort := basePort + i

			// Create test config with appropriate protocol
			protocol := string(tt.p)
			testConfig, err := createTestConfig(bc.ConfigFile, protocol, tt.usecache, testPort)
			if err != nil {
				t.Fatalf("Failed to create test config: %v", err)
			}
			defer os.Remove(testConfig)

			// Start backend with test config
			daemonCmd, err := bc.StartDaemonWithConfig("svgd", testConfig)
			if err != nil {
				t.Fatalf("Failed to start daemon: %v", err)
			}
			metricsPID := daemonCmd.Process.Pid

			defer func() {
				if daemonCmd != nil && daemonCmd.Process != nil {
					if err := daemonCmd.Process.Kill(); err != nil {
						t.Logf("Warning: failed to kill daemon: %v", err)
					}
				}
			}()

			if err := bc.StartMetricsCollector(metricsPID); err != nil {
				t.Fatalf("Failed to start metrics collector: %v", err)
			}
			defer bc.StopMetricsCollector()

			var client any
			if tt.p == protoLSRP {
				c, err := lsrp.NewClient("localhost", testPort)
				if err != nil {
					t.Fatalf("Failed to create lsrp client: %v", err)
				}
				defer c.Close()
				client = c
			} else {
				// HTTP client connects directly to backend (native HTTP mode)
				c, err := http.NewClient("localhost", testPort)
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

			row := &ComparisonRow{
				TestName:      tt.name,
				Protocol:      string(tt.p),
				RRDCached:     tt.usecache,
				Throughput:    result.ThroughputRPS,
				AvgLatency:    result.AvgLatency,
				P95Latency:    result.P95Latency,
				TotalRequests: result.TotalRequests,
				SuccessCount:  result.SuccessCount,
				FailCount:     result.FailCount,
				SuccessRate:   result.SuccessRate,
				MedianLatency: result.MedianLatency,
				MinLatency:    result.MinLatency,
				MaxLatency:    result.MaxLatency,
				P99Latency:    result.P99Latency,
				TestDuration:  result.Duration,
			}
			if metrics != nil {
				row.CPUAvg = metrics.CPUAvg
				row.MemAvgMB = metrics.MemAvgMB
				row.IOReadMB = metrics.IOReadMB
				row.IOWriteMB = metrics.IOWriteMB
				row.CPUMedian = metrics.CPUMedian
				row.CPUMax = metrics.CPUMax
				row.MemMedianMB = metrics.MemMedianMB
				row.MemMaxMB = metrics.MemMaxMB
				row.IOReadOpsPS = metrics.IOReadOpsPS
				row.IOWriteOpsPS = metrics.IOWriteOpsPS
				row.ThreadsAvg = metrics.ThreadsAvg
				row.ThreadsMax = metrics.ThreadsMax
				row.FDsAvg = metrics.FDsAvg
				row.FDsMax = metrics.FDsMax
				row.CtxSwitchVoluntaryPS = metrics.CtxSwitchVoluntaryPS
				row.CtxSwitchInvoluntaryPS = metrics.CtxSwitchInvoluntaryPS
				row.PageFaultsMinorPS = metrics.PageFaultsMinorPS
				row.PageFaultsMajorPS = metrics.PageFaultsMajorPS
			}
			comparisonRows = append(comparisonRows, row)
		})
	}

	printBenchmarkComparisonTable(comparisonRows)

	if err := writeBenchmarkResults(comparisonRows); err != nil {
		fmt.Printf("Warning: failed to write results to CSV: %v\n", err)
	}
}

type SavedResults struct {
	Benchmark *benchmark.BenchmarkResult `json:"benchmark"`
	Metrics   *benchmark.MetricsSummary  `json:"metrics"`
}

func Repeat(s string, count int) string {
	result := ""
	for i := 0; i < count; i++ {
		result += s
	}
	return result
}
