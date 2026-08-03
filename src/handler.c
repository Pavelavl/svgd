/**
 * @file handler.c
 * @brief Request handling implementation
 */

#include "../include/handler.h"
#include "../include/path_util.h"
#include "../include/rrd_r.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

/* External JS context from main.c */
extern duk_context *global_ctx;

/**
 * Extract parameter value from query string
 */
char* handler_get_param(const char *params, const char *key) {
    if (!params || !key) return NULL;

    char search_key[256];
    snprintf(search_key, sizeof(search_key), "%s=", key);

    const char *key_pos = strstr(params, search_key);
    if (!key_pos) return NULL;

    key_pos += strlen(search_key);
    const char *end = strchr(key_pos, '&');
    size_t len = end ? (size_t)(end - key_pos) : strlen(key_pos);

    char *value = malloc(len + 1);
    if (!value) return NULL;

    strncpy(value, key_pos, len);
    value[len] = '\0';
    return value;
}

/**
 * Create error result
 */
static handler_result_t* create_error_result(const char *message) {
    handler_result_t *result = malloc(sizeof(handler_result_t));
    if (!result) return NULL;

    result->data = strdup(message);
    result->data_len = strlen(message);
    result->is_json = 0;
    result->status = 1;
    return result;
}

/* extract_param_from_path() и build_rrd_path() вынесены в src/path_util.c
 * (см. include/path_util.h) — чистая логика построения путей, покрытая
 * unit-тестами в tests/c/test_path.c. */

/* ============================================================================
 * Grafana datasource support
 *
 * Implements the simpod / classic-SimpleJson structured JSON datasource
 * contract so a Grafana instance can query svgd metrics as time-series:
 *   _grafana/search -> ["endpoint", ...]
 *   _grafana/query  -> [{"target": ..., "datapoints": [[value, epoch_ms], ...]}]
 * The gate forwards Grafana requests here (the backend has Duktape for JSON).
 * ============================================================================ */

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* URL-decode (application/x-www-form-urlencoded) into a freshly malloc'd string. */
static char *url_decode(const char *enc) {
    if (!enc) return NULL;
    size_t len = strlen(enc);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        char c = enc[i];
        if (c == '+') {
            out[o++] = ' ';
        } else if (c == '%' && i + 2 < len &&
                   isxdigit((unsigned char)enc[i + 1]) &&
                   isxdigit((unsigned char)enc[i + 2])) {
            int hi = hexval((unsigned char)enc[i + 1]);
            int lo = hexval((unsigned char)enc[i + 2]);
            out[o++] = (char)((hi << 4) | lo);
            i += 2;
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
    return out;
}

/* Parse an ISO-8601 timestamp like "2026-08-02T00:00:00.000Z" to UTC epoch seconds.
   Uses an explicit days-from-civil calculation (no timegm / feature macros needed).
   Returns 0 if it can't be parsed (caller falls back to a default). */
static time_t parse_iso8601(const char *s) {
    if (!s) return 0;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 5) return 0;
    /* Howard Hinnant's days_from_civil (proleptic Gregorian, epoch 1970-01-01). */
    if (mo <= 2) y -= 1;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned mp = (mo > 2) ? (unsigned)mo - 3 : (unsigned)mo + 9;
    unsigned doy = (153u * mp + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    return (time_t)(days * 86400 + (long)h * 3600 + (long)mi * 60 + se);
}

/* Append printf-style text to a growable buffer. Returns 0 on success, -1 on error. */
static int buf_append(char **buf, size_t *cap, size_t *off, const char *fmt, ...) {
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(*buf + *off, *cap - *off, fmt, ap);
        va_end(ap);
        if (n < 0) return -1;
        if ((size_t)n < *cap - *off) { *off += (size_t)n; return 0; }
        size_t newcap = *cap * 2;
        while (*off + (size_t)n + 1 > newcap) newcap *= 2;
        char *nb = realloc(*buf, newcap);
        if (!nb) return -1;
        *buf = nb;
        *cap = newcap;
    }
}

