/**
 * @file test_cache.c
 * @brief Unit-тесты RRD-кэша (src/rrd/cache.c)
 *
 * Кэш теперь инициализируется в обоих режимах (LSRP и HTTP — parity с v0.2.0),
 * поэтому эти тесты покрывают механику put/get/clone/TTL для общего пути.
 */
#include "minitest.h"
#include "rrd/cache.h"   /* rrd_cache_init/put/get/free */
#include "rrd/reader.h"  /* MetricData, DataPoint, free_metric_data */
#include <stdlib.h>
#include <unistd.h>      /* sleep() */

/* Сборка простого MetricData: 1 серия, 2 точки. */
static MetricData *make_test_data(const char *name, double v1, double v2) {
    MetricData *d = calloc(1, sizeof(MetricData));
    d->series_count = 1;
    d->series_names = calloc(1, sizeof(char *));
    d->series_names[0] = strdup(name);
    d->series_counts = calloc(1, sizeof(int));
    d->series_counts[0] = 2;
    d->series_data = calloc(1, sizeof(DataPoint *));
    d->series_data[0] = calloc(2, sizeof(DataPoint));
    d->series_data[0][0].timestamp = 1000;
    d->series_data[0][0].value = v1;
    d->series_data[0][1].timestamp = 2000;
    d->series_data[0][1].value = v2;
    d->param1 = strdup("testparam");
    d->metric_config = NULL;
    return d;
}

/* put → get возвращает независимый клон с теми же значениями. */
TEST(cache_put_get_clone) {
    rrd_cache_init(60);

    MetricData *orig = make_test_data("cpu", 50.0, 75.0);
    rrd_cache_put("clone.rrd", 3600, orig);  /* кэш забирает владение */

    MetricData *clone = rrd_cache_get("clone.rrd", 3600);
    ASSERT(clone != NULL);
    ASSERT(clone != orig);                          /* другой указатель */
    ASSERT(clone->series_count == 1);
    ASSERT_STR(clone->series_names[0], "cpu");
    ASSERT(clone->series_counts[0] == 2);
    ASSERT(clone->series_data[0][0].value == 50.0);
    ASSERT(clone->series_data[0][1].value == 75.0);
    ASSERT_STR(clone->param1, "testparam");

    free_metric_data(clone);
    rrd_cache_free();
}

/* Get по неизвестному ключу (другой путь или другой период) → NULL. */
TEST(cache_miss_unknown_key) {
    rrd_cache_init(60);

    MetricData *d = make_test_data("mem", 10.0, 20.0);
    rrd_cache_put("alpha.rrd", 3600, d);

    ASSERT(rrd_cache_get("beta.rrd", 3600) == NULL);   /* другой путь */
    ASSERT(rrd_cache_get("alpha.rrd", 7200) == NULL);  /* другой период */

    rrd_cache_free();
}

/* Get до инициализации кэша → NULL (cache_initialized == 0). */
TEST(cache_get_before_init_returns_null) {
    /* После rrd_cache_free() в предыдущем тесте cache_initialized == 0. */
    ASSERT(rrd_cache_get("anything.rrd", 3600) == NULL);
    ASSERT(rrd_cache_get(NULL, 3600) == NULL);
}

/* Истечение TTL: после TTL запись считается просроченной → NULL. */
TEST(cache_ttl_expiry) {
    rrd_cache_init(1);  /* TTL = 1 секунда */

    MetricData *d = make_test_data("net", 1.0, 2.0);
    rrd_cache_put("net.rrd", 3600, d);

    /* Немедленный get — запись ещё свежая. */
    MetricData *fresh = rrd_cache_get("net.rrd", 3600);
    ASSERT(fresh != NULL);
    ASSERT(fresh->series_data[0][0].value == 1.0);
    free_metric_data(fresh);

    /* После истечения TTL — NULL. */
    sleep(2);
    ASSERT(rrd_cache_get("net.rrd", 3600) == NULL);

    rrd_cache_free();
}

/* Повторный put по тому же ключу заменяет запись. */
TEST(cache_put_replaces_entry) {
    rrd_cache_init(60);

    MetricData *first = make_test_data("old", 1.0, 2.0);
    rrd_cache_put("replace.rrd", 3600, first);

    MetricData *second = make_test_data("new", 3.0, 4.0);
    rrd_cache_put("replace.rrd", 3600, second);  /* вытесняет first */

    MetricData *got = rrd_cache_get("replace.rrd", 3600);
    ASSERT(got != NULL);
    ASSERT_STR(got->series_names[0], "new");     /* вторая запись */
    ASSERT(got->series_data[0][0].value == 3.0);

    free_metric_data(got);
    rrd_cache_free();
}

TEST_MAIN()
    RUN(cache_put_get_clone);
    RUN(cache_miss_unknown_key);
    RUN(cache_get_before_init_returns_null);
    RUN(cache_ttl_expiry);
    RUN(cache_put_replaces_entry);
TEST_RETURN()
