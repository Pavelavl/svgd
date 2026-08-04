/**
 * @file prometheus_source.c
 * @brief Реализация источника SRC_PROMETHEUS — парсинг text-exposition
 *        (Фаза 2, Stage 3)
 *
 * См. include/prometheus_source.h. Чистые парсеры (prom_parse_url /
 * prom_parse_line) тестируются напрямую (test_prom.c); http_get_body выполняет
 * I/O (сырой сокет, HTTP/1.1, без TLS).
 *
 * Сборка MetricData: каждая подходящая строка exposition (metric name ==
 * metric->endpoint, значение конечно) становится отдельной серией с одной
 * точкой. Имя серии = содержимое {...} либо, при отсутствии лейблов, имя метрики.
 * Timestamp: из exposition (мс epoch → с), иначе now.
 */
#include "../include/prometheus_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

/* Сетевой I/O для HTTP GET. */
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>   /* close */

/* ---- Чистые функции (без I/O, тестируются напрямую) ---- */

int prom_parse_url(const char *url, char *host, size_t host_size,
                   int *port, char *path, size_t path_size) {
    if (!url || !host || host_size == 0 || !port || !path || path_size == 0) return -1;
    *port = 80;
    host[0] = '\0';
    path[0] = '\0';

    /* Обязателен http:// (https не поддерживается — нет TLS). */
    if (strncmp(url, "http://", 7) != 0) return -1;
    const char *p = url + 7;

    /* host: до ':' или '/'. */
    size_t hi = 0;
    while (*p && *p != ':' && *p != '/') {
        if (hi + 1 < host_size) host[hi++] = *p;
        p++;
    }
    host[hi] = '\0';
    if (hi == 0) return -1;

    /* опциональный :port */
    if (*p == ':') {
        p++;
        if (!isdigit((unsigned char)*p)) return -1;
        int pvt = 0;
        while (isdigit((unsigned char)*p)) {
            pvt = pvt * 10 + (*p - '0');
            p++;
            if (pvt > 65535) return -1;
        }
        if (pvt <= 0) return -1;
        *port = pvt;
    }

    /* путь: с '/' до конца, иначе "/". */
    if (*p == '/') {
        size_t pl = strlen(p);
        if (pl >= path_size) pl = path_size - 1;
        memcpy(path, p, pl);
        path[pl] = '\0';
    } else {
        path[0] = '/'; path[1] = '\0';
    }
    return 0;
}

int prom_parse_line(const char *line,
                    char *name_buf, size_t name_size,
                    char *labels_buf, size_t labels_size,
                    double *value, int *has_ts, long long *ts_ms) {
    if (!line || !name_buf || name_size == 0 || !labels_buf || labels_size == 0 ||
        !value || !has_ts || !ts_ms) return -1;
    name_buf[0] = '\0';
    labels_buf[0] = '\0';
    *has_ts = 0;
    *ts_ms = 0;

    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') return 1; /* skip */

    /* metric name: до '{', пробела, конца строки. */
    size_t ni = 0;
    while (*p && *p != '{' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
        if (ni + 1 < name_size) name_buf[ni++] = *p;
        p++;
    }
    name_buf[ni] = '\0';
    if (ni == 0) return -1;

    while (*p == ' ' || *p == '\t') p++;

    /* опциональные лейблы { ... } */
    if (*p == '{') {
        p++;
        const char *start = p;
        const char *end = strchr(p, '}');
        if (!end) return -1; /* незакрытые лейблы */
        size_t llen = (size_t)(end - start);
        if (llen >= labels_size) llen = labels_size - 1;
        memcpy(labels_buf, start, llen);
        labels_buf[llen] = '\0';
        p = end + 1;
    }

    /* value */
    while (*p == ' ' || *p == '\t') p++;
    char val_tok[64];
    size_t vi = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
        if (vi + 1 < sizeof(val_tok)) val_tok[vi++] = *p;
        p++;
    }
    val_tok[vi] = '\0';
    if (vi == 0) return -1;

    if (strcmp(val_tok, "NaN") == 0) *value = NAN;
    else if (strcmp(val_tok, "+Inf") == 0 || strcmp(val_tok, "Inf") == 0) *value = INFINITY;
    else if (strcmp(val_tok, "-Inf") == 0) *value = -INFINITY;
    else *value = strtod(val_tok, NULL);

    /* опциональный timestamp (мс epoch по конвенции Prometheus) */
    while (*p == ' ' || *p == '\t') p++;
    if (*p && *p != '\n' && *p != '\r') {
        char ts_tok[32];
        size_t ti = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            if (ti + 1 < sizeof(ts_tok)) ts_tok[ti++] = *p;
            p++;
        }
        ts_tok[ti] = '\0';
        if (ti > 0) {
            *has_ts = 1;
            *ts_ms = strtoll(ts_tok, NULL, 10);
        }
    }
    return 0;
}

/* ---- I/O: HTTP/1.1 GET (сырой сокет, без TLS) ---- */

/* GET path на host:port, возвращает malloc'd тело ответа (caller frees) или NULL.
 * Читает до закрытия сервером соединения (Connection: close) с таймаутом на recv. */
