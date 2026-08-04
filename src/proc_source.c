/**
 * @file proc_source.c
 * @brief Реализация источника SRC_PROC — live-чтение /proc (Фаза 2, Stage 2)
 *
 * См. include/proc_source.h. Чистые парсеры выделены и тестируются (test_proc.c);
 * read_cpu/read_load выполняют I/O и сборку MetricData.
 *
 * Утилизация CPU считается дельтой между последовательными вызовами (как в
 * collectd/node_exporter): статический предыдущий сэмпл под мьютексом. Первый
 * вызов даёт утилизацию с момента загрузки. При работе через кэш (LSRP, TTL≈5 с)
 * дельта берётся по интервалу кэша — корректная семантика «среднее за окно».
 */
#include "../include/proc_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>

/* ---- Чистые функции (без I/O, тестируются напрямую) ---- */

int proc_parse_cpu_stat(const char *line, unsigned long long *busy,
                        unsigned long long *total) {
    if (!line || !busy || !total) return -1;

    /* Пропустить ведущие пробелы и токен "cpu". */
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "cpu", 3) != 0) return -1;
    p += 3;
    /* Агрегатная строка — "cpu", за которой пробел; поядровые "cpu0".. здесь
     * не ожидаются (читаем только первую строку /proc/stat), но для надёжности
     * отвергаем цифру сразу после "cpu". */
    if (isdigit((unsigned char)*p)) return -1;

    /* user nice system idle iowait irq softirq steal (первые 8; guest* не считаем
     * — они уже включены в user/nice). Отсутствующие поля = 0. */
    unsigned long long v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int n = sscanf(p, "%llu %llu %llu %llu %llu %llu %llu %llu",
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    if (n < 4) return -1; /* нужен минимум user,nice,system,idle */

    unsigned long long b = v[0] + v[1] + v[2] + v[5] + v[6] + v[7]; /* +irq,softirq,steal */
    unsigned long long idle = v[3] + v[4];                          /* idle,iowait */
    *busy = b;
    *total = b + idle;
    return 0;
}

double proc_cpu_utilization(unsigned long long busy0, unsigned long long total0,
                            unsigned long long busy1, unsigned long long total1) {
    /* Дельты считаем как signed: jiffies укладываются в signed long long, а при
     * сбросе/возврате счётчика Δbusy может стать отрицательной — clamp в [0,100]
     * даст честный 0% вместо unsigned-переполнения (огромное положительное). */
    long long dt_ll = (long long)total1 - (long long)total0;
    long long db_ll = (long long)busy1 - (long long)busy0;
    if (dt_ll <= 0) return 0.0; /* нет прошедшего времени / wrap */
    double util = ((double)db_ll / (double)dt_ll) * 100.0;
    if (util < 0.0) util = 0.0;
    if (util > 100.0) util = 100.0;
    return util;
}

int proc_parse_loadavg(const char *line, double *l1, double *l5, double *l15) {
    if (!line || !l1 || !l5 || !l15) return -1;
    if (sscanf(line, "%lf %lf %lf", l1, l5, l15) != 3) return -1;
    return 0;
}

/* ---- I/O и сборка MetricData ---- */

/* Предыдущий сэмпл CPU для дельты (thread-safe). */
static struct {
    unsigned long long busy;
    unsigned long long total;
    int have;
} g_cpu_prev = {0, 0, 0};
static pthread_mutex_t g_cpu_prev_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Прочитать первую строку файла в buf. 0 — успех. */
static int read_first_line(const char *path, char *buf, size_t size) {
    if (!path || !buf || size == 0) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    buf[0] = '\0';
    char *got = fgets(buf, (int)size, f);
    fclose(f);
    return (got && buf[0]) ? 0 : -1;
}

/* Выделить MetricData с series_count сериями (имена/данные заполняет вызывающий).
 * series_data[i] и series_names[i] остаются NULL — ридер обязан их задать. */
