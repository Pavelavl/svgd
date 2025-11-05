#ifndef CONFIG_H
#define CONFIG_H

#include <string.h>
#include <stdlib.h>
#include <duktape.h>

// Metric configuration structure
typedef struct {
    char endpoint[128];           // e.g., "cpu", "cpu/process", "network"
    char rrd_path[256];           // Path template (may contain %s for parameter)
    int requires_param;           // Does this metric need a parameter?
    char param_name[64];          // Name of parameter (e.g., "process_name", "interface")
    
    // Display configuration
    char title[128];              // Chart title template (may contain %s)
    char y_label[64];             // Y-axis label
    int is_percentage;            // Is this a percentage metric? (0-100)
    
    // Data transformation
    char transform_type[32];      // "none", "ps_cputime_sum", "bytes_to_mb", "multiply"
    double value_multiplier;      // Multiply values by this
    double transform_divisor;     // Divide values by this
    
    // Additional metadata (passed to JS)
    char value_format[32];        // e.g., "%.1f", "%.2f", "%d"
} MetricConfig;

typedef struct {
    int tcp_port;
    char allowed_ips[1024];
    char rrd_base_path[256];
    char rrdcached_addr[256];
    char js_script_path[256];
    
    MetricConfig *metrics;
    int metrics_count;
} Config;

// Load configuration from JSON file
Config load_config(duk_context *ctx, const char *filename);

// Free configuration resources
void free_config(Config *config);

// Find metric by endpoint and optional parameter
MetricConfig* find_metric_config(Config *config, const char *endpoint_path);

#endif