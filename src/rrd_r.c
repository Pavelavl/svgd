#include "../include/rrd_r.h"
#include "../include/cfg.h"

// Static cache for JavaScript file contents
static char *js_cache = NULL;
static long js_cache_len = 0;
static int js_cache_initialized = 0;
static pthread_key_t js_context_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

// Initialize thread-local context key (called only once)
static void make_key(void) {
    pthread_key_create(&js_context_key, (void (*)(void*))duk_destroy_heap);
}

void init_thread_local_context(void) {
    pthread_once(&key_once, make_key);
}

// Get or create thread-local Duktape context
static duk_context *get_thread_local_context(const char *filename) {
    pthread_once(&key_once, make_key);
    
    duk_context *ctx = pthread_getspecific(js_context_key);
    if (!ctx) {
        ctx = duk_create_heap_default();
        if (!ctx) return NULL;
        pthread_setspecific(js_context_key, ctx);
        
        if (js_cache_initialized && js_cache) {
            if (duk_peval_string(ctx, js_cache) != 0) {
                duk_destroy_heap(ctx);
                pthread_setspecific(js_context_key, NULL);
                return NULL;
            }
            duk_pop(ctx);
        }
    }
    return ctx;
}

// Initialize JavaScript cache
int init_js_cache(const char *filename) {
    if (js_cache_initialized) {
        return 0;
    }

    FILE *f = fopen(filename, "rb");
    if (!f) {
        return -1;
    }

    fseek(f, 0, SEEK_END);
    js_cache_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    js_cache = (char *)malloc(js_cache_len + 1);
    if (!js_cache) {
        fclose(f);
        return -1;
    }

    size_t read_bytes = fread(js_cache, 1, js_cache_len, f);
    js_cache[js_cache_len] = 0;
    fclose(f);

    if (read_bytes != (size_t)js_cache_len) {
        free(js_cache);
        js_cache = NULL;
        js_cache_len = 0;
        return -1;
    }

    js_cache_initialized = 1;
    return 0;
}

// Free JavaScript cache
void free_js_cache(void) {
    if (js_cache) {
        free(js_cache);
        js_cache = NULL;
        js_cache_len = 0;
        js_cache_initialized = 0;
    }
}

// Internal function for selecting optimal step
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

MetricData *fetch_metric_data(const char *rrdcached_addr, const char *filename, time_t start, char *param1) {
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
    unsigned long step = select_optimal_step(filename, start, end, end+start);
    unsigned long ds_cnt;
    char **ds_names = NULL;
    rrd_value_t *data = NULL;

    int status = use_rrdcached && rrdcached_connected ? rrdc_fetch(filename, "AVERAGE", &start, &end, &step, &ds_cnt, &ds_names, &data) :
                                                       rrd_fetch_r(filename, "AVERAGE", &start, &end, &step, &ds_cnt, &ds_names, &data);

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

    int is_ps_cputime = strstr(filename, "ps_cputime.rrd") != NULL;
    metric_data->series_count = is_ps_cputime ? 1 : ds_cnt;
    metric_data->series_names = malloc(metric_data->series_count * sizeof(char*));
    metric_data->series_data = malloc(metric_data->series_count * sizeof(DataPoint*));
    metric_data->series_counts = malloc(metric_data->series_count * sizeof(int));
    metric_data->param1 = strdup(param1 ? param1 : "");
    metric_data->metric_config = NULL; // Will be set by caller

    if (is_ps_cputime) {
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

    for (unsigned long ds = 0; ds < metric_data->series_count; ds++) {
        if (metric_data->series_counts[ds] > 0) return metric_data;
    }

    free_metric_data(metric_data);
    return NULL;
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

    if (!js_cache_initialized && init_js_cache(script_path) != 0) return NULL;

    ctx = get_thread_local_context(script_path);
    if (!ctx) return NULL;

    duk_push_global_object(ctx);
    if (!duk_get_prop_string(ctx, -1, "generateSVG")) {
        duk_pop_2(ctx);
        return NULL;
    }
    duk_remove(ctx, -2);

    // Build series array
    duk_push_array(ctx);
    int array_idx = 0;
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
        duk_put_prop_index(ctx, -2, array_idx++);
    }

    // Build options object with metric configuration
    duk_push_object(ctx);
    duk_push_string(ctx, data->metric_type ? data->metric_type : "unknown");
    duk_put_prop_string(ctx, -2, "metricType");
    if (data->param1 && *data->param1) {
        duk_push_string(ctx, data->param1);
        duk_put_prop_string(ctx, -2, "param1");
    }

    // Pass metric configuration if available
    if (data->metric_config) {
        MetricConfig *cfg = data->metric_config;
        
        // Add display configuration
        duk_push_string(ctx, cfg->title);
        duk_put_prop_string(ctx, -2, "title");
        duk_push_string(ctx, cfg->y_label);
        duk_put_prop_string(ctx, -2, "yLabel");
        duk_push_boolean(ctx, cfg->is_percentage);
        duk_put_prop_string(ctx, -2, "isPercentage");
        
        // Add transformation configuration
        duk_push_string(ctx, cfg->transform_type);
        duk_put_prop_string(ctx, -2, "transformType");
        duk_push_number(ctx, cfg->value_multiplier);
        duk_put_prop_string(ctx, -2, "valueMultiplier");
        duk_push_number(ctx, cfg->transform_divisor);
        duk_put_prop_string(ctx, -2, "transformDivisor");
        duk_push_string(ctx, cfg->value_format);
        duk_put_prop_string(ctx, -2, "valueFormat");
    }

    if (duk_pcall(ctx, 2) != 0) {
        fprintf(stderr, "JS Error: %s\n", duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        return NULL;
    }

    const char *svg = duk_get_string(ctx, -1);
    if (!svg) {
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
    if (!js_cache_initialized && init_js_cache(filename) != 0) return -1;
    
    ctx = get_thread_local_context(filename);
    return ctx ? 0 : -1;
}