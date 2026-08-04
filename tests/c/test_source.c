/**
 * @file test_source.c
 * @brief Тесты metric_source_from_string — выбора источника метрики по строке
 *
 * Функция — static inline в include/metric_source.h (чистая, без зависимостей),
 * поэтому тестовый бинарник компилируется изолированно (без линковки бэкендов).
 * Контракт: "rrd"/""/NULL/unknown → SRC_RRD, "proc" → SRC_PROC,
 * "prometheus" → SRC_PROMETHEUS.
 */
#include "minitest.h"
#include "metric_source.h"

TEST(source_rrd_default_for_rrd_empty_null) {
    ASSERT(metric_source_from_string("rrd") == SRC_RRD);
    ASSERT(metric_source_from_string("") == SRC_RRD);
    ASSERT(metric_source_from_string(NULL) == SRC_RRD);
}

TEST(source_proc) {
    ASSERT(metric_source_from_string("proc") == SRC_PROC);
}

TEST(source_prometheus) {
    ASSERT(metric_source_from_string("prometheus") == SRC_PROMETHEUS);
}

TEST(source_unknown_falls_back_to_rrd) {
    ASSERT(metric_source_from_string("csv") == SRC_RRD);
    ASSERT(metric_source_from_string("influxdb") == SRC_RRD);
    /* Регистро-зависимое сравнение (как остальные строковые ключи конфига). */
    ASSERT(metric_source_from_string("RRD") == SRC_RRD);
    ASSERT(metric_source_from_string("Prometheus") == SRC_RRD);
    /* Сокращения не признаются — только точные имена. */
    ASSERT(metric_source_from_string("prom") == SRC_RRD);
}

TEST_MAIN()
    RUN(source_rrd_default_for_rrd_empty_null);
    RUN(source_proc);
    RUN(source_prometheus);
    RUN(source_unknown_falls_back_to_rrd);
TEST_RETURN()
