/**
 * @file reader.h
 * @brief RRD file reading module
 *
 * Provides functions to read time-series data from RRD files,
 * with optional rrdcached support for reduced disk I/O.
 */

#ifndef SVGD_RRD_READER_H
#define SVGD_RRD_READER_H

#include <time.h>
#include <stddef.h>
#include "../cfg.h"  /* For MetricConfig definition */

/* Data structures */
typedef struct {
    time_t timestamp;
    double value;
} DataPoint;

typedef struct MetricData {
    int series_count;
    char **series_names;
    DataPoint **series_data;
    int *series_counts;
    char *param1;
    MetricConfig *metric_config;
} MetricData;

/**
 * @brief Описание одного RRA для выбора шага агрегации
 *
 * Используется select_step_from_rras — чистой (без I/O) частью
 * select_optimal_step, выделенной для unit-тестирования.
 */
typedef struct {
    unsigned long pdp_per_row;    /**< PDP на строку (1 = «сырой» RRA) */
    unsigned long effective_step; /**< Эффективный шаг = pdp_per_row * base_step */
    const char *cf;               /**< Функция консолидации (AVERAGE/MAX/...) */
} RRAStepInfo;

/**
 * @brief Выбрать оптимальный шаг агрегации по списку RRA
 *
 * Чистая функция (без I/O и побочных эффектов), выделена из select_optimal_step
 * для тестируемости. Алгоритм побайтово идентичен исходной inline-логике:
 * среди RRA с cf="AVERAGE" и step >= base_step ищется шаг, дающий число точек
 * в окне [100, 2400]; при отсутствии — шаг с максимальным числом точек (< 100)
 * либо минимальный шаг (> 2400); fallback на «сырой» RRA (pdp_per_row == 1),
 * если диапазон укладывается в запрошенный период; иначе base_step.
 *
 * @param rras Массив описаний RRA (рассматриваются только AVERAGE)
 * @param rra_count Число элементов rras
 * @param range Диапазон выборки в секундах (end - start), должен быть > 0
 * @param period Запрошенный период в секундах (для fallback-логики)
 * @param base_step Базовый шаг RRD (он же min_step / значение по умолчанию)
 * @return Выбранный шаг в секундах
 */
unsigned long select_step_from_rras(const RRAStepInfo *rras, int rra_count,
                                    time_t range, time_t period,
                                    unsigned long base_step);

/**
 * Fetch metric data from RRD file
 *
 * @param rrdcached_addr Address of rrdcached daemon (NULL or empty for direct file access)
 * @param filename Path to RRD file
 * @param start Start timestamp
 * @param param1 Optional parameter for template substitution
 * @param metric_config Metric configuration for transformations
 * @return Allocated MetricData (caller must free with rrd_data_free), or NULL on error
 */
MetricData* rrd_fetch_data(const char *rrdcached_addr, const char *filename,
                           time_t start, const char *param1, MetricConfig *metric_config);

/**
 * Free MetricData structure
 */
void rrd_data_free(MetricData *data);

/* Compatibility aliases */
#define fetch_metric_data rrd_fetch_data
#define free_metric_data rrd_data_free

#endif /* SVGD_RRD_READER_H */
