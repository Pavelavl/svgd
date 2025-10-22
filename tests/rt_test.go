package main

import (
	"encoding/json"
	"fmt"
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

var tc testCase

func TestMain(m *testing.M) {
	// Initialize test configuration
	tc.requests = 100
	tc.concurrency = 5
	tc.endpoint = "endpoint=cpu&period=3600" // LSRP format
	tc.httpEndpoint = "cpu"                  // HTTP format
	tc.rrdFile = "/opt/collectd/var/lib/collectd/rrd/localhost/cpu-total/percent-active.rrd"
	tc.outputDir = "temp_svgs"
	tc.logDir = "logs"
	tc.configFile = "../config.json"

	// Create directories
	if err := os.MkdirAll(tc.outputDir, 0755); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to create directory %s: %v\n", tc.outputDir, err)
		os.Exit(1)
	}
	if err := os.MkdirAll(tc.logDir, 0755); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to create directory %s: %v\n", tc.logDir, err)
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
	if err := tc.readConfigPort(); err != nil {
		fmt.Fprintf(os.Stderr, "%v\n", err)
		os.Exit(1)
	}
	fmt.Printf("Using port %d from config.json\n", tc.config.Server.TcpPort)
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

func Test_HttpVersusLsrp(t *testing.T) {
	tests := []struct {
		binary    string
		logPrefix string
		mode      string
		protocol  string // "http" or "lsrp"
	}{
		// {binary: "http_with_rrdcached", logPrefix: "http_with_rrdcached", mode: "sync", protocol: "http"},
		// {binary: "http_with_rrdcached", logPrefix: "http_with_rrdcached", mode: "parallel", protocol: "http"},
		// {binary: "http_without_rrdcached", logPrefix: "http_without_rrdcached", mode: "sync", protocol: "http"},
		// {binary: "http_without_rrdcached", logPrefix: "http_without_rrdcached", mode: "parallel", protocol: "http"},
		{binary: "lsrp_with_rrdcached", logPrefix: "lsrp_with_rrdcached", mode: "sync", protocol: "lsrp"},
		{binary: "lsrp_with_rrdcached", logPrefix: "lsrp_with_rrdcached", mode: "parallel", protocol: "lsrp"},
		{binary: "lsrp_without_rrdcached", logPrefix: "lsrp_without_rrdcached", mode: "sync", protocol: "lsrp"},
		{binary: "lsrp_without_rrdcached", logPrefix: "lsrp_without_rrdcached", mode: "parallel", protocol: "lsrp"},
	}

	for _, tt := range tests {
		t.Run(fmt.Sprintf("%s_%s", tt.logPrefix, tt.mode), func(t *testing.T) {
			if err := tc.run(tt.binary, tt.logPrefix, tt.mode, tt.protocol); err != nil {
				t.Errorf("Test failed for %s in %s mode: %v", tt.binary, tt.mode, err)
			}
			tc.config.Server.TcpPort += 1
			if err := tc.updateConfig(); err != nil {
				t.Error(err)
			}
		})
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

func (tc *testCase) readConfigPort() error {
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
	data, err := json.Marshal(tc.config)
	if err != nil {
		return err
	}
	return os.WriteFile(tc.configFile, data, 0644)
}

func (tc testCase) checkPort() error {
	cmd := exec.Command("lsof", "-i", fmt.Sprintf(":%d", tc.config.Server.TcpPort))
	if err := cmd.Run(); err == nil {
		cmdKill := exec.Command("kill", "-TERM", "$(lsof -t -i :"+strconv.Itoa(tc.config.Server.TcpPort)+")")
		if err := cmdKill.Run(); err != nil {
			return fmt.Errorf("failed to free port %d: %v", tc.config.Server.TcpPort, err)
		}
		time.Sleep(1 * time.Second)
		if err := cmd.Run(); err == nil {
			return fmt.Errorf("port %d still in use after attempting to free it", tc.config.Server.TcpPort)
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

func (tc testCase) sendRequest(requestNum int, client interface{}, endpoint string, errorLog *os.File, results chan<- result, protocol string) {
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
			errorLog.WriteString(logEntry)
			results <- result{requestNum, time.Since(start).Milliseconds(), "FAIL"}
			return
		}
		lsrpResp, err := lsrpClient.Send(endpoint)
		if err != nil {
			logEntry := fmt.Sprintf("Request %d failed: %s\n", requestNum, err)
			errorLog.WriteString(logEntry)
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
		fmt.Printf("Failed to write to error log: %v\n", err)
	}

	if status == 0 {
		if err := os.WriteFile(outputFile, data, 0644); err != nil {
			logEntry := fmt.Sprintf("Request %d failed to write output: output_file=%s, error=%v\n", requestNum, outputFile, err)
			_, err := errorLog.WriteString(logEntry)
			if err != nil {
				fmt.Printf("Failed to write to error log: %v\n", err)
			}
		} else if checkSvg(outputFile) {
			resultStatus = "SUCCESS"
		} else {
			logEntry := fmt.Sprintf("Request %d failed: invalid SVG, output_file=%s\n", requestNum, outputFile)
			_, err := errorLog.WriteString(logEntry)
			if err != nil {
				fmt.Printf("Failed to write to error log: %v\n", err)
			}
		}
	} else {
		logEntry := fmt.Sprintf("Request %d failed: status=%d, data=%s\n", requestNum, status, string(data))
		_, err := errorLog.WriteString(logEntry)
		if err != nil {
			fmt.Printf("Failed to write to error log: %v\n", err)
		}
	}

	results <- result{requestNum, elapsed, resultStatus}
}

func analyzeResults(logPrefix string, results []result) {
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
		fmt.Printf("=== Results for %s ===\n", logPrefix)
		fmt.Println("Error: No valid request data found")
		fmt.Printf("Server log: %s/%s_server.log\n", tc.logDir, logPrefix)
		fmt.Printf("Client errors: %s/client_errors.log\n", tc.logDir)
		fmt.Println("==============================")
		return
	}

	avgTime := totalTime / int64(count)
	successRate := (successCount * 100) / count

	// Calculate median
	sort.Slice(times, func(i, j int) bool { return times[i] < times[j] })
	var medianTime int64
	if len(times)%2 == 0 {
		mid := len(times) / 2
		medianTime = (times[mid-1] + times[mid]) / 2
	} else {
		medianTime = times[len(times)/2]
	}

	fmt.Printf("=== Results for %s ===\n", logPrefix)
	fmt.Printf("Total requests: %d\n", count)
	fmt.Printf("Successful requests: %d (%d%%)\n", successCount, successRate)
	fmt.Printf("Average time: %d ms\n", avgTime)
	fmt.Printf("Median time: %d ms\n", medianTime)
	fmt.Printf("Min time: %d ms\n", minTime)
	fmt.Printf("Max time: %d ms\n", maxTime)
	fmt.Printf("Server log: %s/%s_server.log\n", tc.logDir, logPrefix)
	fmt.Printf("Client errors: %s/client_errors.log\n", tc.logDir)
	fmt.Println("==============================")
}

func (tc *testCase) run(binary, logPrefix, mode, protocol string) error {
	logFile := filepath.Join(tc.logDir, fmt.Sprintf("%s_%s.log", logPrefix, mode))
	results := make([]result, 0, tc.requests)

	// Check binary
	if err := checkFile("./bin/"+binary, true); err != nil {
		return err
	}

	// Check dependencies
	cmd := exec.Command("ldd", "./bin/"+binary)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("dependency check failed for ./bin/%s: %v", binary, err)
	}

	// Start server
	fmt.Printf("Starting %s server on port %d with binary ./bin/%s and config %s\n", logPrefix, tc.config.Server.TcpPort, binary, tc.configFile)
	serverCmd := exec.Command("./bin/"+binary, tc.configFile)
	serverLog, err := os.OpenFile(logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return fmt.Errorf("failed to open log file %s: %v", logFile, err)
	}
	defer serverLog.Close()
	serverCmd.Stdout = serverLog
	serverCmd.Stderr = serverLog
	if err := serverCmd.Start(); err != nil {
		return fmt.Errorf("failed to start %s server: %v", logPrefix, err)
	}
	defer serverCmd.Process.Kill()
	time.Sleep(2 * time.Second)

	// Verify server is running
	if _, err := os.FindProcess(serverCmd.Process.Pid); err != nil {
		return fmt.Errorf("server %s failed to start, check %s", logPrefix, logFile)
	}

	// Verify server is listening
	cmd = exec.Command("lsof", "-i", fmt.Sprintf(":%d", tc.config.Server.TcpPort))
	if err := cmd.Run(); err != nil {
		fmt.Printf("Warning: %s server not listening on port %d, check %s\n", logPrefix, tc.config.Server.TcpPort, logFile)
	}

	// Create client
	var client interface{}
	endpoint := tc.endpoint
	if protocol == "http" {
		client, err = http.NewClient("localhost", tc.config.Server.TcpPort)
		if err != nil {
			return fmt.Errorf("http.NewClient: %w", err)
		}
		endpoint = tc.httpEndpoint
	} else {
		lsrpClient, err := lsrp.NewClient("localhost", tc.config.Server.TcpPort)
		if err != nil {
			return fmt.Errorf("failed to create LSRP client for %s:%d: %v", "localhost", tc.config.Server.TcpPort, err)
		}
		client = lsrpClient
		defer lsrpClient.Close()
	}

	// Open client error log
	errorLog, err := os.OpenFile(filepath.Join(tc.logDir, "client_errors.log"), os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return fmt.Errorf("failed to open client error log: %v", err)
	}
	defer errorLog.Close()

	fmt.Printf("Running %d requests in %s mode for %s on port %d using %s protocol...\n", tc.requests, mode, logPrefix, tc.config.Server.TcpPort, protocol)

	if mode == "sync" {
		resultChan := make(chan result, tc.requests)
		for i := 1; i <= tc.requests; i++ {
			tc.sendRequest(i, client, endpoint, errorLog, resultChan, protocol)
		}
		close(resultChan)
		for res := range resultChan {
			results = append(results, res)
		}
	} else {
		resultChan := make(chan result, tc.requests)
		var wg sync.WaitGroup

		for i := 1; i <= tc.requests; i++ {
			wg.Add(1)
			go func(reqNum int) {
				defer wg.Done()
				tc.sendRequest(reqNum, client, endpoint, errorLog, resultChan, protocol)
			}(i)
		}
		wg.Wait()
		close(resultChan)
		for res := range resultChan {
			results = append(results, res)
		}
	}

	analyzeResults(logPrefix+" ("+mode+")", results)

	// Clean up SVG files
	if files, err := filepath.Glob(filepath.Join(tc.outputDir, "*.svg")); err == nil && len(files) > 0 {
		for _, file := range files {
			os.Remove(file)
		}
	}

	return nil
}
