package sources

// E2E-харнес для источников metric-source (Фаза 2): proc и prometheus.
//
// Отдельный под-пакет tests/internal/e2e/sources (а не пакет e2e) — намеренно:
// proc/prometheus не зависят от collectd/RRD/rrdcached, поэтому не должны
// гипотетически блокироваться на TestMain пакет e2e (который требует реальный
// config.json + percent-active.rrd + rrdcached). Здесь нужен только собранный
// bin/svgd, live /proc раннера и REPO_ROOT. `go test ./internal/e2e/...`
// рекурсивно подхватывает и этот пакет → `make test-e2e`.
//
// Подход повторяет существующий харнес (response_time_test.go): spawn бинарника,
// темповый конфиг в os.TempDir, e2e-клиенты из tests/shared/http + go-lsrp,
// прогон того же endpoint'а через LSRP и HTTP. Ассерты — count-агностик:
// proc/prometheus пока отдают одну точку (timestamp=now); ring-buffer с
// multi-point историей делается параллельно. Поэтому проверяем валидный непустой
// SVG, корректные имена серий / labelset и успешный roundtrip — НЕ точное число
// точек.

import (
	"encoding/json"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"tests/shared/http"

	"github.com/Pavelavl/go-lsrp"
)

// repoRoot — абсолютный путь к корню репо (передаётся через REPO_ROOT, как и для
// остальных e2e-тестов). Нужен чтобы найти bin/svgd и выставить cmd.Dir (от него
// считается относительный ./scripts/generate_svg.js).
var repoRoot string

func TestMain(m *testing.M) {
	repoRoot = os.Getenv("REPO_ROOT")
	if repoRoot == "" {
		fmt.Fprintln(os.Stderr, "Error: REPO_ROOT environment variable is not set")
		os.Exit(1)
	}
	abs, err := filepath.Abs(repoRoot)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error resolving REPO_ROOT: %v\n", err)
		os.Exit(1)
	}
	repoRoot = abs

	// Нужен собранный backend. config.json / collectd / rrdcached НЕ требуются —
	// proc/prometheus живут без RRD.
	if err := checkFile(binPath(), true); err != nil {
		fmt.Fprintf(os.Stderr, "svgd binary not ready: %v\n", err)
		os.Exit(1)
	}

	os.Exit(m.Run())
}

// binPath — путь к собранному svgd-backend.
func binPath() string { return filepath.Join(repoRoot, "bin", "svgd") }

// checkFile — как в response_time_test.go.
func checkFile(path string, executable bool) error {
	info, err := os.Stat(path)
	if err != nil {
		return fmt.Errorf("%s not found: %w", path, err)
	}
	if executable && info.Mode().Perm()&0111 == 0 {
		return fmt.Errorf("%s is not executable", path)
	}
	return nil
}

// srcMetric — метрика для темпового конфига (одно поле на каждый source).
type srcMetric struct {
	Endpoint      string
	Source        string // "rrd" по умолчанию; здесь — "proc" / "prometheus"
	RrdPath       string // только SRC_RRD
	ProcMetric    string // только SRC_PROC: "cpu" | "load"
	PrometheusURL string // только SRC_PROMETHEUS: http://host[:port]/path
	Title         string
	YLabel        string
	ValueFormat   string
	IsPercentage  bool
}

