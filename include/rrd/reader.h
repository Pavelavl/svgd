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