/* Protected JSON decode for duk_safe_call: decodes the string on top of the stack. */
static duk_ret_t gf_json_decode(duk_context *ctx, void *ud) {
    (void)ud;
    duk_json_decode(ctx, -1);  /* replaces the top string with the parsed value */
    return 1;
}

/* _grafana/search: return the metric endpoints as a JSON string array. */
static handler_result_t* grafana_search(Config *config) {
    size_t cap = 256 + (size_t)config->metrics_count * 140;
    char *json = malloc(cap);
    if (!json) return create_error_result("Out of memory");
    size_t off = 0;
    if (buf_append(&json, &cap, &off, "[") != 0) { free(json); return create_error_result("Out of memory"); }
    for (int i = 0; i < config->metrics_count; i++) {
        const char *sep = (i > 0) ? "," : "";
        if (buf_append(&json, &cap, &off, "%s\"%s\"", sep, config->metrics[i].endpoint) != 0) {
            free(json);
            return create_error_result("Out of memory");
        }
    }
    if (buf_append(&json, &cap, &off, "]") != 0) { free(json); return create_error_result("Out of memory"); }

    handler_result_t *r = malloc(sizeof(handler_result_t));
    if (!r) { free(json); return create_error_result("Out of memory"); }
    r->data = json;
    r->data_len = off;
    r->is_json = 1;
    r->status = 0;
    return r;
}

/* _grafana/query: parse the Grafana query body, fetch each target metric, and
   return Grafana time-series JSON. Body arrives URL-encoded in the `body` param. */