static MetricData* alloc_metric_data(int series_count) {
    if (series_count <= 0) return NULL;
    MetricData *d = calloc(1, sizeof(MetricData));
    if (!d) return NULL;
    d->series_count = series_count;
    d->series_names = calloc((size_t)series_count, sizeof(char *));
    d->series_data = calloc((size_t)series_count, sizeof(DataPoint *));
    d->series_counts = calloc((size_t)series_count, sizeof(int));
    d->param1 = NULL;
    d->metric_config = NULL;
    if (!d->series_names || !d->series_data || !d->series_counts) {
        free(d->series_names);
        free(d->series_data);
        free(d->series_counts);
        free(d);
        return NULL;
    }
    return d;
}

/* Освободить недособранный MetricData (для пути с ошибкой после alloc). */
static void free_metric_data_partial(MetricData *d) {
    if (!d) return;
    /* rrd_data_free корректен и для частично заполненной структуры: он идёт по
     * series_count и free()ёт series_names[i]/series_data[i] (допустимы NULL). */
    free_metric_data(d);
}

static MetricData* read_cpu(void) {
    char line[512];
    if (read_first_line("/proc/stat", line, sizeof(line)) != 0) return NULL;

    unsigned long long busy = 0, total = 0;
    if (proc_parse_cpu_stat(line, &busy, &total) != 0) return NULL;

    double util;
    pthread_mutex_lock(&g_cpu_prev_mutex);
    if (g_cpu_prev.have) {
        util = proc_cpu_utilization(g_cpu_prev.busy, g_cpu_prev.total, busy, total);
    } else {
        /* Первый сэмпл: утилизация с момента загрузки. */
        util = proc_cpu_utilization(0, 0, busy, total);
    }
    g_cpu_prev.busy = busy;
    g_cpu_prev.total = total;
    g_cpu_prev.have = 1;
    pthread_mutex_unlock(&g_cpu_prev_mutex);

    MetricData *d = alloc_metric_data(1);
    if (!d) return NULL;
    d->series_names[0] = strdup("utilization");
    d->series_data[0] = malloc(sizeof(DataPoint));
    if (!d->series_names[0] || !d->series_data[0]) {
        free_metric_data_partial(d);
        return NULL;
    }
    d->series_data[0][0].timestamp = time(NULL);
    d->series_data[0][0].value = util;
    d->series_counts[0] = 1;
    d->param1 = strdup("");
    if (!d->param1) {
        free_metric_data_partial(d);
        return NULL;
    }
    return d;
}

static MetricData* read_load(void) {
    char line[256];
    if (read_first_line("/proc/loadavg", line, sizeof(line)) != 0) return NULL;

    double l1 = 0, l5 = 0, l15 = 0;
    if (proc_parse_loadavg(line, &l1, &l5, &l15) != 0) return NULL;

    MetricData *d = alloc_metric_data(3);
    if (!d) return NULL;
    time_t now = time(NULL);
    const char *names[3] = {"load1", "load5", "load15"};
    double vals[3] = {l1, l5, l15};
    for (int i = 0; i < 3; i++) {
        d->series_names[i] = strdup(names[i]);
        d->series_data[i] = malloc(sizeof(DataPoint));
        if (!d->series_names[i] || !d->series_data[i]) {
            free_metric_data_partial(d);
            return NULL;
        }
        d->series_data[i][0].timestamp = now;
        d->series_data[i][0].value = vals[i];
        d->series_counts[i] = 1;
    }
    d->param1 = strdup("");
    if (!d->param1) {
        free_metric_data_partial(d);
        return NULL;
    }
    return d;
}

MetricData* proc_source_fetch(Config *config, MetricConfig *metric, int period) {
    (void)config;   /* proc читает системное /proc — настройки сервера не нужны */
    (void)period;   /* live-значение: история не строится (см. заметку в header) */
    if (!metric) return NULL;

    if (strcmp(metric->proc_metric, "cpu") == 0) return read_cpu();
    if (strcmp(metric->proc_metric, "load") == 0) return read_load();

    fprintf(stderr, "Warning: unknown proc_metric '%s' (supported: \"cpu\", \"load\")\n",
            metric->proc_metric[0] ? metric->proc_metric : "(empty)");
    return NULL;
}
