package main

import (
	"bufio"
	"context"
	"fmt"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

// ProcessMetrics holds collected metrics for a process
type ProcessMetrics struct {
	Timestamp              time.Time
	CPUPercent             float64
	CPUTimeUser            uint64
	CPUTimeSystem          uint64
	MemRSSKB               uint64
	MemVSZKB               uint64
	IOReadBytes            uint64
	IOWriteBytes           uint64
	IOReadOps              uint64
	IOWriteOps             uint64
	NumThreads             int
	NumFDs                 int
	CtxSwitchesVoluntary   uint64
	CtxSwitchesInvoluntary uint64
	PageFaultsMinor        uint64
	PageFaultsMajor        uint64
}

// MetricsCollector holds collector state
type MetricsCollector struct {
	PID            int
	OutputFile     string
	SampleInterval time.Duration
	running        bool
	mu             sync.Mutex
}

func main() {
	if len(os.Args) < 3 {
		fmt.Fprintf(os.Stderr, "Usage: %s <pid> <output_csv> [sample_interval_sec]\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Example: %s 12345 metrics.csv 1\n", os.Args[0])
		os.Exit(1)
	}

	pid, err := strconv.Atoi(os.Args[1])
	if err != nil {
		fmt.Fprintf(os.Stderr, "Invalid PID: %v\n", err)
		os.Exit(1)
	}

	outputFile := os.Args[2]
	sampleInterval := 1 * time.Second
	if len(os.Args) >= 4 {
		seconds, err := strconv.Atoi(os.Args[3])
		if err != nil || seconds < 1 {
			seconds = 1
		}
		sampleInterval = time.Duration(seconds) * time.Second
	}

	// Check if process exists
	if !processExists(pid) {
		fmt.Fprintf(os.Stderr, "Process %d does not exist or not accessible\n", pid)
		os.Exit(1)
	}

	collector := &MetricsCollector{
		PID:            pid,
		OutputFile:     outputFile,
		SampleInterval: sampleInterval,
		running:        true,
	}

	fmt.Printf("Collecting metrics for PID %d\n", pid)
	fmt.Printf("Output file: %s\n", outputFile)
	fmt.Printf("Sample interval: %s\n", sampleInterval)
	fmt.Println("Press Ctrl+C to stop")

	if err := collector.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("\nMetrics collection stopped. Results saved to %s\n", outputFile)
}

// processExists checks if a process with given PID exists
func processExists(pid int) bool {
	if pid <= 0 {
		return false
	}
	process, err := os.FindProcess(pid)
	if err != nil {
		return false
	}
	err = process.Signal(syscall.Signal(0))
	return err == nil
}

// Run starts the metrics collection loop
func (c *MetricsCollector) Run() error {
	// Create context for graceful shutdown
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Handle signals
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		c.mu.Lock()
		c.running = false
		c.mu.Unlock()
		cancel()
	}()

	// Open output file
	f, err := os.Create(c.OutputFile)
	if err != nil {
		return fmt.Errorf("failed to create output file: %w", err)
	}
	defer f.Close()

	writer := bufio.NewWriter(f)
	defer writer.Flush()

	// Write CSV header
	header := "timestamp,cpu_percent,cpu_user,cpu_system,mem_rss_kb,mem_vsz_kb," +
		"io_read_bytes,io_write_bytes,io_read_ops,io_write_ops,threads,fds," +
		"ctx_switches_voluntary,ctx_switches_involuntary,page_faults_minor,page_faults_major\n"
	if _, err := writer.WriteString(header); err != nil {
		return fmt.Errorf("failed to write header: %w", err)
	}

	// Initial metrics collection for baseline
	var previous, current ProcessMetrics
	if err := c.collectMetrics(&previous); err != nil {
		return fmt.Errorf("failed to collect initial metrics: %w", err)
	}

	prevTime := time.Now()

	// Main collection loop
	ticker := time.NewTicker(c.SampleInterval)
	defer ticker.Stop()

	for {
		c.mu.Lock()
		running := c.running
		c.mu.Unlock()

		if !running {
			break
		}

		select {
		case <-ticker.C:
			now := time.Now()
			elapsed := now.Sub(prevTime).Seconds()

			if err := c.collectMetrics(&current); err != nil {
				fmt.Fprintf(os.Stderr, "Failed to collect metrics: %v\n", err)
				continue
			}

			// Calculate CPU percent
			if elapsed > 0 {
				prevTotal := previous.CPUTimeUser + previous.CPUTimeSystem
				currTotal := current.CPUTimeUser + current.CPUTimeSystem
				current.CPUPercent = calculateCPUPercent(prevTotal, currTotal, elapsed)
			} else {
				current.CPUPercent = 0
			}

			// Write metrics to CSV
			if err := c.writeMetrics(writer, &current); err != nil {
				fmt.Fprintf(os.Stderr, "Failed to write metrics: %v\n", err)
			}

			previous = current
			prevTime = now

		case <-ctx.Done():
			return nil
		}
	}

	return nil
}

