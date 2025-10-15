#include <sys/socket.h>
#include <netinet/in.h>
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

static int handler(lsrp_request_t *req, lsrp_response_t *resp) {
    if (!req->params || req->params_len == 0) {
        resp->status = 1;
        resp->data = strdup("No parameters provided");
        resp->data_len = strlen(resp->data);
        return -1;
    }

    char* endpoint = get_param_value(req->params, "endpoint");
    if (!endpoint) {
        resp->status = 1;
        resp->data = strdup("Missing endpoint");
        resp->data_len = strlen(resp->data);
        return -1;
    }

    char* period_str = get_param_value(req->params, "period");
    int period = period_str ? atoi(period_str) : 3600;
    free(period_str);

    char rrd_path[512] = {0};
    char metric_type[64] = {0};
    char param1[256] = {0};

    time_t now = time(NULL);

    char path[256];
    strncpy(path, endpoint, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    free(endpoint);

    char *token = strtok(path, "/");
    if (token) {
        if (strcmp(token, "cpu") == 0) {
            token = strtok(NULL, "/");
            if (!token || strcmp(token, "") == 0) {
                snprintf(rrd_path, sizeof(rrd_path), "%s/%s", global_config.rrd_base_path, global_config.path_cpu_total);
                strcpy(metric_type, "cpu_total");
            } else if (strcmp(token, "process") == 0) {
                token = strtok(NULL, "/");
                if (token) {
                    snprintf(rrd_path, sizeof(rrd_path), "%s/", global_config.rrd_base_path);
                    snprintf(rrd_path + strlen(rrd_path), sizeof(rrd_path) - strlen(rrd_path), global_config.path_cpu_process, token);
                    strcpy(metric_type, "cpu_process");
                    strcpy(param1, token);
                }
            }
        } else if (strcmp(token, "ram") == 0) {
            token = strtok(NULL, "/");
            if (!token || strcmp(token, "") == 0) {
                snprintf(rrd_path, sizeof(rrd_path), "%s/%s", global_config.rrd_base_path, global_config.path_ram_total);
                strcpy(metric_type, "ram_total");
            } else if (strcmp(token, "process") == 0) {
                token = strtok(NULL, "/");
                if (token) {
                    snprintf(rrd_path, sizeof(rrd_path), "%s/", global_config.rrd_base_path);
                    snprintf(rrd_path + strlen(rrd_path), sizeof(rrd_path) - strlen(rrd_path), global_config.path_ram_process, token);
                    strcpy(metric_type, "ram_process");
                    strcpy(param1, token);
                }
            }
        } else if (strcmp(token, "network") == 0) {
            token = strtok(NULL, "/");
            if (token) {
                snprintf(rrd_path, sizeof(rrd_path), "%s/", global_config.rrd_base_path);
                snprintf(rrd_path + strlen(rrd_path), sizeof(rrd_path) - strlen(rrd_path), global_config.path_network, token);
                strcpy(metric_type, "network");
                strcpy(param1, token);
            }
        } else if (strcmp(token, "disk") == 0) {
            token = strtok(NULL, "/");
            if (token) {
                snprintf(rrd_path, sizeof(rrd_path), "%s/", global_config.rrd_base_path);
                snprintf(rrd_path + strlen(rrd_path), sizeof(rrd_path) - strlen(rrd_path), global_config.path_disk, token);
                strcpy(metric_type, "disk");
                strcpy(param1, token);
            }
        } else if (strcmp(token, "postgresql") == 0) {
            token = strtok(NULL, "/");
            if (token && strcmp(token, "connections") == 0) {
                snprintf(rrd_path, sizeof(rrd_path), "%s/%s", global_config.rrd_base_path, global_config.path_postgresql_connections);
                strcpy(metric_type, "postgresql_connections");
            }
        }
    }

    if (strlen(rrd_path) == 0) {
        resp->status = 1;
        resp->data = strdup("Invalid endpoint");
        resp->data_len = strlen(resp->data);
        return -1;
    }

    fprintf(stderr, "Fetching data for RRD: %s, metric: %s\n", rrd_path, metric_type);
    MetricData *data = fetch_metric_data(rrd_path, now - period, metric_type, param1);
    if (data) {
        fprintf(stderr, "Data fetched: %d series\n", data->series_count);
        char *svg = generate_svg(global_ctx, global_config.js_script_path, data);
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
    } else {
        resp->status = 1;
        resp->data = strdup("Failed to fetch metric data");
        resp->data_len = strlen(resp->data);
        return -1;
    }
}

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "config.ini";
    global_config = load_config(config_file);
    global_ctx = duk_create_heap_default();
    if (!global_ctx) {
        fprintf(stderr, "Failed to create Duktape context\n");
        return 1;
    }

    printf("Server started on port %d\n", global_config.tcp_port);

    int ret = lsrp_server_start(global_config.tcp_port, handler);
    if (ret < 0) {
        fprintf(stderr, "Failed to start LSRP server: %d\n", ret);
    }

    duk_destroy_heap(global_ctx);
    return 0;
}