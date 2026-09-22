/* MacLiteOS core — common types & helpers.
 *
 * Design rules for this codebase (see docs/ARCHITECTURE.md):
 *   - C11, no mandatory external dependencies (libc + libm + pthread only).
 *   - No allocation in the render/input hot path.
 *   - No polling loops: everything is epoll/timerfd/inotify driven.
 *   - Every cache has an explicit byte budget.
 */
#ifndef ML_COMMON_H
#define ML_COMMON_H

#define _GNU_SOURCE 1
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#define ML_NAME        "MacLiteOS"
#define ML_NAME_LOWER  "macliteos"
#define ML_VERSION     "0.1.0"

#ifndef ML_ARRAY_SIZE
#define ML_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define ML_MIN(a, b) ((a) < (b) ? (a) : (b))
#define ML_MAX(a, b) ((a) > (b) ? (a) : (b))
#define ML_CLAMP(v, lo, hi) (ML_MIN(ML_MAX((v), (lo)), (hi)))
#define ML_UNUSED __attribute__((unused))
#define ML_PRINTF_LIKE(f, a) __attribute__((format(printf, f, a)))

/* ---- time (monotonic; never wall clock for animation) ------------------- */
static inline uint64_t ml_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static inline double ml_now_s(void) { return (double)ml_now_ns() * 1e-9; }
static inline uint64_t ml_wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

/* ---- allocation: fail fast, never limp on with NULL --------------------- */
void *ml_alloc(size_t n);
void *ml_zalloc(size_t n);
void *ml_realloc(void *p, size_t n);
char *ml_strdup(const char *s);
char *ml_strdupf(const char *fmt, ...) ML_PRINTF_LIKE(1, 2);
void ml_free(void *p);

/* ---- geometry ---------------------------------------------------------- */
typedef struct { int x, y, w, h; } ml_rect;
typedef struct { int w, h; } ml_size;
typedef struct { int x, y; } ml_point;
typedef struct { double x, y; } ml_pointd;
typedef struct { uint8_t r, g, b, a; } ml_color;

static inline ml_rect ml_rect_make(int x, int y, int w, int h)
{
    ml_rect r = { x, y, w, h }; return r;
}
static inline bool ml_rect_empty(ml_rect r) { return r.w <= 0 || r.h <= 0; }
static inline int ml_rect_right(ml_rect r) { return r.x + r.w; }
static inline int ml_rect_bottom(ml_rect r) { return r.y + r.h; }
static inline bool ml_rect_eq(ml_rect a, ml_rect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
static inline bool ml_rect_contains(ml_rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static inline ml_rect ml_rect_intersect(ml_rect a, ml_rect b)
{
    int x1 = ML_MAX(a.x, b.x), y1 = ML_MAX(a.y, b.y);
    int x2 = ML_MIN(ml_rect_right(a), ml_rect_right(b));
    int y2 = ML_MIN(ml_rect_bottom(a), ml_rect_bottom(b));
    return ml_rect_make(x1, y1, ML_MAX(0, x2 - x1), ML_MAX(0, y2 - y1));
}
static inline bool ml_rect_intersects(ml_rect a, ml_rect b)
{
    return !ml_rect_empty(ml_rect_intersect(a, b));
}
static inline bool ml_rect_contains_rect(ml_rect a, ml_rect b)
{
    return b.x >= a.x && b.y >= a.y && b.x + b.w <= a.x + a.w && b.y + b.h <= a.y + a.h;
}
static inline ml_rect ml_rect_union_bounds(ml_rect a, ml_rect b)
{
    if (ml_rect_empty(a)) return b;
    if (ml_rect_empty(b)) return a;
    int x1 = ML_MIN(a.x, b.x), y1 = ML_MIN(a.y, b.y);
    int x2 = ML_MAX(ml_rect_right(a), ml_rect_right(b));
    int y2 = ML_MAX(ml_rect_bottom(a), ml_rect_bottom(b));
    return ml_rect_make(x1, y1, x2 - x1, y2 - y1);
}
static inline int64_t ml_rect_area(ml_rect r) { return (int64_t)r.w * r.h; }

static inline ml_color ml_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    ml_color c = { r, g, b, a }; return c;
}
static inline ml_color ml_rgb(uint8_t r, uint8_t g, uint8_t b) { return ml_rgba(r, g, b, 255); }
uint32_t ml_color_u32(ml_color c);              /* 0xAARRGGBB */
ml_color ml_color_from_u32(uint32_t v);
ml_color ml_color_mix(ml_color a, ml_color b, double t);
bool ml_color_parse(const char *s, ml_color *out); /* #rgb #rrggbb #rrggbbaa name */

/* ---- generic dynamic array (macro-generated, type safe) ---------------- */
#define ML_VEC_DECL(T, name)                                                   \
    typedef struct { T *v; size_t n, cap; } name;                              \
    void name##_init(name *a);                                                 \
    void name##_free(name *a);                                                 \
    void name##_reserve(name *a, size_t cap);                                  \
    T *name##_push(name *a, T val);                                            \
    void name##_remove(name *a, size_t i);                                     \
    void name##_clear(name *a)

#define ML_VEC_IMPL(T, name)                                                   \
    void name##_init(name *a) { a->v = NULL; a->n = a->cap = 0; }              \
    void name##_free(name *a) { free(a->v); a->v = NULL; a->n = a->cap = 0; }  \
    void name##_clear(name *a) { a->n = 0; }                                   \
    void name##_reserve(name *a, size_t cap)                                   \
    {                                                                          \
        if (cap <= a->cap) return;                                             \
        size_t nc = a->cap ? a->cap : 8;                                       \
        while (nc < cap) nc *= 2;                                              \
        a->v = realloc(a->v, nc * sizeof(T));                                  \
        if (!a->v) { fputs("ml: out of memory\n", stderr); abort(); }          \
        a->cap = nc;                                                           \
    }                                                                          \
    T *name##_push(name *a, T val)                                             \
    {                                                                          \
        name##_reserve(a, a->n + 1);                                           \
        a->v[a->n] = val;                                                      \
        return &a->v[a->n++];                                                  \
    }                                                                          \
    void name##_remove(name *a, size_t i)                                      \
    {                                                                          \
        if (i >= a->n) return;                                                 \
        memmove(&a->v[i], &a->v[i + 1], (a->n - i - 1) * sizeof(T));           \
        a->n--;                                                                \
    }

#endif /* ML_COMMON_H */
