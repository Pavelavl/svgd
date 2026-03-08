#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "../lsrp/lsrp_client.h"

#define DEFAULT_SVGD_HOST "127.0.0.1"
#define DEFAULT_SVGD_PORT 8081
#define DEFAULT_HTTP_PORT 8080
#define DEFAULT_STATIC_PATH "./gate/static"
#define MAX_REQUEST_LEN 8192
#define MAX_PARAMS_LEN LSRP_MAX_PARAMS_LEN
#define MAX_FILE_SIZE (1024 * 1024)  // 1MB max for static files

struct Config {
    const char *svgd_host;
    int svgd_port;
    int http_port;
    const char *static_path;
};

static struct Config global_config = {
    .svgd_host = DEFAULT_SVGD_HOST,
    .svgd_port = DEFAULT_SVGD_PORT,
    .http_port = DEFAULT_HTTP_PORT,
    .static_path = DEFAULT_STATIC_PATH
};

static volatile sig_atomic_t running = 1;
static int server_sock = -1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
    if (server_sock >= 0) {
        shutdown(server_sock, SHUT_RDWR);
        close(server_sock);
    }
}

// Extract path from HTTP request (returns malloc'd string, caller must free)
static char *extract_path(const char *request) {
    if (strncmp(request, "GET ", 4) != 0) return NULL;
    const char *path_start = request + 4;
    const char *path_end = strstr(path_start, " HTTP/");
    if (!path_end) return NULL;

    // Find query string start (if any)
    const char *query_start = strchr(path_start, '?');
    const char *end = query_start && query_start < path_end ? query_start : path_end;
    size_t path_len = end - path_start;

    char *path = malloc(path_len + 1);
    if (!path) return NULL;
    strncpy(path, path_start, path_len);
    path[path_len] = '\0';
    return path;
}

// Parse GET request and extract path and query parameters for API
static char *parse_api_request(const char *request, size_t *params_len) {
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
    fprintf(stderr, "API request: %s\n", params);
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
    send(client_sock, response, len, MSG_NOSIGNAL);
}

// Send HTTP 404 response
static void send_404(int client_sock) {
    const char *response =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 9\r\n"
        "Connection: close\r\n\r\n"
        "Not Found";
    send(client_sock, response, strlen(response), MSG_NOSIGNAL);
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
    send(client_sock, header, header_len, MSG_NOSIGNAL);
    send(client_sock, data, data_len, MSG_NOSIGNAL);
}

// Handle CORS preflight request
static void handle_options(int client_sock) {
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    send(client_sock, response, strlen(response), MSG_NOSIGNAL);
}

// Check if request is for static file
static int is_static_request(const char *path) {
    if (strcmp(path, "/") == 0 ||
        strcmp(path, "/index.html") == 0 ||
        strcmp(path, "/script.js") == 0) {
        return 1;
    }
    return 0;
}

// Get MIME type based on file extension
static const char* get_mime_type(const char *path) {
    if (strstr(path, ".html") || strcmp(path, "/") == 0) {
        return "text/html; charset=utf-8";
    } else if (strstr(path, ".js")) {
        return "application/javascript; charset=utf-8";
    } else if (strstr(path, ".css")) {
        return "text/css; charset=utf-8";
    } else if (strstr(path, ".json")) {
        return "application/json; charset=utf-8";
    } else if (strstr(path, ".svg")) {
        return "image/svg+xml";
    } else if (strstr(path, ".png")) {
        return "image/png";
    }
    return "application/octet-stream";
}

