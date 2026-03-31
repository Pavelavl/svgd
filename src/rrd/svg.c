/**
 * @file svg.c
 * @brief SVG generation using Duktape JavaScript engine
 */

#include "../include/rrd/svg.h"
#include "../include/cfg.h"
#include <duktape.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/* Static cache for JavaScript file contents */
static char *js_cache = NULL;
static long js_cache_len = 0;
static volatile int js_cache_initialized = 0;
static pthread_key_t js_context_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

/* Verbose logging accessor - declared in cfg.h, implemented in main.c */
extern int is_verbose_logging(void);

/* Initialize thread-local context key (called once) */
static void make_context_key(void) {
    pthread_key_create(&js_context_key, (void (*)(void*))duk_destroy_heap);
}

/* Get or create thread-local Duktape context */
static duk_context* get_thread_context(void) {
    pthread_once(&key_once, make_context_key);

    duk_context *ctx = pthread_getspecific(js_context_key);
    if (!ctx) {
        ctx = duk_create_heap_default();
        if (!ctx) return NULL;
        pthread_setspecific(js_context_key, ctx);

        /* Pre-load JS cache if available */
        if (js_cache_initialized && js_cache) {
            if (duk_peval_string(ctx, js_cache) != 0) {
                duk_destroy_heap(ctx);
                pthread_setspecific(js_context_key, NULL);
                return NULL;
            }
            duk_pop(ctx);
        }
    }
    return ctx;
}

int svg_init_cache(const char *filename) {
    if (js_cache_initialized) return 0;

    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    js_cache_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    js_cache = malloc(js_cache_len + 1);
    if (!js_cache) {
        fclose(f);
        return -1;
    }

    size_t read_bytes = fread(js_cache, 1, js_cache_len, f);
    js_cache[js_cache_len] = '\0';
    fclose(f);

    if (read_bytes != (size_t)js_cache_len) {
        free(js_cache);
        js_cache = NULL;
        js_cache_len = 0;
        return -1;
    }

    js_cache_initialized = 1;
    return 0;
}

void svg_free_cache(void) {
    if (js_cache) {
        free(js_cache);
        js_cache = NULL;
        js_cache_len = 0;
        js_cache_initialized = 0;
    }
}

void svg_prewarm_context(void) {
    if (!js_cache_initialized) return;
    get_thread_context();
}

char* svg_generate(const char *script_path, MetricData *data, int width, int height) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Initialize cache if needed */
    if (!js_cache_initialized && svg_init_cache(script_path) != 0) return NULL;

    duk_context *ctx = get_thread_context();
    if (!ctx) return NULL;

    /* Find generateSVG function */
    duk_push_global_object(ctx);
    if (!duk_get_prop_string(ctx, -1, "generateSVG")) {
        duk_pop_2(ctx);
        return NULL;
    }
    duk_remove(ctx, -2);

    /* Build series array */
    duk_push_array(ctx);
    int array_idx = 0;
    for (int s = 0; s < data->series_count; s++) {
        if (data->series_counts[s] == 0) continue;

        duk_push_object(ctx);
        duk_push_string(ctx, data->series_names[s]);
        duk_put_prop_string(ctx, -2, "name");

        duk_push_array(ctx);
        for (int i = 0; i < data->series_counts[s]; i++) {
            duk_push_object(ctx);
            duk_push_number(ctx, (double)data->series_data[s][i].timestamp);
            duk_put_prop_string(ctx, -2, "timestamp");
            duk_push_number(ctx, data->series_data[s][i].value);
            duk_put_prop_string(ctx, -2, "value");
            duk_put_prop_index(ctx, -2, i);
        }
        duk_put_prop_string(ctx, -2, "data");
        duk_put_prop_index(ctx, -2, array_idx++);
    }

    /* Build options object */
    duk_push_object(ctx);
    if (data->param1 && *data->param1) {
        duk_push_string(ctx, data->param1);
        duk_put_prop_string(ctx, -2, "param1");
    }

    /* Add metric configuration */
    if (data->metric_config) {
        MetricConfig *cfg = data->metric_config;

        duk_push_string(ctx, cfg->title);
        duk_put_prop_string(ctx, -2, "title");
        duk_push_string(ctx, cfg->y_label);
        duk_put_prop_string(ctx, -2, "yLabel");
        duk_push_boolean(ctx, cfg->is_percentage);
        duk_put_prop_string(ctx, -2, "isPercentage");
        duk_push_string(ctx, cfg->transform_type);
        duk_put_prop_string(ctx, -2, "transformType");
        duk_push_number(ctx, cfg->value_multiplier);
        duk_put_prop_string(ctx, -2, "valueMultiplier");
        duk_push_number(ctx, cfg->transform_divisor);
        duk_put_prop_string(ctx, -2, "transformDivisor");
        duk_push_string(ctx, cfg->value_format);
        duk_put_prop_string(ctx, -2, "valueFormat");
        duk_push_string(ctx, cfg->panel_type);
        duk_put_prop_string(ctx, -2, "panelType");
    }

    /* Add width/height options */
    duk_push_number(ctx, width);
    duk_put_prop_string(ctx, -2, "width");
    duk_push_number(ctx, height);
    duk_put_prop_string(ctx, -2, "height");

    /* Call generateSVG */
    if (duk_pcall(ctx, 2) != 0) {
        fprintf(stderr, "JS Error: %s\n", duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        return NULL;
    }

    const char *svg = duk_get_string(ctx, -1);
    if (!svg) {
        duk_pop(ctx);
        return NULL;
    }

    char *result = strdup(svg);
    duk_pop(ctx);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6;

    if (is_verbose_logging()) {
        fprintf(stderr, "svg_generate took %.2f ms\n", elapsed_ms);
    }

    return result;
}
