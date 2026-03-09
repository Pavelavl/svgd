/**
 * @file handler.h
 * @brief Request handling module - shared by HTTP and LSRP servers
 */

#ifndef SVGD_HANDLER_H
#define SVGD_HANDLER_H

#include <stddef.h>
#include "cfg.h"  /* For Config definition */

/**
 * Handler result structure
 */
typedef struct {
    char *data;           /* Response data (SVG or JSON) */
    size_t data_len;      /* Response length */
    int is_json;          /* 1 if JSON, 0 if SVG */
    int status;           /* 0 = success, non-zero = error */
} handler_result_t;

/**
 * Process a metric request
 *
 * @param config Server configuration
 * @param endpoint Endpoint path (e.g., "cpu", "cpu/process/nginx")
 * @param query Query string (may be NULL)
 * @param period Time period in seconds
 * @param use_cache Whether to use RRD data caching
 * @return Handler result (caller must free with handler_result_free)
 */
handler_result_t* handler_process(Config *config,
                                  const char *endpoint,
                                  const char *query,
                                  int period,
                                  int use_cache);

/**
 * Free handler result
 */
void handler_result_free(handler_result_t *result);

/**
 * Extract parameter value from query string
 *
 * @param params Query string (e.g., "endpoint=cpu&period=3600")
 * @param key Parameter key to find
 * @return Allocated parameter value (caller must free), or NULL if not found
 */
char* handler_get_param(const char *params, const char *key);

#endif /* SVGD_HANDLER_H */
