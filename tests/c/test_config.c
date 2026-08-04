/**
 * @file test_config.c
 * @brief Тесты load_config — парсинг config.json через Duktape
 *
 * Каждый тест создаёт свежий Duktape-heap и временный JSON-файл.
 *
 * Примечание о покрытии: тестируются реалистичные конфиги, содержащие все
 * верхнеуровневые секции (server/rrd/js/metrics) — как config.sample.json.
 * Синтаксически битый JSON намеренно не тестируется: duk_json_decode без
 * защищённого кадра бросает throw (отдельная задача робастности).
 *
 * Раньше конфиги без секции server/rrd/js исключались: load_config оставлял
 * undefined на стеке Duktape → следующий get_prop падал (латентный баг). Этот
 * баг починен (безусловный duk_pop), и теперь missing_section_* тесты явно
 * проверяют устойчивость к неполным конфигам.
 */
#include "minitest.h"
#include "cfg.h"
#include <duktape.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Шаблон временного каталога (mkdtemp замещает XXXXXX на месте). */
static char tmpdir[] = "/tmp/svgd_cfg_test_XXXXXX";

static int ensure_tmpdir(void) {
    if (access(tmpdir, F_OK) == 0) return 0;
    return (mkdtemp(tmpdir) != NULL) ? 0 : -1;
}

static const char* write_config(const char *name, const char *json) {
    static char path[512];
    snprintf(path, sizeof path, "%s/%s", tmpdir, name);
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    fputs(json, f);
    fclose(f);
    return path;
}

TEST(parses_full_config) {
    duk_context *ctx = duk_create_heap_default();
    ASSERT(ctx != NULL);
    ASSERT(ensure_tmpdir() == 0);
    const char *json =
        "{"
        "  \"server\": { \"tcp_port\": 9090, \"thread_pool_size\": 8 },"
        "  \"rrd\": { \"base_path\": \"/var/rrd\" },"
        "  \"js\": { \"script_path\": \"./src/scripts/generate_svg.js\" },"
        "  \"metrics\": ["
        "    { \"endpoint\": \"cpu\", \"rrd_path\": \"cpu/percent.rrd\" },"
        "    { \"endpoint\": \"cpu/process\","
        "      \"rrd_path\": \"processes-%s/ps_cputime.rrd\","
        "      \"requires_param\": true,"
        "      \"param_name\": \"process_name\","
        "      \"transform_type\": \"sum\" }"
        "  ]"
        "}";
    const char *path = write_config("full.json", json);
    ASSERT(path != NULL);

    Config c = load_config(ctx, path);
    ASSERT(c.tcp_port == 9090);
    ASSERT(c.thread_pool_size == 8);
    ASSERT_STR(c.rrd_base_path, "/var/rrd");
    ASSERT_STR(c.js_script_path, "./src/scripts/generate_svg.js");
    ASSERT(c.metrics_count == 2);
    ASSERT_STR(c.metrics[0].endpoint, "cpu");
    ASSERT_STR(c.metrics[0].rrd_path, "cpu/percent.rrd");
    ASSERT(c.metrics[0].requires_param == 0);
    ASSERT_STR(c.metrics[1].endpoint, "cpu/process");
    ASSERT_STR(c.metrics[1].rrd_path, "processes-%s/ps_cputime.rrd");
    ASSERT(c.metrics[1].requires_param == 1);
    ASSERT_STR(c.metrics[1].param_name, "process_name");
    ASSERT_STR(c.metrics[1].transform_type, "sum");

    free_config(&c);
    duk_destroy_heap(ctx);
}

TEST(missing_metrics_section_keeps_defaults) {
    duk_context *ctx = duk_create_heap_default();
    ASSERT(ctx != NULL);
    ASSERT(ensure_tmpdir() == 0);
    /* Все секции, кроме metrics, присутствуют. */
    const char *json =
        "{"
        "  \"server\": { \"tcp_port\": 7777 },"
        "  \"rrd\": { \"base_path\": \"/r\" },"
        "  \"js\": { \"script_path\": \"./s.js\" }"
        "}";
    const char *path = write_config("nometrics.json", json);
    ASSERT(path != NULL);

    Config c = load_config(ctx, path);
    ASSERT(c.tcp_port == 7777);
    ASSERT(c.metrics_count == 0);
    ASSERT(c.metrics == NULL);

    free_config(&c);
    duk_destroy_heap(ctx);
}

