package benchmark

import (
	"encoding/csv"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
)

type MetricsCollector struct {
	cmd         *exec.Cmd
	metricsFile string
}

func NewMetricsCollector(pid int, outputPath string) *MetricsCollector {
	return &MetricsCollector{
		metricsFile: outputPath,
	}
}

func (mc *MetricsCollector) Start(pid int) error {
	// Get the directory of this file
	_, filename, _, ok := runtime.Caller(0)
	if !ok {
		return fmt.Errorf("failed to get current file path")
	}
	thisDir := filepath.Dir(filename)

	// Get repo root: from tests/shared/benchmark -> .. -> tests, .. -> svgd (repo root)
	repoRoot := filepath.Join(thisDir, "..", "..", "..")

	collectorPath := filepath.Join(repoRoot, "bin", "metrics_collector")
	collectorSource := filepath.Join(repoRoot, "tests", "metrics_collector.c")

	if _, err := os.Stat(collectorPath); os.IsNotExist(err) {
		fmt.Println("Building metrics_collector...")
		buildCmd := exec.Command("gcc", "-o", collectorPath, collectorSource, "-pthread")
		if output, err := buildCmd.CombinedOutput(); err != nil {
			return fmt.Errorf("failed to build metrics_collector: %w\n%s", err, output)
		}
	}

	cmd := exec.Command(collectorPath, strconv.Itoa(pid), mc.metricsFile, "1")
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start metrics collector: %w", err)
	}

	mc.cmd = cmd
	return nil
}

func (mc *MetricsCollector) Stop() error {
	if mc.cmd != nil && mc.cmd.Process != nil {
		mc.cmd.Process.Signal(os.Interrupt)
	}
	return nil
}

func (mc *MetricsCollector) Analyze() (*MetricsSummary, error) {
	file, err := os.Open(mc.metricsFile)
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

	records = records[1:]
	summary := &MetricsSummary{}

	cpuValues := make([]float64, 0)
	memValues := make([]float64, 0)

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

		if threads > summary.ThreadsMax {
			summary.ThreadsMax = threads
		}
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

	duration := float64(len(records))
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
