package benchmark

import (
	"math"
	"sort"
	"time"
)

type RequestResult struct {
	RequestNum int
	Latency    time.Duration
	Success    bool
	Error      error
}

func AnalyzeResults(results chan RequestResult, duration time.Duration) (*BenchmarkResult, error) {
	br := &BenchmarkResult{
		Duration:   duration,
		MinLatency: time.Duration(math.MaxInt64),
	}

	latencies := make([]time.Duration, 0)

	for result := range results {
		br.TotalRequests++

		if result.Success {
			br.SuccessCount++
			latencies = append(latencies, result.Latency)

			if result.Latency < br.MinLatency {
				br.MinLatency = result.Latency
			}
			if result.Latency > br.MaxLatency {
				br.MaxLatency = result.Latency
			}
		} else {
			br.FailCount++
		}
	}

	if br.SuccessCount == 0 {
		return br, nil
	}

	br.SuccessRate = float64(br.SuccessCount) / float64(br.TotalRequests) * 100
	br.ThroughputRPS = float64(br.SuccessCount) / duration.Seconds()
	br.Latencies = latencies

	sort.Slice(latencies, func(i, j int) bool {
		return latencies[i] < latencies[j]
	})

	var totalLatency time.Duration
	for _, l := range latencies {
		totalLatency += l
	}
	br.AvgLatency = totalLatency / time.Duration(len(latencies))
	br.MedianLatency = latencies[len(latencies)/2]

	if len(latencies) > 0 {
		p95Idx := int(float64(len(latencies)) * 0.95)
		p99Idx := int(float64(len(latencies)) * 0.99)
		if p95Idx >= len(latencies) {
			p95Idx = len(latencies) - 1
		}
		if p99Idx >= len(latencies) {
			p99Idx = len(latencies) - 1
		}
		br.P95Latency = latencies[p95Idx]
		br.P99Latency = latencies[p99Idx]
	}

	return br, nil
}