static handler_result_t* grafana_query(Config *config, const char *query) {
    if (!query) return create_error_result("Missing query");
    char *body_enc = handler_get_param(query, "body");
    if (!body_enc) return create_error_result("Missing body");
    char *body = url_decode(body_enc);
    free(body_enc);
    if (!body) return create_error_result("Invalid body encoding");

    duk_context *ctx = svg_get_context();
    if (!ctx) { free(body); return create_error_result("JS context unavailable"); }

    /* Protected JSON parse of the body. */
    duk_push_string(ctx, body);
    free(body);
    if (duk_safe_call(ctx, gf_json_decode, NULL, 1, 1) != DUK_EXEC_SUCCESS) {
        duk_pop(ctx);  /* error value */
        return create_error_result("Invalid JSON body");
    }
    /* stack: [obj] */

    /* range.from / range.to (ISO-8601) -> period in seconds (data is fetched to NOW). */
    long period = 3600;
    time_t t_from = 0, t_to = 0;
    duk_get_prop_string(ctx, -1, "range");          /* [obj, range] */
    if (duk_is_object(ctx, -1)) {
        duk_get_prop_string(ctx, -1, "from");       /* [obj, range, from] */
        const char *fs = duk_get_string(ctx, -1);
        if (fs) t_from = parse_iso8601(fs);
        duk_pop(ctx);                                /* [obj, range] */
        duk_get_prop_string(ctx, -1, "to");         /* [obj, range, to] */
        const char *ts = duk_get_string(ctx, -1);
        if (ts) t_to = parse_iso8601(ts);
        duk_pop(ctx);                                /* [obj, range] */
    }
    duk_pop(ctx);                                    /* [obj] */
    if (t_to > t_from) period = (long)(t_to - t_from);
    if (period <= 0) period = 3600;

    /* Collect targets[] into a C array, then drop the Duktape stack. */
    char **targets = NULL;
    int n_targets = 0;
    if (duk_get_prop_string(ctx, -1, "targets")) {        /* [obj, targets] */
        if (duk_is_array(ctx, -1)) {
            duk_size_t n = duk_get_length(ctx, -1);
            if (n > 0) {
                targets = calloc(n, sizeof(char *));
                if (targets) {
                    for (duk_size_t i = 0; i < n; i++) {
                        if (!duk_get_prop_index(ctx, -1, (duk_uarridx_t)i)) {
                            duk_pop(ctx);
                            continue;
                        }                                   /* [obj, targets, targetObj] */
                        if (duk_get_prop_string(ctx, -1, "target")) { /* [..., targetObj, targetStr] */
                            const char *t = duk_get_string(ctx, -1);
                            if (t && *t) targets[n_targets++] = strdup(t);
                        }
                        duk_pop_2(ctx);                     /* [obj, targets] */
                    }
                }
            }
        }
        duk_pop(ctx);                                       /* [obj] */
    } else {
        duk_pop(ctx);  /* pop undefined */
    }
    duk_pop(ctx);  /* pop obj -> stack empty */

    /* Build the Grafana response by fetching each target. */
    size_t cap = 4096;
    char *out = malloc(cap);
    if (!out) {
        for (int i = 0; i < n_targets; i++) free(targets[i]);
        free(targets);
        return create_error_result("Out of memory");
    }
    size_t off = 0;
    int has_any = 0;
    if (buf_append(&out, &cap, &off, "[") != 0) goto gf_oom;

    for (int i = 0; i < n_targets; i++) {
        const char *target = targets[i];
        MetricConfig *m = find_metric_config(config, target);
        if (!m) continue;

        char *param = NULL;
        if (m->requires_param) param = extract_param_from_path(target, m->endpoint);
        char rrd_path[512] = {0};
        build_rrd_path(rrd_path, sizeof(rrd_path), config->rrd_base_path, m->rrd_path, param);

        time_t now = time(NULL);
        MetricData *data = fetch_metric_data(config->rrdcached_addr, rrd_path,
                                             now - period, param, m);
        if (param) free(param);

        if (data) {
            for (int s = 0; s < data->series_count; s++) {
                if (data->series_counts[s] == 0) continue;
                const char *name = (data->series_names && data->series_names[s])
                                   ? data->series_names[s] : target;
                if (buf_append(&out, &cap, &off, "%s{\"target\":\"%s\",\"datapoints\":[",
                               has_any ? "," : "", name) != 0) {
                    free_metric_data(data);
                    goto gf_oom;
                }
                for (int p = 0; p < data->series_counts[s]; p++) {
                    long long ts_ms = (long long)data->series_data[s][p].timestamp * 1000;
                    if (buf_append(&out, &cap, &off, "[%g,%lld],",
                                   data->series_data[s][p].value, ts_ms) != 0) {
                        free_metric_data(data);
                        goto gf_oom;
                    }
                }
                if (off > 0 && out[off - 1] == ',') off--;  /* trim trailing comma */
                if (buf_append(&out, &cap, &off, "]}") != 0) { free_metric_data(data); goto gf_oom; }
                has_any = 1;
            }
            free_metric_data(data);
        }
    }

    if (buf_append(&out, &cap, &off, "]") != 0) goto gf_oom;

    for (int i = 0; i < n_targets; i++) free(targets[i]);
    free(targets);

    {
        handler_result_t *r = malloc(sizeof(handler_result_t));
        if (!r) { free(out); return create_error_result("Out of memory"); }
        r->data = out;
        r->data_len = off;
        r->is_json = 1;
        r->status = 0;
        return r;
    }

gf_oom:
    free(out);
    for (int i = 0; i < n_targets; i++) free(targets[i]);
    free(targets);
    return create_error_result("Out of memory");
}

/**
 * Process a metric request
 */
