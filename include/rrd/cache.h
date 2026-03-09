/**
 * @file cache.h
 * @brief RRD data caching module
 *
 * Provides thread-safe caching of fetched RRD data to reduce I/O operations.
 * Uses hash table with TTL-based expiration.
 */

#ifndef SVGD_RRD_CACHE_H
#define SVGD_RRD_CACHE_H

#include <stddef.h>
#include "reader.h"  /* For MetricData definition */

/**
 * Initialize the RRD cache
 * @param ttl_seconds Time-to-live for cached entries (0 = default 5s)
 */
void rrd_cache_init(int ttl_seconds);

/**
 * Get cached data for RRD path and period
 * @param rrd_path Full path to RRD file
 * @param period Time period in seconds
 * @return Cloned MetricData (caller must free with rrd_data_free), or NULL if not cached/expired
 */
MetricData* rrd_cache_get(const char *rrd_path, int period);

/**
 * Store data in cache
 * @param rrd_path Full path to RRD file
 * @param period Time period in seconds
 * @param data Data to cache (cache takes ownership)
 */
void rrd_cache_put(const char *rrd_path, int period, MetricData *data);

/**
 * Free the RRD cache and all stored data
 */
void rrd_cache_free(void);

/* Compatibility aliases for existing code */
#define cache_get rrd_cache_get
#define cache_put rrd_cache_put
#define init_rrd_cache rrd_cache_init
#define free_rrd_cache rrd_cache_free

#endif /* SVGD_RRD_CACHE_H */
