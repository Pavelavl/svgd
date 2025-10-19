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

	"github.com/Pavelavl/go-lsrp"
)

var tc testCase

func TestMain(m *testing.M) {
	// Initialize test configuration
	tc.requests = 1
	tc.concurrency = 5
	tc.endpoint = "endpoint=cpu&period=3600"
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
	fmt.Printf("Using port %d from config.json\n", tc.port)
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

func Test_RRDCachedVersusWithoutIt(t *testing.T) {
	tests := []struct {
		binary    string
		logPrefix string
		mode      string
	}{
		{binary: "with_rrdcached", logPrefix: "with_rrdcached", mode: "sync"},
		{binary: "with_rrdcached", logPrefix: "with_rrdcached", mode: "parallel"},
		{binary: "without_rrdcached", logPrefix: "without_rrdcached", mode: "sync"},
		{binary: "without_rrdcached", logPrefix: "without_rrdcached", mode: "parallel"},
	}

	for _, tt := range tests {
		t.Run(fmt.Sprintf("%s_%s", tt.logPrefix, tt.mode), func(t *testing.T) {
			if err := tc.run(tt.binary, tt.logPrefix, tt.mode); err != nil {
				t.Errorf("Test failed for %s in %s mode: %v", tt.binary, tt.mode, err)
			}
		})
	}
}

type testCase struct {
	port        int
	requests    int
	concurrency int
	endpoint    string
	rrdFile     string
	outputDir   string
	logDir      string
	configFile  string
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

func (tc *testCase) readConfigPort() error {
	data, err := os.ReadFile(tc.configFile)
	if err != nil {
		return fmt.Errorf("failed to read %s: %v", tc.configFile, err)
	}
	var config struct {
		Server struct {
			TcpPort int `json:"tcp_port"`
		} `json:"server"`
	}
	if err := json.Unmarshal(data, &config); err != nil {
		return fmt.Errorf("invalid JSON in %s: %v", tc.configFile, err)
	}
	tc.port = config.Server.TcpPort
	return nil
}

func (tc testCase) checkPort() error {
	cmd := exec.Command("lsof", "-i", fmt.Sprintf(":%d", tc.port))
	if err := cmd.Run(); err == nil {
		cmdKill := exec.Command("kill", "-TERM", "$(lsof -t -i :"+strconv.Itoa(tc.port)+")")
		if err := cmdKill.Run(); err != nil {
			return fmt.Errorf("failed to free port %d: %v", tc.port, err)
		}
		time.Sleep(1 * time.Second)
		if err := cmd.Run(); err == nil {
			return fmt.Errorf("port %d still in use after attempting to free it", tc.port)
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

func singleRequest(requestNum int, client *lsrp.Client, endpoint, outputDir string, errorLog *os.File, results chan<- result) {
	outputFile := filepath.Join(outputDir, fmt.Sprintf("test_%d.svg", requestNum))

	start := time.Now()
	resp, err := client.Send(endpoint)
	elapsed := time.Since(start).Milliseconds()
	status := "FAIL"

	if err != nil {
		logEntry := fmt.Sprintf("Request %d failed: error=%v\n", requestNum, err)
		_, err := errorLog.WriteString(logEntry)
		if err != nil {
			fmt.Printf("Failed to write to error log: %v\n", err)
		}
		results <- result{requestNum, elapsed, status}
		return
	}

	if resp.Status == 0 {
		if err := os.WriteFile(outputFile, resp.Data, 0644); err != nil {
			logEntry := fmt.Sprintf("Request %d failed to write output: output_file=%s, error=%v\n", requestNum, outputFile, err)
			_, err := errorLog.WriteString(logEntry)
			if err != nil {
				fmt.Printf("Failed to write to error log: %v\n", err)
			}
		} else if checkSvg(outputFile) {
			status = "SUCCESS"
		} else {
			logEntry := fmt.Sprintf("Request %d failed: invalid SVG, output_file=%s\n", requestNum, outputFile)
			_, err := errorLog.WriteString(logEntry)
			if err != nil {
				fmt.Printf("Failed to write to error log: %v\n", err)
			}
		}
	} else {
		logEntry := fmt.Sprintf("Request %d failed: status=%d, data=%s\n", requestNum, resp.Status, string(resp.Data))
		_, err := errorLog.WriteString(logEntry)
		if err != nil {
			fmt.Printf("Failed to write to error log: %v\n", err)
		}
	}

	results <- result{requestNum, elapsed, status}
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

func (tc *testCase) run(binary, logPrefix, mode string) error {
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
	fmt.Printf("Starting %s server on port %d with binary ./bin/%s and config %s\n", logPrefix, tc.port, binary, tc.configFile)
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
	cmd = exec.Command("lsof", "-i", fmt.Sprintf(":%d", tc.port))
	if err := cmd.Run(); err != nil {
		fmt.Printf("Warning: %s server not listening on port %d, check %s\n", logPrefix, tc.port, logFile)
	}

	// Create LSRP client
	client, err := lsrp.NewClient("localhost", tc.port)
	if err != nil {
		return fmt.Errorf("failed to create LSRP client for %s:%d: %v", "localhost", tc.port, err)
	}
	defer client.Close()

	// Open client error log
	errorLog, err := os.OpenFile(filepath.Join(tc.logDir, "client_errors.log"), os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return fmt.Errorf("failed to open client error log: %v", err)
	}
	defer errorLog.Close()

	fmt.Printf("Running %d requests in %s mode for %s on port %d...\n", tc.requests, mode, logPrefix, tc.port)

	if mode == "sync" {
		resultChan := make(chan result, tc.requests)
		for i := 1; i <= tc.requests; i++ {
			singleRequest(i, client, tc.endpoint, tc.outputDir, errorLog, resultChan)
		}
		close(resultChan)
		for res := range resultChan {
			results = append(results, res)
		}
	} else {
		resultChan := make(chan result, tc.requests)
		var wg sync.WaitGroup
		semaphore := make(chan struct{}, tc.concurrency)

		for i := 1; i <= tc.requests; i++ {
			wg.Add(1)
			semaphore <- struct{}{}
			go func(reqNum int) {
				defer wg.Done()
				defer func() { <-semaphore }()
				singleRequest(reqNum, client, tc.endpoint, tc.outputDir, errorLog, resultChan)
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
