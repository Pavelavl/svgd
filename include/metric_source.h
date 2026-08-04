/**
 * @file metric_source.h
 * @brief Плагинабельный источник метрик (Фаза 2 — RESEARCH-strategy.md §4.4)
 *
 * Абстракция-шов между хранилищем метрик и конвейером рендеринга
 * (handler_process → generate_svg). До Фазы 2 единственный источник данных —
 * RRD-файлы (rrd_fetch_data). Здесь вводится диспетчер, выбирающий бэкенд по
 * per-metric полю "source" в config.json:
 *   - "rrd"        (по умолчанию) — RRD-файлы collectd/svgd-collect;
 *   - "proc"       — live-чтение /proc (без диска; для сверхслабых устройств);
 *   - "prometheus" — парсинг Prometheus text-exposition (svgd визуализирует чужие метрики).
 *
 * Диспетчер metric_source_fetch() инкапсулирует построение пути/URL/ключа кэша,
 * обращение к кэшу и вызов бэкенда. Для SRC_RRD поведение побайтово идентично
 * прежней inline-логике handler.c (нулевой этап = чистый рефакторинг без изменения
 * поведения).
 *
 * @note metric_source_from_string реализована как static inline в этом заголовке:
 *       чистая функция без внешних зависимостей, тестируется напрямую (tests/c),
 *       не требует линковки бэкендов. Диспетчер metric_source_fetch живёт в
 *       src/metric_source.c (ему нужны rrd_fetch_data/cache/path_util).
 */
#ifndef SVGD_METRIC_SOURCE_H
#define SVGD_METRIC_SOURCE_H

#include "cfg.h"        /* Config, MetricConfig, metric_source_t */
#include "rrd/reader.h" /* MetricData */

/**
 * @brief Разобрать строковое имя источника в enum metric_source_t
 *
 * Чистая функция. Контракт:
 *   "rrd" / "" / NULL            → SRC_RRD (умолчание — обратная совместимость);
 *   "proc"                       → SRC_PROC;
 *   "prometheus"                 → SRC_PROMETHEUS;
 *   любое неизвестное значение   → SRC_RRD (безопасный фолбэк).
 *
 * Сравнение регистро-зависимое (как и остальные строковые ключи в config.json).
 */
static inline metric_source_t metric_source_from_string(const char *s) {
    if (!s || !*s) return SRC_RRD;
    if (strcmp(s, "proc") == 0) return SRC_PROC;
    if (strcmp(s, "prometheus") == 0) return SRC_PROMETHEUS;
    return SRC_RRD; /* "rrd" и неизвестные → RRD */
}

/**
 * @brief Получить MetricData из источника, выбранного метрикой
 *
 * Инкапсулирует построение пути (RRD) / URL (Prometheus) / имени (proc),
 * обращение к кэшу (если use_cache) и вызов соответствующего бэкенда.
 * Для SRC_RRD — побайтово идентично прежней inline-логике handler.c
 * (build_rrd_path + cache_get/put + fetch_metric_data).
 *
 * @param config Конфиг сервера (использует rrd_base_path, rrdcached_addr)
 * @param metric Конфиг метрики (поле source выбирает бэкенд)
 * @param param  Параметр пути (может быть NULL; используется RRD-бэкендом)
 * @param period Запрошенный период в секундах
 * @param use_cache 1 = использовать кэш (LSRP-режим), 0 = всегда свежие данные
 * @return MetricData (вызывающий освобождает free_metric_data) или NULL при ошибке
 */
MetricData* metric_source_fetch(Config *config, MetricConfig *metric,
                                const char *param, int period, int use_cache);

#endif /* SVGD_METRIC_SOURCE_H */
