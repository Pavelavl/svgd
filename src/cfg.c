#include "../include/cfg.h"
#include <stdio.h>
#include <string.h>

static void set_string_field(duk_context *ctx, const char *field, char *dest, size_t dest_size, const char *default_val) {
    if (duk_get_prop_string(ctx, -1, field)) {
        if (duk_is_string(ctx, -1)) {
            strncpy(dest, duk_get_string(ctx, -1), dest_size - 1);
            dest[dest_size - 1] = '\0';
        } else {
            fprintf(stderr, "Warning: %s is not a string, using default\n", field);
            strncpy(dest, default_val, dest_size - 1);
            dest[dest_size - 1] = '\0';
        }
    } else {
        strncpy(dest, default_val, dest_size - 1);
        dest[dest_size - 1] = '\0';
    }
    duk_pop(ctx);
}

static int get_int_field(duk_context *ctx, const char *field, int default_val) {
    int result = default_val;
    if (duk_get_prop_string(ctx, -1, field)) {
        if (duk_is_number(ctx, -1)) {
            result = duk_get_int(ctx, -1);
        } else if (duk_is_boolean(ctx, -1)) {
            result = duk_get_boolean(ctx, -1) ? 1 : 0;
        }
    }
    duk_pop(ctx);
    return result;
}

static double get_double_field(duk_context *ctx, const char *field, double default_val) {
    double result = default_val;
    if (duk_get_prop_string(ctx, -1, field)) {
        if (duk_is_number(ctx, -1)) {
            result = duk_get_number(ctx, -1);
        }
    }
    duk_pop(ctx);
    return result;
}

static MetricConfig parse_metric_config(duk_context *ctx) {
    MetricConfig metric = {0};
    
    // Required fields
    set_string_field(ctx, "endpoint", metric.endpoint, sizeof(metric.endpoint), "");
    set_string_field(ctx, "rrd_path", metric.rrd_path, sizeof(metric.rrd_path), "");
    
    // Optional fields
    metric.requires_param = get_int_field(ctx, "requires_param", 0);
    set_string_field(ctx, "param_name", metric.param_name, sizeof(metric.param_name), "");
    
    // Display configuration
    set_string_field(ctx, "title", metric.title, sizeof(metric.title), "Metric");
    set_string_field(ctx, "y_label", metric.y_label, sizeof(metric.y_label), "Value");
    metric.is_percentage = get_int_field(ctx, "is_percentage", 0);
    
    // Transformation
    set_string_field(ctx, "transform_type", metric.transform_type, sizeof(metric.transform_type), "none");
    metric.value_multiplier = get_double_field(ctx, "value_multiplier", 1.0);
    metric.transform_divisor = get_double_field(ctx, "transform_divisor", 1.0);
    set_string_field(ctx, "value_format", metric.value_format, sizeof(metric.value_format), "%.2f");
    
    return metric;
}

Config load_config(duk_context *ctx, const char *filename) {
    Config config = {
        .tcp_port = 8080,
        .allowed_ips = "127.0.0.1",
        .rrdcached_addr = "unix:/var/run/rrdcached.sock",
        .rrd_base_path = "/opt/collectd/var/lib/collectd/rrd/localhost",
        .js_script_path = "/home/workerpool/svgd/scripts/generate_cpu_svg.js",
        .metrics = NULL,
        .metrics_count = 0
    };

    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Warning: Cannot open config file %s, using default configuration\n", filename);
        return config;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_code = malloc(fsize + 1);
    if (!json_code) {
        fprintf(stderr, "Error: Cannot allocate memory for config file\n");
        fclose(f);
        return config;
    }

    fread(json_code, 1, fsize, f);
    json_code[fsize] = '\0';
    fclose(f);

    duk_idx_t top = duk_get_top(ctx);
    duk_push_string(ctx, json_code);
    free(json_code);

    duk_json_decode(ctx, -1);
    if (duk_is_error(ctx, -1)) {
        fprintf(stderr, "Error: Failed to parse config.json: %s\n", duk_safe_to_string(ctx, -1));
        duk_pop_n(ctx, duk_get_top(ctx) - top);
        return config;
    }

    if (!duk_is_object(ctx, -1)) {
        fprintf(stderr, "Error: config.json must contain an object\n");
        duk_pop_n(ctx, duk_get_top(ctx) - top);
        return config;
    }

    // Parse server section
    if (duk_get_prop_string(ctx, -1, "server")) {
        if (duk_is_object(ctx, -1)) {
            config.tcp_port = get_int_field(ctx, "tcp_port", 8080);
            set_string_field(ctx, "allowed_ips", config.allowed_ips, sizeof(config.allowed_ips), "127.0.0.1");
            set_string_field(ctx, "rrdcached_addr", config.rrdcached_addr, sizeof(config.rrdcached_addr), "");
        }
        duk_pop(ctx);
    }

    // Parse RRD section
    if (duk_get_prop_string(ctx, -1, "rrd")) {
        if (duk_is_object(ctx, -1)) {
            set_string_field(ctx, "base_path", config.rrd_base_path, sizeof(config.rrd_base_path), "/opt/collectd/var/lib/collectd/rrd/localhost");
        }
        duk_pop(ctx);
    }

    // Parse JS section
    if (duk_get_prop_string(ctx, -1, "js")) {
        if (duk_is_object(ctx, -1)) {
            set_string_field(ctx, "script_path", config.js_script_path, sizeof(config.js_script_path), "");
        }
        duk_pop(ctx);
    }

    // Parse metrics array
    if (duk_get_prop_string(ctx, -1, "metrics")) {
        if (duk_is_array(ctx, -1)) {
            duk_size_t len = duk_get_length(ctx, -1);
            config.metrics_count = (int)len;
            config.metrics = malloc(sizeof(MetricConfig) * config.metrics_count);
            
            if (!config.metrics) {
                fprintf(stderr, "Error: Cannot allocate memory for metrics\n");
                config.metrics_count = 0;
            } else {
                for (duk_size_t i = 0; i < len; i++) {
                    duk_get_prop_index(ctx, -1, i);
                    if (duk_is_object(ctx, -1)) {
                        config.metrics[i] = parse_metric_config(ctx);
                        
                        // Validate required fields
                        if (strlen(config.metrics[i].endpoint) == 0 || strlen(config.metrics[i].rrd_path) == 0) {
                            fprintf(stderr, "Warning: Metric at index %zu has missing required fields, skipping\n", i);
                            memset(&config.metrics[i], 0, sizeof(MetricConfig));
                        }
                    }
                    duk_pop(ctx);
                }
                
                fprintf(stderr, "Loaded %d metrics from config\n", config.metrics_count);
            }
        } else {
            fprintf(stderr, "Warning: 'metrics' is not an array\n");
        }
        duk_pop(ctx);
    } else {
        fprintf(stderr, "Warning: No 'metrics' section found in config\n");
    }

    duk_pop_n(ctx, duk_get_top(ctx) - top);
    return config;
}

