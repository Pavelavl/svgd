/**
 * @file metric_source.c
 * @brief Реализация диспетчера источников метрик (Фаза 2)
 *
 * См. include/metric_source.h. Диспетчер metric_source_fetch() — единая точка
 * между хранилищем и рендерингом; выбирает бэкенд по metric->source.
 *
 * Ветвь SRC_RRD побайтово повторяет прежнюю inline-логику handler.c
 * (build_rrd_path → cache_get/put → fetch_metric_data): Stage 1 = чистый
 * рефакторинг с нулевым изменением поведения. SRC_PROC (Stage 2) и
 * SRC_PROMETHEUS (Stage 3) добавлены позже.
 *
 * Кэш: используется существующий универсальный кэш (src/rrd/cache.c), ключ
 * строится как "<spec>:<period>". Для RRD spec = rrd_path (как раньше); для proc
 * и prometheus spec = "proc:<metric>" / "prom:<url>" (не коллидирует с RRD-путями,
 * те начинаются с '/'). TTL общий (config.cache_ttl_seconds); для live-данных proc
 * и опроса prometheus короткий TTL (по умолчанию 5 с) склеивает всплески запросов.
 */
#include "../include/metric_source.h"
#include "../include/path_util.h"
#include "../include/proc_source.h"
#include "../include/prometheus_source.h"
#include "../include/rrd/cache.h"
#include <time.h>
#include <stdio.h>

MetricData* metric_source_fetch(Config *config, MetricConfig *metric,
                                const char *param, int period, int use_cache) {
    if (!config || !metric) return NULL;

    MetricData *data = NULL;

    switch (metric->source) {
    case SRC_RRD: {
        /* Полностью повторяет прежнюю inline-логику handler.c (нулевая стадия). */
        char rrd_path[512] = {0};
        build_rrd_path(rrd_path, sizeof(rrd_path), config->rrd_base_path,
                       metric->rrd_path, param);

        if (use_cache) {
            data = cache_get(rrd_path, period);
        }

        if (!data) {
            time_t now = time(NULL);
            MetricData *fresh_data = fetch_metric_data(config->rrdcached_addr, rrd_path,
                                                       now - period, param, metric);
            if (fresh_data) {
                if (use_cache) {
                    cache_put(rrd_path, period, fresh_data);
                    /* cache_put забирает владение fresh_data; клонируем обратно. */
                    data = cache_get(rrd_path, period);
                    /* При неудаче клонирования (OOM) данные остались в кэше, но
                     * сейчас вернуть нечего — следующий запрос попробует снова. */
                } else {
                    data = fresh_data;
                }
            }
        }
        break;
    }

    case SRC_PROC: {
        /* Stage 2: live-чтение /proc → сборка MetricData в памяти (без диска). */
        data = proc_source_fetch(config, metric, period);
        break;
    }

    case SRC_PROMETHEUS: {
        /* Stage 3: парсинг Prometheus text-exposition (HTTP GET /metrics). */
        data = prometheus_source_fetch(config, metric, period);
        break;
    }

    default:
        /* Неизвестный источник — безопасный фолбэк (никаких данных). */
        data = NULL;
        break;
    }

    return data;
}
