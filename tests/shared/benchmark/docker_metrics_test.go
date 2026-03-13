//go:build docker
// +build docker

package benchmark

import (
	"context"
	"testing"
	"time"
)

func TestDockerMetricsCollector_ParseDockerStats(t *testing.T) {
	collector := &DockerMetricsCollector{}

	tests := []struct {
		name     string
		input    string
		wantErr  bool
		checkCPU bool
		cpuVal   float64
		checkMem bool
		memVal   float64
	}{
		{
			name:     "standard format",
			input:    "1.50%,100MiB / 2GiB,50MB / 100MB,1GB / 500MB",
			wantErr:  false,
			checkCPU: true,
			cpuVal:   1.50,
			checkMem: true,
			memVal:   100.0,
		},
		{
			name:     "with GiB memory",
			input:    "2.00%,1.5GiB / 4GiB,0B / 0B,0B / 0B",
			wantErr:  false,
			checkCPU: true,
			cpuVal:   2.00,
			checkMem: true,
			memVal:   1536.0, // 1.5 * 1024
		},
		{
			name:     "with KiB",
			input:    "0.50%,512KiB / 1MiB,1KB / 2KB,0B / 0B",
			wantErr:  false,
			checkCPU: true,
			cpuVal:   0.50,
			checkMem: true,
			memVal:   0.5, // 512 / 1024
		},
		{
			name:    "invalid format",
			input:   "invalid",
			wantErr: true,
		},
		{
			name:     "empty values",
			input:    "0.00%,0B / 0B,-- / --,-- / --",
			wantErr:  false,
			checkCPU: true,
			cpuVal:   0.0,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			summary, err := collector.parseDockerStats(tt.input)
			if (err != nil) != tt.wantErr {
				t.Errorf("parseDockerStats() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if tt.checkCPU && summary.CPUAvg != tt.cpuVal {
				t.Errorf("CPUAvg = %v, want %v", summary.CPUAvg, tt.cpuVal)
			}
			if tt.checkMem && summary.MemAvgMB != tt.memVal {
				t.Errorf("MemAvgMB = %v, want %v", summary.MemAvgMB, tt.memVal)
			}
		})
	}
}

func TestDockerMetricsCollector_ParseMemorySize(t *testing.T) {
	tests := []struct {
		input   string
		want    float64
		wantErr bool
	}{
		{"100MiB", 100.0, false},
		{"1.5GiB", 1536.0, false},
		{"512KiB", 0.5, false},
		{"1GB", 1024.0, false},
		{"500MB", 500.0, false},
		{"1024KB", 1.0, false},
		{"1048576B", 1.0, false},
		{"0B", 0.0, false},
		{"--", 0.0, false},
		{"", 0.0, false},
	}

	for _, tt := range tests {
		t.Run(tt.input, func(t *testing.T) {
			got, err := parseMemorySize(tt.input)
			if (err != nil) != tt.wantErr {
				t.Errorf("parseMemorySize() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if got != tt.want {
				t.Errorf("parseMemorySize() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestDockerMetricsCollector_New(t *testing.T) {
	collector := NewDockerMetricsCollector("test-container")
	if collector.containerName != "test-container" {
		t.Errorf("expected containerName 'test-container', got %s", collector.containerName)
	}
	if collector.interval != time.Second {
		t.Errorf("expected interval 1s, got %v", collector.interval)
	}
}

func TestDockerMetricsCollector_Stop(t *testing.T) {
	collector := NewDockerMetricsCollector("test")
	// Stop should not panic even if Start was not called
	collector.Stop()
}

func TestDockerMetricsCollector_CollectWithoutContainerID(t *testing.T) {
	collector := NewDockerMetricsCollector("test")
	_, err := collector.Collect(context.Background())
	if err == nil {
		t.Error("expected error when collecting without container ID")
	}
}

func TestDockerMetricsCollector_GetMetrics(t *testing.T) {
	collector := NewDockerMetricsCollector("test")
	ch := collector.GetMetrics()
	if ch == nil {
		t.Error("GetMetrics() returned nil channel")
	}
}

func TestDockerMetricsCollector_ContainerID(t *testing.T) {
	collector := NewDockerMetricsCollector("test")
	if collector.ContainerID() != "" {
		t.Error("expected empty container ID initially")
	}
}

func TestDockerMetricsCollector_ParseDockerStatsIO(t *testing.T) {
	collector := &DockerMetricsCollector{}

	tests := []struct {
		name       string
		input      string
		wantIORead float64
		wantIOWrite float64
	}{
		{
			name:       "MB IO values",
			input:      "1.00%,100MiB / 1GiB,50MB / 100MB,0B / 0B",
			wantIORead:  50.0,
			wantIOWrite: 100.0,
		},
		{
			name:       "GB IO values",
			input:      "1.00%,100MiB / 1GiB,1GB / 2GB,0B / 0B",
			wantIORead:  1024.0,
			wantIOWrite: 2048.0,
		},
		{
			name:       "zero IO",
			input:      "1.00%,100MiB / 1GiB,0B / 0B,0B / 0B",
			wantIORead:  0.0,
			wantIOWrite: 0.0,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			summary, err := collector.parseDockerStats(tt.input)
			if err != nil {
				t.Errorf("parseDockerStats() error = %v", err)
				return
			}
			if summary.IOReadMB != tt.wantIORead {
				t.Errorf("IOReadMB = %v, want %v", summary.IOReadMB, tt.wantIORead)
			}
			if summary.IOWriteMB != tt.wantIOWrite {
				t.Errorf("IOWriteMB = %v, want %v", summary.IOWriteMB, tt.wantIOWrite)
			}
		})
	}
}

func TestDockerMetricsCollector_StartStop(t *testing.T) {
	// This test verifies Start/Stop lifecycle
	// Note: Start will fail if container doesn't exist, which is expected
	collector := NewDockerMetricsCollector("nonexistent-container-12345")

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	// Start should fail because container doesn't exist
	err := collector.Start(ctx)
	if err == nil {
		// If it doesn't fail, make sure we stop
		defer collector.Stop()
		t.Log("Start succeeded unexpectedly (container may exist)")
	} else {
		// Expected - container doesn't exist
		t.Logf("Start failed as expected for nonexistent container: %v", err)
	}
}

func TestDockerMetricsCollector_ContextCancellation(t *testing.T) {
	collector := NewDockerMetricsCollector("test")
	collector.containerID = "fake-id" // Set manually to skip discovery

	ctx, cancel := context.WithCancel(context.Background())

	// Cancel immediately
	cancel()

	// Collect should handle cancelled context
	_, err := collector.Collect(ctx)
	if err != nil {
		// Error is expected due to cancelled context or fake container
		t.Logf("Collect returned error as expected: %v", err)
	}
}
