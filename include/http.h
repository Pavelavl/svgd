#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_MAX_PATH 256
#define HTTP_MAX_QUERY 512
#define HTTP_MAX_METHOD 8

typedef struct {
    char method[8];           // GET, POST, OPTIONS
    char path[HTTP_MAX_PATH];     // /cpu/usage
    char query[HTTP_MAX_QUERY];  // period=3600
} http_request_t;

typedef struct {
    int status;               // 200, 400, 404
    char content_type[64];    // image/svg+xml, application/json
    const char *body;
    size_t body_len;
} http_response_t;

// Parse HTTP request from raw bytes
// Returns 0 on success, -1 on error
int http_parse_request(const char *raw, size_t len, http_request_t *req);

// Build HTTP response to raw bytes (caller must free)
// Returns allocated buffer, sets out_len
char *http_build_response(const http_response_t *resp, size_t *out_len);

// Helper to send error response (JSON format)
void http_send_error(int client_sock, int status, const char *message);

// Helper to send success response
void http_send_response(int client_sock, const char *content_type,
                        const char *body, size_t body_len);

// Handle CORS preflight
void http_send_options(int client_sock);

// Get status text for code
const char *http_status_text(int status);

#endif
