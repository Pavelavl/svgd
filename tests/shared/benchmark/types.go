package benchmark

import "time"

type BenchmarkResult struct {
	TestName      string
	TotalRequests int
	SuccessCount  int
	FailCount     int
	SuccessRate   float64
	AvgLatency    time.Duration
	MedianLatency time.Duration
	MinLatency    time.Duration
	MaxLatency    time.Duration
	P95Latency    time.Duration
	P99Latency    time.Duration
	ThroughputRPS float64
	Duration      time.Duration
	Latencies     []time.Duration
}

type MetricsSummary struct {
	CPUAvg                 float64
	CPUMedian              float64
	CPUMax                 float64
	MemAvgMB               float64
	MemMedianMB            float64
	MemMaxMB               float64
	IOReadMB               float64
	IOWriteMB              float64
	IOReadOpsPS            float64
	IOWriteOpsPS           float64
	ThreadsAvg             float64
	ThreadsMax             int
	FDsAvg                 float64
	FDsMax                 int
	CtxSwitchVoluntaryPS   float64
	CtxSwitchInvoluntaryPS float64
	PageFaultsMinorPS      float64
	PageFaultsMajorPS      float64
}

type ComparisonRow struct {
	System       string
	Scenario     string
	Concurrency  int
	Requests     int
	RPS          float64
	SuccessRate  float64
	LatencyAvgMs float64
	LatencyP50Ms float64
	LatencyP95Ms float64
	LatencyP99Ms float64
	CPUAvgPct    float64
	CPUMaxPct    float64
	MemAvgMB     float64
	MemMaxMB     float64
	IOReadMB     float64
	IOWriteMB    float64
	CtxSwitchVolPS     float64
	CtxSwitchInvolPS   float64
	PageFaultsMinorPS  float64
	ThreadsAvg   float64
	FDsAvg       float64
}

type Scenario struct {
	Name        string
	Concurrency int
	Requests    int
}

var DefaultScenarios = []Scenario{
	{Name: "light", Concurrency: 1, Requests: 1000},
	{Name: "medium", Concurrency: 10, Requests: 1000},
	{Name: "heavy", Concurrency: 50, Requests: 1000},
}