TEST(nonexistent_file_returns_defaults) {
    duk_context *ctx = duk_create_heap_default();
    ASSERT(ctx != NULL);

    Config c = load_config(ctx, "/no/such/svgd_file.json");
    /* fopen() падает -> warning + возврат дефолтной конфигурации. */
    ASSERT(c.tcp_port == 8080);
    ASSERT(c.metrics_count == 0);

    free_config(&c);
    duk_destroy_heap(ctx);
}

TEST(metric_missing_required_fields_zeroed) {
    duk_context *ctx = duk_create_heap_default();
    ASSERT(ctx != NULL);
    ASSERT(ensure_tmpdir() == 0);
    const char *json =
        "{"
        "  \"server\": { \"tcp_port\": 1 },"
        "  \"rrd\": { \"base_path\": \"/r\" },"
        "  \"js\": { \"script_path\": \"./s.js\" },"
        "  \"metrics\": ["
        "    { \"endpoint\": \"\", \"rrd_path\": \"\" },"
        "    { \"endpoint\": \"cpu\", \"rrd_path\": \"cpu.rrd\" }"
        "  ]"
        "}";
    const char *path = write_config("missing.json", json);
    ASSERT(path != NULL);

    Config c = load_config(ctx, path);
    /* Слот остаётся в массиве (metrics_count учитывает его), но обнулён. */
    ASSERT(c.metrics_count == 2);
    ASSERT_STR(c.metrics[0].endpoint, "");
    ASSERT_STR(c.metrics[0].rrd_path, "");
    ASSERT_STR(c.metrics[1].endpoint, "cpu");
    ASSERT_STR(c.metrics[1].rrd_path, "cpu.rrd");

    free_config(&c);
    duk_destroy_heap(ctx);
}

TEST(json_not_object_returns_defaults) {
    duk_context *ctx = duk_create_heap_default();
    ASSERT(ctx != NULL);
    ASSERT(ensure_tmpdir() == 0);
    /* Валидный JSON, но не объект (число) — должен сработать guard
     * «config.json must contain an object» с возвратом дефолтной конфигурации. */
    const char *json = "42";
    const char *path = write_config("num.json", json);
    ASSERT(path != NULL);

    Config c = load_config(ctx, path);
    ASSERT(c.metrics_count == 0);
    ASSERT(c.tcp_port == 8080);

    free_config(&c);
    duk_destroy_heap(ctx);
}