// Serve static file from disk
static int serve_static_file(int client_sock, const char *path) {
    char filepath[512];

    // Map URL path to file path
    if (strcmp(path, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "%s/index.html", global_config.static_path);
    } else {
        snprintf(filepath, sizeof(filepath), "%s%s", global_config.static_path, path);
    }

    // Open file
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "Static file not found: %s\n", filepath);
        send_404(client_sock);
        return -1;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size > MAX_FILE_SIZE || file_size < 0) {
        fclose(f);
        send_error(client_sock, "File too large");
        return -1;
    }

    // Read file content
    char *content = malloc(file_size);
    if (!content) {
        fclose(f);
        send_error(client_sock, "Memory allocation failed");
        return -1;
    }

    size_t read_size = fread(content, 1, file_size, f);
    fclose(f);

    if (read_size != (size_t)file_size) {
        free(content);
        send_error(client_sock, "Failed to read file");
        return -1;
    }

    // Send response
    const char *mime_type = get_mime_type(path);
    send_response(client_sock, mime_type, content, file_size);
    free(content);

    fprintf(stderr, "Served static: %s (%ld bytes)\n", filepath, file_size);
    return 0;
}

// Determine content type based on endpoint for API responses
static const char* get_api_content_type(const char *endpoint) {
    if (strncmp(endpoint, "_config/", 8) == 0) {
        return "application/json";
    }
    return "image/svg+xml";
}

int main(int argc, char *argv[]) {
    // Parse command-line arguments
    if (argc > 1 && argv[1][0] != '\0') global_config.svgd_host = argv[1];
    if (argc > 2) {
        int port = atoi(argv[2]);
        if (port > 0) global_config.svgd_port = port;
    }
    if (argc > 3) {
        int port = atoi(argv[3]);
        if (port > 0) global_config.http_port = port;
    }
    if (argc > 4 && argv[4][0] != '\0') global_config.static_path = argv[4];

    // Setup signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
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

    printf("svgd-gate running on http://localhost:%d\n", global_config.http_port);
    printf("  Static files: %s\n", global_config.static_path);
    printf("  Backend: %s:%d\n", global_config.svgd_host, global_config.svgd_port);

    // Main loop
    char buffer[MAX_REQUEST_LEN];
    while (running) {
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

        // Handle CORS preflight
        if (strncmp(buffer, "OPTIONS ", 8) == 0) {
            handle_options(client_sock);
            shutdown(client_sock, SHUT_WR);
            close(client_sock);
            continue;
        }

        // Extract path from request
        char *path = extract_path(buffer);
        if (!path) {
            send_error(client_sock, "Invalid request");
            shutdown(client_sock, SHUT_WR);
            close(client_sock);
            continue;
        }

        // Check if static file request
        if (is_static_request(path)) {
            serve_static_file(client_sock, path);
            free(path);
            shutdown(client_sock, SHUT_WR);
            close(client_sock);
            continue;
        }

        free(path);

        // Parse as API request
        size_t params_len;
        char *params = parse_api_request(buffer, &params_len);
        if (!params) {
            send_error(client_sock, "Invalid or missing query parameters");
            shutdown(client_sock, SHUT_WR);
            close(client_sock);
            continue;
        }

        // Extract endpoint from params for content type determination
        char endpoint[256] = "";
        const char *endpoint_param = strstr(params, "endpoint=");
        if (endpoint_param) {
            endpoint_param += 9; // Skip "endpoint="
            const char *end = strchr(endpoint_param, '&');
            size_t len = end ? (end - endpoint_param) : strlen(endpoint_param);
            if (len < sizeof(endpoint)) {
                strncpy(endpoint, endpoint_param, len);
                endpoint[len] = '\0';
            }
        }

        // Send LSRP request
        lsrp_response_t lsrp_resp = {0};
        int ret = lsrp_client_send(global_config.svgd_host, global_config.svgd_port, params, &lsrp_resp);
        free(params);

        if (ret != 0) {
            send_error(client_sock, "Failed to communicate with svgd service");
            shutdown(client_sock, SHUT_WR);
            close(client_sock);
            continue;
        }

        // Send response with appropriate content type
        if (lsrp_resp.status == 0) {
            const char *content_type = get_api_content_type(endpoint);
            send_response(client_sock, content_type, lsrp_resp.data, lsrp_resp.data_len);
        } else {
            send_error(client_sock, lsrp_resp.data);
        }
        free(lsrp_resp.data);
        shutdown(client_sock, SHUT_WR);
        close(client_sock);
    }

    printf("\nShutting down...\n");
    shutdown(server_sock, SHUT_RDWR);
    close(server_sock);
    return 0;
}
