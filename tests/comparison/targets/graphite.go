//go:build docker
// +build docker

package targets

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"time"
)

// GraphiteTarget implements BenchmarkTarget for Graphite
// This target requires Docker to be running.
// Run with: docker run -d -p 8082:80 graphiteapp/graphite-statsd:1.1.10

type GraphiteTarget struct {
	url string
}

// NewGraphiteTarget creates a new Graphite benchmark target
func NewGraphiteTarget() *GraphiteTarget {
	return &GraphiteTarget{
		url: "http://localhost:8082/render?target=cpu.*.value&format=svg&width=800&height=300&from=-1h",
	}
}

func (g *GraphiteTarget) Name() string {
	return "graphite"
}

func (g *GraphiteTarget) Setup(ctx context.Context) error {
	// Check if service is available (Graphite takes longer to start)
	for i := 0; i < 40; i++ {
		if g.HealthCheck(ctx) == nil {
			return nil
		}
		time.Sleep(500 * time.Millisecond)
	}
	return fmt.Errorf("graphite not available at localhost:8082. Make sure Docker container is running")
}

func (g *GraphiteTarget) Teardown(ctx context.Context) error {
	return nil // Container managed externally
}

func (g *GraphiteTarget) Endpoint() string {
	return g.url
}

func (g *GraphiteTarget) HealthCheck(ctx context.Context) error {
	resp, err := http.Get("http://localhost:8082/")
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode >= 500 {
		return fmt.Errorf("health check failed: %d", resp.StatusCode)
	}
	return nil
}

func (g *GraphiteTarget) PID() int {
	return 0 // Container
}

func (g *GraphiteTarget) ContainerID() string {
	return "benchmark-graphite"
}

func (g *GraphiteTarget) MakeRequest(ctx context.Context) (float64, error) {
	start := time.Now()

	req, err := http.NewRequestWithContext(ctx, "GET", g.url, nil)
	if err != nil {
		return 0, err
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return 0, err
	}
	defer resp.Body.Close()

	// Read and discard body
	io.Copy(io.Discard, resp.Body)

	latency := float64(time.Since(start).Microseconds()) / 1000
	return latency, nil
}
