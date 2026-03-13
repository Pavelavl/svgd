package targets

import "context"

// BenchmarkTarget defines the interface for systems to benchmark
type BenchmarkTarget interface {
	// Name returns the system name for reports
	Name() string

	// Setup prepares the system (starts container/process)
	Setup(ctx context.Context) error

	// Teardown stops the system
	Teardown(ctx context.Context) error

	// Endpoint returns the URL for requests
	Endpoint() string

	// HealthCheck verifies the system is ready
	HealthCheck(ctx context.Context) error

	// PID returns process ID for metrics collection (0 if container)
	PID() int

	// ContainerID returns Docker container ID for metrics collection ("" if process)
	ContainerID() string

	// MakeRequest performs a single benchmark request
	MakeRequest(ctx context.Context) (latencyMs float64, err error)
}
