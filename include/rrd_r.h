#ifndef RRD_READER_H
#define RRD_READER_H

#include <rrd.h>
#include <duktape.h>
#include <errno.h>
#include <pthread.h>
#include <rrd_client.h>

#define MAX_POINTS 1000

typedef struct {
    time_t timestamp;
    double value;
} DataPoint;

typedef struct {
    int series_count;
    char **series_names;
    DataPoint **series_data;
    int *series_counts;
    char *metric_type;
    char *param1;
} MetricData;

MetricData *fetch_metric_data(const char *rrdcached_addr, const char *filename, time_t start, char *metric_type, char *param1);
char* generate_svg(duk_context *ctx, const char *script_path, MetricData *data);
void free_metric_data(MetricData *data);
int load_js_file(duk_context *ctx, const char *filename);
void free_js_cache(void);

#endif