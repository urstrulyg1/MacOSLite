#include "ml/cache.h"

static void unlink_entry(ml_cache *c, ml_cache_entry *e)
{
    if (e->prev) e->prev->next = e->next; else c->head = e->next;
    if (e->next) e->next->prev = e->prev; else c->tail = e->prev;
    e->next = e->prev = NULL;
}
static void push_front(ml_cache *c, ml_cache_entry *e)
{
    e->prev = NULL;
    e->next = c->head;
    if (c->head) c->head->prev = e;
    c->head = e;
    if (!c->tail) c->tail = e;
}
static void entry_destroy(ml_cache *c, ml_cache_entry *e)
{
    unlink_entry(c, e);
    c->n--;
    c->bytes -= ML_MIN(c->bytes, e->bytes);
    if (c->freefn && e->value) c->freefn(e->value);
    ml_free(e->key);
    ml_free(e);
}

ml_cache *ml_cache_new(size_t max_bytes, size_t max_items, ml_cache_free_fn freefn)
{
    ml_cache *c = ml_zalloc(sizeof *c);
    c->max_bytes = max_bytes ? max_bytes : (size_t)8 * 1024 * 1024;
    c->max_items = max_items ? max_items : 1024;
    c->freefn = freefn;
    return c;
}
void ml_cache_destroy(ml_cache *c)
{
    if (!c) return;
    ml_cache_clear(c);
    ml_free(c);
}
void *ml_cache_get(ml_cache *c, const char *key)
{
    for (ml_cache_entry *e = c->head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            e->last_use = ++c->clock;
            unlink_entry(c, e);
            push_front(c, e);
            c->hits++;
            return e->value;
        }
    }
    c->misses++;
    return NULL;
}
void ml_cache_put(ml_cache *c, const char *key, void *value, size_t bytes)
{
    for (ml_cache_entry *e = c->head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            if (c->freefn && e->value) c->freefn(e->value);
            c->bytes -= ML_MIN(c->bytes, e->bytes);
            e->value = value;
            e->bytes = bytes;
            e->last_use = ++c->clock;
            unlink_entry(c, e);
            push_front(c, e);
            c->bytes += bytes;
            goto trim;
        }
    }
    ml_cache_entry *e = ml_zalloc(sizeof *e);
    e->key = ml_strdup(key);
    e->value = value;
    e->bytes = bytes;
    e->last_use = ++c->clock;
    push_front(c, e);
    c->n++;
    c->bytes += bytes;
trim:
    while ((c->bytes > c->max_bytes || c->n > c->max_items) && c->tail) {
        entry_destroy(c, c->tail);
        c->evictions++;
    }
}
bool ml_cache_remove(ml_cache *c, const char *key)
{
    for (ml_cache_entry *e = c->head; e; e = e->next)
        if (strcmp(e->key, key) == 0) { entry_destroy(c, e); return true; }
    return false;
}
void ml_cache_clear(ml_cache *c)
{
    while (c->tail) entry_destroy(c, c->tail);
    c->head = c->tail = NULL;
    c->n = 0;
    c->bytes = 0;
}
size_t ml_cache_bytes(const ml_cache *c) { return c->bytes; }
size_t ml_cache_count(const ml_cache *c) { return c->n; }
void ml_cache_stats(const ml_cache *c, uint64_t *hits, uint64_t *misses, uint64_t *ev, size_t *bytes)
{
    if (hits) *hits = c->hits;
    if (misses) *misses = c->misses;
    if (ev) *ev = c->evictions;
    if (bytes) *bytes = c->bytes;
}
double ml_cache_hit_ratio(const ml_cache *c)
{
    uint64_t t = c->hits + c->misses;
    return t ? (double)c->hits / (double)t : 0.0;
}
