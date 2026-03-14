package main

import (
	"context"
	"fmt"
	"sync"
	"time"

	"svgd/tests/shared/benchmark"
	"svgd/tests/comparison/targets"
)

// BenchmarkRunner executes benchmarks against targets
type BenchmarkRunner struct {
	scenarios           []benchmark.Scenario
	warmup              int
	metricsCollector    *benchmark.MetricsCollector
	dockerMetricsCollector *benchmark.DockerMetricsCollector
}

// NewBenchmarkRunner creates a new benchmark runner
func NewBenchmarkRunner() *BenchmarkRunner {
	return &BenchmarkRunner{
		scenarios: benchmark.DefaultScenarios,
		warmup:    10,
	}
}

// Run executes benchmarks against a target
func (r *BenchmarkRunner) Run(ctx context.Context, target targets.BenchmarkTarget) ([]*benchmark.ComparisonRow, error) {
	var results []*benchmark.ComparisonRow

	fmt.Printf("\n=== Benchmarking: %s ===\n", target.Name())

	// Setup
	fmt.Print("Starting... ")
	if err := target.Setup(ctx); err != nil {
		return nil, fmt.Errorf("setup failed: %w", err)
	}
	defer target.Teardown(ctx)
	fmt.Println("Ready")

	// Warmup
	fmt.Printf("Warming up (%d requests)... ", r.warmup)
	for i := 0; i < r.warmup; i++ {
		target.MakeRequest(ctx)
	}
	fmt.Println("Done")

	// Run scenarios
	for _, scenario := range r.scenarios {
		fmt.Printf("\nScenario: %s (concurrency=%d, requests=%d)\n",
			scenario.Name, scenario.Concurrency, scenario.Requests)

		row, err := r.runScenario(ctx, target, scenario)
		if err != nil {
			fmt.Printf("  ERROR: %v\n", err)
			continue
		}
		results = append(results, row)

		fmt.Printf("  RPS: %.2f, Latency P99: %.2fms\n", row.RPS, row.LatencyP99Ms)
	}

	return results, nil
}

func (r *BenchmarkRunner) runScenario(ctx context.Context, target targets.BenchmarkTarget, scenario benchmark.Scenario) (*benchmark.ComparisonRow, error) {
	// Start metrics collection if PID is available (for process targets like svgd)
	pid := target.PID()
	if pid > 0 {
		r.metricsCollector = benchmark.NewMetricsCollector(pid)
		if err := r.metricsCollector.Start(ctx); err != nil {
			fmt.Printf("  Warning: failed to start metrics collector: %v\n", err)
			r.metricsCollector = nil
		}
	} else {
		// Try Docker metrics collection for container targets
		containerID := target.ContainerID()
		if containerID != "" {
			r.dockerMetricsCollector = benchmark.NewDockerMetricsCollector(containerID)
			if err := r.dockerMetricsCollector.Start(ctx); err != nil {
				fmt.Printf("  Warning: failed to start Docker metrics collector: %v\n", err)
				r.dockerMetricsCollector = nil
			}
		}
	}

	results := make(chan benchmark.RequestResult, scenario.Requests)
	var wg sync.WaitGroup
	sem := make(chan struct{}, scenario.Concurrency)

	start := time.Now()

	for i := 0; i < scenario.Requests; i++ {
		wg.Add(1)
		sem <- struct{}{}

		go func(reqNum int) {
			defer wg.Done()
			defer func() { <-sem }()

			latency, err := target.MakeRequest(ctx)
			results <- benchmark.RequestResult{
				RequestNum: reqNum,
				Latency:    time.Duration(latency * float64(time.Millisecond)),
				Success:    err == nil,
				Error:      err,
			}
		}(i)
	}

	wg.Wait()
	close(results)
	duration := time.Since(start)

	// Stop metrics collection and analyze
	var metricsSummary *benchmark.MetricsSummary
	if r.metricsCollector != nil {
		r.metricsCollector.Stop()
		var err error
		metricsSummary, err = r.metricsCollector.Analyze()
		if err != nil {
			fmt.Printf("  Warning: failed to analyze metrics: %v\n", err)
		}
		r.metricsCollector = nil
	} else if r.dockerMetricsCollector != nil {
		r.dockerMetricsCollector.Stop()
		// Get final metrics from Docker collector
		select {
		case ms := <-r.dockerMetricsCollector.GetMetrics():
			metricsSummary = &ms
		default:
			// If no metrics in channel, collect one final sample
			ms, err := r.dockerMetricsCollector.Collect(ctx)
			if err != nil {
				fmt.Printf("  Warning: failed to collect final Docker metrics: %v\n", err)
			} else {
				metricsSummary = &ms
			}
		}
		r.dockerMetricsCollector = nil
	}

	br, _ := benchmark.AnalyzeResults(results, duration)

	row := &benchmark.ComparisonRow{
		System:       target.Name(),
		Scenario:     scenario.Name,
		Concurrency:  scenario.Concurrency,
		Requests:     scenario.Requests,
		RPS:          br.ThroughputRPS,
		SuccessRate:  br.SuccessRate,
		LatencyAvgMs: float64(br.AvgLatency.Microseconds()) / 1000,
		LatencyP50Ms: float64(br.MedianLatency.Microseconds()) / 1000,
		LatencyP95Ms: float64(br.P95Latency.Microseconds()) / 1000,
		LatencyP99Ms: float64(br.P99Latency.Microseconds()) / 1000,
	}

	// Add metrics summary to row if available
	if metricsSummary != nil {
		row.CPUAvgPct = metricsSummary.CPUAvg
		row.CPUMaxPct = metricsSummary.CPUMax
		row.MemAvgMB = metricsSummary.MemAvgMB
		row.MemMaxMB = metricsSummary.MemMaxMB
		row.IOReadMB = metricsSummary.IOReadMB
		row.IOWriteMB = metricsSummary.IOWriteMB
		row.CtxSwitchVolPS = metricsSummary.CtxSwitchVoluntaryPS
		row.CtxSwitchInvolPS = metricsSummary.CtxSwitchInvoluntaryPS
		row.PageFaultsMinorPS = metricsSummary.PageFaultsMinorPS
		row.ThreadsAvg = metricsSummary.ThreadsAvg
		row.FDsAvg = metricsSummary.FDsAvg
	}

	return row, nil
}
