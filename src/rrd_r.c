#include "../include/rrd_r.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

// Static cache for JavaScript file contents
static char *js_cache = NULL;
static long js_cache_len = 0;
static int js_cache_initialized = 0;
static pthread_mutex_t js_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t js_context_key;

// Initialize thread-local context key
void init_thread_local_context(void) {
    pthread_key_create(&js_context_key, (void (*)(void*))duk_destroy_heap);
}

// Get or create thread-local Duktape context
static duk_context *get_thread_local_context(const char *filename) {
    duk_context *ctx = pthread_getspecific(js_context_key);
    if (!ctx) {
        ctx = duk_create_heap_default();
        if (!ctx) {
            fprintf(stderr, "Failed to create Duktape context\n");
            return NULL;
        }
        pthread_setspecific(js_context_key, ctx);
        pthread_mutex_lock(&js_cache_mutex);
        if (js_cache_initialized) {
            fprintf(stderr, "Evaluating cached JS for thread %lu\n", (unsigned long)pthread_self());
            if (duk_peval_string(ctx, js_cache) != 0) {
                fprintf(stderr, "Failed to evaluate cached JS: %s\n", duk_safe_to_string(ctx, -1));
                duk_destroy_heap(ctx);
                pthread_setspecific(js_context_key, NULL);
                pthread_mutex_unlock(&js_cache_mutex);
                return NULL;
            }
            // Ensure generateSVG is in global scope
            duk_push_global_object(ctx);
            if (!duk_has_prop_string(ctx, -1, "generateSVG")) {
                fprintf(stderr, "generateSVG not found after evaluation, attempting to fix\n");
                duk_push_string(ctx, "if (typeof generateSVG !== 'undefined') { this.generateSVG = generateSVG; }");
                if (duk_peval(ctx) != 0) {
                    fprintf(stderr, "Failed to register generateSVG: %s\n", duk_safe_to_string(ctx, -1));
                    duk_destroy_heap(ctx);
                    pthread_setspecific(js_context_key, NULL);
                    pthread_mutex_unlock(&js_cache_mutex);
                    return NULL;
                }
            }
            duk_pop(ctx);
        }
        pthread_mutex_unlock(&js_cache_mutex);
    }
    return ctx;
}

// Initialize JavaScript cache
int init_js_cache(const char *filename) {
    pthread_mutex_lock(&js_cache_mutex);
    if (js_cache_initialized) {
        pthread_mutex_unlock(&js_cache_mutex);
        return 0;
    }

    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open JS file: %s (%s)\n", filename, strerror(errno));
        pthread_mutex_unlock(&js_cache_mutex);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    js_cache_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    js_cache = (char *)malloc(js_cache_len + 1);
    if (!js_cache) {
        fclose(f);
        fprintf(stderr, "Memory allocation failed for JS file: %s\n", filename);
        pthread_mutex_unlock(&js_cache_mutex);
        return -1;
    }

    size_t read_bytes = fread(js_cache, 1, js_cache_len, f);
    js_cache[js_cache_len] = 0;
    fclose(f);

    if (read_bytes != (size_t)js_cache_len) {
        fprintf(stderr, "Incomplete read of JS file: %s (read %zu of %ld bytes)\n", filename, read_bytes, js_cache_len);
        free(js_cache);
        js_cache = NULL;
        js_cache_len = 0;
        pthread_mutex_unlock(&js_cache_mutex);
        return -1;
    }

    fprintf(stderr, "JS file %s loaded (%ld bytes): %.100s...\n", filename, js_cache_len, js_cache);
    js_cache_initialized = 1;
    pthread_mutex_unlock(&js_cache_mutex);
    return 0;
}

// Free JavaScript cache
void free_js_cache(void) {
    pthread_mutex_lock(&js_cache_mutex);
    if (js_cache) {
        free(js_cache);
        js_cache = NULL;
        js_cache_len = 0;
        js_cache_initialized = 0;
    }
    pthread_mutex_unlock(&js_cache_mutex);
}

