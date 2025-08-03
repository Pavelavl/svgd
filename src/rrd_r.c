#include "../include/rrd_r.h"

MetricData *fetch_metric_data(const char *rrdcached_addr, const char *filename, time_t start, char *metric_type, char *param1) {
    int is_unix = 0;
    const char *socket_path = rrdcached_addr;
    
    if (strncmp(rrdcached_addr, "unix:", 5) == 0) {
        is_unix = 1;
        socket_path = rrdcached_addr + 5;
        if (access(socket_path, F_OK) != 0) {
            fprintf(stderr, "RRDcached socket not found: %s\n", socket_path);
            return NULL;
        }
    }

    if (rrdc_connect(rrdcached_addr) != 0) {
        fprintf(stderr, "Failed to connect to rrdcached at %s: %s\n", rrdcached_addr, rrd_get_error());
        return NULL;
    }

    if (rrdc_flush(filename) != 0) {
        fprintf(stderr, "Failed to flush RRD file: %s\n", rrd_get_error());
        rrdc_disconnect();
        return NULL;
    }

    time_t end = time(NULL);
    unsigned long step;
    unsigned long ds_cnt;
    char **ds_names = NULL;
    rrd_value_t *data = NULL;

    int status = -1;
    for (int attempt = 0; attempt < 3 && status != 0; attempt++) {
        status = rrdc_fetch(filename, "AVERAGE", &start, &end, &step, &ds_cnt, &ds_names, &data);
        if (status != 0 && attempt < 2) {
            sleep(1);
            fprintf(stderr, "Retrying fetch for %s...\n", filename);
        }
    }

    if (status != 0) {
        fprintf(stderr, "RRD fetch failed for %s: %s\n", filename, rrd_get_error());
        rrdc_disconnect();
        return NULL;
    }

    int num_points = (end - start) / step;
    if (num_points <= 0) {
        fprintf(stderr, "No data points in range for %s\n", filename);
        rrdc_disconnect();
        return NULL;
    }

    MetricData *metric_data = malloc(sizeof(MetricData));
    if (!metric_data) {
        perror("Memory allocation failed");
        rrdc_disconnect();
        return NULL;
    }

    metric_data->series_count = (strstr(filename, "ps_cputime.rrd") != NULL) ? 1 : ds_cnt;
    metric_data->series_names = malloc(metric_data->series_count * sizeof(char*));
    metric_data->series_data = malloc(metric_data->series_count * sizeof(DataPoint*));
    metric_data->series_counts = malloc(metric_data->series_count * sizeof(int));
    metric_data->metric_type = strdup(metric_type ? metric_type : "unknown");
    metric_data->param1 = strdup(param1 ? param1 : "");

    if (strstr(filename, "ps_cputime.rrd") != NULL) {
        metric_data->series_names[0] = strdup("total");
        metric_data->series_data[0] = malloc(num_points * sizeof(DataPoint));
        metric_data->series_counts[0] = 0;

        for (int i = 0; i < num_points; i++) {
            double user = data[i * ds_cnt + (ds_cnt >= 1 ? 0 : 0)];
            double syst = data[i * ds_cnt + (ds_cnt >= 2 ? 1 : 0)];
            double total = (isnan(user) ? 0 : user) + (isnan(syst) ? 0 : syst);
            if (!isnan(total) && total >= 0) {
                metric_data->series_data[0][metric_data->series_counts[0]].timestamp = start + i * step;
                metric_data->series_data[0][metric_data->series_counts[0]].value = total;
                metric_data->series_counts[0]++;
            }
        }
    } else {
        for (int ds = 0; ds < ds_cnt; ds++) {
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

    int has_valid_data = 0;
    for (int ds = 0; ds < metric_data->series_count; ds++) {
        if (metric_data->series_counts[ds] > 0) {
            has_valid_data = 1;
            break;
        }
    }

    if (!has_valid_data) {
        fprintf(stderr, "No valid data points in RRD file: %s\n", filename);
        free_metric_data(metric_data);
        rrdc_disconnect();
        return NULL;
    }

    if (ds_names) rrd_freemem(ds_names);
    if (data) rrd_freemem(data);
    rrdc_disconnect();

    return metric_data;
}

void free_metric_data(MetricData *data) {
    if (!data) return;
    for (int i = 0; i < data->series_count; i++) {
        free(data->series_names[i]);
        free(data->series_data[i]);
    }
    free(data->series_names);
    free(data->series_data);
    free(data->series_counts);
    free(data->metric_type);
    free(data->param1);
    free(data);
}

char *generate_svg(duk_context *ctx, const char *script_path, MetricData *data) {
    if (load_js_file(ctx, script_path) != 0) {
        fprintf(stderr, "Failed to load JS file %s: %s\n", script_path, duk_safe_to_string(ctx, -1));
        return NULL;
    }

    duk_push_global_object(ctx);
    if (!duk_has_prop_string(ctx, -1, "generateSVG")) {
        fprintf(stderr, "Function 'generateSVG' not found in global object for %s\n", script_path);
        duk_pop(ctx);
        return NULL;
    }
    duk_get_prop_string(ctx, -1, "generateSVG");

    duk_push_array(ctx);
    for (int s = 0; s < data->series_count; s++) {
        if (data->series_counts[s] == 0) continue;
        duk_push_object(ctx);
        duk_push_string(ctx, data->series_names[s]);
        duk_put_prop_string(ctx, -2, "name");
        duk_push_array(ctx);
        for (int i = 0; i < data->series_counts[s]; i++) {
            duk_push_object(ctx);
            duk_push_number(ctx, (double)data->series_data[s][i].timestamp);
            duk_put_prop_string(ctx, -2, "timestamp");
            duk_push_number(ctx, data->series_data[s][i].value);
            duk_put_prop_string(ctx, -2, "value");
            duk_put_prop_index(ctx, -2, i);
        }
        duk_put_prop_string(ctx, -2, "data");
        duk_put_prop_index(ctx, -2, s);
    }

    duk_push_object(ctx);
    duk_push_string(ctx, data->metric_type ? data->metric_type : "unknown");
    duk_put_prop_string(ctx, -2, "metricType");
    if (data->param1 && strlen(data->param1) > 0) {
        duk_push_string(ctx, data->param1);
        duk_put_prop_string(ctx, -2, "param1");
    }

    if (duk_pcall(ctx, 2) != 0) {
        fprintf(stderr, "JS call error in %s: %s\n", script_path, duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        return NULL;
    }

    const char *svg = duk_get_string(ctx, -1);
    if (!svg) {
        fprintf(stderr, "Failed to get SVG string from JS in %s\n", script_path);
        duk_pop(ctx);
        return NULL;
    }

    char *result = strdup(svg);
    duk_pop(ctx);
    return result;
}

int load_js_file(duk_context *ctx, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open JS file: %s (%s)\n", filename, strerror(errno));
        duk_push_error_object(ctx, DUK_ERR_ERROR, "cannot open file: %s", filename);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "Memory allocation failed for JS file: %s\n", filename);
        duk_push_error_object(ctx, DUK_ERR_ERROR, "memory allocation failed");
        return -1;
    }

    fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);

    int ret = duk_peval_string(ctx, buf);
    if (ret != 0) {
        fprintf(stderr, "Failed to evaluate JS file %s: %s\n", filename, duk_safe_to_string(ctx, -1));
    }
    free(buf);
    return ret;
}