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
#include "auth/auth.h"

#define DEFAULT_SVGD_HOST "127.0.0.1"
#define DEFAULT_SVGD_PORT 8081
#define DEFAULT_HTTP_PORT 8080
#define DEFAULT_STATIC_PATH "./gate/static"
#define MAX_REQUEST_LEN 8192
#define MAX_PARAMS_LEN LSRP_MAX_PARAMS_LEN
#define MAX_FILE_SIZE (1024 * 1024)  // 1MB max for static files

// Datasource configuration
#define MAX_DATASOURCES 16
#define MAX_DS_NAME_LEN 64
#define MAX_DS_HOST_LEN 256
#define DATASOURCES_FILE "datasources.json"

typedef struct {
    char name[MAX_DS_NAME_LEN];
    char host[MAX_DS_HOST_LEN];
    int port;
} Datasource;

typedef struct {
    Datasource items[MAX_DATASOURCES];
    int count;
    char default_name[MAX_DS_NAME_LEN];
} DatasourceList;

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

static DatasourceList datasources = {0};

static int load_datasources(void) {
    FILE *f = fopen(DATASOURCES_FILE, "r");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 0) { fclose(f); return -1; }
    if (fsize == 0) { fclose(f); return 0; }
    fseek(f, 0, SEEK_SET);

    char *json = malloc(fsize + 1);
    if (!json) { fclose(f); return -1; }

    size_t bytes_read = fread(json, 1, fsize, f);
    if (bytes_read != (size_t)fsize) {
        free(json);
        fclose(f);
        return -1;
    }
    json[fsize] = '\0';
    fclose(f);

    // Simple JSON parsing (no library)
    datasources.count = 0;

    // Find default
    char *def = strstr(json, "\"default\"");
    if (def) {
        def = strchr(def + 9, '"');
        if (def) {
            def++;
            char *end = strchr(def, '"');
            if (end) {
                size_t len = end - def;
                if (len < MAX_DS_NAME_LEN) {
                    strncpy(datasources.default_name, def, len);
                    datasources.default_name[len] = '\0';
                }
            }
        }
    }

    // Parse datasources array
    char *arr = strstr(json, "\"datasources\"");
    if (!arr) { free(json); return 0; }

    arr = strchr(arr, '[');
    if (!arr) { free(json); return 0; }

    while (datasources.count < MAX_DATASOURCES) {
        char *obj = strchr(arr, '{');
        if (!obj) break;

        Datasource *ds = &datasources.items[datasources.count];
        memset(ds, 0, sizeof(Datasource));

        // Parse name
        char *name = strstr(obj, "\"name\"");
        if (name) {
            name = strchr(name + 6, '"');
            if (name) {
                name++;
                char *end = strchr(name, '"');
                if (end) {
                    size_t len = end - name;
                    if (len < MAX_DS_NAME_LEN) {
                        strncpy(ds->name, name, len);
                        ds->name[len] = '\0';
                    }
                }
            }
        }

        // Parse host
        char *host = strstr(obj, "\"host\"");
        if (host) {
            host = strchr(host + 6, '"');
            if (host) {
                host++;
                char *end = strchr(host, '"');
                if (end) {
                    size_t len = end - host;
                    if (len < MAX_DS_HOST_LEN) {
                        strncpy(ds->host, host, len);
                        ds->host[len] = '\0';
                    }
                }
            }
        }

        // Parse port
        char *port = strstr(obj, "\"port\"");
        if (port) {
            port = strchr(port + 6, ':');
            if (port) ds->port = atoi(port + 1);
        }

        if (ds->name[0] && ds->host[0] && ds->port > 0) {
            datasources.count++;
        }

        arr = strchr(obj, '}');
        if (!arr) break;
        arr = strchr(arr + 1, '{');
        if (!arr) break;
    }

    free(json);
    fprintf(stderr, "Loaded %d datasources from %s\n", datasources.count, DATASOURCES_FILE);
    return datasources.count;
}

