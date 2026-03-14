//go:build docker
// +build docker

package main

import (
	"context"
	"fmt"

	"svgd/tests/comparison/targets"
	"svgd/tests/pkg/benchmark"
)

func runDockerTargets(ctx context.Context, runner *BenchmarkRunner, allResults *[]*benchmark.ComparisonRow) {
	// Benchmark RRDtool
	fmt.Println("\n--- RRDtool CGI ---")
	rrdtoolTarget := targets.NewRRDToolTarget()
	results, err := runner.Run(ctx, rrdtoolTarget)
	if err != nil {
		fmt.Printf("RRDtool benchmark failed: %v\n", err)
	} else {
		*allResults = append(*allResults, results...)
	}

	// Benchmark Graphite
	fmt.Println("\n--- Graphite ---")
	graphiteTarget := targets.NewGraphiteTarget()
	results, err = runner.Run(ctx, graphiteTarget)
	if err != nil {
		fmt.Printf("Graphite benchmark failed: %v\n", err)
	} else {
		*allResults = append(*allResults, results...)
	}
}
