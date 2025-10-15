#include "../include/rrd_r.h"

MetricData *fetch_metric_data(const char *filename, time_t start, char *metric_type, char *param1) {
    time_t end = time(NULL);
    unsigned long step;
    unsigned long ds_cnt;
    char **ds_names = NULL;
    rrd_value_t *data = NULL;

    if (end - start > 3600) {
        start = end - 3600; // Enforce 1-hour range to match period=3600
        fprintf(stderr, "Adjusted time range to 1 hour: start=%ld (%s), end=%ld (%s)\n", 
                start, ctime(&start), end, ctime(&end));
    }

    fprintf(stderr, "Fetching data for %s, start=%ld (%s), end=%ld (%s)\n", 
                filename, start, ctime(&start), end, ctime(&end));
    int status = rrd_fetch_r(filename, "AVERAGE", &start, &end, &step, &ds_cnt, &ds_names, &data);
    if (status != 0) {
        fprintf(stderr, "RRD fetch failed for %s: %s\n", filename, rrd_get_error());
        return NULL;
    }

    fprintf(stderr, "After rrd_fetch_r: start=%ld, end=%ld, step=%lu, ds_cnt=%lu\n", start, end, step, ds_cnt);
    for (unsigned long ds = 0; ds < ds_cnt; ds++) {
        fprintf(stderr, "Data source %lu: %s\n", ds, ds_names[ds]);
    }

    // Validate step to ensure high-resolution RRA (10s from rrdtool info)
    if (step == 0 || step > 60) { // Max step for high-resolution is ~60s
        fprintf(stderr, "Invalid step size %lu, using default 10s (from RRD info)\n", step);
        step = 10; // Force 10s step for rra[0]
    }

    int num_points = (end - start + step - 1) / step; // Round up to include partial steps
    fprintf(stderr, "Calculated num_points=%d\n", num_points);
    if (num_points <= 0) {
        fprintf(stderr, "No data points in range for %s (start=%ld, end=%ld, step=%lu)\n", filename, start, end, step);
        if (ds_names) rrd_freemem(ds_names);
        if (data) rrd_freemem(data);
        return NULL;
    }

    // Debug: Log raw data values
    for (int i = 0; i < num_points; i++) {
        for (unsigned long ds = 0; ds < ds_cnt; ds++) {
            double value = data[i * ds_cnt + ds];
            fprintf(stderr, "Raw data point %d, ds=%lu: timestamp=%ld (%s), value=%f\n", 
                    i, ds, start + i * step, ctime(&(time_t){start + i * step}), value);
        }
    }

    MetricData *metric_data = malloc(sizeof(MetricData));
    if (!metric_data) {
        perror("Memory allocation failed");
        if (ds_names) rrd_freemem(ds_names);
        if (data) rrd_freemem(data);
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
            if (isnan(user)) user = 0;
            if (user < 0) user = 0;
            if (isnan(syst)) syst = 0;
            if (syst < 0) syst = 0;
            double total = user + syst;
            metric_data->series_data[0][metric_data->series_counts[0]].timestamp = start + i * step;
            metric_data->series_data[0][metric_data->series_counts[0]].value = total;
            metric_data->series_counts[0]++;
        }
    } else {
        for (int ds = 0; ds < ds_cnt; ds++) {
            metric_data->series_names[ds] = strdup(ds_names[ds]);
            metric_data->series_data[ds] = malloc(num_points * sizeof(DataPoint));
            metric_data->series_counts[ds] = 0;

            for (int i = 0; i < num_points; i++) {
                double value = data[i * ds_cnt + ds];
                if (isnan(value)) value = 0;
                if (value < 0) value = 0;
                metric_data->series_data[ds][metric_data->series_counts[ds]].timestamp = start + i * step;
                metric_data->series_data[ds][metric_data->series_counts[ds]].value = value;
                metric_data->series_counts[ds]++;
            }
        }
    }

    // Removed the has_valid_data check and early return; points are now always added if num_points > 0

    if (ds_names) rrd_freemem(ds_names);
    if (data) rrd_freemem(data);

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
