#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../lsrp/lsrp_client.h"

#define DEFAULT_SVGD_HOST "127.0.0.1"
#define DEFAULT_SVGD_PORT 8081
#define DEFAULT_HTTP_PORT 8080
#define MAX_REQUEST_LEN 8192
#define MAX_PARAMS_LEN LSRP_MAX_PARAMS_LEN

struct Config {
    const char *svgd_host;
    int svgd_port;
    int http_port;
};

static struct Config global_config = {
    .svgd_host = DEFAULT_SVGD_HOST,
    .svgd_port = DEFAULT_SVGD_PORT,
    .http_port = DEFAULT_HTTP_PORT
};

// Parse GET request and extract path and query parameters
static char *parse_get_request(const char *request, size_t *params_len) {
    if (strncmp(request, "GET ", 4) != 0) return NULL;
    const char *path_start = request + 4;
    const char *path_end = strstr(path_start, " HTTP/");
    if (!path_end) return NULL;

    // Find query string start (if any)
    const char *query_start = strchr(path_start, '?');
    const char *end = query_start && query_start < path_end ? query_start : path_end;
    size_t path_len = end - path_start - 1; // Skip leading '/'
    if (path_len < 1 || path_start[0] != '/') return NULL;

    // Extract path (skip leading '/')
    char path[256];
    if (path_len >= sizeof(path)) return NULL;
    strncpy(path, path_start + 1, path_len);
    path[path_len] = '\0';

    // Extract query (after '?', if present)
    char query[256] = "";
    if (query_start && query_start < path_end) {
        query_start++;
        size_t query_len = path_end - query_start;
        if (query_len > 0 && query_len < sizeof(query)) {
            strncpy(query, query_start, query_len);
            query[query_len] = '\0';
        }
    }

    // Combine into LSRP params: endpoint=<path>&<query>
    char *params = malloc(MAX_PARAMS_LEN);
    if (!params) return NULL;
    if (strlen(query) > 0) {
        *params_len = snprintf(params, MAX_PARAMS_LEN, "endpoint=%s&%s", path, query);
    } else {
        *params_len = snprintf(params, MAX_PARAMS_LEN, "endpoint=%s", path);
    }
    if (*params_len >= MAX_PARAMS_LEN) {
        free(params);
        return NULL;
    }
    fprintf(stderr, "Parsed params: %s\n", params); // Debugging
    return params;
}

// Send HTTP error response (JSON format)
static void send_error(int client_sock, const char *message) {
    char response[512];
    int len = snprintf(response, sizeof(response),
                       "HTTP/1.1 400 Bad Request\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %zu\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Connection: close\r\n\r\n"
                       "{\"error\":\"%s\"}",
                       strlen(message) + 12, message);
    send(client_sock, response, len, 0);
}

// Send HTTP success response
static void send_response(int client_sock, const char *content_type, const char *data, size_t data_len) {
    char header[512];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Connection: close\r\n\r\n",
                              content_type, data_len);
    send(client_sock, header, header_len, 0);
    send(client_sock, data, data_len, 0);
}

int main(int argc, char *argv[]) {
    // Parse command-line arguments
    if (argc > 1) global_config.svgd_host = argv[1];
    if (argc > 2) global_config.svgd_port = atoi(argv[2]);
    if (argc > 3) global_config.http_port = atoi(argv[3]);

    // Create socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return 1;
    }

    // Set socket options
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(global_config.http_port);
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Failed to bind socket: %s\n", strerror(errno));
        close(server_sock);
        return 1;
    }

    // Listen
    if (listen(server_sock, 10) < 0) {
        fprintf(stderr, "Failed to listen: %s\n", strerror(errno));
        close(server_sock);
        return 1;
    }

    printf("svgd-gate running on port %d, forwarding to svgd at %s:%d\n",
           global_config.http_port, global_config.svgd_host, global_config.svgd_port);

    // Main loop
    char buffer[MAX_REQUEST_LEN];
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            fprintf(stderr, "Failed to accept connection: %s\n", strerror(errno));
            continue;
        }

        // Read request
        ssize_t bytes_read = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            close(client_sock);
            continue;
        }
        buffer[bytes_read] = '\0';

        // Parse request
        size_t params_len;
        char *params = parse_get_request(buffer, &params_len);
        if (!params) {
            send_error(client_sock, "Invalid or missing query parameters");
            close(client_sock);
            continue;
        }

        // Send LSRP request
        lsrp_response_t lsrp_resp = {0};
        int ret = lsrp_client_send(global_config.svgd_host, global_config.svgd_port, params, &lsrp_resp);
        free(params);

        if (ret != 0) {
            send_error(client_sock, "Failed to communicate with svgd service");
            close(client_sock);
            continue;
        }

        // Send response
        if (lsrp_resp.status == 0) {
            send_response(client_sock, "image/svg+xml", lsrp_resp.data, lsrp_resp.data_len);
        } else {
            send_error(client_sock, lsrp_resp.data);
        }
        free(lsrp_resp.data);
        close(client_sock);
    }

    close(server_sock);
    return 0;
}