void free_config(Config *config) {
    if (config && config->metrics) {
        free(config->metrics);
        config->metrics = NULL;
        config->metrics_count = 0;
    }
}

// Find metric configuration by matching endpoint path
// Supports both exact matches and parametrized endpoints
MetricConfig* find_metric_config(Config *config, const char *endpoint_path) {
    if (!config || !endpoint_path) return NULL;
    
    // First pass: look for exact matches
    for (int i = 0; i < config->metrics_count; i++) {
        if (strcmp(config->metrics[i].endpoint, endpoint_path) == 0) {
            return &config->metrics[i];
        }
    }
    
    // Second pass: look for parametrized matches
    // e.g., endpoint_path="cpu/process/nginx" should match endpoint="cpu/process"
    for (int i = 0; i < config->metrics_count; i++) {
        if (config->metrics[i].requires_param) {
            size_t endpoint_len = strlen(config->metrics[i].endpoint);
            if (strncmp(config->metrics[i].endpoint, endpoint_path, endpoint_len) == 0) {
                // Check if there's a parameter after the endpoint
                if (endpoint_path[endpoint_len] == '/' || endpoint_path[endpoint_len] == '\0') {
                    return &config->metrics[i];
                }
            }
        }
    }
    
    return NULL;
}

// Generate JSON list of available metrics
char* generate_metrics_json(Config *config) {
    // Calculate required buffer size
    size_t buffer_size = 1024 + config->metrics_count * 512;
    char *json = malloc(buffer_size);
    if (!json) return NULL;
    
    size_t offset = 0;
    offset += snprintf(json + offset, buffer_size - offset, 
                      "{\"version\":\"1.0\",\"metrics\":[");
    
    for (int i = 0; i < config->metrics_count; i++) {
        MetricConfig *m = &config->metrics[i];
        
        if (i > 0) {
            offset += snprintf(json + offset, buffer_size - offset, ",");
        }
        
        offset += snprintf(json + offset, buffer_size - offset,
            "{\"endpoint\":\"%s\",\"requires_param\":%s",
            m->endpoint, m->requires_param ? "true" : "false");
        
        if (m->requires_param) {
            offset += snprintf(json + offset, buffer_size - offset,
                ",\"param_name\":\"%s\"", m->param_name);
        }
        
        offset += snprintf(json + offset, buffer_size - offset,
            ",\"title\":\"%s\",\"y_label\":\"%s\",\"is_percentage\":%s",
            m->title, m->y_label, m->is_percentage ? "true" : "false");
        
        offset += snprintf(json + offset, buffer_size - offset, "}");
        
        if (offset >= buffer_size - 512) {
            // Buffer too small, reallocate
            buffer_size *= 2;
            char *new_json = realloc(json, buffer_size);
            if (!new_json) {
                free(json);
                return NULL;
            }
            json = new_json;
        }
    }
    
    offset += snprintf(json + offset, buffer_size - offset, "]}");
    
    return json;
}
