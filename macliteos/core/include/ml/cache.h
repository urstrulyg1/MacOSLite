#ifndef ML_CACHE_H
#define ML_CACHE_H
#include "ml/common.h"

/* Bounded LRU cache. Spec §44: no cache in MacLiteOS is allowed to be
 * unbounded. Callers give a byte budget and (optionally) an item budget; the
 * cache evicts least-recently-used entries until it fits. */
typedef void (*ml_cache_free_fn)(void *value);

typedef struct ml_cache_entry {
    char *key;
    void *value;
    size_t bytes;
    uint64_t last_use;
    struct ml_cache_entry *next, *prev;
} ml_cache_entry;

typedef struct {
    ml_cache_entry *head, *tail;   /* head = MRU */
    size_t n;
    size_t bytes;
    size_t max_bytes;
    size_t max_items;
    uint64_t clock;
    uint64_t hits, misses, evictions;
    ml_cache_free_fn freefn;
} ml_cache;

ml_cache *ml_cache_new(size_t max_bytes, size_t max_items, ml_cache_free_fn freefn);
void ml_cache_destroy(ml_cache *c);
void *ml_cache_get(ml_cache *c, const char *key);          /* NULL if absent */
void ml_cache_put(ml_cache *c, const char *key, void *value, size_t bytes);
bool ml_cache_remove(ml_cache *c, const char *key);
void ml_cache_clear(ml_cache *c);
size_t ml_cache_bytes(const ml_cache *c);
size_t ml_cache_count(const ml_cache *c);
void ml_cache_stats(const ml_cache *c, uint64_t *hits, uint64_t *misses, uint64_t *evictions, size_t *bytes);
double ml_cache_hit_ratio(const ml_cache *c);
#endif
