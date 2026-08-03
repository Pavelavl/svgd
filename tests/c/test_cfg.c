/**
 * @file test_cfg.c
 * @brief Тесты find_metric_config — поиска метрики по endpoint
 *
 * Config строится в памяти напрямую (без парсинга JSON/Duktape), поэтому
 * проверяется только чистая логика сопоставления endpoint'ов.
 */
#include "minitest.h"
#include "cfg.h"
#include <string.h>

static Config make_cfg(MetricConfig *metrics, int count) {
    Config c;
    memset(&c, 0, sizeof c);
    c.metrics = metrics;
    c.metrics_count = count;
    return c;
}

static MetricConfig metric(const char *endpoint, int requires_param) {
    MetricConfig m;
    memset(&m, 0, sizeof m);
    snprintf(m.endpoint, sizeof m.endpoint, "%s", endpoint);
    m.requires_param = requires_param;
    return m;
}

TEST(exact_match) {
    MetricConfig ms[] = { metric("cpu", 0), metric("ram", 0) };
    Config c = make_cfg(ms, 2);
    MetricConfig *m = find_metric_config(&c, "cpu");
    ASSERT(m != NULL);
    ASSERT_STR(m->endpoint, "cpu");
}

TEST(unknown_endpoint_returns_null) {
    MetricConfig ms[] = { metric("cpu", 0) };
    Config c = make_cfg(ms, 1);
    ASSERT(find_metric_config(&c, "nope") == NULL);
}

TEST(parametrized_prefix_match) {
    MetricConfig ms[] = { metric("ram/process", 1) };
    Config c = make_cfg(ms, 1);
    MetricConfig *m = find_metric_config(&c, "ram/process/postgres");
    ASSERT(m != NULL);
    ASSERT_STR(m->endpoint, "ram/process");
}

TEST(longest_prefix_wins) {
    /* Документированный кейс: "disk/io_time/nvme0n1" должно матчится с
     * "disk/io_time", а не с более коротким "disk". */
    MetricConfig ms[] = { metric("disk", 1), metric("disk/io_time", 1) };
    Config c = make_cfg(ms, 2);
    MetricConfig *m = find_metric_config(&c, "disk/io_time/nvme0n1");
    ASSERT(m != NULL);
    ASSERT_STR(m->endpoint, "disk/io_time");
}

TEST(prefix_without_slash_after_endpoint_returns_null) {
    /* "diskio_time" начинается с "disk" (strncmp совпадает), но символ после
     * префикса — не '/', поэтому параметризованный матч во втором проходе
     * отвергается. */
    MetricConfig ms[] = { metric("disk", 1) };
    Config c = make_cfg(ms, 1);
    ASSERT(find_metric_config(&c, "diskio_time") == NULL);
}

TEST(exact_match_preferred) {
    /* Точный матч имеет приоритет: запрос "cpu" находит обычную метрику "cpu",
     * а не параметризованного «соседа» "cpu/process". */
    MetricConfig ms[] = { metric("cpu", 0), metric("cpu/process", 1) };
    Config c = make_cfg(ms, 2);
    MetricConfig *m = find_metric_config(&c, "cpu");
    ASSERT(m != NULL);
    ASSERT_STR(m->endpoint, "cpu");
    ASSERT(m->requires_param == 0);
}

TEST(null_inputs_return_null) {
    MetricConfig ms[] = { metric("cpu", 0) };
    Config c = make_cfg(ms, 1);
    ASSERT(find_metric_config(NULL, "cpu") == NULL);
    ASSERT(find_metric_config(&c, NULL) == NULL);
}

TEST_MAIN()
    RUN(exact_match);
    RUN(unknown_endpoint_returns_null);
    RUN(parametrized_prefix_match);
    RUN(longest_prefix_wins);
    RUN(prefix_without_slash_after_endpoint_returns_null);
    RUN(exact_match_preferred);
    RUN(null_inputs_return_null);
TEST_RETURN()
