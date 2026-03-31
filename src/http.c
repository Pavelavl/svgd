/**
 * @file http.c
 * @brief HTTP utilities implementation
 */

#include "../include/http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/* Escape a string for safe embedding in a JSON string value.
 * Returns number of characters written (excluding NUL). */
static size_t http_json_escape(const char *src, char *dest, size_t dest_size) {
    if (!src || !dest || dest_size == 0) return 0;
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dest_size - 1; i++) {
        unsigned char c = src[i];
        if (c == '"' || c == '\\') {
            if (j < dest_size - 2) { dest[j++] = '\\'; dest[j++] = c; }
        } else if (c == '\n') {
            if (j < dest_size - 2) { dest[j++] = '\\'; dest[j++] = 'n'; }
        } else if (c == '\r') {
            if (j < dest_size - 2) { dest[j++] = '\\'; dest[j++] = 'r'; }
        } else if (c == '\t') {
            if (j < dest_size - 2) { dest[j++] = '\\'; dest[j++] = 't'; }
        } else if (c < 0x20) {
            if (j < dest_size - 7) j += snprintf(dest + j, dest_size - j, "\\u%04x", c);
        } else {
            dest[j++] = c;
        }
    }
    dest[j] = '\0';
    return j;
}

const char *http_status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default:  return "Error";
    }
}

int http_parse_request(const char *raw, size_t len, http_request_t *req) {
    if (!raw || !req || len == 0) return -1;

    memset(req, 0, sizeof(*req));

    /* Find end of first line */
    const char *line_end = memchr(raw, '\r', len);
    if (!line_end) line_end = memchr(raw, '\n', len);
    if (!line_end) return -1;

    size_t line_len = line_end - raw;

    /* Parse method (first word) */
    const char *space1 = memchr(raw, ' ', line_len);
    if (!space1) return -1;

    size_t method_len = space1 - raw;
    if (method_len >= HTTP_MAX_METHOD) return -1;
    memcpy(req->method, raw, method_len);
    req->method[method_len] = '\0';

    /* Parse path (second word, may include query) */
    const char *path_start = space1 + 1;
    const char *space2 = memchr(path_start, ' ', line_len - (path_start - raw));
    if (!space2) return -1;

    size_t full_path_len = space2 - path_start;
    if (full_path_len >= HTTP_MAX_PATH + HTTP_MAX_QUERY) return -1;

    /* Split path and query */
    const char *query_start = memchr(path_start, '?', full_path_len);
    if (query_start) {
        size_t path_len = query_start - path_start;
        if (path_len >= sizeof(req->path)) return -1;
        memcpy(req->path, path_start, path_len);
        req->path[path_len] = '\0';

        size_t query_len = space2 - query_start - 1;
        if (query_len >= sizeof(req->query)) return -1;
        memcpy(req->query, query_start + 1, query_len);
        req->query[query_len] = '\0';
    } else {
        if (full_path_len >= sizeof(req->path)) return -1;
        memcpy(req->path, path_start, full_path_len);
        req->path[full_path_len] = '\0';
        req->query[0] = '\0';
    }

    return 0;
}

char *http_build_response(const http_response_t *resp, size_t *out_len) {
    const char *status_text = http_status_text(resp->status);

    /* Build headers */
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        resp->status, status_text,
        resp->content_type,
        resp->body_len);

    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        return NULL;
    }

    /* Allocate response buffer */
    *out_len = header_len + resp->body_len;
    char *response = malloc(*out_len);
    if (!response) return NULL;

    memcpy(response, header, header_len);
    if (resp->body && resp->body_len > 0) {
        memcpy(response + header_len, resp->body, resp->body_len);
    }

    return response;
}

void http_send_error(int client_sock, int status, const char *message) {
    char body[512];
    char escaped[256];
    http_json_escape(message, escaped, sizeof(escaped));
    int body_len = snprintf(body, sizeof(body), "{\"error\":\"%s\"}", escaped);

    http_response_t resp = {0};
    resp.status = status;
    strncpy(resp.content_type, "application/json", sizeof(resp.content_type) - 1);
    resp.body = body;
    resp.body_len = body_len;

    size_t resp_len;
    char *raw = http_build_response(&resp, &resp_len);
    if (raw) {
        send(client_sock, raw, resp_len, MSG_NOSIGNAL);
        free(raw);
    }
}

void http_send_response(int client_sock, const char *content_type,
                        const char *body, size_t body_len) {
    http_response_t resp = {0};
    resp.status = 200;
    strncpy(resp.content_type, content_type, sizeof(resp.content_type) - 1);
    resp.body = body;
    resp.body_len = body_len;

    size_t resp_len;
    char *raw = http_build_response(&resp, &resp_len);
    if (raw) {
        send(client_sock, raw, resp_len, MSG_NOSIGNAL);
        free(raw);
    }
}

void http_send_options(int client_sock) {
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
    send(client_sock, response, strlen(response), MSG_NOSIGNAL);
}
