/**
 * @file cache.c
 * @brief RRD data caching implementation
 */

#include "../include/rrd/cache.h"
#include "../include/rrd/reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define CACHE_MAX_ENTRIES 64
#define CACHE_KEY_SIZE 512

/* Cache entry structure */
typedef struct cache_entry {
    char key[CACHE_KEY_SIZE];
    MetricData *data;
    time_t expires_at;
    struct cache_entry *next;
} cache_entry_t;

/* Global cache state */
static cache_entry_t *cache_slots[CACHE_MAX_ENTRIES] = {0};
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static int cache_ttl_seconds = 5;
static int cache_initialized = 0;

/* Simple DJB2 hash function */
static unsigned int cache_hash(const char *key) {
    unsigned int hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % CACHE_MAX_ENTRIES;
}

/* Build cache key from rrd_path and period */
static void build_cache_key(char *buf, size_t size, const char *rrd_path, int period) {
    snprintf(buf, size, "%s:%d", rrd_path, period);
}

/* Clone MetricData for thread-safe return */
static MetricData* clone_metric_data(MetricData *src) {
    if (!src) return NULL;

    MetricData *dst = malloc(sizeof(MetricData));
    if (!dst) return NULL;

    dst->series_count = src->series_count;
    dst->series_names = calloc(dst->series_count, sizeof(char*));
    dst->series_data = calloc(dst->series_count, sizeof(DataPoint*));
    dst->series_counts = calloc(dst->series_count, sizeof(int));
    dst->param1 = strdup(src->param1 ? src->param1 : "");
    dst->metric_config = src->metric_config;

    if (dst->series_count > 0 && (!dst->series_names || !dst->series_data || !dst->series_counts)) {
        free(dst->series_names);
        free(dst->series_data);
        free(dst->series_counts);
        free(dst->param1);
        free(dst);
        return NULL;
    }

    int i;
    for (i = 0; i < dst->series_count; i++) {
        if (!src->series_names[i]) goto clone_fail;
        dst->series_names[i] = strdup(src->series_names[i]);
        if (!dst->series_names[i]) goto clone_fail;
        dst->series_counts[i] = src->series_counts[i];
        dst->series_data[i] = malloc(dst->series_counts[i] * sizeof(DataPoint));
        if (!dst->series_data[i]) {
            free(dst->series_names[i]);
            goto clone_fail;
        }
        memcpy(dst->series_data[i], src->series_data[i],
               dst->series_counts[i] * sizeof(DataPoint));
    }

    return dst;

clone_fail:
    for (int j = 0; j < i; j++) {
        free(dst->series_names[j]);
        free(dst->series_data[j]);
    }
    free(dst->series_names);
    free(dst->series_data);
    free(dst->series_counts);
    free(dst->param1);
    free(dst);
    return NULL;
}

void rrd_cache_init(int ttl_seconds) {
    pthread_mutex_lock(&cache_mutex);
    cache_ttl_seconds = ttl_seconds > 0 ? ttl_seconds : 5;
    cache_initialized = 1;
    pthread_mutex_unlock(&cache_mutex);
}

MetricData* rrd_cache_get(const char *rrd_path, int period) {
    if (!cache_initialized || !rrd_path) return NULL;

    char key[CACHE_KEY_SIZE];
    build_cache_key(key, sizeof(key), rrd_path, period);
    unsigned int slot = cache_hash(key);

    pthread_mutex_lock(&cache_mutex);
    cache_entry_t *entry = cache_slots[slot];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (entry->expires_at > time(NULL)) {
                /* Cache hit - return clone for thread safety */
                MetricData *result = clone_metric_data(entry->data);
                pthread_mutex_unlock(&cache_mutex);
                return result;
            }
            /* Expired entry - will be replaced on next put */
            break;
        }
        entry = entry->next;
    }

    pthread_mutex_unlock(&cache_mutex);
    return NULL;
}

void rrd_cache_put(const char *rrd_path, int period, MetricData *data) {
    if (!cache_initialized || !data || !rrd_path) return;

    char key[CACHE_KEY_SIZE];
    build_cache_key(key, sizeof(key), rrd_path, period);
    unsigned int slot = cache_hash(key);

    pthread_mutex_lock(&cache_mutex);

    /* Look for existing entry */
    cache_entry_t *entry = cache_slots[slot];
    cache_entry_t *prev = NULL;

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            /* Update existing entry */
            free_metric_data(entry->data);
            entry->data = data;
            entry->expires_at = time(NULL) + cache_ttl_seconds;
            pthread_mutex_unlock(&cache_mutex);
            return;
        }
        prev = entry;
        entry = entry->next;
    }

    /* Create new entry */
    entry = malloc(sizeof(cache_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&cache_mutex);
        return;
    }

    strncpy(entry->key, key, CACHE_KEY_SIZE - 1);
    entry->key[CACHE_KEY_SIZE - 1] = '\0';
    entry->data = data;
    entry->expires_at = time(NULL) + cache_ttl_seconds;
    entry->next = NULL;

    if (prev) {
        prev->next = entry;
    } else {
        cache_slots[slot] = entry;
    }

    pthread_mutex_unlock(&cache_mutex);
}

void rrd_cache_free(void) {
    pthread_mutex_lock(&cache_mutex);

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        cache_entry_t *entry = cache_slots[i];
        while (entry) {
            cache_entry_t *next = entry->next;
            if (entry->data) free_metric_data(entry->data);
            free(entry);
            entry = next;
        }
        cache_slots[i] = NULL;
    }

    cache_initialized = 0;
    pthread_mutex_unlock(&cache_mutex);
}