static int save_datasources(void) {
    FILE *f = fopen(DATASOURCES_FILE, "w");
    if (!f) return -1;

    fprintf(f, "{\n  \"default\": \"%s\",\n  \"datasources\": [\n", datasources.default_name);

    for (int i = 0; i < datasources.count; i++) {
        Datasource *ds = &datasources.items[i];
        fprintf(f, "    {\"name\": \"%s\", \"host\": \"%s\", \"port\": %d}%s\n",
                ds->name, ds->host, ds->port,
                (i < datasources.count - 1) ? "," : "");
    }

    fprintf(f, "  ]\n}\n");
    fclose(f);
    fprintf(stderr, "Saved %d datasources to %s\n", datasources.count, DATASOURCES_FILE);
    return 0;
}

static Datasource* find_datasource(const char *name) {
    for (int i = 0; i < datasources.count; i++) {
        if (strcmp(datasources.items[i].name, name) == 0) {
            return &datasources.items[i];
        }
    }
    return NULL;
}

// Extract datasource parameter from query string
static char* extract_datasource_param(const char *query) {
    if (!query) return NULL;

    const char *ds = strstr(query, "datasource=");
    if (!ds) return NULL;

    ds += 11; // Skip "datasource="
    const char *amp = strchr(ds, '&');
    const char *space = strchr(ds, ' ');
    const char *end = amp;
    if (space && (!end || space < end)) end = space;
    size_t len = end ? (size_t)(end - ds) : strlen(ds);

    char *value = malloc(len + 1);
    if (!value) return NULL;
    strncpy(value, ds, len);
    value[len] = '\0';
    return value;
}

static int add_datasource(const char *name, const char *host, int port) {
    if (datasources.count >= MAX_DATASOURCES) return -1;
    if (find_datasource(name)) return -2; // Already exists

    // Validate port range
    if (port <= 0 || port > 65535) return -3;

    Datasource *ds = &datasources.items[datasources.count];
    strncpy(ds->name, name, MAX_DS_NAME_LEN - 1);
    ds->name[MAX_DS_NAME_LEN - 1] = '\0';
    strncpy(ds->host, host, MAX_DS_HOST_LEN - 1);
    ds->host[MAX_DS_HOST_LEN - 1] = '\0';
    ds->port = port;
    datasources.count++;

    // Set as default if first
    if (datasources.count == 1) {
        strncpy(datasources.default_name, name, MAX_DS_NAME_LEN - 1);
        datasources.default_name[MAX_DS_NAME_LEN - 1] = '\0';
    }

    save_datasources();
    return 0;
}

static int remove_datasource(const char *name) {
    for (int i = 0; i < datasources.count; i++) {
        if (strcmp(datasources.items[i].name, name) == 0) {
            // Shift remaining
            for (int j = i; j < datasources.count - 1; j++) {
                datasources.items[j] = datasources.items[j + 1];
            }
            datasources.count--;

            // Update default if needed
            if (strcmp(datasources.default_name, name) == 0) {
                if (datasources.count > 0) {
                    strncpy(datasources.default_name, datasources.items[0].name, MAX_DS_NAME_LEN - 1);
                    datasources.default_name[MAX_DS_NAME_LEN - 1] = '\0';
                } else {
                    datasources.default_name[0] = '\0';
                }
            }

            save_datasources();
            return 0;
        }
    }
    return -1; // Not found
}

// Send JSON response with status code
static void send_json(int client_sock, int status, const char *json) {
    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 %d %s\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: %zu\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Connection: close\r\n\r\n",
                              status,
                              status == 200 ? "OK" : status == 201 ? "Created" :
                              status == 204 ? "No Content" : status == 404 ? "Not Found" :
                              status == 409 ? "Conflict" : "Error",
                              strlen(json));
    send(client_sock, header, header_len, MSG_NOSIGNAL);
    if (strlen(json) > 0) {
        send(client_sock, json, strlen(json), MSG_NOSIGNAL);
    }
}

