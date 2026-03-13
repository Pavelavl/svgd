package benchmark

import (
	"encoding/csv"
	"fmt"
	"os"
	"path/filepath"
)

func WriteCSV(rows []*ComparisonRow, outputPath string) error {
	if len(rows) == 0 {
		return nil
	}

	if err := os.MkdirAll(filepath.Dir(outputPath), 0755); err != nil {
		return err
	}

	file, err := os.Create(outputPath)
	if err != nil {
		return fmt.Errorf("failed to create CSV file: %w", err)
	}
	defer file.Close()

	writer := csv.NewWriter(file)
	defer writer.Flush()

	header := []string{
		"system", "scenario", "concurrency", "requests",
		"rps", "success_rate",
		"latency_avg_ms", "latency_p50_ms", "latency_p95_ms", "latency_p99_ms",
		"cpu_avg_pct", "cpu_max_pct",
		"mem_avg_mb", "mem_max_mb",
		"io_read_mb", "io_write_mb",
		"ctx_switch_vol_ps", "ctx_switch_invol_ps",
		"page_faults_minor_ps", "threads_avg", "fds_avg",
	}
	if err := writer.Write(header); err != nil {
		return fmt.Errorf("failed to write CSV header: %w", err)
	}

	for _, row := range rows {
		record := []string{
			row.System,
			row.Scenario,
			fmt.Sprintf("%d", row.Concurrency),
			fmt.Sprintf("%d", row.Requests),
			fmt.Sprintf("%.2f", row.RPS),
			fmt.Sprintf("%.2f", row.SuccessRate),
			fmt.Sprintf("%.2f", row.LatencyAvgMs),
			fmt.Sprintf("%.2f", row.LatencyP50Ms),
			fmt.Sprintf("%.2f", row.LatencyP95Ms),
			fmt.Sprintf("%.2f", row.LatencyP99Ms),
			fmt.Sprintf("%.2f", row.CPUAvgPct),
			fmt.Sprintf("%.2f", row.CPUMaxPct),
			fmt.Sprintf("%.2f", row.MemAvgMB),
			fmt.Sprintf("%.2f", row.MemMaxMB),
			fmt.Sprintf("%.2f", row.IOReadMB),
			fmt.Sprintf("%.2f", row.IOWriteMB),
			fmt.Sprintf("%.2f", row.CtxSwitchVolPS),
			fmt.Sprintf("%.2f", row.CtxSwitchInvolPS),
			fmt.Sprintf("%.2f", row.PageFaultsMinorPS),
			fmt.Sprintf("%.2f", row.ThreadsAvg),
			fmt.Sprintf("%.2f", row.FDsAvg),
		}
		if err := writer.Write(record); err != nil {
			return fmt.Errorf("failed to write CSV row: %w", err)
		}
	}

	return nil
}