TEST(parses_metric_source_fields) {
    duk_context *ctx = duk_create_heap_default();
    ASSERT(ctx != NULL);
    ASSERT(ensure_tmpdir() == 0);
    /* Три метрики с разными источниками. RRD-метрика без rrd_path помечается
     * невалидной (обнуляется), proc/prometheus без rrd_path — валидны (ослабленная
     * проверка). Также проверяем, что отсутствие поля "source" даёт SRC_RRD. */
    const char *json =
        "{"
        "  \"server\": { \"tcp_port\": 1 },"
        "  \"rrd\": { \"base_path\": \"/r\" },"
        "  \"js\": { \"script_path\": \"./s.js\" },"
        "  \"metrics\": ["
        "    { \"endpoint\": \"cpu\", \"rrd_path\": \"cpu/percent.rrd\" },"
        "    { \"endpoint\": \"livecpu\", \"source\": \"proc\","
        "      \"proc_metric\": \"cpu\" },"
        "    { \"endpoint\": \"extq\", \"source\": \"prometheus\","
        "      \"prometheus_url\": \"http://exporter:9100/metrics\" },"
        "    { \"endpoint\": \"bad\", \"source\": \"rrd\", \"rrd_path\": \"\" }"
        "  ]"
        "}";
    const char *path = write_config("sources.json", json);
    ASSERT(path != NULL);

    Config c = load_config(ctx, path);
    ASSERT(c.metrics_count == 4);

    /* [0] без "source" → SRC_RRD (обратная совместимость). */
    ASSERT(c.metrics[0].source == SRC_RRD);
    ASSERT_STR(c.metrics[0].rrd_path, "cpu/percent.rrd");

    /* [1] proc: source/proc_metric разобраны; rrd_path пуст, но метрика валидна. */
    ASSERT(c.metrics[1].source == SRC_PROC);
    ASSERT_STR(c.metrics[1].proc_metric, "cpu");
    ASSERT_STR(c.metrics[1].rrd_path, "");

    /* [2] prometheus: url разобран; метрика валидна без rrd_path. */
    ASSERT(c.metrics[2].source == SRC_PROMETHEUS);
    ASSERT_STR(c.metrics[2].prometheus_url, "http://exporter:9100/metrics");

    /* [3] SRC_RRD без rrd_path → обнулён (невалиден). */
    ASSERT(c.metrics[3].source == SRC_RRD);
    ASSERT_STR(c.metrics[3].endpoint, "");

    free_config(&c);
    duk_destroy_heap(ctx);
}

/* Regression-тесты на латентный баг load_config: отсутствие верхнеуровневых
 * секций больше не роняет парсер (duk_fatal). Раньше при отсутствии секции
 * undefined оставался на стеке Duktape, следующий get_prop таргетил undefined →
 * TypeError → SIGABRT. После фикса (безусловный duk_pop) — устойчиво. */

TEST(missing_server_section_keeps_defaults) {
    duk_context *ctx = duk_create_heap_default();
    ASSERT(ctx != NULL);
    ASSERT(ensure_tmpdir() == 0);
    /* Нет секции server — значения по умолчанию, парсер не падает. */
    const char *json =
        "{"
        "  \"rrd\": { \"base_path\": \"/r\" },"
        "  \"js\": { \"script_path\": \"./s.js\" },"
        "  \"metrics\": [ { \"endpoint\": \"cpu\", \"rrd_path\": \"cpu.rrd\" } ]"
        "}";
    const char *path = write_config("noserver.json", json);
    ASSERT(path != NULL);

    Config c = load_config(ctx, path);
    ASSERT(c.tcp_port == 8080);          /* default */
    ASSERT(c.thread_pool_size == 4);     /* default */
    ASSERT(c.metrics_count == 1);
    ASSERT_STR(c.metrics[0].endpoint, "cpu");

    free_config(&c);
    duk_destroy_heap(ctx);
}

TEST(missing_rrd_and_js_sections) {
    duk_context *ctx = duk_create_heap_default();
    ASSERT(ctx != NULL);
    ASSERT(ensure_tmpdir() == 0);
    /* Есть только server + metrics (нет rrd и js). Раньше отсутствие rrd после
     * server (или js после rrd) оставляло undefined → fatal. */
    const char *json =
        "{"
        "  \"server\": { \"tcp_port\": 4242 },"
        "  \"metrics\": [ { \"endpoint\": \"cpu\", \"rrd_path\": \"cpu.rrd\" } ]"
        "}";
    const char *path = write_config("norrdjs.json", json);
    ASSERT(path != NULL);

    Config c = load_config(ctx, path);
    ASSERT(c.tcp_port == 4242);
    ASSERT(c.metrics_count == 1);
    ASSERT_STR(c.metrics[0].endpoint, "cpu");

    free_config(&c);
    duk_destroy_heap(ctx);
}

TEST_MAIN()
    RUN(parses_full_config);
    RUN(missing_metrics_section_keeps_defaults);
    RUN(nonexistent_file_returns_defaults);
    RUN(metric_missing_required_fields_zeroed);
    RUN(json_not_object_returns_defaults);
    RUN(parses_metric_source_fields);
    RUN(missing_server_section_keeps_defaults);
    RUN(missing_rrd_and_js_sections);
TEST_RETURN()