handler_result_t* handler_process(Config *config,
                                  const char *endpoint,
                                  const char *query,
                                  int period,
                                  int width,
                                  int height,
                                  int use_cache) {
    if (!config || !endpoint) {
        return create_error_result("Invalid parameters");
    }

    /* Parse width/height from query string, use passed values as defaults */
    int svg_width = width;
    int svg_height = height;

    if (query) {
        char *width_str = handler_get_param(query, "width");
        char *height_str = handler_get_param(query, "height");

        if (width_str) {
            char *endptr;
            long val = strtol(width_str, &endptr, 10);
            if (*endptr == '\0' && val > 0) svg_width = (int)val;
            free(width_str);
        }

        if (height_str) {
            char *endptr;
            long val = strtol(height_str, &endptr, 10);
            if (*endptr == '\0' && val > 0) svg_height = (int)val;
            free(height_str);
        }
    }

    /* Resolve render theme: the ?theme= query param overrides "server.theme"
       from config.json. Unknown values fall back to "light" inside the JS.
       See docs/gallery.md. */
    char *theme_query = query ? handler_get_param(query, "theme") : NULL;
    const char *theme = (theme_query && *theme_query) ? theme_query
                        : (config->theme[0] ? config->theme : "light");

    /* Apply defaults and bounds checking */
    if (svg_width <= 0) svg_width = 800;
    if (svg_width < 200) svg_width = 200;
    if (svg_width > 1600) svg_width = 1600;

    if (svg_height <= 0) svg_height = 450;
    if (svg_height < 120) svg_height = 120;
    if (svg_height > 800) svg_height = 800;

    /* Special endpoint: metrics configuration */
    if (strcmp(endpoint, "_config/metrics") == 0) {
        char *json = generate_metrics_json(config);
        if (!json) {
            return create_error_result("Failed to generate metrics config");
        }

        handler_result_t *result = malloc(sizeof(handler_result_t));
        if (!result) {
            free(json);
            return create_error_result("Out of memory");
        }

        result->data = json;
        result->data_len = strlen(json);
        result->is_json = 1;
        result->status = 0;
        return result;
    }

    /* Grafana datasource endpoints (forwarded by svgd-gate). */
    if (strcmp(endpoint, "_grafana/search") == 0) {
        return grafana_search(config);
    }
    if (strcmp(endpoint, "_grafana/query") == 0) {
        return grafana_query(config, query);
    }

    /* Find matching metric configuration */
    MetricConfig *metric = find_metric_config(config, endpoint);
    if (!metric) {
        char error_buf[256];
        snprintf(error_buf, sizeof(error_buf), "Unknown endpoint: %s", endpoint);
        return create_error_result(error_buf);
    }

    /* Extract parameter if required */
    char *param = NULL;
    if (metric->requires_param) {
        param = extract_param_from_path(endpoint, metric->endpoint);
        if (!param || strlen(param) == 0) {
            char error_buf[256];
            snprintf(error_buf, sizeof(error_buf), "Endpoint '%s' requires parameter '%s'",
                    metric->endpoint, metric->param_name);
            if (param) free(param);
            return create_error_result(error_buf);
        }
    }

    /* Build RRD file path */
    char rrd_path[512] = {0};
    build_rrd_path(rrd_path, sizeof(rrd_path), config->rrd_base_path,
                   metric->rrd_path, param);

    /* Fetch data (with optional caching) */
    MetricData *data = NULL;

    if (use_cache) {
        data = cache_get(rrd_path, period);
    }

    if (!data) {
        time_t now = time(NULL);
        MetricData *fresh_data = fetch_metric_data(config->rrdcached_addr, rrd_path,
                                                   now - period, param, metric);
        if (fresh_data) {
            if (use_cache) {
                cache_put(rrd_path, period, fresh_data);
                /* cache_put takes ownership of fresh_data; clone back for use */
                data = cache_get(rrd_path, period);
                /* If clone fails (OOM), the data is still in cache but we can't use it now.
                   Next request will try cache_get again. */
            } else {
                data = fresh_data;
            }
        }
    }

    if (param) free(param);

    if (!data) {
        return create_error_result("Failed to fetch metric data");
    }

    /* Generate SVG */
    data->metric_config = metric;
    char *svg = generate_svg(global_ctx, config->js_script_path, data, svg_width, svg_height, theme);
    free_metric_data(data);
    if (theme_query) free(theme_query);

    if (!svg) {
        return create_error_result("Failed to generate SVG");
    }

    handler_result_t *result = malloc(sizeof(handler_result_t));
    if (!result) {
        free(svg);
        return create_error_result("Out of memory");
    }

    result->data = svg;
    result->data_len = strlen(svg);
    result->is_json = 0;
    result->status = 0;
    return result;
}

/**
 * Free handler result
 */
void handler_result_free(handler_result_t *result) {
    if (!result) return;
    if (result->data) free(result->data);
    free(result);
}