static char* http_get_body(const char *host, int port, const char *path) {
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;      /* IPv4; минимальный Stage 3 */
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return NULL;

    int sock = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;
        /* таймаут на чтение, чтобы не зависнуть на «молчащем» экспортёре */
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) return NULL;

    /* Запрос: Connection: close — сервер закроет сокет после ответа. */
    char req[1024];
    int reqlen = snprintf(req, sizeof(req),
                          "GET %s HTTP/1.1\r\n"
                          "Host: %s\r\n"
                          "User-Agent: svgd\r\n"
                          "Accept: text/plain\r\n"
                          "Connection: close\r\n"
                          "\r\n",
                          path, host);
    if (reqlen <= 0 || (size_t)reqlen >= sizeof(req)) { close(sock); return NULL; }

    const char *wp = req;
    size_t left = (size_t)reqlen;
    while (left > 0) {
        ssize_t n = send(sock, wp, left, 0);
        if (n <= 0) { close(sock); return NULL; }
        wp += n; left -= (size_t)n;
    }

    /* Чтение ответа до EOF (Connection: close). */
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    if (!buf) { close(sock); return NULL; }
    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); close(sock); return NULL; }
            buf = nb;
        }
        ssize_t n = recv(sock, buf + len, cap - len - 1, 0);
        if (n <= 0) break;
        len += (size_t)n;
        if (len > 8 * 1024 * 1024) break; /* верхний предел 8 МБ */
    }
    buf[len] = '\0';
    close(sock);

    /* Тело — после первой пустой строки (\r\n\r\n). */
    char *body = strstr(buf, "\r\n\r\n");
    if (!body) { free(buf); return NULL; }
    body += 4;
    char *out = strdup(body);
    free(buf);
    return out;
}

/* ---- Сборка MetricData ---- */

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
        free(d->series_names); free(d->series_data); free(d->series_counts); free(d);
        return NULL;
    }
    return d;
}

/* Не мутирующий обход строк: копирует очередную строку (до \n) в linebuf и
 * сдвигает *cursor. Возвращает 0, если строка прочитана; -1 в конце. Два прохода
 * по body безопасны (в отличие от strtok_r, который затирал бы \n). */
static int next_line(const char **cursor, const char *end, char *linebuf, size_t size) {
    if (!cursor || !*cursor || *cursor >= end || size == 0) return -1;
    const char *start = *cursor;
    const char *nl = memchr(start, '\n', (size_t)(end - start));
    const char *line_end = nl ? nl : end;
    size_t l = (size_t)(line_end - start);
    if (l >= size) l = size - 1;
    memcpy(linebuf, start, l);
    linebuf[l] = '\0';
    *cursor = nl ? nl + 1 : end;
    return 0;
}

MetricData* prometheus_source_fetch(Config *config, MetricConfig *metric, int period) {
    (void)config;   /* не используется */
    (void)period;   /* live-значение, без истории (см. заметку в header) */
    if (!metric) return NULL;

    char host[256], path[512];
    int port = 80;
    if (prom_parse_url(metric->prometheus_url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        fprintf(stderr, "Warning: prometheus: invalid url '%s' (need http://host[:port]/path)\n",
                metric->prometheus_url);
        return NULL;
    }

    char *body = http_get_body(host, port, path);
    if (!body) {
        fprintf(stderr, "Warning: prometheus: fetch failed for %s\n", metric->prometheus_url);
        return NULL;
    }

    const char *end = body + strlen(body);

    /* Pass 1: посчитать подходящие строки (metric name == endpoint, значение конечно). */
    const int MAX_SERIES = 1024;
    int count = 0;
    {
        const char *cur = body;
        char linebuf[1024];
        while (next_line(&cur, end, linebuf, sizeof(linebuf)) == 0) {
            char nm[128], lb[256];
            double v; int hts; long long tms;
            int r = prom_parse_line(linebuf, nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms);
            if (r != 0) continue;
            if (strcmp(nm, metric->endpoint) != 0) continue;
            if (!isfinite(v)) continue;
            count++;
            if (count >= MAX_SERIES) break;
        }
    }

    if (count == 0) {
        free(body);
        return NULL;
    }

    MetricData *d = alloc_metric_data(count);
    if (!d) { free(body); return NULL; }

    /* Pass 2: заполнить серии (тем же обходом с начала). */
    int idx = 0;
    {
        const char *cur = body;
        char linebuf[1024];
        while (next_line(&cur, end, linebuf, sizeof(linebuf)) == 0 && idx < count) {
            char nm[128], lb[256];
            double v; int hts; long long tms;
            int r = prom_parse_line(linebuf, nm, sizeof(nm), lb, sizeof(lb), &v, &hts, &tms);
            if (r != 0) continue;
            if (strcmp(nm, metric->endpoint) != 0) continue;
            if (!isfinite(v)) continue;

            const char *sname = (lb[0] != '\0') ? lb : nm;
            d->series_names[idx] = strdup(sname);
            d->series_data[idx] = malloc(sizeof(DataPoint));
            if (!d->series_names[idx] || !d->series_data[idx]) {
                free_metric_data(d);
                free(body);
                return NULL;
            }
            d->series_data[idx][0].value = v;
            d->series_data[idx][0].timestamp = hts ? (time_t)(tms / 1000) : time(NULL);
            d->series_counts[idx] = 1;
            idx++;
        }
    }

    free(body);
    if (idx == 0) {
        free_metric_data(d);
        return NULL;
    }
    d->param1 = strdup("");
    return d;
}