// collectMetrics gathers all metrics for the target PID
func (c *MetricsCollector) collectMetrics(metrics *ProcessMetrics) error {
	metrics.Timestamp = time.Now()

	// Get CPU and memory metrics from /proc/[pid]/stat
	if err := c.getCPUMemMetrics(metrics); err != nil {
		return err
	}

	// Get IO metrics from /proc/[pid]/io
	if err := c.getIOMetrics(metrics); err != nil {
		return err
	}

	// Get context switches from /proc/[pid]/status
	if err := c.getContextSwitches(metrics); err != nil {
		return err
	}

	// Count file descriptors
	metrics.NumFDs = c.countFDs()

	return nil
}

// getCPUMemMetrics reads /proc/[pid]/stat
func (c *MetricsCollector) getCPUMemMetrics(metrics *ProcessMetrics) error {
	path := fmt.Sprintf("/proc/%d/stat", c.PID)
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}

	fields := strings.Fields(string(data))
	if len(fields) < 24 {
		return fmt.Errorf("invalid stat format")
	}

	// Parse fields from /proc/[pid]/stat
	// utime (field 14, index 13), stime (field 15, index 14)
	// minflt (field 10, index 9), majflt (field 12, index 11)
	// num_threads (field 20, index 19), vsize (field 23, index 22), rss (field 24, index 23)

	utime, _ := strconv.ParseUint(fields[13], 10, 64)
	stime, _ := strconv.ParseUint(fields[14], 10, 64)
	minflt, _ := strconv.ParseUint(fields[9], 10, 64)
	majflt, _ := strconv.ParseUint(fields[11], 10, 64)
	numThreads, _ := strconv.Atoi(fields[19])
	vsize, _ := strconv.ParseUint(fields[22], 10, 64)
	rss, _ := strconv.ParseUint(fields[23], 10, 64)

	metrics.CPUTimeUser = utime
	metrics.CPUTimeSystem = stime
	metrics.PageFaultsMinor = minflt
	metrics.PageFaultsMajor = majflt
	metrics.NumThreads = numThreads
	metrics.MemVSZKB = vsize / 1024
	metrics.MemRSSKB = rss * uint64(os.Getpagesize()/1024)

	return nil
}

// getIOMetrics reads /proc/[pid]/io
func (c *MetricsCollector) getIOMetrics(metrics *ProcessMetrics) error {
	path := fmt.Sprintf("/proc/%d/io", c.PID)
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.SplitN(line, ":", 2)
		if len(parts) != 2 {
			continue
		}

		key := strings.TrimSpace(parts[0])
		valueStr := strings.TrimSpace(parts[1])
		value, _ := strconv.ParseUint(valueStr, 10, 64)

		switch key {
		case "read_bytes":
			metrics.IOReadBytes = value
		case "write_bytes":
			metrics.IOWriteBytes = value
		case "syscr":
			metrics.IOReadOps = value
		case "syscw":
			metrics.IOWriteOps = value
		}
	}

	return scanner.Err()
}

// getContextSwitches reads /proc/[pid]/status
func (c *MetricsCollector) getContextSwitches(metrics *ProcessMetrics) error {
	path := fmt.Sprintf("/proc/%d/status", c.PID)
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.SplitN(line, ":", 2)
		if len(parts) != 2 {
			continue
		}

		key := strings.TrimSpace(parts[0])
		valueStr := strings.TrimSpace(parts[1])
		value, _ := strconv.ParseUint(valueStr, 10, 64)

		switch key {
		case "voluntary_ctxt_switches":
			metrics.CtxSwitchesVoluntary = value
		case "nonvoluntary_ctxt_switches":
			metrics.CtxSwitchesInvoluntary = value
		}
	}

	return scanner.Err()
}

// countFDs counts file descriptors in /proc/[pid]/fd
func (c *MetricsCollector) countFDs() int {
	path := fmt.Sprintf("/proc/%d/fd", c.PID)
	entries, err := os.ReadDir(path)
	if err != nil {
		return 0
	}

	count := 0
	for _, entry := range entries {
		if entry.Name()[0] != '.' {
			count++
		}
	}
	return count
}

// writeMetrics writes metrics to CSV
func (c *MetricsCollector) writeMetrics(writer *bufio.Writer, metrics *ProcessMetrics) error {
	timestamp := metrics.Timestamp.Unix()
	line := fmt.Sprintf("%d,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
		timestamp,
		metrics.CPUPercent,
		metrics.CPUTimeUser,
		metrics.CPUTimeSystem,
		metrics.MemRSSKB,
		metrics.MemVSZKB,
		metrics.IOReadBytes,
		metrics.IOWriteBytes,
		metrics.IOReadOps,
		metrics.IOWriteOps,
		metrics.NumThreads,
		metrics.NumFDs,
		metrics.CtxSwitchesVoluntary,
		metrics.CtxSwitchesInvoluntary,
		metrics.PageFaultsMinor,
		metrics.PageFaultsMajor)

	_, err := writer.WriteString(line)
	if err != nil {
		return err
	}
	return writer.Flush()
}

// calculateCPUPercent calculates CPU percentage from delta
func calculateCPUPercent(prevTotal, currTotal uint64, elapsedSec float64) float64 {
	if elapsedSec <= 0 {
		return 0
	}
	delta := float64(currTotal - prevTotal)
	hz := float64(os.Getpagesize()) // Approximate clock ticks
	return (delta * 100.0) / (hz * elapsedSec)
}
