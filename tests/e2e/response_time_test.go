package base

import (
	"encoding/csv"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"lsrp_test/http"

	"github.com/Pavelavl/go-lsrp"
)

var tc testCase
var repoRoot string

func init() {
	// Get repository root (3 levels up from this file)
	// tests/e2e/response_time_test.go -> tests/e2e -> tests -> svgd (repo root)
	_, filename, _, _ := runtime.Caller(0)
	repoRoot = filepath.Dir(filepath.Dir(filepath.Dir(filename)))
}

// binPath returns absolute path to binary
func binPath(name string) string {
	return filepath.Join(repoRoot, "bin", name)
}

func TestMain(m *testing.M) {
	// Initialize test configuration
	tc.requests = 100
	tc.concurrency = 5
	tc.endpoint = "endpoint=cpu&period=3600" // LSRP format
	tc.httpEndpoint = "cpu"                  // HTTP format
	tc.rrdFile = "/opt/collectd/var/lib/collectd/rrd/localhost/cpu-total/percent-active.rrd"
	tc.outputDir = filepath.Join(repoRoot, "tests", "e2e", "temp_svgs")
	tc.logDir = filepath.Join(repoRoot, "tests", "e2e", "logs")
	tc.resultsDir = filepath.Join(repoRoot, "tests", "e2e", "results")
	tc.configFile = filepath.Join(repoRoot, "config.json")

	// Create directories
	if err := os.MkdirAll(tc.outputDir, 0755); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to create directory %s: %v\n", tc.outputDir, err)
		os.Exit(1)
	}
	if err := os.MkdirAll(tc.logDir, 0755); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to create directory %s: %v\n", tc.logDir, err)
		os.Exit(1)
	}
	if err := os.MkdirAll(tc.resultsDir, 0755); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to create directory %s: %v\n", tc.resultsDir, err)
		os.Exit(1)
	}

	// Clear client errors log
	if err := os.WriteFile(filepath.Join(tc.logDir, "client_errors.log"), []byte{}, 0644); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to clear client errors log: %v\n", err)
		os.Exit(1)
	}

	// Check dependencies and files
	if err := checkDependencies(); err != nil {
		fmt.Fprintf(os.Stderr, "%v\n", err)
		os.Exit(1)
	}
	if err := checkFile(tc.rrdFile, false); err != nil {
		fmt.Fprintf(os.Stderr, "%v\n", err)
		os.Exit(1)
	}
	if err := checkFile(tc.configFile, false); err != nil {
		fmt.Fprintf(os.Stderr, "%v\n", err)
		os.Exit(1)
	}
	if err := tc.readConfig(); err != nil {
		fmt.Fprintf(os.Stderr, "%v\n", err)
		os.Exit(1)
	}
	fmt.Printf("Using port %d from config.json\n", tc.config.Server.TCPPort)
	if err := tc.checkPort(); err != nil {
		fmt.Fprintf(os.Stderr, "%v\n", err)
		os.Exit(1)
	}
	if err := checkRrdcached(); err != nil {
		fmt.Fprintf(os.Stderr, "%v\n", err)
		os.Exit(1)
	}

	// Run tests
	os.Exit(m.Run())
}

