#include "ml/util.h"
#include "ml/log.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

void *ml_alloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fputs("ml: out of memory\n", stderr); abort(); }
    return p;
}
void *ml_zalloc(size_t n) { void *p = ml_alloc(n); memset(p, 0, n); return p; }
void *ml_realloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fputs("ml: out of memory\n", stderr); abort(); }
    return q;
}
void ml_free(void *p) { free(p); }
char *ml_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = ml_alloc(n);
    memcpy(p, s, n);
    return p;
}
char *ml_strdupf(const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return ml_strdup(""); }
    char *buf = ml_alloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return buf;
}

uint32_t ml_color_u32(ml_color c) { return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b; }
ml_color ml_color_from_u32(uint32_t v)
{
    return ml_rgba((uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v, (uint8_t)(v >> 24));
}
ml_color ml_color_mix(ml_color a, ml_color b, double t)
{
    t = ML_CLAMP(t, 0.0, 1.0);
    return ml_rgba((uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t),
                   (uint8_t)(a.b + (b.b - a.b) * t), (uint8_t)(a.a + (b.a - a.a) * t));
}

static struct { const char *name; uint32_t v; } named_colors[] = {
    { "black", 0xff000000 }, { "white", 0xffffffff }, { "red", 0xffff0000 },
    { "green", 0xff00c800 }, { "blue", 0xff0000ff }, { "gray", 0xff808080 },
    { "grey", 0xff808080 }, { "clear", 0x00000000 }, { "transparent", 0x00000000 },
};

bool ml_color_parse(const char *s, ml_color *out)
{
    if (!s) return false;
    while (*s == ' ') s++;
    if (*s == '#') {
        s++;
        size_t n = strlen(s);
        unsigned long v = strtoul(s, NULL, 16);
        if (n == 6) { *out = ml_color_from_u32(0xff000000u | (uint32_t)v); return true; }
        if (n == 8) { *out = ml_color_from_u32((uint32_t)v); return true; }
        if (n == 3) {
            unsigned r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
            *out = ml_rgba(r * 17, g * 17, b * 17, 255);
            return true;
        }
        return false;
    }
    for (size_t i = 0; i < ML_ARRAY_SIZE(named_colors); i++)
        if (strcasecmp(s, named_colors[i].name) == 0) { *out = ml_color_from_u32(named_colors[i].v); return true; }
    return false;
}

bool ml_file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}
bool ml_is_dir(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

char *ml_read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, n = 0;
    char *buf = ml_alloc(cap);
    for (;;) {
        if (n + 1024 > cap) { cap *= 2; buf = ml_realloc(buf, cap); }
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        n += r;
        if (r == 0) break;
    }
    fclose(f);
    buf[n] = 0;
    if (out_len) *out_len = n;
    return buf;
}

bool ml_write_file(const char *path, const void *data, size_t len)
{
    char *tmp = ml_strdupf("%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { ml_free(tmp); return false; }
    bool ok = fwrite(data, 1, len, f) == len && fflush(f) == 0;
    fclose(f);
    if (ok) ok = rename(tmp, path) == 0;
    if (!ok) unlink(tmp);
    ml_free(tmp);
    return ok;
}

bool ml_mkdirs(const char *path, int mode)
{
    if (!path || !*path) return false;
    char *p = ml_strdup(path);
    bool ok = true;
    for (char *s = p + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            if (mkdir(p, mode) != 0 && errno != EEXIST) { ok = false; break; }
            *s = '/';
        }
    }
    if (ok && mkdir(p, mode) != 0 && errno != EEXIST) ok = false;
    ml_free(p);
    return ok;
}