// writeConfig собирает минимальный config.json и пишет его в os.TempDir.
// Возвращает путь (caller отвечает за os.Remove). JS-скрипт — стандартный
// ./scripts/generate_svg.js (symlink создаётся `make build`); rrdcached не нужен.
func writeConfig(t *testing.T, port int, protocol string, metrics []srcMetric) string {
	t.Helper()
	type metricJSON struct {
		Endpoint      string `json:"endpoint"`
		Source        string `json:"source,omitempty"`
		RrdPath       string `json:"rrd_path,omitempty"`
		ProcMetric    string `json:"proc_metric,omitempty"`
		PrometheusURL string `json:"prometheus_url,omitempty"`
		Title         string `json:"title"`
		YLabel        string `json:"y_label"`
		ValueFormat   string `json:"value_format"`
		IsPercentage  bool   `json:"is_percentage"`
	}
	cfg := struct {
		Server struct {
			TCPPort        int    `json:"tcp_port"`
			Protocol       string `json:"protocol"`
			AllowedIps     string `json:"allowed_ips"`
			RRDCachedAddr  string `json:"rrdcached_addr"`
			ThreadPoolSize int    `json:"thread_pool_size"`
			CacheTTLSec    int    `json:"cache_ttl_seconds"`
			Verbose        int    `json:"verbose"`
		} `json:"server"`
		RRD struct {
			BasePath string `json:"base_path"`
		} `json:"rrd"`
		JS struct {
			ScriptPath string `json:"script_path"`
		} `json:"js"`
		Metrics []metricJSON `json:"metrics"`
	}{}
	cfg.Server.TCPPort = port
	cfg.Server.Protocol = protocol
	cfg.Server.AllowedIps = "127.0.0.1"
	cfg.Server.RRDCachedAddr = "" // proc/prometheus не используют rrdcached
	cfg.Server.ThreadPoolSize = 2
	cfg.Server.CacheTTLSec = 5
	cfg.Server.Verbose = 0
	cfg.JS.ScriptPath = "./scripts/generate_svg.js"
	for _, m := range metrics {
		cfg.Metrics = append(cfg.Metrics, metricJSON{
			Endpoint:      m.Endpoint,
			Source:        m.Source,
			RrdPath:       m.RrdPath,
			ProcMetric:    m.ProcMetric,
			PrometheusURL: m.PrometheusURL,
			Title:         m.Title,
			YLabel:        m.YLabel,
			ValueFormat:   m.ValueFormat,
			IsPercentage:  m.IsPercentage,
		})
	}
	data, err := json.MarshalIndent(cfg, "", "\t")
	if err != nil {
		t.Fatalf("marshal temp config: %v", err)
	}
	f, err := os.CreateTemp("", fmt.Sprintf("svgd-src-%s-*.json", protocol))
	if err != nil {
		t.Fatalf("create temp config: %v", err)
	}
	if _, err := f.Write(data); err != nil {
		f.Close()
		t.Fatalf("write temp config: %v", err)
	}
	f.Close()
	return f.Name()
}

// findFreePort возвращает свободный TCP-порт на 127.0.0.1 (TOCTOU-окно пренебрежимо
// мало для тестов; это позволяет не конфликтовать с портом config.json=8081).
func findFreePort(t *testing.T) int {
	t.Helper()
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for free port: %v", err)
	}
	port := l.Addr().(*net.TCPAddr).Port
	l.Close()
	return port
}

// waitForPort опрашивает порт до timeout — надёжнее чем фиксированный sleep.
func waitForPort(port int, timeout time.Duration) bool {
	deadline := time.Now().Add(timeout)
	addr := fmt.Sprintf("127.0.0.1:%d", port)
	for time.Now().Before(deadline) {
		conn, err := net.DialTimeout("tcp", addr, 250*time.Millisecond)
		if err == nil {
			conn.Close()
			return true
		}
		time.Sleep(75 * time.Millisecond)
	}
	return false
}

// backendProc — запущенный svgd-backend + его лог-файл.
type backendProc struct {
	cmd  *exec.Cmd
	logf *os.File
	port int
	pid  int
}

// startBackend запускает svgd с заданным конфигом и ждёт пока он начнёт слушать.
// cmd.Dir = repoRoot — чтобы относительный ./scripts/generate_svg.js резолвился.
func startBackend(t *testing.T, configFile string, port int) *backendProc {
	t.Helper()
	logDir := filepath.Join(repoRoot, "tests", "internal", "e2e", "logs")
	if err := os.MkdirAll(logDir, 0755); err != nil {
		t.Fatalf("mkdir logs: %v", err)
	}
	logPath := filepath.Join(logDir, fmt.Sprintf("sources_%d.log", time.Now().UnixNano()))
	logf, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644)
	if err != nil {
		t.Fatalf("open backend log: %v", err)
	}
	cmd := exec.Command(binPath(), configFile)
	cmd.Dir = repoRoot
	cmd.Stdout = logf
	cmd.Stderr = logf
	if err := cmd.Start(); err != nil {
		logf.Close()
		t.Fatalf("start svgd: %v", err)
	}
	bp := &backendProc{cmd: cmd, logf: logf, port: port, pid: cmd.Process.Pid}
	if !waitForPort(port, 6*time.Second) {
		body, _ := os.ReadFile(logPath)
		bp.stop()
		t.Fatalf("svgd did not start listening on port %d within 6s; log %s:\n%s",
			port, logPath, string(body))
	}
	t.Logf("svgd started (pid=%d) on port %d (%s), config=%s", bp.pid, port, configFile, configFile)
	return bp
}

