package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"svgd/tests/shared/benchmark"
	"svgd/tests/comparison/targets"
)

func main() {
	outputFlag := flag.String("output", "results.csv", "Output CSV file path")
	svgdOnly := flag.Bool("svgd-only", false, "Only benchmark svgd (skip Docker targets)")
	flag.Parse()

	fmt.Println("Cross-System Benchmark")
	fmt.Println("=======================")
	fmt.Printf("Output: %s\n\n", *outputFlag)

	ctx := context.Background()
	runner := NewBenchmarkRunner()
	var allResults []*benchmark.ComparisonRow

	// Get repo root (2 levels up from this file: comparison -> tests -> repo_root)
	repoRoot := filepath.Dir(filepath.Dir("."))
	if abs, err := filepath.Abs("."); err == nil {
		repoRoot = filepath.Dir(filepath.Dir(abs))
	}

	// Benchmark svgd
	svgdTarget := targets.NewSVGdTarget(
		filepath.Join(repoRoot, "bin", "svgd"),
		filepath.Join(repoRoot, "config.json"),
		8081,  // Port from config.json
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

	// Write results
	if err := benchmark.WriteCSV(allResults, *outputFlag); err != nil {
		fmt.Printf("Failed to write CSV: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("\nResults written to: %s\n", *outputFlag)
	fmt.Println("Benchmark complete!")
}
