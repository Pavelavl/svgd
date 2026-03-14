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

// RRDToolTarget implements BenchmarkTarget for RRDtool CGI
// This target requires Docker to be running and the image to be built.
// Build with: docker build -t benchmark-rrdtool tests/comparison/docker/rrdtool/
// Run with: docker run -d -p 8081:8080 -v /opt/collectd/var/lib/collectd/rrd:/var/lib/collectd/rrd:ro benchmark-rrdtool

type RRDToolTarget struct {
	url string
}

// NewRRDToolTarget creates a new RRDtool benchmark target
func NewRRDToolTarget() *RRDToolTarget {
	return &RRDToolTarget{
		url: "http://localhost:8083/graph",
	}
}

func (r *RRDToolTarget) Name() string {
	return "rrdtool"
}

func (r *RRDToolTarget) Setup(ctx context.Context) error {
	// Check if service is available
	for i := 0; i < 10; i++ {
		if r.HealthCheck(ctx) == nil {
			return nil
		}
		time.Sleep(500 * time.Millisecond)
	}
	return fmt.Errorf("rrdtool not available at %s. Make sure Docker container is running", r.url)
}

func (r *RRDToolTarget) Teardown(ctx context.Context) error {
	return nil // Container managed externally
}

func (r *RRDToolTarget) Endpoint() string {
	return r.url
}

func (r *RRDToolTarget) HealthCheck(ctx context.Context) error {
	resp, err := http.Get(r.url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return fmt.Errorf("health check failed: %d", resp.StatusCode)
	}
	return nil
}

func (r *RRDToolTarget) PID() int {
	return 0 // Container
}

func (r *RRDToolTarget) ContainerID() string {
	return "benchmark-rrdtool"
}

func (r *RRDToolTarget) MakeRequest(ctx context.Context) (float64, error) {
	start := time.Now()

	req, err := http.NewRequestWithContext(ctx, "GET", r.url, nil)
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
