/**
 * @file reader.c
 * @brief RRD file reading implementation
 */

#include "../include/rrd/reader.h"
#include "../include/cfg.h"
#include <rrd.h>
#include <rrd_client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>

/* Select optimal step based on RRD file structure */
static unsigned long select_optimal_step(const char *filename, time_t start, time_t end, int period) {
    rrd_info_t *info = rrd_info_r(filename);
    const int default_step = 15;
    if (!info) return default_step;

    unsigned long base_step = default_step;
    for (rrd_info_t *ptr = info; ptr; ptr = ptr->next) {
        if (strcmp(ptr->key, "step") == 0) {
            base_step = ptr->value.u_cnt;
            break;
        }
    }

    typedef struct {
        unsigned long pdp_per_row;
        unsigned long rows;
        unsigned long effective_step;
        char *cf;
        int index;
    } RRAInfo;

    RRAInfo rras[20] = {0};
    int rra_count = 0;

    for (rrd_info_t *ptr = info; ptr; ptr = ptr->next) {
        if (strncmp(ptr->key, "rra[", 4) == 0) {
            char *endptr;
            errno = 0;
            long rra_idx = strtol(ptr->key + 4, &endptr, 10);
            if (errno != 0 || rra_idx < 0 || rra_idx >= 20 || *endptr != ']') continue;

            if (strstr(ptr->key, ".pdp_per_row")) {
                if (ptr->value.u_cnt == 0) continue;
                rras[rra_count].pdp_per_row = ptr->value.u_cnt;
                rras[rra_count].index = (int)rra_idx;
            } else if (strstr(ptr->key, ".rows")) {
                rras[rra_count].rows = ptr->value.u_cnt;
            } else if (strstr(ptr->key, ".cf")) {
                rras[rra_count].cf = strdup(ptr->value.u_str);
                rras[rra_count].effective_step = rras[rra_count].pdp_per_row * base_step;
                if (rras[rra_count].effective_step == 0 || rras[rra_count].effective_step > 1000000) {
                    free(rras[rra_count].cf);
                    continue;
                }
                rra_count++;
            }
        }
    }

    /* Find first available timestamp */
    time_t first_timestamp = -1;
    for (int i = 0; i < rra_count; i++) {
        if (strcmp(rras[i].cf, "AVERAGE") == 0 && (first_timestamp == -1 || rras[i].pdp_per_row == 1)) {
            time_t ts = rrd_first_r(filename, rras[i].index);
            if (ts != -1) {
                first_timestamp = ts;
                if (rras[i].pdp_per_row == 1) break;
            }
        }
    }
    if (first_timestamp == -1) first_timestamp = end - period;
    if (start < first_timestamp) start = first_timestamp;

    time_t range = end - start;
    if (range <= 0) {
        for (int i = 0; i < rra_count; i++) free(rras[i].cf);
        rrd_info_free(info);
        return default_step;
    }

    const int min_points = 100;
    const int max_points = 2400;
    const unsigned long min_step = default_step;

    unsigned long optimal_step = 0;
    int best_num_points = 0;

    for (int i = 0; i < rra_count; i++) {
        if (strcmp(rras[i].cf, "AVERAGE") != 0) continue;
        unsigned long step = rras[i].effective_step;
        if (step < min_step) continue;

        int num_points = (range + step - 1) / step;
        if (num_points >= min_points && num_points <= max_points) {
            optimal_step = step;
            best_num_points = num_points;
            break;
        }
        if (num_points < min_points && (optimal_step == 0 || num_points > best_num_points)) {
            optimal_step = step;
            best_num_points = num_points;
        }
        if (num_points > max_points && (optimal_step == 0 || step < optimal_step)) {
            optimal_step = step;
            best_num_points = num_points;
        }
    }

    if (optimal_step == 0 && range <= period) {
        for (int i = 0; i < rra_count; i++) {
            if (strcmp(rras[i].cf, "AVERAGE") == 0 && rras[i].pdp_per_row == 1) {
                optimal_step = rras[i].effective_step;
                best_num_points = (range + optimal_step - 1) / optimal_step;
                break;
            }
        }
    }

    if (optimal_step == 0) optimal_step = min_step;

    for (int i = 0; i < rra_count; i++) free(rras[i].cf);
    rrd_info_free(info);
    return optimal_step;
}

