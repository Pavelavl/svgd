/**
 * @file prometheus_source.h
 * @brief Источник метрик SRC_PROMETHEUS — парсинг Prometheus text-exposition
 *        (Фаза 2, Stage 3)
 *
 * HTTP GET по metric->prometheus_url (endpoint /metrics экспортёра) → парсинг
 * строк формата `metric{label="x"} value [ts]` → MetricData. svgd визуализирует
 * чужие метрики (node_exporter, любой exporter с text-exposition).
 *
 * Выбор метрики: для SRC_PROMETHEUS поле metric->endpoint используется как имя
 * Prometheus-метрики (фильтр по `metric name == endpoint`). Несколько сэмплов
 * одной метрики с разными labelset'ами → несколько серий; имя серии = содержимое
 * `{...}` (например `cpu="0",mode="idle"`), без лейблов — имя метрики.
 *
 * Чистые функции prom_parse_url / prom_parse_line выделены без I/O и покрыты
 * unit-тестами (tests/c/test_prom.c) — тот же приём, что select_step_from_rras.
 *
 * Ограничения (честные gap'ы Stage 3):
 *   - Только HTTP (без TLS): сырой сокет, без новых зависимостей. https://
 *     отвергается (для LAN-экспортёров HTTP — норма; node_exporter по умолчанию
 *     так и работает).
 *   - Как и proc, отдаётся текущее значение (одна точка на серию); исторический
 *     ряд не строится.
 *   - Каждая строка exposition = одна серия (1 точка); повторяющиеся labelset'ы
 *     дают несколько серий (редкий случай — экспортёры отдают один сэмпл на серию).
 */
#ifndef SVGD_PROMETHEUS_SOURCE_H
#define SVGD_PROMETHEUS_SOURCE_H

#include "cfg.h"
#include "rrd/reader.h"  /* MetricData */

/**
 * @brief Разобрать URL вида http://host[:port][/path]
 *
 * @param url Строка URL (обязателен префикс "http://"; https не поддерживается)
 * @param host[out] Буфер под имя хоста
 * @param port[out] Порт (80 по умолчанию)
 * @param path[out] Буфер под путь (включая ведущий '/'; "/" если не задан)
 * @return 0 при успехе, -1 при ошибке (не http://, пустой хост, плохой порт)
 */
int prom_parse_url(const char *url, char *host, size_t host_size,
                   int *port, char *path, size_t path_size);

/**
 * @brief Разобрать одну строку Prometheus text-exposition
 *
 * @param line Строка (без требования \n на конце)
 * @param name_buf[out] Имя метрики (до '{' или пробела)
 * @param labels_buf[out] Содержимое между { и } (пусто, если лейблов нет)
 * @param value[out] Численное значение (NaN/Inf обрабатываются)
 * @param has_ts[out] 1, если был завершающий timestamp
 * @param ts_ms[out] Timestamp в миллисекундах (epoch), если has_ts
 * @return 0 — строка-метрика разобрана; 1 — пропуск (пустая строка / комментарий '#');
 *        -1 — некорректная строка
 */
int prom_parse_line(const char *line,
                    char *name_buf, size_t name_size,
                    char *labels_buf, size_t labels_size,
                    double *value, int *has_ts, long long *ts_ms);

/**
 * @brief Получить MetricData из Prometheus text-exposition
 *
 * HTTP GET metric->prometheus_url → парсинг → серии, отфильтрованные по
 * metric->endpoint (как имя метрики). Один сэмпл на серию (текущее значение).
 * @param config (не используется)
 * @param metric Конфиг (prometheus_url, endpoint)
 * @param period (не используется — live-значение)
 * @return MetricData (free_metric_data) или NULL при ошибке/отсутствии метрики
 */
MetricData* prometheus_source_fetch(Config *config, MetricConfig *metric, int period);

#endif /* SVGD_PROMETHEUS_SOURCE_H */
