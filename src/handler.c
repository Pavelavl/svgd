/**
 * @file handler.c
 * @brief Request handling implementation
 */

#include "../include/handler.h"
#include "../include/rrd_r.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* External JS context from main.c */
extern duk_context *global_ctx;

/**
 * Extract parameter value from query string
 */
char* handler_get_param(const char *params, const char *key) {
    if (!params || !key) return NULL;

    char search_key[256];
    snprintf(search_key, sizeof(search_key), "%s=", key);

    const char *key_pos = strstr(params, search_key);
    if (!key_pos) return NULL;

    key_pos += strlen(search_key);
    const char *end = strchr(key_pos, '&');
    size_t len = end ? (size_t)(end - key_pos) : strlen(key_pos);

    char *value = malloc(len + 1);
    if (!value) return NULL;

    strncpy(value, key_pos, len);
    value[len] = '\0';
    return value;
}

/**
 * Create error result
 */
static handler_result_t* create_error_result(const char *message) {
    handler_result_t *result = malloc(sizeof(handler_result_t));
    if (!result) return NULL;

    result->data = strdup(message);
    result->data_len = strlen(message);
    result->is_json = 0;
    result->status = 1;
    return result;
}

/**
 * Extract parameter from endpoint path
 * e.g., "cpu/process/nginx" with endpoint "cpu/process" -> "nginx"
 */
static char* extract_param_from_path(const char *path, const char *endpoint) {
    if (!path || !endpoint) return NULL;

    size_t endpoint_len = strlen(endpoint);
    if (strncmp(path, endpoint, endpoint_len) != 0) {
        return NULL;
    }

    const char *param_start = path + endpoint_len;
    if (*param_start == '/') param_start++;

    if (*param_start == '\0') return NULL;

    return strdup(param_start);
}

/**
 * Build RRD file path from template
 */
static void build_rrd_path(char *dest, size_t dest_size, const char *base_path,
                          const char *path_template, const char *param) {
    if (!dest || dest_size == 0) return;

    if (strchr(path_template, '%') && param) {
        snprintf(dest, dest_size, "%s/", base_path);
        snprintf(dest + strlen(dest), dest_size - strlen(dest), path_template, param);
    } else {
        snprintf(dest, dest_size, "%s/%s", base_path, path_template);
    }
}

/**
 * Process a metric request
 */
handler_result_t* handler_process(Config *config,
                                  const char *endpoint,
                                  const char *query,
                                  int period,
                                  int width,
                                  int height,
                                  int use_cache) {
    if (!config || !endpoint) {
        return create_error_result("Invalid parameters");
    }

    /* Parse width/height from query string, use passed values as defaults */
    int svg_width = width;
    int svg_height = height;

    if (query) {
        char *width_str = handler_get_param(query, "width");
        char *height_str = handler_get_param(query, "height");

        if (width_str) {
            svg_width = atoi(width_str);
            free(width_str);
        }

        if (height_str) {
            svg_height = atoi(height_str);
            free(height_str);
        }
    }

    /* Apply defaults and bounds checking */
    if (svg_width <= 0) svg_width = 800;
    if (svg_width < 200) svg_width = 200;
    if (svg_width > 1600) svg_width = 1600;

    if (svg_height <= 0) svg_height = 450;
    if (svg_height < 120) svg_height = 120;
    if (svg_height > 800) svg_height = 800;

    /* Special endpoint: metrics configuration */
    if (strcmp(endpoint, "_config/metrics") == 0) {
        char *json = generate_metrics_json(config);
        if (!json) {
            return create_error_result("Failed to generate metrics config");
        }

        handler_result_t *result = malloc(sizeof(handler_result_t));
        if (!result) {
            free(json);
            return create_error_result("Out of memory");
        }

        result->data = json;
        result->data_len = strlen(json);
        result->is_json = 1;
        result->status = 0;
        return result;
    }

    /* Find matching metric configuration */
    MetricConfig *metric = find_metric_config(config, endpoint);
    if (!metric) {
        char error_buf[256];
        snprintf(error_buf, sizeof(error_buf), "Unknown endpoint: %s", endpoint);
        return create_error_result(error_buf);
    }

    /* Extract parameter if required */
    char *param = NULL;
    if (metric->requires_param) {
        param = extract_param_from_path(endpoint, metric->endpoint);
        if (!param || strlen(param) == 0) {
            char error_buf[256];
            snprintf(error_buf, sizeof(error_buf), "Endpoint '%s' requires parameter '%s'",
                    metric->endpoint, metric->param_name);
            if (param) free(param);
            return create_error_result(error_buf);
        }
    }

    /* Build RRD file path */
    char rrd_path[512] = {0};
    build_rrd_path(rrd_path, sizeof(rrd_path), config->rrd_base_path,
                   metric->rrd_path, param);

    /* Fetch data (with optional caching) */
    MetricData *data = NULL;

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
                data = cache_get(rrd_path, period);
            } else {
                data = fresh_data;
            }
        }
    }

    if (param) free(param);

    if (!data) {
        return create_error_result("Failed to fetch metric data");
    }

    /* Generate SVG */
    data->metric_config = metric;
    char *svg = generate_svg(global_ctx, config->js_script_path, data, svg_width, svg_height);
    free_metric_data(data);

    if (!svg) {
        return create_error_result("Failed to generate SVG");
    }

    handler_result_t *result = malloc(sizeof(handler_result_t));
    if (!result) {
        free(svg);
        return create_error_result("Out of memory");
    }

    result->data = svg;
    result->data_len = strlen(svg);
    result->is_json = 0;
    result->status = 0;
    return result;
}

/**
 * Free handler result
 */
void handler_result_free(handler_result_t *result) {
    if (!result) return;
    if (result->data) free(result->data);
    free(result);
}