char *ml_path_join(const char *a, const char *b)
{
    if (!a || !*a) return ml_strdup(b ? b : "");
    if (!b || !*b) return ml_strdup(a);
    size_t la = strlen(a);
    bool slash = a[la - 1] == '/';
    return ml_strdupf("%s%s%s", a, slash ? "" : "/", (*b == '/' && slash) ? b + 1 : b);
}
const char *ml_path_base(const char *p)
{
    if (!p) return "";
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}
char *ml_path_dir(const char *p)
{
    if (!p) return ml_strdup(".");
    const char *s = strrchr(p, '/');
    if (!s) return ml_strdup(".");
    if (s == p) return ml_strdup("/");
    return strndup(p, (size_t)(s - p));
}
const char *ml_home(void)
{
    const char *h = getenv("HOME");
    if (h && *h) return h;
    struct passwd *pw = getpwuid(getuid());
    return (pw && pw->pw_dir) ? pw->pw_dir : "/tmp";
}
char *ml_expand_home(const char *p)
{
    if (!p) return NULL;
    if (p[0] == '~' && (p[1] == '/' || p[1] == 0)) return ml_path_join(ml_home(), p + 1);
    return ml_strdup(p);
}
static char *xdg(const char *var, const char *fallback_sub)
{
    const char *v = getenv(var);
    if (v && *v) return ml_path_join(v, ML_NAME_LOWER);
    char *h = ml_path_join(ml_home(), fallback_sub);
    char *r = ml_path_join(h, ML_NAME_LOWER);
    ml_free(h);
    ml_mkdirs(r, 0700);
    return r;
}
char *ml_config_dir(void)  { return xdg("XDG_CONFIG_HOME", ".config"); }
char *ml_cache_dir(void)   { return xdg("XDG_CACHE_HOME", ".cache"); }
char *ml_data_dir(void)    { return xdg("XDG_DATA_HOME", ".local/share"); }
char *ml_runtime_dir(void)
{
    char *p = NULL;
    const char *v = getenv("XDG_RUNTIME_DIR");
    if (v && *v)
        p = ml_strdup(v);
    else
        p = ml_strdupf("/tmp/macliteos-%d", (int)getuid());
    ml_mkdirs(p, 0700);
    return p;
}
char *ml_find_in_path(const char *exe)
{
    const char *path = getenv("PATH");
    if (!path) path = "/usr/bin:/bin";
    char *dup = ml_strdup(path);
    char *found = NULL;
    for (char *tok = strtok(dup, ":"); tok; tok = strtok(NULL, ":")) {
        char *cand = ml_path_join(tok, exe);
        if (access(cand, X_OK) == 0) { found = cand; break; }
        ml_free(cand);
    }
    ml_free(dup);
    return found;
}

long ml_sysfs_long(const char *path, long def)
{
    char *s = ml_read_file(path, NULL);
    if (!s) return def;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    ml_free(s);
    if (errno || end == s) return def;
    return v;
}
char *ml_sysfs_str(const char *path, const char *def)
{
    char *s = ml_read_file(path, NULL);
    if (!s) return ml_strdup(def ? def : "");
    char *t = ml_str_trim(s);
    char *r = ml_strdup(t);
    ml_free(s);
    return r;
}
bool ml_sysfs_write(const char *path, const char *value)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    bool ok = fputs(value, f) >= 0;
    fclose(f);
    return ok;
}

bool ml_str_endswith(const char *s, const char *suf)
{
    size_t ls = strlen(s), lf = strlen(suf);
    return lf <= ls && strcmp(s + ls - lf, suf) == 0;
}
bool ml_str_startswith(const char *s, const char *pre) { return strncmp(s, pre, strlen(pre)) == 0; }
char *ml_str_trim(char *s)
{
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}
int ml_str_ieq(const char *a, const char *b) { return a && b && strcasecmp(a, b) == 0; }
char **ml_str_split(const char *s, char sep, int *out_n)
{
    int cap = 8, n = 0;
    char **arr = ml_alloc(sizeof(char *) * (size_t)cap);
    const char *p = s;
    while (p) {
        const char *q = strchr(p, sep);
        size_t len = q ? (size_t)(q - p) : strlen(p);
        if (n + 1 >= cap) { cap *= 2; arr = ml_realloc(arr, sizeof(char *) * (size_t)cap); }
        arr[n++] = strndup(p, len);
        p = q ? q + 1 : NULL;
    }
    arr[n] = NULL;
    if (out_n) *out_n = n;
    return arr;
}

void ml_str_init(ml_str *s) { s->p = NULL; s->n = s->cap = 0; }
void ml_str_free(ml_str *s) { ml_free(s->p); s->p = NULL; s->n = s->cap = 0; }
void ml_str_reset(ml_str *s) { s->n = 0; if (s->p) s->p[0] = 0; }
void ml_str_reserve(ml_str *s, size_t extra)
{
    if (s->n + extra + 1 <= s->cap) return;
    size_t cap = s->cap ? s->cap : 256;
    while (cap < s->n + extra + 1) cap *= 2;
    s->p = ml_realloc(s->p, cap);
    s->cap = cap;
}
void ml_str_append(ml_str *s, const char *txt)
{
    size_t n = strlen(txt);
    ml_str_reserve(s, n);
    memcpy(s->p + s->n, txt, n);
    s->n += n;
    s->p[s->n] = 0;
}
void ml_str_append_char(ml_str *s, char c)
{
    ml_str_reserve(s, 1);
    s->p[s->n++] = c;
    s->p[s->n] = 0;
}
void ml_str_appendf(ml_str *s, const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    ml_str_reserve(s, (size_t)n);
    vsnprintf(s->p + s->n, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    s->n += (size_t)n;
}
double ml_elapsed_ms(uint64_t start_ns) { return (double)(ml_now_ns() - start_ns) / 1e6; }

void ml_format_bytes(uint64_t bytes, char *out, size_t outlen)
{
    static const char *u[] = { "B", "KB", "MB", "GB", "TB" };
    double v = (double)bytes;
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
    if (i == 0) snprintf(out, outlen, "%llu B", (unsigned long long)bytes);
    else snprintf(out, outlen, "%.1f %s", v, u[i]);
}
