//go:build !docker
// +build !docker

package benchmark

import (
	"context"
)

// DockerMetricsCollector is a stub for when Docker is not available
type DockerMetricsCollector struct{}

// NewDockerMetricsCollector creates a stub collector
func NewDockerMetricsCollector(containerName string) *DockerMetricsCollector {
	return &DockerMetricsCollector{}
}

// Start returns an error since Docker is not available
func (d *DockerMetricsCollector) Start(ctx context.Context) error {
	return nil
}

// Stop does nothing
func (d *DockerMetricsCollector) Stop() {}

// Collect returns an error since Docker is not available
func (d *DockerMetricsCollector) Collect(ctx context.Context) (MetricsSummary, error) {
	return MetricsSummary{}, nil
}

// GetMetrics returns nil channel
func (d *DockerMetricsCollector) GetMetrics() <-chan MetricsSummary {
	return nil
}

// ContainerID returns empty string
func (d *DockerMetricsCollector) ContainerID() string {
	return ""
}