unsigned long select_optimal_step(const char *filename, time_t start, time_t end) {
    rrd_info_t *info = rrd_info_r(filename);
    if (!info) {
        fprintf(stderr, "Failed to get RRD info: %s\n", rrd_get_error());
        return 70;
    }

    unsigned long base_step = 10;
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
    RRAInfo rras[20] = {0}; // Zero-initialize
    int rra_count = 0;

    for (rrd_info_t *ptr = info; ptr; ptr = ptr->next) {
        if (strncmp(ptr->key, "rra[", 4) == 0) {
            char *endptr;
            errno = 0;
            long rra_idx = strtol(ptr->key + 4, &endptr, 10);
            if (errno != 0 || rra_idx < 0 || rra_idx >= 20 || *endptr != ']') {
                fprintf(stderr, "Invalid RRA index in key: %s\n", ptr->key);
                continue;
            }
            if (strstr(ptr->key, ".pdp_per_row")) {
                if (ptr->value.u_cnt == 0) {
                    fprintf(stderr, "Invalid pdp_per_row for rra[%ld]: %lu\n", rra_idx, ptr->value.u_cnt);
                    continue;
                }
                rras[rra_count].pdp_per_row = ptr->value.u_cnt;
                rras[rra_count].index = (int)rra_idx;
            } else if (strstr(ptr->key, ".rows")) {
                rras[rra_count].rows = ptr->value.u_cnt;
            } else if (strstr(ptr->key, ".cf")) {
                rras[rra_count].cf = strdup(ptr->value.u_str);
                rras[rra_count].effective_step = rras[rra_count].pdp_per_row * base_step;
                if (rras[rra_count].effective_step == 0 || rras[rra_count].effective_step > 1000000) {
                    fprintf(stderr, "Invalid effective_step for rra[%ld]: %lu\n", rra_idx, rras[rra_count].effective_step);
                    free(rras[rra_count].cf);
                    continue;
                }
                rra_count++;
            }
        }
    }

    time_t first_timestamp = -1;
    int selected_rra_index = -1;
    for (int i = 0; i < rra_count; i++) {
        if (strcmp(rras[i].cf, "AVERAGE") == 0 && (first_timestamp == -1 || rras[i].pdp_per_row == 1)) {
            time_t ts = rrd_first_r(filename, rras[i].index);
            if (ts != -1) {
                first_timestamp = ts;
                selected_rra_index = rras[i].index;
                fprintf(stderr, "First timestamp for rra[%d] (step=%lu): %ld (%s)", 
                        rras[i].index, rras[i].effective_step, first_timestamp, ctime(&first_timestamp));
                if (rras[i].pdp_per_row == 1) break;
            }
        }
    }
    if (first_timestamp == -1) {
        fprintf(stderr, "Failed to get first timestamp: %s\n", rrd_get_error());
        first_timestamp = end - 3600;
    }
    if (start < first_timestamp) {
        fprintf(stderr, "Adjusting start time from %ld to %ld\n", start, first_timestamp);
        start = first_timestamp;
    }

    time_t range = end - start;
    if (range <= 0) {
        fprintf(stderr, "Invalid time range: start=%ld, end=%ld\n", start, end);
        for (int i = 0; i < rra_count; i++) free(rras[i].cf);
        rrd_info_free(info);
        return 70;
    }

    const int min_points = 100;
    const int max_points = 1000;
    const unsigned long min_step = 70;

    unsigned long optimal_step = 0;
    int best_num_points = 0;
    int best_rra_index = -1;

    for (int i = 0; i < rra_count; i++) {
        if (strcmp(rras[i].cf, "AVERAGE") != 0) continue;
        unsigned long step = rras[i].effective_step;
        if (step < min_step) continue;

        int num_points = (range + step - 1) / step;
        fprintf(stderr, "Evaluating rra[%d]: step=%lu, num_points=%d\n", 
                rras[i].index, step, num_points);

        if (num_points >= min_points && num_points <= max_points) {
            optimal_step = step;
            best_rra_index = rras[i].index;
            best_num_points = num_points;
            break;
        }
        if (num_points < min_points && (optimal_step == 0 || num_points > best_num_points)) {
            optimal_step = step;
            best_rra_index = rras[i].index;
            best_num_points = num_points;
        }
        if (num_points > max_points && (optimal_step == 0 || step < optimal_step)) {
            optimal_step = step;
            best_rra_index = rras[i].index;
            best_num_points = num_points;
        }
    }

    if (optimal_step == 0 && range <= 3600) {
        for (int i = 0; i < rra_count; i++) {
            if (strcmp(rras[i].cf, "AVERAGE") == 0 && rras[i].pdp_per_row == 1) {
                optimal_step = rras[i].effective_step;
                best_rra_index = rras[i].index;
                best_num_points = (range + optimal_step - 1) / optimal_step;
                fprintf(stderr, "Falling back to step=10 for short range (%ld seconds)\n", range);
                break;
            }
        }
    }

    if (optimal_step == 0) {
        optimal_step = min_step;
        best_num_points = (range + optimal_step - 1) / optimal_step;
        fprintf(stderr, "No suitable step found, using fallback step=%lu\n", optimal_step);
    }

    unsigned long test_step = optimal_step;
    unsigned long ds_cnt;
    char **ds_names = NULL;
    rrd_value_t *data = NULL;
    time_t test_start = start;
    time_t test_end = end;
    int status = rrd_fetch_r(filename, "AVERAGE", &test_start, &test_end, &test_step, &ds_cnt, &ds_names, &data);
    if (status != 0) {
        fprintf(stderr, "Test fetch failed for step=%lu: %s\n", test_step, rrd_get_error());
        optimal_step = min_step;
        best_num_points = (range + optimal_step - 1) / optimal_step;
    } else {
        int valid_points = 0;
        int num_points = (test_end - test_start + test_step - 1) / test_step;
        for (int i = 0; i < num_points; i++) {
            for (unsigned long ds = 0; ds < ds_cnt; ds++) {
                if (!isnan(data[i * ds_cnt + ds]) && data[i * ds_cnt + ds] >= 0) {
                    valid_points++;
                    break;
                }
            }
        }
        if (ds_names) rrd_freemem(ds_names);
        if (data) rrd_freemem(data);

        if (valid_points == 0) {
            fprintf(stderr, "No valid data for step=%lu, switching to fallback step=%lu\n", 
                    test_step, min_step);
            optimal_step = min_step;
            best_num_points = (range + optimal_step - 1) / optimal_step;
        }
    }

    fprintf(stderr, "Selected step=%lu for range=%ld seconds (%d points), rra[%d]\n", 
            optimal_step, range, best_num_points, best_rra_index);

    for (int i = 0; i < rra_count; i++) free(rras[i].cf);
    rrd_info_free(info);
    return optimal_step;
}