MetricData* rrd_fetch_data(const char *rrdcached_addr, const char *filename,
                           time_t start, const char *param1, MetricConfig *metric_config) {
    int use_rrdcached = (rrdcached_addr != NULL && strlen(rrdcached_addr) > 0);
    int rrdcached_connected = 0;

    if (use_rrdcached) {
        if (strncmp(rrdcached_addr, "unix:", 5) == 0 && access(rrdcached_addr + 5, F_OK) != 0) {
            use_rrdcached = 0;
        } else if (rrdc_connect(rrdcached_addr) == 0) {
            rrdcached_connected = 1;
            rrdc_flush(filename);
        } else {
            use_rrdcached = 0;
        }
    }

    time_t end = time(NULL);
    unsigned long step = select_optimal_step(filename, start, end, (int)(end - start));
    unsigned long ds_cnt;
    char **ds_names = NULL;
    rrd_value_t *data = NULL;

    int status = use_rrdcached && rrdcached_connected
        ? rrdc_fetch(filename, "AVERAGE", &start, &end, &step, &ds_cnt, &ds_names, &data)
        : rrd_fetch_r(filename, "AVERAGE", &start, &end, &step, &ds_cnt, &ds_names, &data);

    if (status != 0) {
        if (rrdcached_connected) rrdc_disconnect();
        return NULL;
    }

    int num_points = (end - start + step - 1) / step;
    if (num_points <= 0) {
        if (ds_names) rrd_freemem(ds_names);
        if (data) rrd_freemem(data);
        if (rrdcached_connected) rrdc_disconnect();
        return NULL;
    }

    MetricData *metric_data = malloc(sizeof(MetricData));
    if (!metric_data) {
        if (ds_names) rrd_freemem(ds_names);
        if (data) rrd_freemem(data);
        if (rrdcached_connected) rrdc_disconnect();
        return NULL;
    }

    int do_sum = metric_config && strcmp(metric_config->transform_type, "sum") == 0;
    metric_data->series_count = do_sum ? 1 : (int)ds_cnt;
    metric_data->series_names = malloc(metric_data->series_count * sizeof(char*));
    metric_data->series_data = malloc(metric_data->series_count * sizeof(DataPoint*));
    metric_data->series_counts = malloc(metric_data->series_count * sizeof(int));
    metric_data->param1 = strdup(param1 ? param1 : "");
    metric_data->metric_config = metric_config;

    if (do_sum) {
        metric_data->series_names[0] = strdup("total");
        metric_data->series_data[0] = malloc(num_points * sizeof(DataPoint));
        metric_data->series_counts[0] = 0;

        for (int i = 0; i < num_points; i++) {
            double total = 0;
            if (ds_cnt >= 1) total += isnan(data[i * ds_cnt]) ? 0 : data[i * ds_cnt];
            if (ds_cnt >= 2) total += isnan(data[i * ds_cnt + 1]) ? 0 : data[i * ds_cnt + 1];
            if (total >= 0) {
                metric_data->series_data[0][metric_data->series_counts[0]].timestamp = start + i * step;
                metric_data->series_data[0][metric_data->series_counts[0]].value = total;
                metric_data->series_counts[0]++;
            }
        }
    } else {
        for (unsigned long ds = 0; ds < ds_cnt; ds++) {
            metric_data->series_names[ds] = strdup(ds_names[ds]);
            metric_data->series_data[ds] = malloc(num_points * sizeof(DataPoint));
            metric_data->series_counts[ds] = 0;

            for (int i = 0; i < num_points; i++) {
                double value = data[i * ds_cnt + ds];
                if (!isnan(value) && value >= 0) {
                    metric_data->series_data[ds][metric_data->series_counts[ds]].timestamp = start + i * step;
                    metric_data->series_data[ds][metric_data->series_counts[ds]].value = value;
                    metric_data->series_counts[ds]++;
                }
            }
        }
    }

    if (ds_names) rrd_freemem(ds_names);
    if (data) rrd_freemem(data);
    if (rrdcached_connected) rrdc_disconnect();

    /* Verify we got some data */
    for (int ds = 0; ds < metric_data->series_count; ds++) {
        if (metric_data->series_counts[ds] > 0) return metric_data;
    }

    rrd_data_free(metric_data);
    return NULL;
}

void rrd_data_free(MetricData *data) {
    if (!data) return;
    for (int i = 0; i < data->series_count; i++) {
        free(data->series_names[i]);
        free(data->series_data[i]);
    }
    free(data->series_names);
    free(data->series_data);
    free(data->series_counts);
    free(data->param1);
    free(data);
}
