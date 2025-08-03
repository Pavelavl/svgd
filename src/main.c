#include <sys/socket.h>
#include <netinet/in.h>
#include "../include/cfg.h"
#include "../include/sock.h"
#include "../include/rrd_r.h"

int create_tcp_socket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = htons(port),
                               .sin_addr.s_addr = INADDR_ANY};
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(sock, 5);
    return sock;
}

int accept_connection(int tcp_sock, int unix_sock) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    return accept(tcp_sock, (struct sockaddr *)&client_addr, &addr_len);
}

void send_error(int client_sock, const char *message) {
    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 400 Bad Request\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n"
             "{\"error\":\"%s\"}",
             strlen(message) + 12, message);
    write(client_sock, response, strlen(response));
}

void send_response(int client_sock, const char *content_type, const char *content) {
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n\r\n",
             content_type, strlen(content));

    write(client_sock, header, strlen(header));
    write(client_sock, content, strlen(content));
}

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "config.ini";
    Config config = load_config(config_file);
    duk_context *ctx = duk_create_heap_default();
    if (!ctx) {
        fprintf(stderr, "Failed to create Duktape context\n");
        return 1;
    }

    int tcp_sock = create_tcp_socket(config.tcp_port);
    if (tcp_sock < 0) {
        fprintf(stderr, "Failed to create TCP socket\n");
        duk_destroy_heap(ctx);
        return 1;
    }
    printf("Server started on port %d\n", config.tcp_port);

    while (1) {
        int client_sock = accept_connection(tcp_sock, -1);
        if (client_sock < 0) continue;

        char buffer[1024];
        ssize_t len = read(client_sock, buffer, sizeof(buffer) - 1);
        if (len <= 0) {
            close(client_sock);
            continue;
        }
        buffer[len] = '\0';

        char rrd_path[512] = {0};
        char metric_type[64] = {0};
        char param1[256] = {0};

        time_t now = time(NULL);
        int period = 3600; // Default: 1 hour
        char *period_start = strstr(buffer, "period=");
        if (period_start) period = atoi(period_start + 7);

        char *path_start = strstr(buffer, "GET ");
        if (path_start) {
            path_start += 4;
            char *path_end = strchr(path_start, ' ');
            if (path_end) {
                size_t path_len = path_end - path_start;
                char path[256];
                strncpy(path, path_start, path_len);
                path[path_len] = '\0';

                char *token = strtok(path, "/");
                if (token) {
                    if (strcmp(token, "cpu") == 0) {
                        token = strtok(NULL, "/");
                        if (!token || strcmp(token, "") == 0) {
                            snprintf(rrd_path, sizeof(rrd_path), "%s/%s", config.rrd_base_path, config.path_cpu_total);
                            strcpy(metric_type, "cpu_total");
                        } else if (strcmp(token, "process") == 0) {
                            token = strtok(NULL, "/");
                            if (token) {
                                snprintf(rrd_path, sizeof(rrd_path), "%s/", config.rrd_base_path);
                                snprintf(rrd_path + strlen(rrd_path), sizeof(rrd_path) - strlen(rrd_path), config.path_cpu_process, token);
                                strcpy(metric_type, "cpu_process");
                                strcpy(param1, token);
                            }
                        }
                    } else if (strcmp(token, "ram") == 0) {
                        token = strtok(NULL, "/");
                        if (!token || strcmp(token, "") == 0) {
                            snprintf(rrd_path, sizeof(rrd_path), "%s/%s", config.rrd_base_path, config.path_ram_total);
                            strcpy(metric_type, "ram_total");
                        } else if (strcmp(token, "process") == 0) {
                            token = strtok(NULL, "/");
                            if (token) {
                                snprintf(rrd_path, sizeof(rrd_path), "%s/", config.rrd_base_path);
                                snprintf(rrd_path + strlen(rrd_path), sizeof(rrd_path) - strlen(rrd_path), config.path_ram_process, token);
                                strcpy(metric_type, "ram_process");
                                strcpy(param1, token);
                            }
                        }
                    } else if (strcmp(token, "network") == 0) {
                        token = strtok(NULL, "/");
                        if (token) {
                            snprintf(rrd_path, sizeof(rrd_path), "%s/", config.rrd_base_path);
                            snprintf(rrd_path + strlen(rrd_path), sizeof(rrd_path) - strlen(rrd_path), config.path_network, token);
                            strcpy(metric_type, "network");
                            strcpy(param1, token);
                        }
                    } else if (strcmp(token, "disk") == 0) {
                        token = strtok(NULL, "/");
                        if (token) {
                            snprintf(rrd_path, sizeof(rrd_path), "%s/", config.rrd_base_path);
                            snprintf(rrd_path + strlen(rrd_path), sizeof(rrd_path) - strlen(rrd_path), config.path_disk, token);
                            strcpy(metric_type, "disk");
                            strcpy(param1, token);
                        }
                    } else if (strcmp(token, "postgresql") == 0) {
                        token = strtok(NULL, "/");
                        if (token && strcmp(token, "connections") == 0) {
                            snprintf(rrd_path, sizeof(rrd_path), "%s/%s", config.rrd_base_path, config.path_postgresql_connections);
                            strcpy(metric_type, "postgresql_connections");
                        }
                    }
                }
            }
        }

        if (strlen(rrd_path) > 0) {
            fprintf(stderr, "Fetching data for RRD: %s, metric: %s\n", rrd_path, metric_type);
            MetricData *data = fetch_metric_data(config.rrdcached_addr, rrd_path, now - period, metric_type, param1);
            if (data) {
                fprintf(stderr, "Data fetched: %d series\n", data->series_count);
                char *svg = generate_svg(ctx, config.js_script_path, data);
                if (svg) {
                    send_response(client_sock, "image/svg+xml", svg);
                    free(svg);
                } else {
                    send_error(client_sock, "Failed to generate SVG");
                }
                free_metric_data(data);
            } else {
                send_error(client_sock, "Failed to fetch metric data");
            }
        } else {
            send_error(client_sock, "Invalid endpoint");
        }
        close(client_sock);
    }

    duk_destroy_heap(ctx);
    close(tcp_sock);
    return 0;
}