package targets

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"time"

	"github.com/Pavelavl/go-lsrp"
)

// SVGdTarget implements BenchmarkTarget for svgd binary
type SVGdTarget struct {
	binaryPath string
	configPath string
	port       int
	cmd        *exec.Cmd
	protocol   string
}

// NewSVGdTarget creates a new svgd benchmark target
func NewSVGdTarget(binaryPath, configPath string, port int, protocol string) *SVGdTarget {
	return &SVGdTarget{
		binaryPath: binaryPath,
		configPath: configPath,
		port:       port,
		protocol:   protocol,
	}
}

func (s *SVGdTarget) Name() string {
	return "svgd"
}

func (s *SVGdTarget) Setup(ctx context.Context) error {
	logFile := filepath.Join(os.TempDir(), "svgd-benchmark.log")
	log, err := os.Create(logFile)
	if err != nil {
		return fmt.Errorf("failed to create log file: %w", err)
	}

	// Set working directory to the directory containing the config
	// so svgd can find RRD files and JS scripts with relative paths
	configDir := filepath.Dir(s.configPath)

	s.cmd = exec.Command(s.binaryPath, s.configPath)
	s.cmd.Dir = configDir
	s.cmd.Stdout = log
	s.cmd.Stderr = log

	if err := s.cmd.Start(); err != nil {
		return fmt.Errorf("failed to start svgd: %w", err)
	}

	// Wait for ready
	for i := 0; i < 10; i++ {
		c, err := lsrp.NewClient("localhost", s.port)
		if err == nil {
			c.Close()
			return nil
		}
		time.Sleep(500 * time.Millisecond)
	}

	return fmt.Errorf("svgd not ready after 5 seconds")
}

func (s *SVGdTarget) Teardown(ctx context.Context) error {
	if s.cmd != nil && s.cmd.Process != nil {
		return s.cmd.Process.Kill()
	}
	return nil
}

func (s *SVGdTarget) Endpoint() string {
	return fmt.Sprintf("localhost:%d", s.port)
}

func (s *SVGdTarget) HealthCheck(ctx context.Context) error {
	c, err := lsrp.NewClient("localhost", s.port)
	if err != nil {
		return err
	}
	defer c.Close()
	_, err = c.Send("endpoint=cpu&period=3600")
	return err
}

func (s *SVGdTarget) PID() int {
	if s.cmd != nil && s.cmd.Process != nil {
		return s.cmd.Process.Pid
	}
	return 0
}

func (s *SVGdTarget) ContainerID() string {
	return "" // Not a container
}

func (s *SVGdTarget) MakeRequest(ctx context.Context) (float64, error) {
	start := time.Now()

	c, err := lsrp.NewClient("localhost", s.port)
	if err != nil {
		return 0, err
	}
	defer c.Close()

	_, err = c.Send("endpoint=cpu&period=3600")
	latency := float64(time.Since(start).Microseconds()) / 1000
	return latency, err
}