func Test_HTTPVsLSRP(t *testing.T) {
	tests := []struct {
		binary    string
		logPrefix string
		rrdcached bool
		mode      string
		protocol  string // "http" or "lsrp"
	}{
		{binary: "http", rrdcached: true, logPrefix: "http_with_rrdcached", mode: "sync", protocol: "http"},
		{binary: "http", rrdcached: true, logPrefix: "http_with_rrdcached", mode: "parallel", protocol: "http"},
		{binary: "http", rrdcached: false, logPrefix: "http_without_rrdcached", mode: "sync", protocol: "http"},
		{binary: "http", rrdcached: false, logPrefix: "http_without_rrdcached", mode: "parallel", protocol: "http"},
		{binary: "lsrp", rrdcached: true, logPrefix: "lsrp_with_rrdcached", mode: "sync", protocol: "lsrp"},
		{binary: "lsrp", rrdcached: true, logPrefix: "lsrp_with_rrdcached", mode: "parallel", protocol: "lsrp"},
		{binary: "lsrp", rrdcached: false, logPrefix: "lsrp_without_rrdcached", mode: "sync", protocol: "lsrp"},
		{binary: "lsrp", rrdcached: false, logPrefix: "lsrp_without_rrdcached", mode: "parallel", protocol: "lsrp"},
	}

	var allResults []*TestResult
	for _, tt := range tests {
		t.Run(fmt.Sprintf("%s_%s", tt.logPrefix, tt.mode), func(t *testing.T) {
			if !tt.rrdcached {
				tc.config.Server.RRDCachedAddr = ""
			} else {
				tc.config.Server.RRDCachedAddr = "unix:/var/run/rrdcached.sock"
			}

			if err := tc.updateConfig(); err != nil {
				t.Error(err)
			}

			result, err := tc.run(t, tt.binary, tt.logPrefix, tt.mode, tt.protocol)
			if err != nil {
				t.Errorf("Test failed for %s in %s mode: %v", tt.binary, tt.mode, err)
			} else {
				allResults = append(allResults, result)
			}
		})
	}

	printComparisonTable(allResults)

	// Write results to CSV
	if err := writeResultsToCSV(allResults, tc.resultsDir); err != nil {
		t.Logf("Warning: failed to write results to CSV: %v", err)
	}
}

type testCase struct {
	requests     int
	concurrency  int
	endpoint     string // LSRP endpoint (e.g., "endpoint=cpu&period=3600")
	httpEndpoint string // HTTP endpoint (e.g., "cpu")
	rrdFile      string
	outputDir    string
	logDir       string
	resultsDir   string
	configFile   string
	config       config
}

type result struct {
	requestNum int
	timeMs     int64
	status     string
}

func checkDependencies() error {
	deps := []string{"ldd", "jq", "lsof", "flock"}
	for _, dep := range deps {
		if _, err := exec.LookPath(dep); err != nil {
			return fmt.Errorf("dependency %s not found: please install (e.g., sudo apt install %s)", dep, dep)
		}
	}
	return nil
}

func checkFile(path string, executable bool) error {
	info, err := os.Stat(path)
	if err != nil {
		return fmt.Errorf("%s not found: %v", path, err)
	}
	if executable && info.Mode().Perm()&0111 == 0 {
		return fmt.Errorf("%s is not executable", path)
	}
	return nil
}

