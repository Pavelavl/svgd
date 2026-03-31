/**
 * @file main.c
 * @brief SVGD server entry point
 *
 * Supports two modes:
 * - HTTP: Native HTTP server (single-threaded)
 * - LSRP: Custom binary protocol with thread pool
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <execinfo.h>
#include <duktape.h>

#include "../include/cfg.h"
#include "../include/rrd_r.h"
#include "../lsrp/lsrp.h"
#include "../lsrp/lsrp_server.h"
#include "../include/http.h"
#include "../include/handler.h"

/* ============================================================================
 * Crash Handler
 * ============================================================================ */

static void crash_handler(int sig) {
    void *frames[32];
    int n = backtrace(frames, 32);
    fprintf(stderr, "\n=== CRASH: signal %d ===\n", sig);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _exit(128 + sig);
}

/* ============================================================================
 * Global State
 * ============================================================================ */

duk_context *global_ctx = NULL;  /* Used by handler.c */
static Config global_config;
static volatile sig_atomic_t running = 1;
static int server_sock = -1;
static int verbose_logging = 0;

/* Verbose logging accessor for other modules */
int is_verbose_logging(void) { return verbose_logging; }

/* ============================================================================
 * HTTP Server
 * ============================================================================ */

static void http_signal_handler(int sig) {
    (void)sig;
    running = 0;
    if (server_sock >= 0) {
        shutdown(server_sock, SHUT_RDWR);
        close(server_sock);
    }
}

static void run_http_server(int port) {
    struct sigaction sa = { .sa_handler = http_signal_handler };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(server_sock, SOMAXCONN) < 0) {
        fprintf(stderr, "Failed to bind/listen: %s\n", strerror(errno));
        close(server_sock);
        return;
    }

    fprintf(stderr, "HTTP server listening on port %d\n", port);

    char buffer[8192];
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);

        if (client_sock < 0) {
            if (running) fprintf(stderr, "Accept failed: %s\n", strerror(errno));
            continue;
        }

        ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            close(client_sock);
            continue;
        }
        buffer[n] = '\0';

        /* Handle CORS preflight */
        if (strncmp(buffer, "OPTIONS", 7) == 0) {
            http_send_options(client_sock);
            close(client_sock);
            continue;
        }

        /* Parse HTTP request */
        http_request_t req;
        if (http_parse_request(buffer, n, &req) != 0) {
            http_send_error(client_sock, 400, "Bad Request");
            close(client_sock);
            continue;
        }

        /* Extract endpoint and period */
        const char *endpoint = req.path;
        if (*endpoint == '/') endpoint++;

        int period = 3600;
        if (req.query[0]) {
            char *p = strstr(req.query, "period=");
            if (p) {
                period = atoi(p + 7);
                if (period <= 0) period = 3600;
            }
        }

        if (verbose_logging) {
            fprintf(stderr, "HTTP: %s %s (period=%d)\n", req.method, req.path, period);
        }

        /* Process request (width/height will be parsed from query in handler) */
        handler_result_t *result = handler_process(&global_config, endpoint, req.query, period, 0, 0, 0);

        if (result && result->status == 0) {
            http_send_response(client_sock,
                result->is_json ? "application/json" : "image/svg+xml",
                result->data, result->data_len);
        } else {
            http_send_error(client_sock, 400,
                result && result->data ? result->data : "Unknown error");
        }

        handler_result_free(result);
        close(client_sock);
    }

    fprintf(stderr, "\nShutting down HTTP server...\n");
    close(server_sock);
}

/* ============================================================================
 * LSRP Handler
 * ============================================================================ */

static int lsrp_handler(lsrp_request_t *req, lsrp_response_t *resp) {
    if (!req->params || req->params_len == 0) {
        resp->status = 1;
        resp->data = strdup("No parameters provided");
        resp->data_len = strlen(resp->data);
        return -1;
    }

    char *endpoint = handler_get_param(req->params, "endpoint");
    if (!endpoint) {
        resp->status = 1;
        resp->data = strdup("Missing endpoint parameter");
        resp->data_len = strlen(resp->data);
        return -1;
    }

    char *period_str = handler_get_param(req->params, "period");
    int period = period_str ? atoi(period_str) : 3600;
    if (period <= 0) period = 3600;
    free(period_str);

    /* Parse width/height from LSRP params */
    int width = 0;
    int height = 0;
    char *width_str = handler_get_param(req->params, "width");
    char *height_str = handler_get_param(req->params, "height");
    if (width_str) {
        width = atoi(width_str);
        free(width_str);
    }
    if (height_str) {
        height = atoi(height_str);
        free(height_str);
    }

    /* Process request with caching enabled (width/height 0 = use defaults) */
    handler_result_t *result = handler_process(&global_config, endpoint, NULL, period, width, height, 1);
    free(endpoint);

    if (result && result->status == 0) {
        resp->status = 0;
        resp->data = result->data;
        resp->data_len = result->data_len;
        result->data = NULL;  /* Transfer ownership */
    } else {
        resp->status = 1;
        resp->data = strdup(result && result->data ? result->data : "Unknown error");
        resp->data_len = strlen(resp->data);
    }

    handler_result_free(result);
    return resp->status == 0 ? 0 : -1;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(int argc, char *argv[]) {
    /* Install crash handler for SIGSEGV, SIGABRT, SIGBUS */
    struct sigaction sa_crash = { .sa_handler = crash_handler };
    sigemptyset(&sa_crash.sa_mask);
    sigaction(SIGSEGV, &sa_crash, NULL);
    sigaction(SIGABRT, &sa_crash, NULL);
    sigaction(SIGBUS, &sa_crash, NULL);

    /* Initialize Duktape context */
    global_ctx = duk_create_heap_default();
    if (!global_ctx) {
        fprintf(stderr, "Failed to create Duktape context\n");
        return 1;
    }

    /* Load configuration */
    const char *config_file = (argc > 1) ? argv[1] : "config.json";
    global_config = load_config(global_ctx, config_file);

    /* Set up verbose logging */
    verbose_logging = global_config.verbose;

    /* Initialize RRD cache for LSRP mode */
    if (strcmp(global_config.protocol, "http") != 0) {
        init_rrd_cache(global_config.cache_ttl_seconds);
        init_js_cache(global_config.js_script_path);
        fprintf(stderr, "JS cache pre-warmed for LSRP workers\n");
    }

    if (global_config.metrics_count == 0) {
        fprintf(stderr, "Error: No metrics configured\n");
        duk_destroy_heap(global_ctx);
        return 1;
    }

    /* Determine protocol */
    const char *protocol = global_config.protocol;
    if (strlen(protocol) == 0) protocol = "lsrp";

    fprintf(stderr, "Starting %s server on port %d (%d metrics)\n",
            protocol, global_config.tcp_port, global_config.metrics_count);

    /* Start appropriate server */
    if (strcmp(protocol, "http") == 0) {
        run_http_server(global_config.tcp_port);
    } else {
        int ret = lsrp_server_start(global_config.tcp_port, lsrp_handler,
                                    global_config.thread_pool_size);
        if (ret < 0) {
            fprintf(stderr, "Failed to start LSRP server: %d\n", ret);
        }
    }

    /* Cleanup */
    free_config(&global_config);
    duk_destroy_heap(global_ctx);
    free_js_cache();
    free_rrd_cache();

    return 0;
}