MetricData *fetch_metric_data(const char *filename, time_t start, char *metric_type, char *param1) {
    time_t end = time(NULL);
    unsigned long step = select_optimal_step(filename, start, end);
    unsigned long ds_cnt;
    char **ds_names = NULL;
    rrd_value_t *data = NULL;

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

    int num_points = (end - start + step - 1) / step;
    fprintf(stderr, "Calculated num_points=%d\n", num_points);
    if (num_points <= 0) {
        fprintf(stderr, "No data points in range for %s (start=%ld, end=%ld, step=%lu)\n", filename, start, end, step);
        if (ds_names) rrd_freemem(ds_names);
        if (data) rrd_freemem(data);
        return NULL;
    }

    MetricData *metric_data = malloc(sizeof(MetricData));
    if (!metric_data) {
        perror("Memory allocation failed");
        if (ds_names) rrd_freemem(ds_names);
        if (data) rrd_freemem(data);
        return NULL;
    }

    metric_data->series_count = ds_cnt;
    metric_data->series_names = malloc(ds_cnt * sizeof(char*));
    metric_data->series_data = malloc(ds_cnt * sizeof(DataPoint*));
    metric_data->series_counts = malloc(ds_cnt * sizeof(int));
    metric_data->metric_type = strdup(metric_type ? metric_type : "unknown");
    metric_data->param1 = strdup(param1 ? param1 : "");

    for (unsigned long ds = 0; ds < ds_cnt; ds++) {
        metric_data->series_names[ds] = strdup(ds_names[ds]);
        metric_data->series_data[ds] = malloc(num_points * sizeof(DataPoint));
        metric_data->series_counts[ds] = 0;

        for (int i = 0; i < num_points; i++) {
            double value = data[i * ds_cnt + ds];
            if (isnan(value) || value < 0) continue;
            metric_data->series_data[ds][metric_data->series_counts[ds]].timestamp = start + i * step;
            metric_data->series_data[ds][metric_data->series_counts[ds]].value = value;
            metric_data->series_counts[ds]++;
        }
        fprintf(stderr, "Series %s has %d valid points\n", metric_data->series_names[ds], metric_data->series_counts[ds]);
    }

    if (ds_names) rrd_freemem(ds_names);
    if (data) rrd_freemem(data);

    int has_valid_data = 0;
    for (unsigned long ds = 0; ds < ds_cnt; ds++) {
        if (metric_data->series_counts[ds] > 0) {
            has_valid_data = 1;
            break;
        }
    }
    if (!has_valid_data) {
        fprintf(stderr, "No valid data points found for %s\n", filename);
        free_metric_data(metric_data);
        return NULL;
    }

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
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    ctx = get_thread_local_context(script_path);
    if (!ctx) {
        fprintf(stderr, "Failed to get thread-local context for %s\n", script_path);
        return NULL;
    }

    if (!js_cache_initialized) {
        if (init_js_cache(script_path) != 0) {
            fprintf(stderr, "Failed to initialize JS cache for %s\n", script_path);
            return NULL;
        }
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

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6;
    fprintf(stderr, "generate_svg took %.2f ms\n", elapsed_ms);
    return result;
}

int load_js_file(duk_context *ctx, const char *filename) {
    ctx = get_thread_local_context(filename);
    if (!ctx) {
        return -1;
    }

    if (!js_cache_initialized) {
        if (init_js_cache(filename) != 0) {
            return -1;
        }
    }

    return 0;
}