// Handle GET /_datasources - list all datasources
static void handle_get_datasources(int client_sock) {
    char *json = malloc(4096 + datasources.count * 256);
    if (!json) {
        send_json(client_sock, 500, "{\"error\":\"Out of memory\"}");
        return;
    }

    size_t offset = snprintf(json, 4096 + datasources.count * 256,
                            "{\"default\":\"%s\",\"datasources\":[",
                            datasources.default_name);

    for (int i = 0; i < datasources.count; i++) {
        Datasource *ds = &datasources.items[i];
        offset += snprintf(json + offset, 4096 + datasources.count * 256 - offset,
                          "%s{\"name\":\"%s\",\"host\":\"%s\",\"port\":%d}",
                          i > 0 ? "," : "", ds->name, ds->host, ds->port);
    }

    offset += snprintf(json + offset, 4096 + datasources.count * 256 - offset, "]}");
    send_json(client_sock, 200, json);
    free(json);
}

// Handle POST /_datasources - add new datasource
static void handle_post_datasource(int client_sock, const char *body) {
    // Parse JSON body (simple parsing)
    char name[64] = "", host[256] = "";
    int port = 0;

    const char *n = strstr(body, "\"name\"");
    if (n) {
        n = strchr(n + 6, '"');
        if (n) {
            n++;
            const char *end = strchr(n, '"');
            if (end) {
                size_t len = end - n < 63 ? end - n : 63;
                strncpy(name, n, len);
                name[len] = '\0';
            }
        }
    }

    const char *h = strstr(body, "\"host\"");
    if (h) {
        h = strchr(h + 6, '"');
        if (h) {
            h++;
            const char *end = strchr(h, '"');
            if (end) {
                size_t len = end - h < 255 ? end - h : 255;
                strncpy(host, h, len);
                host[len] = '\0';
            }
        }
    }

    const char *p = strstr(body, "\"port\"");
    if (p) {
        p = strchr(p + 6, ':');
        if (p) port = atoi(p + 1);
    }

    if (!name[0] || !host[0] || port <= 0) {
        send_json(client_sock, 400, "{\"error\":\"Missing name, host, or port\"}");
        return;
    }

    int ret = add_datasource(name, host, port);
    if (ret == -1) {
        send_json(client_sock, 500, "{\"error\":\"Maximum datasources reached\"}");
    } else if (ret == -2) {
        send_json(client_sock, 409, "{\"error\":\"Datasource already exists\"}");
    } else {
        send_json(client_sock, 201, "{\"success\":true}");
    }
}

// Handle DELETE /_datasources/<name> - remove datasource
static void handle_delete_datasource(int client_sock, const char *name) {
    if (remove_datasource(name) == 0) {
        send_json(client_sock, 204, "");
    } else {
        char error[128];
        snprintf(error, sizeof(error), "{\"error\":\"Datasource '%s' not found\"}", name);
        send_json(client_sock, 404, error);
    }
}

// Handle PUT /_datasources/<name>/default - set default datasource
static void handle_set_default(int client_sock, const char *name) {
    if (find_datasource(name)) {
        strncpy(datasources.default_name, name, MAX_DS_NAME_LEN - 1);
        datasources.default_name[MAX_DS_NAME_LEN - 1] = '\0';
        save_datasources();
        send_json(client_sock, 200, "{\"success\":true}");
    } else {
        char error[128];
        snprintf(error, sizeof(error), "{\"error\":\"Datasource '%s' not found\"}", name);
        send_json(client_sock, 404, error);
    }
}

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
    const char *path_start = NULL;

    if (strncmp(request, "GET ", 4) == 0) {
        path_start = request + 4;
    } else if (strncmp(request, "POST ", 5) == 0) {
        path_start = request + 5;
    } else if (strncmp(request, "PUT ", 4) == 0) {
        path_start = request + 4;
    } else if (strncmp(request, "DELETE ", 7) == 0) {
        path_start = request + 7;
    } else if (strncmp(request, "OPTIONS ", 8) == 0) {
        path_start = request + 8;
    } else {
        return NULL;
    }

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

