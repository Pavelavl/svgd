//go:build !docker
// +build !docker

package main

import (
	"context"
	"fmt"

	"tests/pkg/benchmark"
)

func runDockerTargets(ctx context.Context, runner *BenchmarkRunner, allResults *[]*benchmark.ComparisonRow) {
	fmt.Println("\nDocker targets skipped (build with -tags docker to enable)")
	fmt.Println("Prerequisites:")
	fmt.Println("  1. Build Docker images:")
	fmt.Println("     docker build -t benchmark-rrdtool tests/comparison/docker/rrdtool/")
	fmt.Println("     docker build -t benchmark-graphite tests/comparison/docker/graphite/")
	fmt.Println("  2. Start containers:")
	fmt.Println("     docker run -d -p 8083:8080 -v /opt/collectd/var/lib/collectd/rrd:/var/lib/collectd/rrd:ro benchmark-rrdtool")
	fmt.Println("     docker run -d -p 8082:80 benchmark-graphite")
	fmt.Println("  3. Build and run:")
	fmt.Println("     cd tests/comparison && go build -tags docker && ./comparison")
}