// stop посылает SIGINT (корректное завершение svgd) и ждёт, с fallback на SIGKILL.
func (b *backendProc) stop() {
	if b == nil || b.cmd == nil || b.cmd.Process == nil {
		return
	}
	_ = b.cmd.Process.Signal(os.Interrupt)
	done := make(chan error, 1)
	go func() { done <- b.cmd.Wait() }()
	select {
	case <-done:
	case <-time.After(3 * time.Second):
		_ = b.cmd.Process.Kill()
		<-done
	}
	if b.logf != nil {
		_ = b.logf.Close()
	}
}

// fetch запрашивает endpoint через http или lsrp и возвращает тело (SVG).
// status != 0 → t.Fatalf (для count-агностик-ассертов нужен успешный roundtrip).
func fetch(t *testing.T, proto string, port int, endpoint string) []byte {
	t.Helper()
	// period для proc/prometheus игнорируется (живое значение), но параметр
	// обязателен в обоих протоколах — шлём 60с как «недавнее окно».
	const period = 60
	switch proto {
	case "http":
		c, err := http.NewClient("localhost", port)
		if err != nil {
			t.Fatalf("http.NewClient: %v", err)
		}
		resp, err := c.Send(fmt.Sprintf("%s?period=%d", endpoint, period))
		if err != nil {
			t.Fatalf("http GET %s: %v", endpoint, err)
		}
		if resp.Status != 0 {
			t.Fatalf("http %s: status=%d body=%s", endpoint, resp.Status, truncate(string(resp.Data), 400))
		}
		return resp.Data
	case "lsrp":
		c, err := lsrp.NewClient("localhost", port)
		if err != nil {
			t.Fatalf("lsrp.NewClient: %v", err)
		}
		defer c.Close()
		resp, err := c.Send(fmt.Sprintf("endpoint=%s&period=%d", endpoint, period))
		if err != nil {
			t.Fatalf("lsrp Send %s: %v", endpoint, err)
		}
		if resp.Status != 0 {
			t.Fatalf("lsrp %s: status=%d body=%s", endpoint, resp.Status, truncate(string(resp.Data), 400))
		}
		return resp.Data
	default:
		t.Fatalf("unknown protocol %q", proto)
		return nil
	}
}

// runBothProtocols прогоняет тело теста через LSRP и HTTP (по отдельному backend
// на свой порт для каждого протокола — изоляция режимов: HTTP однопоточный без
// кэша, LSRP — thread-pool + кэш).
func runBothProtocols(t *testing.T, metrics []srcMetric, check func(t *testing.T, proto string, port int)) {
	for _, proto := range []string{"lsrp", "http"} {
		t.Run(proto, func(t *testing.T) {
			port := findFreePort(t)
			cfgPath := writeConfig(t, port, proto, metrics)
			defer os.Remove(cfgPath)
			bp := startBackend(t, cfgPath, port)
			defer bp.stop()
			check(t, proto, port)
		})
	}
}

// assertValidSVG — count-агностик: валидный непустой SVG без JS-заглушек ошибок.
// generate_svg.js рендерит real SVG для удачных данных и short SVG-заглушки для
// ошибок ("Error: No data series" / "No valid data points" / "Invalid input").
func assertValidSVG(t *testing.T, data []byte, ctx string) {
	t.Helper()
	s := string(data)
	if len(s) < 200 {
		t.Errorf("%s: SVG suspiciously short (%d bytes): %q", ctx, len(s), truncate(s, 300))
		return
	}
	if !strings.Contains(s, "<svg") {
		t.Errorf("%s: not an SVG (no <svg): %q", ctx, truncate(s, 300))
		return
	}
	// JS error-placeholders (см. src/scripts/generate_svg.js).
	if strings.Contains(s, ">Error:") ||
		strings.Contains(s, "No data series") ||
		strings.Contains(s, "No valid data points") ||
		strings.Contains(s, "Invalid input") {
		t.Errorf("%s: SVG rendered a JS error placeholder: %s", ctx, truncate(s, 400))
	}
}

// assertSeriesInSVG проверяет, что имя серии / labelset попало в SVG. Легенда
// generate_svg.js рендерит series.name как <text>…{s.name}:</text>, поэтому имена
// серий (proc: utilization/load1/…; prom: labelset вида instance="host1") видны
// как подстроки. Проверка count-агностик: ищем имя, а не число точек.
func assertSeriesInSVG(t *testing.T, svg string, ctx string, names ...string) {
	t.Helper()
	for _, n := range names {
		if !strings.Contains(svg, n) {
			t.Errorf("%s: SVG missing expected series/label %q (legend not rendered?)", ctx, n)
		}
	}
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "…"
}
