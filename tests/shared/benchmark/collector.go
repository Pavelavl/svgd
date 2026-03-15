package benchmark

import (
	"bufio"
	"context"
	"fmt"
	"os"
	"sort"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

// ProcessMetrics holds collected metrics for a process at a point in time
type ProcessMetrics struct {
	Timestamp           time.Time
	CPUPercent         float64
	CPUTimeUser        uint64
	CPUTimeSystem      uint64
	MemRSSKB           uint64
	MemVSZKB           uint64
	IOReadBytes        uint64
	IOWriteBytes       uint64
	IOReadOps          uint64
	IOWriteOps         uint64
	NumThreads         int
	NumFDs             int
	CtxSwitchesVoluntary    uint64
	CtxSwitchesInvoluntary  uint64
	PageFaultsMinor    uint64
	PageFaultsMajor    uint64
}

// MetricsCollector collects system metrics for a process and sends results via channel
type MetricsCollector struct {
	PID            int
	sampleInterval  time.Duration
	metricsChan    chan ProcessMetrics
	samples        []ProcessMetrics
	mu             sync.Mutex
	cancel         context.CancelFunc
	running        bool
}

// NewMetricsCollector creates a new metrics collector for the given PID
func NewMetricsCollector(pid int) *MetricsCollector {
	return &MetricsCollector{
		PID:           pid,
		sampleInterval: time.Second,
		metricsChan:   make(chan ProcessMetrics, 100),
		samples:       make([]ProcessMetrics, 0),
		running:       false,
	}
}

// Metrics returns the channel that receives metric samples
func (mc *MetricsCollector) Metrics() <-chan ProcessMetrics {
	return mc.metricsChan
}

// Start begins collecting metrics in a background goroutine
func (mc *MetricsCollector) Start(ctx context.Context) error {
	if !mc.processExists(mc.PID) {
		return fmt.Errorf("process %d does not exist or not accessible", mc.PID)
	}

	ctx, cancel := context.WithCancel(ctx)
	mc.cancel = cancel
	mc.running = true

	// Initial sample for baseline
	var firstSample ProcessMetrics
	if err := mc.collectMetrics(&firstSample); err != nil {
		cancel()
		return fmt.Errorf("failed to collect initial metrics: %w", err)
	}

	mc.mu.Lock()
	mc.samples = append(mc.samples, firstSample)
	mc.metricsChan <- firstSample
	mc.mu.Unlock()

	prevSample := firstSample
	prevTime := time.Now()

	// Collection goroutine
	go func() {
		defer close(mc.metricsChan)
		defer cancel()
		defer func() { mc.running = false }()

		ticker := time.NewTicker(mc.sampleInterval)
		defer ticker.Stop()

		for {
			select {
			case <-ticker.C:
				if !mc.running {
					return
				}

				now := time.Now()
				var sample ProcessMetrics

				if err := mc.collectMetrics(&sample); err != nil {
					continue
				}

				// Calculate CPU percent
				elapsed := now.Sub(prevTime).Seconds()
				if elapsed > 0 {
					prevTotal := prevSample.CPUTimeUser + prevSample.CPUTimeSystem
					currTotal := sample.CPUTimeUser + sample.CPUTimeSystem
					sample.CPUPercent = mc.calculateCPUPercent(prevTotal, currTotal, elapsed)
				} else {
					sample.CPUPercent = 0
				}

				mc.mu.Lock()
				mc.samples = append(mc.samples, sample)
				mc.mu.Unlock()

				// Send metrics via channel (non-blocking)
				select {
				case mc.metricsChan <- sample:
				default:
					// Channel full, skip this sample
				}

				prevSample = sample
				prevTime = now

			case <-ctx.Done():
				return
			}
		}
	}()

	return nil
}

// Stop stops metrics collection
func (mc *MetricsCollector) Stop() {
	if mc.cancel != nil {
		mc.running = false
		mc.cancel()
	}
}

// IsRunning returns whether the collector is currently running
func (mc *MetricsCollector) IsRunning() bool {
	return mc.running
}

// processExists checks if a process with given PID exists
func (mc *MetricsCollector) processExists(pid int) bool {
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

// collectMetrics gathers all metrics for the target PID
func (mc *MetricsCollector) collectMetrics(metrics *ProcessMetrics) error {
	metrics.Timestamp = time.Now()

	// Get CPU and memory metrics from /proc/[pid]/stat
	if err := mc.getCPUMemMetrics(metrics); err != nil {
		return err
	}

	// Get IO metrics from /proc/[pid]/io
	if err := mc.getIOMetrics(metrics); err != nil {
		return err
	}

	// Get context switches from /proc/[pid]/status
	if err := mc.getContextSwitches(metrics); err != nil {
		return err
	}

	// Count file descriptors
	metrics.NumFDs = mc.countFDs()

	return nil
}

// getCPUMemMetrics reads /proc/[pid]/stat
func (mc *MetricsCollector) getCPUMemMetrics(metrics *ProcessMetrics) error {
	path := fmt.Sprintf("/proc/%d/stat", mc.PID)
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}

	fields := strings.Fields(string(data))
	if len(fields) < 24 {
		return fmt.Errorf("invalid stat format")
	}

	// Parse fields from /proc/[pid]/stat
	// Index: 13=utime, 14=stime, 9=minflt, 11=majflt, 19=num_threads, 22=vsize, 23=rss
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
func (mc *MetricsCollector) getIOMetrics(metrics *ProcessMetrics) error {
	path := fmt.Sprintf("/proc/%d/io", mc.PID)
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
func (mc *MetricsCollector) getContextSwitches(metrics *ProcessMetrics) error {
	path := fmt.Sprintf("/proc/%d/status", mc.PID)
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
func (mc *MetricsCollector) countFDs() int {
	path := fmt.Sprintf("/proc/%d/fd", mc.PID)
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

// calculateCPUPercent calculates CPU percentage from delta
func (mc *MetricsCollector) calculateCPUPercent(prevTotal, currTotal uint64, elapsedSec float64) float64 {
	if elapsedSec <= 0 {
		return 0
	}
	delta := float64(currTotal - prevTotal)
	hz := float64(os.Getpagesize()) // Approximate clock ticks
	return (delta * 100.0) / (hz * elapsedSec)
}

// Analyze computes summary statistics from collected samples
func (mc *MetricsCollector) Analyze() (*MetricsSummary, error) {
	mc.mu.Lock()
	samples := make([]ProcessMetrics, len(mc.samples))
	copy(samples, mc.samples)
	mc.mu.Unlock()

	if len(samples) < 2 {
		return nil, fmt.Errorf("insufficient metrics data: need at least 2 samples, got %d", len(samples))
	}

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

	for i, sample := range samples {
		cpuValues = append(cpuValues, sample.CPUPercent)
		if sample.CPUPercent > summary.CPUMax {
			summary.CPUMax = sample.CPUPercent
		}

		memMB := float64(sample.MemRSSKB) / 1024.0
		memValues = append(memValues, memMB)
		if memMB > summary.MemMaxMB {
			summary.MemMaxMB = memMB
		}

		threadValues = append(threadValues, sample.NumThreads)
		if sample.NumThreads > summary.ThreadsMax {
			summary.ThreadsMax = sample.NumThreads
		}

		fdValues = append(fdValues, sample.NumFDs)
		if sample.NumFDs > summary.FDsMax {
			summary.FDsMax = sample.NumFDs
		}

		if i == 0 {
			firstIORead = sample.IOReadBytes
			firstIOWrite = sample.IOWriteBytes
			firstIOReadOps = sample.IOReadOps
			firstIOWriteOps = sample.IOWriteOps
			firstCtxVoluntary = sample.CtxSwitchesVoluntary
			firstCtxInvoluntary = sample.CtxSwitchesInvoluntary
			firstPageFaultsMinor = sample.PageFaultsMinor
			firstPageFaultsMajor = sample.PageFaultsMajor
		}
		lastIORead = sample.IOReadBytes
		lastIOWrite = sample.IOWriteBytes
		lastIOReadOps = sample.IOReadOps
		lastIOWriteOps = sample.IOWriteOps
		lastCtxVoluntary = sample.CtxSwitchesVoluntary
		lastCtxInvoluntary = sample.CtxSwitchesInvoluntary
		lastPageFaultsMinor = sample.PageFaultsMinor
		lastPageFaultsMajor = sample.PageFaultsMajor
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
		for _, v := range threadValues {
			summary.ThreadsAvg += float64(v)
		}
		summary.ThreadsAvg /= float64(len(threadValues))
	}

	if len(fdValues) > 0 {
		for _, v := range fdValues {
			summary.FDsAvg += float64(v)
		}
		summary.FDsAvg /= float64(len(fdValues))
	}

	// Calculate per-second metrics
	duration := float64(len(samples))
	if duration > 0 {
		summary.IOReadMB = float64(lastIORead-firstIORead) / (1024 * 1024)
		summary.IOWriteMB = float64(lastIOWrite-firstIOWrite) / (1024 * 1024)
		summary.IOReadOpsPS = float64(lastIOReadOps-firstIOReadOps) / duration
		summary.IOWriteOpsPS = float64(lastIOWriteOps-firstIOWriteOps) / duration
		summary.CtxSwitchVoluntaryPS = float64(lastCtxVoluntary-firstCtxVoluntary) / duration
		summary.CtxSwitchInvoluntaryPS = float64(lastCtxInvoluntary-firstCtxInvoluntary) / duration
		summary.PageFaultsMinorPS = float64(lastPageFaultsMinor-firstPageFaultsMinor) / duration
		summary.PageFaultsMajorPS = float64(lastPageFaultsMajor-firstPageFaultsMajor) / duration
	}

	return summary, nil
}

// GetSamples returns a copy of all collected samples
func (mc *MetricsCollector) GetSamples() []ProcessMetrics {
	mc.mu.Lock()
	defer mc.mu.Unlock()

	samples := make([]ProcessMetrics, len(mc.samples))
	copy(samples, mc.samples)
	return samples
}

// Reset clears all collected samples
func (mc *MetricsCollector) Reset() {
	mc.mu.Lock()
	mc.samples = mc.samples[:0]
	mc.mu.Unlock()
}
