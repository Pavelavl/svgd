//go:build docker
// +build docker

package benchmark

import (
	"bufio"
	"context"
	"fmt"
	"os/exec"
	"strings"
	"time"
)

// DockerMetricsCollector collects metrics from Docker containers using docker stats
type DockerMetricsCollector struct {
	containerName string
	containerID   string
	interval      time.Duration
	ticker        *time.Ticker
	done          chan struct{}
	metrics       chan MetricsSummary
}

// NewDockerMetricsCollector creates a new Docker metrics collector
func NewDockerMetricsCollector(containerName string) *DockerMetricsCollector {
	return &DockerMetricsCollector{
		containerName: containerName,
		interval:      time.Second,
		done:          make(chan struct{}),
		metrics:       make(chan MetricsSummary, 10),
	}
}

// Start begins collecting metrics from the Docker container
func (d *DockerMetricsCollector) Start(ctx context.Context) error {
	// Discover container ID
	containerID, err := d.discoverContainerID(ctx)
	if err != nil {
		return fmt.Errorf("failed to discover container ID: %w", err)
	}
	d.containerID = containerID

	d.ticker = time.NewTicker(d.interval)

	go func() {
		for {
			select {
			case <-d.ticker.C:
				summary, err := d.Collect(ctx)
				if err == nil {
					select {
					case d.metrics <- summary:
					default:
						// Channel full, skip this metric
					}
				}
			case <-d.done:
				return
			case <-ctx.Done():
				return
			}
		}
	}()

	return nil
}

// Stop stops the metrics collection
func (d *DockerMetricsCollector) Stop() {
	if d.ticker != nil {
		d.ticker.Stop()
	}
	close(d.done)
}

// Collect runs docker stats and parses the output
func (d *DockerMetricsCollector) Collect(ctx context.Context) (MetricsSummary, error) {
	if d.containerID == "" {
		return MetricsSummary{}, fmt.Errorf("container ID not set")
	}

	// Run docker stats command
	cmd := exec.CommandContext(ctx, "docker", "stats", "--no-stream", "--format",
		"{{.CPUPerc}},{{.MemUsage}},{{.BlockIO}},{{.NetIO}}", d.containerID)

	output, err := cmd.Output()
	if err != nil {
		return MetricsSummary{}, fmt.Errorf("docker stats failed: %w", err)
	}

	return d.parseDockerStats(string(output))
}

// discoverContainerID gets the container ID from docker ps
func (d *DockerMetricsCollector) discoverContainerID(ctx context.Context) (string, error) {
	cmd := exec.CommandContext(ctx, "docker", "ps", "--filter",
		fmt.Sprintf("name=%s", d.containerName), "--format", "{{.ID}}")

	output, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("docker ps failed: %w", err)
	}

	containerID := strings.TrimSpace(string(output))
	if containerID == "" {
		return "", fmt.Errorf("container '%s' not found", d.containerName)
	}

	return containerID, nil
}

// parseDockerStats parses docker stats output into MetricsSummary
func (d *DockerMetricsCollector) parseDockerStats(output string) (MetricsSummary, error) {
	summary := MetricsSummary{}

	// Output format: CPUPerc,MemUsage,BlockIO,NetIO
	// Example: 1.50%,100MiB / 2GiB,50MB / 100MB,1GB / 500MB
	parts := strings.Split(strings.TrimSpace(output), ",")
	if len(parts) < 4 {
		return summary, fmt.Errorf("unexpected docker stats format: %s", output)
	}

	// Parse CPU percentage (e.g., "1.50%")
	cpuPerc := strings.TrimSuffix(parts[0], "%")
	cpuPerc = strings.TrimSpace(cpuPerc)
	if val, err := parseFloat(cpuPerc); err == nil {
		summary.CPUAvg = val
		summary.CPUMax = val
		summary.CPUMedian = val
	}

	// Parse memory usage (e.g., "100MiB / 2GiB")
	memParts := strings.Split(parts[1], "/")
	if len(memParts) >= 1 {
		memUsed := strings.TrimSpace(memParts[0])
		if val, err := parseMemorySize(memUsed); err == nil {
			summary.MemAvgMB = val
			summary.MemMaxMB = val
			summary.MemMedianMB = val
		}
	}

	// Parse Block I/O (e.g., "50MB / 100MB")
	ioParts := strings.Split(parts[2], "/")
	if len(ioParts) >= 2 {
		ioRead := strings.TrimSpace(ioParts[0])
		ioWrite := strings.TrimSpace(ioParts[1])
		if val, err := parseMemorySize(ioRead); err == nil {
			summary.IOReadMB = val
		}
		if val, err := parseMemorySize(ioWrite); err == nil {
			summary.IOWriteMB = val
		}
	}

	return summary, nil
}

// parseFloat parses a float string, handling various formats
func parseFloat(s string) (float64, error) {
	var val float64
	_, err := fmt.Sscanf(s, "%f", &val)
	return val, err
}

// parseMemorySize parses memory size strings like "100MiB", "2GiB", "50MB"
func parseMemorySize(s string) (float64, error) {
	s = strings.TrimSpace(s)
	if s == "" || s == "--" || s == "0B" {
		return 0, nil
	}

	var value float64
	var unit string

	// Use bufio scanner for more flexible parsing
	scanner := bufio.NewScanner(strings.NewReader(s))
	scanner.Split(bufio.ScanWords)

	if scanner.Scan() {
		if _, err := fmt.Sscanf(scanner.Text(), "%f", &value); err != nil {
			return 0, err
		}
	}

	// Find unit suffix in the string
	for _, suffix := range []string{"GiB", "MiB", "KiB", "GB", "MB", "KB", "B"} {
		if strings.HasSuffix(s, suffix) {
			unit = suffix
			break
		}
	}

	switch unit {
	case "GiB", "GB":
		return value * 1024, nil
	case "MiB", "MB":
		return value, nil
	case "KiB", "KB":
		return value / 1024, nil
	case "B":
		return value / (1024 * 1024), nil
	default:
		// Assume MB if no unit
		return value, nil
	}
}

// GetMetrics returns the channel for receiving metrics
func (d *DockerMetricsCollector) GetMetrics() <-chan MetricsSummary {
	return d.metrics
}

// ContainerID returns the discovered container ID
func (d *DockerMetricsCollector) ContainerID() string {
	return d.containerID
}
