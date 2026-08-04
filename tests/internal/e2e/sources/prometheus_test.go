package sources

// E2E для SRC_PROMETHEUS: svgd парсит Prometheus text-exposition (HTTP GET).
//
// Схема: поднимаем mock-экспортёр (net/http/httptest.Server), отдающий 3 серии
// metric-name == endpoint'у из конфига. svgd через свой raw-socket HTTP/1.1 клиент
// (prometheus_source.c:http_get_body) забирает тело, парсит каждую подходящую
// строку в отдельную серию и рендерит SVG.
//
// Что проверяем (count-агностик):
//   - успешный fetch (svgd достучался до mock-экспортёра и распарсил серии);
//   - валидный непустой SVG без JS-заглушек;
//   - каждый labelset (series name = содержимое {…}) попадает в легенду SVG;
//   - roundtrip и через LSRP, и через HTTP.
//
// Важный инвариант прометеус-источника: серия подходит, если metric name в
// exposition равен metric->endpoint (см. prometheus_source.c). Поэтому endpoint в
// конфиге = "svgd_mock_metric", и exposition-строки имеют то же имя.

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func Test_PrometheusSource(t *testing.T) {
	// Mock Prometheus text-exposition: 3 серии gauge с разными labelset'ами.
	// svgd_mock_metric{instance="host1",job="a"} 12.34  → series name = `instance="host1",job="a"`
	// (содержимое фигурных скобок, см. prom_parse_line / prometheus_source.c).
	const endpoint = "svgd_mock_metric"
	exposition := strings.Join([]string{
		"# HELP " + endpoint + " Mock gauge for svgd e2e",
		"# TYPE " + endpoint + " gauge",
		fmt.Sprintf(`%s{instance="host1",job="app"} 12.34`, endpoint),
		fmt.Sprintf(`%s{instance="host2",job="app"} 56.78`, endpoint),
		fmt.Sprintf(`%s{instance="host3",job="app"} 90.12`, endpoint),
		"", // trailing newline
	}, "\n")

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// svgd шлёт Connection: close и читает до EOF — Go http закроет сокет.
		w.Header().Set("Content-Type", "text/plain; version=0.0.4; charset=utf-8")
		fmt.Fprint(w, exposition)
	}))
	defer srv.Close()

	// httptest.NewServer слушает на 127.0.0.1; prometheus-источник использует
	// IPv4 raw socket (getaddrinfo/AF_INET), поэтому 127.0.0.1 подходит. Путь
	// /metrics извлекается prom_parse_url и используется в GET-запросе.
	promURL := srv.URL + "/metrics"

	metrics := []srcMetric{
		{
			Endpoint:      endpoint,
			Source:        "prometheus",
			PrometheusURL: promURL,
			Title:         "Mock Prometheus metric",
			YLabel:        "Value",
			ValueFormat:   "%.2f",
		},
	}

	runBothProtocols(t, metrics, func(t *testing.T, proto string, port int) {
		svg := fetch(t, proto, port, endpoint)
		assertValidSVG(t, svg, "prometheus/"+proto)

		// Имя каждой серии = содержимое {…}; assert по уникальной подстроке
		// instance="hostN" (она входит в series name целиком).
		assertSeriesInSVG(t, string(svg), "prometheus/"+proto,
			`instance="host1"`, `instance="host2"`, `instance="host3"`)

		t.Logf("prometheus source OK via %s: svg=%d bytes, exporter=%s",
			proto, len(svg), promURL)
	})
}
