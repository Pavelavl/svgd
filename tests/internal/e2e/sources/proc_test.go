package sources

// E2E для SRC_PROC: live-чтение /proc раннера.
//
// Что проверяем (count-агностик — proc пока отдаёт одну точку timestamp=now):
//   - cpu (proc_metric="cpu") → 1 серия "utilization", валидный непустой SVG;
//   - load (proc_metric="load") → 3 серии load1/load5/load15, имена видны в SVG;
//   - успешный roundtrip и через LSRP, и через HTTP (status 0 / 200);
//   - SVG не содержит JS-заглушек "No data series" / "No valid data points".
//
// Источник читает системное /proc/stat и /proc/loadavg напрямую (без диска и
// rrdcached), поэтому нужен только Linux-раннер. На не-Linux — t.Skip.

import (
	"os"
	"testing"
)

func Test_ProcSource(t *testing.T) {
	// /proc обязателен для SRC_PROC. На не-Linux раннере — skip, а не FAIL.
	if _, err := os.Stat("/proc/stat"); err != nil {
		t.Skip("/proc/stat not available — skipping proc source e2e (non-Linux runner?)")
	}
	if _, err := os.Stat("/proc/loadavg"); err != nil {
		t.Skip("/proc/loadavg not available — skipping proc source e2e (non-Linux runner?)")
	}

	metrics := []srcMetric{
		{
			Endpoint:     "proc_cpu",
			Source:       "proc",
			ProcMetric:   "cpu",
			Title:        "CPU (live /proc)",
			YLabel:       "Usage (%)",
			ValueFormat:  "%.1f",
			IsPercentage: true,
		},
		{
			Endpoint:    "proc_load",
			Source:      "proc",
			ProcMetric:  "load",
			Title:       "Load average (live /proc)",
			YLabel:      "Load",
			ValueFormat: "%.2f",
		},
	}

	runBothProtocols(t, metrics, func(t *testing.T, proto string, port int) {
		// --- cpu: одна серия "utilization" (хоть одна точка — >= 1) ---
		cpuSVG := fetch(t, proto, port, "proc_cpu")
		assertValidSVG(t, cpuSVG, "proc_cpu/"+proto)
		assertSeriesInSVG(t, string(cpuSVG), "proc_cpu/"+proto, "utilization")

		// --- load: три серии load1/load5/load15 (имена из read_load) ---
		loadSVG := fetch(t, proto, port, "proc_load")
		assertValidSVG(t, loadSVG, "proc_load/"+proto)
		assertSeriesInSVG(t, string(loadSVG), "proc_load/"+proto, "load1", "load5", "load15")

		t.Logf("proc source OK via %s: cpu=%d bytes, load=%d bytes",
			proto, len(cpuSVG), len(loadSVG))
	})
}
