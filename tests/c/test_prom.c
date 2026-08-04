/**
 * @file test_prom.c
 * @brief Тесты чистых функций SRC_PROMETHEUS (prom_parse_url / prom_parse_line)
 *
 * Парсеры выделены без I/O (см. include/prometheus_source.h) и тестируются на
 * строках-эталонах Prometheus text-exposition. Сам HTTP-fetch (http_get_body)
 * требует сервера — покрыт smoke-тестом бинарника, не unit-тестом.
 */
#include "minitest.h"
#include "prometheus_source.h"
#include <math.h>
#include <string.h>

/* ---- prom_parse_url ---- */

TEST(parse_url_host_port_path) {
    char host[128], path[256]; int port = 0;
    ASSERT(prom_parse_url("http://exporter:9100/metrics", host, sizeof(host), &port, path, sizeof(path)) == 0);
    ASSERT_STR(host, "exporter");
    ASSERT(port == 9100);
    ASSERT_STR(path, "/metrics");
}

TEST(parse_url_default_port_and_path) {
    char host[128], path[256]; int port = 0;
    ASSERT(prom_parse_url("http://node.local", host, sizeof(host), &port, path, sizeof(path)) == 0);
    ASSERT_STR(host, "node.local");
    ASSERT(port == 80);
    ASSERT_STR(path, "/");
}

TEST(parse_url_default_port_with_path) {
    char host[128], path[256]; int port = 0;
    ASSERT(prom_parse_url("http://h:80/foo/bar", host, sizeof(host), &port, path, sizeof(path)) == 0);
    ASSERT_STR(host, "h");
    ASSERT(port == 80);
    ASSERT_STR(path, "/foo/bar");
}

TEST(parse_url_rejects_https) {
    char host[128], path[256]; int port = 0;
    ASSERT(prom_parse_url("https://h/metrics", host, sizeof(host), &port, path, sizeof(path)) == -1);
}

TEST(parse_url_rejects_bad_port_and_nonhttp) {
    char host[128], path[256]; int port = 0;
    ASSERT(prom_parse_url("http://h:notaport/x", host, sizeof(host), &port, path, sizeof(path)) == -1);
    ASSERT(prom_parse_url("http://h:99999/x", host, sizeof(host), &port, path, sizeof(path)) == -1);
    ASSERT(prom_parse_url("just-a-host", host, sizeof(host), &port, path, sizeof(path)) == -1);
    ASSERT(prom_parse_url("http://", host, sizeof(host), &port, path, sizeof(path)) == -1);
}

/* ---- prom_parse_line ---- */

TEST(parse_line_with_labels_and_value) {
    char nm[64], lb[128]; double v; int hts; long long tms;
    ASSERT(prom_parse_line("node_cpu_seconds_total{cpu=\"0\",mode=\"idle\"} 1234.56",
                           nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == 0);
    ASSERT_STR(nm, "node_cpu_seconds_total");
    ASSERT_STR(lb, "cpu=\"0\",mode=\"idle\"");
    ASSERT(v > 1234.55 && v < 1234.57);
    ASSERT(hts == 0);
}

TEST(parse_line_without_labels) {
    char nm[64], lb[128]; double v; int hts; long long tms;
    ASSERT(prom_parse_line("up 1", nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == 0);
    ASSERT_STR(nm, "up");
    ASSERT_STR(lb, "");
    ASSERT(v == 1.0);
}

TEST(parse_line_with_timestamp_ms) {
    char nm[64], lb[128]; double v; int hts; long long tms;
    ASSERT(prom_parse_line("baz{x=\"y\"} 1.5 1700000000123",
                           nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == 0);
    ASSERT(hts == 1);
    ASSERT(tms == 1700000000123LL);
    ASSERT(v == 1.5);
}

TEST(parse_line_comment_and_empty_skipped) {
    char nm[64], lb[128]; double v; int hts; long long tms;
    ASSERT(prom_parse_line("# TYPE foo counter", nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == 1);
    ASSERT(prom_parse_line("", nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == 1);
    ASSERT(prom_parse_line("   ", nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == 1);
}

TEST(parse_line_nan_and_inf) {
    char nm[64], lb[128]; double v; int hts; long long tms;
    ASSERT(prom_parse_line("q NaN", nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == 0);
    ASSERT(isnan(v));
    ASSERT(prom_parse_line("q +Inf", nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == 0);
    ASSERT(v == INFINITY);
    ASSERT(prom_parse_line("q -Inf", nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == 0);
    ASSERT(v == -INFINITY);
}

TEST(parse_line_scientific_value) {
    char nm[64], lb[128]; double v; int hts; long long tms;
    ASSERT(prom_parse_line("bytes 1.5e9", nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == 0);
    ASSERT(v > 1.4999e9 && v < 1.5001e9);
}

TEST(parse_line_malformed) {
    char nm[64], lb[128]; double v; int hts; long long tms;
    /* имя есть, значения нет */
    ASSERT(prom_parse_line("foo", nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == -1);
    /* незакрытые лейблы */
    ASSERT(prom_parse_line("foo{a=\"b\" 1", nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == -1);
    /* NULL */
    ASSERT(prom_parse_line(NULL, nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms) == -1);
}

TEST_MAIN()
    RUN(parse_url_host_port_path);
    RUN(parse_url_default_port_and_path);
    RUN(parse_url_default_port_with_path);
    RUN(parse_url_rejects_https);
    RUN(parse_url_rejects_bad_port_and_nonhttp);
    RUN(parse_line_with_labels_and_value);
    RUN(parse_line_without_labels);
    RUN(parse_line_with_timestamp_ms);
    RUN(parse_line_comment_and_empty_skipped);
    RUN(parse_line_nan_and_inf);
    RUN(parse_line_scientific_value);
    RUN(parse_line_malformed);
TEST_RETURN()
