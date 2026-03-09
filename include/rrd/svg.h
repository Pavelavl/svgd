/**
 * @file svg.h
 * @brief SVG generation module using Duktape JavaScript engine
 *
 * Provides thread-safe SVG generation with cached JavaScript contexts.
 */

#ifndef SVGD_RRD_SVG_H
#define SVGD_RRD_SVG_H

#include "reader.h"  /* For MetricData definition */

/**
 * Initialize the JavaScript cache
 * @param filename Path to JavaScript file (e.g., generate_svg.js)
 * @return 0 on success, -1 on error
 */
int svg_init_cache(const char *filename);

/**
 * Free the JavaScript cache
 */
void svg_free_cache(void);

/**
 * Pre-warm thread-local context for current thread
 * Call this at worker thread startup to avoid first-request latency
 */
void svg_prewarm_context(void);

/**
 * Generate SVG from metric data
 * @param script_path Path to JavaScript file
 * @param data Metric data to render
 * @return Allocated SVG string (caller must free), or NULL on error
 */
char* svg_generate(const char *script_path, MetricData *data);

/* Compatibility aliases */
#define generate_svg(ctx, path, data) svg_generate(path, data)
#define init_js_cache svg_init_cache
#define free_js_cache svg_free_cache
#define prewarm_thread_context svg_prewarm_context

#endif /* SVGD_RRD_SVG_H */