type config struct {
	Server struct {
		TCPPort         int    `json:"tcp_port"`
		Protocol        string `json:"protocol"`
		AllowedIps      string `json:"allowed_ips"`
		RRDCachedAddr   string `json:"rrdcached_addr"`
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

func (tc *testCase) readConfig() error {
	data, err := os.ReadFile(tc.configFile)
	if err != nil {
		return fmt.Errorf("failed to read %s: %v", tc.configFile, err)
	}
	var config config
	if err := json.Unmarshal(data, &config); err != nil {
		return fmt.Errorf("invalid JSON in %s: %v", tc.configFile, err)
	}
	tc.config = config
	return nil
}

func (tc *testCase) updateConfig() error {
	data, err := json.MarshalIndent(tc.config, "", "\t")
	if err != nil {
		return err
	}
	return os.WriteFile(tc.configFile, data, 0644)
}

func (tc testCase) checkPort() error {
	cmd := exec.Command("lsof", "-i", fmt.Sprintf(":%d", tc.config.Server.TCPPort))
	if err := cmd.Run(); err == nil {
		cmdKill := exec.Command("kill", "-TERM", "$(lsof -t -i :"+strconv.Itoa(tc.config.Server.TCPPort)+")")
		if err := cmdKill.Run(); err != nil {
			return fmt.Errorf("failed to free port %d: %v", tc.config.Server.TCPPort, err)
		}
		time.Sleep(1 * time.Second)
		if err := cmd.Run(); err == nil {
			return fmt.Errorf("port %d still in use after attempting to free it", tc.config.Server.TCPPort)
		}
	}
	return nil
}

func checkRrdcached() error {
	cmd := exec.Command("pgrep", "-x", "rrdcached")
	if err := cmd.Run(); err != nil {
		fmt.Println("Warning: rrdcached is not running. Starting it...")
		cmdStart := exec.Command("sudo", "systemctl", "start", "rrdcached")
		if err := cmdStart.Run(); err != nil {
			return fmt.Errorf("failed to start rrdcached: %v", err)
		}
	}
	return nil
}

func checkSvg(file string) bool {
	data, err := os.ReadFile(file)
	if err != nil || len(data) == 0 {
		return false
	}
	return strings.Contains(string(data), "<svg")
}

func (tc testCase) sendRequest(t *testing.T, requestNum int, client interface{}, endpoint string, errorLog *os.File, results chan<- result, protocol string) {
	outputFile := filepath.Join(tc.outputDir, fmt.Sprintf("test_%d.svg", requestNum))

	start := time.Now()
	var status byte
	var data []byte

	switch protocol {
	case "http":
		httpClient, ok := client.(*http.Client)
		if !ok {
			logEntry := fmt.Sprintf("Request %d failed: invalid HTTP client\n", requestNum)
			errorLog.WriteString(logEntry)
			results <- result{requestNum, time.Since(start).Milliseconds(), "FAIL"}
			return
		}
		httpResp, err := httpClient.Send(endpoint)
		if err == nil {
			status = httpResp.Status
			data = httpResp.Data
		}
	case "lsrp":
		lsrpClient, ok := client.(*lsrp.Client)
		if !ok {
			logEntry := fmt.Sprintf("Request %d failed: invalid LSRP client\n", requestNum)
			if _, err := errorLog.WriteString(logEntry); err != nil {
				t.Logf("Failed to write to error log: %v\n", err)
			}
			results <- result{requestNum, time.Since(start).Milliseconds(), "FAIL"}
			return
		}
		lsrpResp, err := lsrpClient.Send(endpoint)
		if err != nil {
			logEntry := fmt.Sprintf("Request %d failed: %s\n", requestNum, err)
			if _, err := errorLog.WriteString(logEntry); err != nil {
				t.Logf("Failed to write to error log: %v\n", err)
			}
			results <- result{requestNum, time.Since(start).Milliseconds(), "FAIL"}
			return
		}
		status = lsrpResp.Status
		data = lsrpResp.Data
	default:
		logEntry := fmt.Sprintf("Request %d failed: unknown protocol %s\n", requestNum, protocol)
		errorLog.WriteString(logEntry)
		results <- result{requestNum, time.Since(start).Milliseconds(), "FAIL"}
		return
	}

	elapsed := time.Since(start).Milliseconds()
	resultStatus := "FAIL"

	const maxLogSize = 100
	logEntry := fmt.Sprintf("Request %d data: %s\n", requestNum, data[:min(len(data), maxLogSize)])
	if _, err := errorLog.WriteString(logEntry); err != nil {
		t.Logf("Failed to write to error log: %v\n", err)
	}

	if status == 0 {
		if err := os.WriteFile(outputFile, data, 0644); err != nil {
			logEntry := fmt.Sprintf("Request %d failed to write output: output_file=%s, error=%v\n", requestNum, outputFile, err)
			_, err := errorLog.WriteString(logEntry)
			if err != nil {
				t.Logf("Failed to write to error log: %v\n", err)
			}
		} else if checkSvg(outputFile) {
			resultStatus = "SUCCESS"
		} else {
			logEntry := fmt.Sprintf("Request %d failed: invalid SVG, output_file=%s\n", requestNum, outputFile)
			_, err := errorLog.WriteString(logEntry)
			if err != nil {
				t.Logf("Failed to write to error log: %v\n", err)
			}
		}
	} else {
		logEntry := fmt.Sprintf("Request %d failed: status=%d, data=%s\n", requestNum, status, string(data))
		_, err := errorLog.WriteString(logEntry)
		if err != nil {
			t.Logf("Failed to write to error log: %v\n", err)
		}
	}

	results <- result{requestNum, elapsed, resultStatus}
}

type TestResult struct {
	TestName      string
	Protocol      string
	Mode          string
	RRDCached     bool
	TotalRequests int
	SuccessCount  int
	SuccessRate   float64
	AvgTimeMs     int64
	MedianTimeMs  int64
	MinTimeMs     int64
	MaxTimeMs     int64
}

func analyzeResults(logPrefix string, results []result) *TestResult {
	var totalTime int64
	var successCount, count int
	var times []int64
	minTime := int64(999999)
	maxTime := int64(0)

	for _, res := range results {
		if res.timeMs > 0 {
			totalTime += res.timeMs
			times = append(times, res.timeMs)
			if res.timeMs < minTime {
				minTime = res.timeMs
			}
			if res.timeMs > maxTime {
				maxTime = res.timeMs
			}
			if res.status == "SUCCESS" {
				successCount++
			}
			count++
		}
	}

	if count == 0 {
		return &TestResult{
			TestName: logPrefix,
		}
	}

	avgTime := totalTime / int64(count)
	successRate := float64(successCount) * 100 / float64(count)

	sort.Slice(times, func(i, j int) bool { return times[i] < times[j] })
	var medianTime int64
	if len(times)%2 == 0 {
		mid := len(times) / 2
		medianTime = (times[mid-1] + times[mid]) / 2
	} else {
		medianTime = times[len(times)/2]
	}

	return &TestResult{
		TestName:      logPrefix,
		TotalRequests: count,
		SuccessCount:  successCount,
		SuccessRate:   successRate,
		AvgTimeMs:     avgTime,
		MedianTimeMs:  medianTime,
		MinTimeMs:     minTime,
		MaxTimeMs:     maxTime,
	}
}

func printComparisonTable(results []*TestResult) {
	fmt.Println("\n" + strings.Repeat("=", 140))
	fmt.Println("# COMPARISON TABLE: All Test Cases")
	fmt.Println(strings.Repeat("=", 140))
	fmt.Println()
	fmt.Println("| Protocol | Mode | RRDCached | Total | Success | Rate | Avg (ms) | Median (ms) | Min (ms) | Max (ms) |")
	fmt.Println("|----------|------|-----------|-------|---------|------|----------|-------------|----------|----------|")

	for _, r := range results {
		if r.TotalRequests == 0 {
			continue
		}
		cached := "No"
		if r.RRDCached {
			cached = "Yes"
		}
		fmt.Printf("| %-8s | %-8s | %-9s | %5d | %7d | %5.1f%% | %8d | %11d | %8d | %8d |\n",
			r.Protocol,
			r.Mode,
			cached,
			r.TotalRequests,
			r.SuccessCount,
			r.SuccessRate,
			r.AvgTimeMs,
			r.MedianTimeMs,
			r.MinTimeMs,
			r.MaxTimeMs,
		)
	}

	fmt.Println()
	fmt.Println(strings.Repeat("=", 140))
}

func writeResultsToCSV(results []*TestResult, resultsDir string) error {
	if len(results) == 0 {
		return nil
	}

	csvPath := filepath.Join(resultsDir, "e2e_results.csv")
	file, err := os.Create(csvPath)
	if err != nil {
		return fmt.Errorf("failed to create CSV file: %w", err)
	}
	defer file.Close()

	writer := csv.NewWriter(file)
	defer writer.Flush()

	// Write header
	header := []string{"Protocol", "Mode", "RRDCached", "Total", "Success", "Rate", "AvgMs", "MedianMs", "MinMs", "MaxMs", "Timestamp"}
	if err := writer.Write(header); err != nil {
		return fmt.Errorf("failed to write CSV header: %w", err)
	}

	timestamp := time.Now().Format(time.RFC3339)

	// Write data rows
	for _, r := range results {
		if r.TotalRequests == 0 {
			continue
		}
		cached := "No"
		if r.RRDCached {
			cached = "Yes"
		}
		row := []string{
			r.Protocol,
			r.Mode,
			cached,
			strconv.Itoa(r.TotalRequests),
			strconv.Itoa(r.SuccessCount),
			fmt.Sprintf("%.1f", r.SuccessRate),
			strconv.FormatInt(r.AvgTimeMs, 10),
			strconv.FormatInt(r.MedianTimeMs, 10),
			strconv.FormatInt(r.MinTimeMs, 10),
			strconv.FormatInt(r.MaxTimeMs, 10),
			timestamp,
		}
		if err := writer.Write(row); err != nil {
			return fmt.Errorf("failed to write CSV row: %w", err)
		}
	}

	fmt.Printf("Results written to %s\n", csvPath)
	return nil
}

func (tc *testCase) run(t *testing.T, binary, logPrefix, mode, protocol string) (*TestResult, error) {
	logFile := filepath.Join(tc.logDir, fmt.Sprintf("%s_%s.log", logPrefix, mode))
	results := make([]result, 0, tc.requests)

	var backendCmd *exec.Cmd
	var backendPID int
	var testConfigFile string

	// Create temp config with appropriate protocol
	testConfig := tc.config
	testConfig.Server.Protocol = protocol
	testConfigData, err := json.MarshalIndent(testConfig, "", "\t")
	if err != nil {
		return nil, fmt.Errorf("failed to marshal test config: %v", err)
	}
	testConfigFile = filepath.Join(os.TempDir(), fmt.Sprintf("svgd-e2e-%s-%d.json", protocol, time.Now().UnixNano()))
	if err := os.WriteFile(testConfigFile, testConfigData, 0644); err != nil {
		return nil, fmt.Errorf("failed to write test config: %v", err)
	}
	defer os.Remove(testConfigFile)

	// Start backend with test config
	backendPath := binPath("svgd")
	if err := checkFile(backendPath, true); err != nil {
		return nil, err
	}
	t.Logf("Starting %s server on port %d with binary %s and config %s\n", protocol, tc.config.Server.TCPPort, backendPath, testConfigFile)
	backendCmd = exec.Command(backendPath, testConfigFile)
	backendCmd.Dir = repoRoot // Set working directory to repo root for relative paths in config
	serverLog, err := os.OpenFile(logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return nil, fmt.Errorf("failed to open log file %s: %v", logFile, err)
	}
	backendCmd.Stdout = serverLog
	backendCmd.Stderr = serverLog
	if err := backendCmd.Start(); err != nil {
		return nil, fmt.Errorf("failed to start %s server: %v", protocol, err)
	}
	backendPID = backendCmd.Process.Pid
	t.Logf("%s server started with PID %d\n", protocol, backendPID)

	defer func() {
		t.Logf("Shutting down %s server (PID %d)\n", protocol, backendPID)
		if backendCmd != nil && backendCmd.Process != nil {
			backendCmd.Process.Signal(os.Interrupt)
			backendCmd.Wait()
		}
	}()

	time.Sleep(5 * time.Second)

	// Verify server is running
	if _, err := os.FindProcess(backendPID); err != nil {
		return nil, fmt.Errorf("backend failed to start, check %s", logFile)
	}

	// Verify server is listening
	cmd := exec.Command("lsof", "-i", fmt.Sprintf(":%d", tc.config.Server.TCPPort))
	if err := cmd.Run(); err != nil {
		t.Logf("Warning: backend not listening on port %d, check %s\n", tc.config.Server.TCPPort, logFile)
	}

	// Create client
	var (
		client     interface{}
		clientPort int
	)
	endpoint := tc.endpoint
	if protocol == "http" {
		clientPort = tc.config.Server.TCPPort // Direct to backend (native HTTP mode)
		client, err = http.NewClient("localhost", clientPort)
		if err != nil {
			return nil, fmt.Errorf("http.NewClient: %w", err)
		}
		endpoint = tc.httpEndpoint
	} else {
		clientPort = tc.config.Server.TCPPort // Backend port for LSRP
		lsrpClient, err := lsrp.NewClient("localhost", clientPort)
		if err != nil {
			return nil, fmt.Errorf("failed to create LSRP client for %s:%d: %v", "localhost", clientPort, err)
		}
		client = lsrpClient
		defer lsrpClient.Close() // Close only in parallel mode
	}

	// Open client error log
	errorLog, err := os.OpenFile(filepath.Join(tc.logDir, "client_errors.log"), os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return nil, fmt.Errorf("failed to open client error log: %v", err)
	}
	defer errorLog.Close()

	t.Logf("Running %d requests in %s mode for %s on port %d using %s protocol...\n", tc.requests, mode, logPrefix, clientPort, protocol)

	if mode == "sync" {
		resultChan := make(chan result, tc.requests)
		done := make(chan struct{})

		go func() {
			for res := range resultChan {
				results = append(results, res)
			}
			close(done) // ← сигнал, что закончили
		}()

		for i := 1; i <= tc.requests; i++ {
			tc.sendRequest(t, i, client, endpoint, errorLog, resultChan, protocol)
		}

		close(resultChan)
		<-done
	} else {
		resultChan := make(chan result, tc.requests)
		var wg sync.WaitGroup

		for i := 1; i <= tc.requests; i++ {
			wg.Add(1)
			go func(reqNum int) {
				defer wg.Done()
				tc.sendRequest(t, reqNum, client, endpoint, errorLog, resultChan, protocol)
			}(i)
		}
		wg.Wait()
		close(resultChan)
		for res := range resultChan {
			results = append(results, res)
		}
	}

	res := analyzeResults(logPrefix+" ("+mode+")", results)
	res.Protocol = protocol
	res.Mode = mode
	res.RRDCached = tc.config.Server.RRDCachedAddr != ""

	// Clean up SVG files
	if files, err := filepath.Glob(filepath.Join(tc.outputDir, "*.svg")); err == nil && len(files) > 0 {
		for _, file := range files {
			os.Remove(file)
		}
	}

	return res, nil
}

// Test_StatPanelSVG verifies stat panel generates valid SVG with expected elements
func Test_StatPanelSVG(t *testing.T) {
	// Skip if no uptime RRD file
	uptimeRrd := filepath.Join(tc.config.RRD.BasePath, "uptime/uptime.rrd")
	if _, err := os.Stat(uptimeRrd); os.IsNotExist(err) {
		t.Skip("Skipping: uptime RRD file not found")
	}

	// Create temp config with HTTP protocol for simpler testing
	testConfig := tc.config
	testConfig.Server.Protocol = "http"
	testConfigData, err := json.MarshalIndent(testConfig, "", "\t")
	if err != nil {
		t.Fatalf("Failed to marshal test config: %v", err)
	}
	testConfigFile := filepath.Join(os.TempDir(), fmt.Sprintf("svgd-stat-test-%d.json", time.Now().UnixNano()))
	if err := os.WriteFile(testConfigFile, testConfigData, 0644); err != nil {
		t.Fatalf("Failed to write test config: %v", err)
	}
	defer os.Remove(testConfigFile)

	// Start backend
	backendPath := binPath("svgd")
	backendCmd := exec.Command(backendPath, testConfigFile)
	backendCmd.Dir = repoRoot
	if err := backendCmd.Start(); err != nil {
		t.Fatalf("Failed to start backend: %v", err)
	}
	defer func() {
		backendCmd.Process.Signal(os.Interrupt)
		backendCmd.Wait()
	}()

	time.Sleep(2 * time.Second)

	// Request stat endpoint via HTTP
	client, err := http.NewClient("localhost", tc.config.Server.TCPPort)
	if err != nil {
		t.Fatalf("Failed to create HTTP client: %v", err)
	}

	resp, err := client.Send("system/uptime?period=3600")
	if err != nil {
		t.Fatalf("Failed to request stat endpoint: %v", err)
	}

	if resp.Status != 0 {
		t.Fatalf("Request failed with status %d: %s", resp.Status, string(resp.Data))
	}

	// Verify SVG structure
	svg := string(resp.Data)

	// Check title presence
	if !strings.Contains(svg, "System Uptime") {
		t.Error("Stat panel should contain title 'System Uptime'")
	}

	// Check sparkline path
	if !strings.Contains(svg, `<path d="M`) {
		t.Error("Stat panel should contain sparkline path")
	}

	t.Logf("Stat panel SVG validated successfully (%d bytes)", len(resp.Data))
}
