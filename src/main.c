#include <sys/socket.h>
#include <netinet/in.h>
#include <duktape.h>
#include "../include/cfg.h"
#include "../include/rrd_r.h"
#include "../lsrp/lsrp.h"
#include "../lsrp/lsrp_server.h"

static Config global_config;
static duk_context *global_ctx;

static char* get_param_value(const char* params, const char* key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "%s=", key);
    const char* key_pos = strstr(params, search_key);
    if (!key_pos) return NULL;
    key_pos += strlen(search_key);
    const char* end = strchr(key_pos, '&');
    size_t len = end ? (end - key_pos) : strlen(key_pos);
    char* value = malloc(len + 1);
    if (!value) return NULL;
    strncpy(value, key_pos, len);
    value[len] = '\0';
    return value;
}

// Extract parameter from endpoint path
// e.g., "cpu/process/nginx" with endpoint "cpu/process" -> "nginx"
static char* extract_param_from_path(const char *path, const char *endpoint) {
    size_t endpoint_len = strlen(endpoint);
    
    if (strncmp(path, endpoint, endpoint_len) != 0) {
        return NULL;
    }
    
    // Skip the endpoint part
    const char *param_start = path + endpoint_len;
    
    // Skip leading slash if present
    if (*param_start == '/') {
        param_start++;
    }
    
    // If there's nothing after the endpoint, no parameter
    if (*param_start == '\0') {
        return NULL;
    }
    
    return strdup(param_start);
}

// Build RRD file path from template
static void build_rrd_path(char *dest, size_t dest_size, const char *base_path, 
                          const char *path_template, const char *param) {
    if (strchr(path_template, '%') && param) {
        // Template contains %s placeholder
        snprintf(dest, dest_size, "%s/", base_path);
        snprintf(dest + strlen(dest), dest_size - strlen(dest), path_template, param);
    } else {
        // Simple path without parameters
        snprintf(dest, dest_size, "%s/%s", base_path, path_template);
    }
}

static int handler(lsrp_request_t *req, lsrp_response_t *resp) {
    if (!req->params || req->params_len == 0) {
        resp->status = 1;
        resp->data = strdup("No parameters provided");
        resp->data_len = strlen(resp->data);
        return -1;
    }

    char* endpoint_str = get_param_value(req->params, "endpoint");
    if (!endpoint_str) {
        resp->status = 1;
        resp->data = strdup("Missing endpoint parameter");
        resp->data_len = strlen(resp->data);
        return -1;
    }

    // Special endpoint for getting metrics configuration
    if (strcmp(endpoint_str, "_config/metrics") == 0) {
        char *json = generate_metrics_json(&global_config);
        if (!json) {
            resp->status = 1;
            resp->data = strdup("Failed to generate metrics config");
            resp->data_len = strlen(resp->data);
            free(endpoint_str);
            return -1;
        }
       
        resp->status = 0;
        resp->data = json;
        resp->data_len = strlen(json);
        free(endpoint_str);
        return 0;
    }

    char* period_str = get_param_value(req->params, "period");
    int period = period_str ? atoi(period_str) : 3600;
    free(period_str);

    // Find matching metric configuration
    MetricConfig *metric = find_metric_config(&global_config, endpoint_str);
    if (!metric) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Unknown endpoint: %s", endpoint_str);
        resp->status = 1;
        resp->data = strdup(error_msg);
        resp->data_len = strlen(resp->data);
        free(endpoint_str);
        return -1;
    }

    // Extract parameter if required
    char *param = NULL;
    if (metric->requires_param) {
        param = extract_param_from_path(endpoint_str, metric->endpoint);
        if (!param || strlen(param) == 0) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Endpoint '%s' requires parameter '%s'",
                    metric->endpoint, metric->param_name);
            resp->status = 1;
            resp->data = strdup(error_msg);
            resp->data_len = strlen(resp->data);
            free(endpoint_str);
            if (param) free(param);
            return -1;
        }
    }

    // Build RRD file path
    char rrd_path[512] = {0};
    build_rrd_path(rrd_path, sizeof(rrd_path), global_config.rrd_base_path,
                   metric->rrd_path, param);

    fprintf(stderr, "Fetching data for endpoint=%s, RRD=%s\n",
            endpoint_str, rrd_path);

    time_t now = time(NULL);
    MetricData *data = fetch_metric_data(global_config.rrdcached_addr, rrd_path,
                                        now - period, param, metric);
   
    free(endpoint_str);

    if (!data) {
        resp->status = 1;
        resp->data = strdup("Failed to fetch metric data");
        resp->data_len = strlen(resp->data);
        if (param) free(param);
        return -1;
    }

    fprintf(stderr, "Data fetched: %d series\n", data->series_count);

    // Pass metric configuration to SVG generator
    data->metric_config = metric;

    char *svg = generate_svg(global_ctx, global_config.js_script_path, data);

    if (param) free(param);

    if (svg) {
        resp->status = 0;
        resp->data = svg;
        resp->data_len = strlen(svg);
        free_metric_data(data);
        return 0;
    } else {
        resp->status = 1;
        resp->data = strdup("Failed to generate SVG");
        resp->data_len = strlen(resp->data);
        free_metric_data(data);
        return -1;
    }
}

int main(int argc, char *argv[]) {
    global_ctx = duk_create_heap_default();
    if (!global_ctx) {
        fprintf(stderr, "Failed to create Duktape context\n");
        return 1;
    }

    const char *config_file = (argc > 1) ? argv[1] : "config.json";
    global_config = load_config(global_ctx, config_file);

    if (global_config.metrics_count == 0) {
        fprintf(stderr, "Error: No metrics configured. Please check your config file.\n");
        duk_destroy_heap(global_ctx);
        return 1;
    }

    fprintf(stderr, "Starting LSRP server on port %d with %d metrics\n", 
            global_config.tcp_port, global_config.metrics_count);
    fprintf(stderr, "Special endpoints:\n");
    fprintf(stderr, "  - _config/metrics: Get list of available metrics\n");

    int ret = lsrp_server_start(global_config.tcp_port, handler);
    if (ret < 0) {
        fprintf(stderr, "Failed to start LSRP server: %d\n", ret);
    }

    free_config(&global_config);
    duk_destroy_heap(global_ctx);
    free_js_cache();

    return 0;
}