// Extract body from HTTP request (returns malloc'd string, caller must free)
static char* extract_body(const char *request) {
    const char *body = strstr(request, "\r\n\r\n");
    if (!body) return NULL;
    body += 4;
    return strdup(body);
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

// Check if path has a file extension (likely a static file)
static int has_file_extension(const char *path) {
    const char *last_slash = strrchr(path, '/');
    const char *last_dot = strrchr(path, '.');
    // Has extension and dot comes after last slash
    return last_dot && last_dot > last_slash;
}

// Check if request is for static file
static int is_static_request(const char *path) {
    // API paths without extensions
    if (strcmp(path, "/_datasources") == 0 ||
        strncmp(path, "/_datasources/", 14) == 0 ||
        strncmp(path, "/_config/", 9) == 0) {
        return 0;
    }
    // Static files: root, or has file extension
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        return 1;
    }
    // Files with extensions (.html, .js, .css, .map, .ico, .png, etc.) are static
    return has_file_extension(path);
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

// Extract Authorization header from request
static char* extract_auth_token(const char *request) {
    const char *auth_hdr = strstr(request, "Authorization: Bearer ");
    if (!auth_hdr) return NULL;

    auth_hdr += 22; // Skip "Authorization: Bearer "
    const char *end = strchr(auth_hdr, '\r');
    const char *end2 = strchr(auth_hdr, '\n');
    if (end2 && (!end || end2 < end)) end = end2;

    if (!end) return NULL;

    size_t len = end - auth_hdr;
    char *token = malloc(len + 1);
    if (!token) return NULL;
    strncpy(token, auth_hdr, len);
    token[len] = '\0';
    return token;
}

// Send 401 Unauthorized response
static void send_401_auth(int client_sock, const char *message) {
    char response[512];
    int body_len = strlen(message) + 12; // {"error":"..."} = 10 + len + 2
    int len = snprintf(response, sizeof(response),
                       "HTTP/1.1 401 Unauthorized\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %d\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Connection: close\r\n\r\n"
                       "{\"error\":\"%s\"}",
                       body_len, message);
    send(client_sock, response, len, MSG_NOSIGNAL);
}

// Check if path requires authentication
static int requires_auth(const char *path) {
    // Static files and login page are public
    if (is_static_request(path)) {
        return 0;
    }

    // Auth endpoints
    if (strcmp(path, "/_auth/login") == 0 || strcmp(path, "/_auth/verify") == 0) {
        return 0;
    }

    // Everything else requires auth (API endpoints)
    return 1;
}

// Handle POST /_auth/login - exchange password for token
static void handle_login(int client_sock, const char *body) {
    if (!auth_is_configured()) {
        send_401_auth(client_sock, "Auth not configured");
        return;
    }

    // Parse password from JSON
    char password[MAX_PASSWORD_LEN] = "";
    const char *p = strstr(body, "\"password\"");
    if (p) {
        p = strchr(p + 10, '"');
        if (p) {
            p++;
            const char *end = strchr(p, '"');
            if (end) {
                size_t len = end - p;
                if (len < MAX_PASSWORD_LEN) {
                    strncpy(password, p, len);
                    password[len] = '\0';
                }
            }
        }
    }

    if (!password[0]) {
        send_401_auth(client_sock, "Password required");
        return;
    }

    if (!auth_verify_password(password)) {
        send_401_auth(client_sock, "Invalid password");
        return;
    }

    // Create token
    char *token = auth_create_token();
    if (!token) {
        send_json(client_sock, 500, "{\"error\":\"Failed to create token\"}");
        return;
    }

    // Return token
    char response[MAX_TOKEN_SIZE + 64];
    snprintf(response, sizeof(response), "{\"token\":\"%s\"}", token);
    send_json(client_sock, 200, response);
    free(token);
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

    // Load datasources configuration
    if (load_datasources() <= 0) {
        // Create default datasource from command-line args
        fprintf(stderr, "No datasources loaded, creating default from args\n");
        add_datasource("default", global_config.svgd_host, global_config.svgd_port);
    }

    // Load auth configuration (try multiple paths)
    if (auth_load_config("auth.json") != 0 &&
        auth_load_config("gate/auth/auth.json") != 0) {
        fprintf(stderr, "Warning: Auth config not loaded, API endpoints will return 401\n");
    }

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

        // Read full request (handle TCP fragmentation)
        ssize_t total_read = 0;
        while (total_read < (ssize_t)(sizeof(buffer) - 1)) {
            ssize_t bytes_read = recv(client_sock, buffer + total_read,
                                             sizeof(buffer) - 1 - total_read, 0);
            if (bytes_read <= 0) break;
            total_read += bytes_read;
            buffer[total_read] = '\0';

            // Check if we have the full request (HTTP headers end with \r\n\r\n)
            if (total_read >= 4 && strstr(buffer, "\r\n\r\n")) {
                // For POST requests, check if body is complete
                char *content_len_str = strstr(buffer, "Content-Length:");
                if (content_len_str) {
                    long content_len = atol(content_len_str + 15);
                    char *body_start = strstr(buffer, "\r\n\r\n");
                    if (body_start) {
                        size_t header_size = body_start - buffer + 4;
                        if ((size_t)(total_read - (ssize_t)header_size) >= (size_t)content_len) {
                            break; // Full body received
                        }
                    }
                } else {
                    break; // No body expected
                }
            }
        }

        if (total_read <= 0) {
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

        // Handle auth login
        if (strcmp(path, "/_auth/login") == 0) {
            if (strncmp(buffer, "POST ", 5) == 0) {
                char *body = extract_body(buffer);
                if (body) {
                    handle_login(client_sock, body);
                    free(body);
                } else {
                    send_json(client_sock, 400, "{\"error\":\"Missing request body\"}");
                }
            } else if (strncmp(buffer, "OPTIONS ", 8) == 0) {
                const char *resp = "HTTP/1.1 200 OK\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                    "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                    "Content-Length: 0\r\n\r\n";
                send(client_sock, resp, strlen(resp), MSG_NOSIGNAL);
            } else {
                send_404(client_sock);
            }
            free(path);
            shutdown(client_sock, SHUT_WR);
            close(client_sock);
            continue;
        }

        // Handle GET /_auth/verify - check if token is valid
        if (strcmp(path, "/_auth/verify") == 0) {
            if (strncmp(buffer, "GET ", 4) == 0 || strncmp(buffer, "OPTIONS ", 8) == 0) {
                if (strncmp(buffer, "OPTIONS ", 8) == 0) {
                    const char *resp = "HTTP/1.1 200 OK\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
                        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                        "Content-Length: 0\r\n\r\n";
                    send(client_sock, resp, strlen(resp), MSG_NOSIGNAL);
                } else {
                    char *token = extract_auth_token(buffer);
                    if (token && auth_validate_token(token) == 0) {
                        send_json(client_sock, 200, "{\"valid\":true}");
                    } else {
                        send_401_auth(client_sock, "Invalid or expired token");
                    }
                    if (token) free(token);
                }
            } else {
                send_404(client_sock);
            }
            free(path);
            shutdown(client_sock, SHUT_WR);
            close(client_sock);
            continue;
        }

        // Handle CORS preflight for all routes BEFORE auth check
        if (strncmp(buffer, "OPTIONS ", 8) == 0) {
            handle_options(client_sock);
            free(path);
            shutdown(client_sock, SHUT_WR);
            close(client_sock);
            continue;
        }

        // Check authentication for protected routes
        if (requires_auth(path)) {
            char *token = extract_auth_token(buffer);
            int auth_valid = (token && auth_validate_token(token) == 0);
            if (token) free(token);

            if (!auth_valid) {
                send_401_auth(client_sock, "Missing or invalid token");
                free(path);
                shutdown(client_sock, SHUT_WR);
                close(client_sock);
                continue;
            }
        }

        // Handle datasources API
        if (strcmp(path, "/_datasources") == 0) {
            if (strncmp(buffer, "GET ", 4) == 0) {
                handle_get_datasources(client_sock);
            } else if (strncmp(buffer, "POST ", 5) == 0) {
                char *body = extract_body(buffer);
                if (body) {
                    handle_post_datasource(client_sock, body);
                    free(body);
                } else {
                    send_json(client_sock, 400, "{\"error\":\"Missing request body\"}");
                }
            } else if (strncmp(buffer, "OPTIONS ", 8) == 0) {
                const char *resp = "HTTP/1.1 200 OK\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
                    "Access-Control-Allow-Headers: Content-Type\r\n"
                    "Content-Length: 0\r\n\r\n";
                send(client_sock, resp, strlen(resp), MSG_NOSIGNAL);
            }
            free(path);
            shutdown(client_sock, SHUT_WR);
            close(client_sock);
            continue;
        }

        // Handle delete/set-default datasources
        if (strncmp(path, "/_datasources/", 14) == 0) {
            const char *name_in_path = path + 14;
            const char *slash = strchr(name_in_path, '/');
            char ds_name[MAX_DS_NAME_LEN];

            // Extract datasource name (copy to avoid modifying path)
            if (slash) {
                size_t len = slash - name_in_path;
                if (len >= MAX_DS_NAME_LEN) len = MAX_DS_NAME_LEN - 1;
                strncpy(ds_name, name_in_path, len);
                ds_name[len] = '\0';
            } else {
                strncpy(ds_name, name_in_path, MAX_DS_NAME_LEN - 1);
                ds_name[MAX_DS_NAME_LEN - 1] = '\0';
            }

            if (strncmp(buffer, "DELETE ", 7) == 0) {
                handle_delete_datasource(client_sock, ds_name);
            } else if (slash && strcmp(slash + 1, "default") == 0 && strncmp(buffer, "PUT ", 4) == 0) {
                handle_set_default(client_sock, ds_name);
            } else if (strncmp(buffer, "OPTIONS ", 8) == 0) {
                const char *resp = "HTTP/1.1 200 OK\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Access-Control-Allow-Methods: GET, POST, DELETE, PUT, OPTIONS\r\n"
                    "Access-Control-Allow-Headers: Content-Type\r\n"
                    "Content-Length: 0\r\n\r\n";
                send(client_sock, resp, strlen(resp), MSG_NOSIGNAL);
            } else {
                send_404(client_sock);
            }
            free(path);
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
            size_t len = end ? (size_t)(end - endpoint_param) : strlen(endpoint_param);
            if (len < sizeof(endpoint)) {
                strncpy(endpoint, endpoint_param, len);
                endpoint[len] = '\0';
            }
        }

        // Extract datasource parameter
        char *ds_name = NULL;
        const char *query_start = strchr(buffer, '?');
        if (query_start) {
            ds_name = extract_datasource_param(query_start);
        }

        // Determine target datasource
        Datasource *target_ds = NULL;
        if (ds_name) {
            target_ds = find_datasource(ds_name);
            if (!target_ds) {
                char error[256];
                snprintf(error, sizeof(error), "{\"error\":\"Datasource '%s' not found\"}", ds_name);
                send_json(client_sock, 404, error);
                free(ds_name);
                free(params);
                shutdown(client_sock, SHUT_WR);
                close(client_sock);
                continue;
            }
        } else if (datasources.count > 0 && datasources.default_name[0]) {
            target_ds = find_datasource(datasources.default_name);
        }

        const char *target_host = target_ds ? target_ds->host : global_config.svgd_host;
        int target_port = target_ds ? target_ds->port : global_config.svgd_port;

        if (ds_name) free(ds_name);

        // Send LSRP request
        lsrp_response_t lsrp_resp = {0};
        int ret = lsrp_client_send(target_host, target_port, params, &lsrp_resp);
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
