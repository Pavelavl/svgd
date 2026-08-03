/**
 * @file test_path.c
 * @brief Тесты build_rrd_path и extract_param_from_path — подстановка %s-параметра
 */
#include "minitest.h"
#include "path_util.h"
#include <stdlib.h>

/* --------------------- extract_param_from_path --------------------- */

TEST(extract_param_basic) {
    char *p = extract_param_from_path("cpu/process/nginx", "cpu/process");
    ASSERT(p != NULL);
    ASSERT_STR(p, "nginx");
    free(p);
}

TEST(extract_param_path_equals_endpoint_returns_null) {
    /* path совпадает с endpoint: после префикса нет символа -> параметр пуст. */
    ASSERT(extract_param_from_path("cpu", "cpu") == NULL);
}

TEST(extract_param_endpoint_not_prefix_returns_null) {
    ASSERT(extract_param_from_path("ram/disk", "cpu") == NULL);
}

TEST(extract_param_null_inputs) {
    ASSERT(extract_param_from_path(NULL, "cpu") == NULL);
    ASSERT(extract_param_from_path("cpu", NULL) == NULL);
}

TEST(extract_param_multilevel) {
    /* Параметр может содержать несколько сегментов пути. */
    char *p = extract_param_from_path("network/eth0/q0", "network");
    ASSERT(p != NULL);
    ASSERT_STR(p, "eth0/q0");
    free(p);
}

/* --------------------------- build_rrd_path --------------------------- */

TEST(build_path_with_param) {
    char out[256];
    build_rrd_path(out, sizeof out, "/r", "processes-%s/ps_rss.rrd", "nginx");
    ASSERT_STR(out, "/r/processes-nginx/ps_rss.rrd");
}

TEST(build_path_without_param_template) {
    char out[256];
    build_rrd_path(out, sizeof out, "/r", "cpu/percent.rrd", NULL);
    ASSERT_STR(out, "/r/cpu/percent.rrd");
}

TEST(build_path_percent_but_null_param) {
    /* Шаблон содержит '%', но param=NULL -> ветка else: шаблон без подстановки
     * (литерал %s остаётся, т.к. используется как данные, а не как формат). */
    char out[256];
    build_rrd_path(out, sizeof out, "/r", "processes-%s/ps_rss.rrd", NULL);
    ASSERT_STR(out, "/r/processes-%s/ps_rss.rrd");
}

TEST(build_path_param_ignored_when_no_percent) {
    /* Шаблон без '%': param игнорируется (ветка else). */
    char out[256];
    build_rrd_path(out, sizeof out, "/r", "cpu/percent.rrd", "ignored");
    ASSERT_STR(out, "/r/cpu/percent.rrd");
}

TEST(build_path_zero_size_noop) {
    /* Граничный случай: dest_size==0 — функция не пишет, не должно падать. */
    char out[1] = {'x'};
    build_rrd_path(out, 0, "/r", "x.rrd", NULL);
    ASSERT(out[0] == 'x');
}

TEST_MAIN()
    RUN(extract_param_basic);
    RUN(extract_param_path_equals_endpoint_returns_null);
    RUN(extract_param_endpoint_not_prefix_returns_null);
    RUN(extract_param_null_inputs);
    RUN(extract_param_multilevel);
    RUN(build_path_with_param);
    RUN(build_path_without_param_template);
    RUN(build_path_percent_but_null_param);
    RUN(build_path_param_ignored_when_no_percent);
    RUN(build_path_zero_size_noop);
TEST_RETURN()
