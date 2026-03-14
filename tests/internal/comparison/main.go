package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"tests/internal/comparison/targets"
	"tests/pkg/benchmark"
	"tests/pkg/system"
)

func main() {
	svgdOnly := flag.Bool("svgd-only", false, "Only benchmark svgd (skip Docker targets)")
	flag.Parse()

	fmt.Println("Cross-System Benchmark")
	fmt.Println("=======================")

	ctx := context.Background()
	runner := NewBenchmarkRunner()
	var allResults []*benchmark.ComparisonRow

	// Get repo root (3 levels up from this file: comparison -> tests -> repo_root)
	repoRoot := filepath.Dir(filepath.Dir(".."))
	if abs, err := filepath.Abs("."); err == nil {
		repoRoot = filepath.Dir(filepath.Dir(abs))
	}

	// Benchmark svgd
	svgdTarget := targets.NewSVGdTarget(
		filepath.Join(repoRoot, "bin", "svgd"),
		filepath.Join(repoRoot, "config.json"),
		8081, // Port from config.json
		"lsrp",
	)
	results, err := runner.Run(ctx, svgdTarget)
	if err != nil {
		fmt.Printf("svgd benchmark failed: %v\n", err)
	} else {
		allResults = append(allResults, results...)
	}

	// Run Docker targets if not svgd-only
	if !*svgdOnly {
		runDockerTargets(ctx, runner, &allResults)
	}

	// Write results using unified reporter
	reporter, err := system.NewReporter("comparison")
	if err != nil {
		fmt.Printf("Failed to create reporter: %v\n", err)
		os.Exit(1)
	}
	defer reporter.Close()

	for _, row := range allResults {
		record := map[string]any{
			"system":               row.System,
			"scenario":             row.Scenario,
			"concurrency":          row.Concurrency,
			"requests":             row.Requests,
			"rps":                  row.RPS,
			"success_rate":         row.SuccessRate,
			"latency_avg_ms":       row.LatencyAvgMs,
			"latency_p50_ms":       row.LatencyP50Ms,
			"latency_p95_ms":       row.LatencyP95Ms,
			"latency_p99_ms":       row.LatencyP99Ms,
			"cpu_avg_pct":          row.CPUAvgPct,
			"cpu_max_pct":          row.CPUMaxPct,
			"mem_avg_mb":           row.MemAvgMB,
			"mem_max_mb":           row.MemMaxMB,
			"io_read_mb":           row.IOReadMB,
			"io_write_mb":          row.IOWriteMB,
			"ctx_switch_vol_ps":    row.CtxSwitchVolPS,
			"ctx_switch_invol_ps":  row.CtxSwitchInvolPS,
			"page_faults_minor_ps": row.PageFaultsMinorPS,
			"threads_avg":          row.ThreadsAvg,
			"fds_avg":              row.FDsAvg,
		}
		if err := reporter.Record(record); err != nil {
			fmt.Printf("Failed to record result: %v\n", err)
		}
	}

	fmt.Println("\nBenchmark complete!")
}
