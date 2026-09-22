#include "ml/log.h"
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>

static struct {
    ml_loglevel level;
    FILE *fp;
    char prog[64];
    pthread_mutex_t lock;
    struct { char key[48]; uint64_t last; uint32_t suppressed; } rl[16];
    size_t rl_n;
} L = { ML_LOG_INFO, NULL, { 0 }, PTHREAD_MUTEX_INITIALIZER, { { { 0 }, 0, 0 } }, 0 };

static const char *lvlname(ml_loglevel l)
{
    switch (l) {
    case ML_LOG_ERROR: return "E";
    case ML_LOG_WARN:  return "W";
    case ML_LOG_INFO:  return "I";
    case ML_LOG_DEBUG: return "D";
    default:           return "T";
    }
}

void ml_log_init(const char *progname, ml_loglevel level, const char *path_or_null)
{
    snprintf(L.prog, sizeof L.prog, "%s", progname ? progname : "ml");
    L.level = level;
    if (path_or_null && *path_or_null) {
        FILE *f = fopen(path_or_null, "ae");
        if (f) { L.fp = f; return; }
    }
    L.fp = stderr;
}

void ml_log_set_level(ml_loglevel l) { L.level = l; }
ml_loglevel ml_log_level(void) { return L.level; }

void ml_log_close(void)
{
    if (L.fp && L.fp != stderr) fclose(L.fp);
    L.fp = NULL;
}

void ml_log(ml_loglevel l, const char *fmt, ...)
{
    if (l > L.level) return;
    FILE *fp = L.fp ? L.fp : stderr;
    pthread_mutex_lock(&L.lock);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    fprintf(fp, "%02d:%02d:%02d.%03d %s %-14s ", tm.tm_hour, tm.tm_min, tm.tm_sec,
            (int)(ts.tv_nsec / 1000000), lvlname(l), L.prog);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fflush(fp);
    pthread_mutex_unlock(&L.lock);
}

void ml_log_ratelimit(ml_loglevel l, const char *key, uint64_t per_ms, const char *fmt, ...)
{
    if (l > L.level) return;
    uint64_t now = ml_wall_ms();
    pthread_mutex_lock(&L.lock);
    for (size_t i = 0; i < L.rl_n; i++) {
        if (strncmp(L.rl[i].key, key, sizeof L.rl[i].key) == 0) {
            if (now - L.rl[i].last < per_ms) { L.rl[i].suppressed++; pthread_mutex_unlock(&L.lock); return; }
            uint32_t sup = L.rl[i].suppressed;
            L.rl[i].suppressed = 0;
            L.rl[i].last = now;
            FILE *fp = L.fp ? L.fp : stderr;
            va_list ap; va_start(ap, fmt);
            if (sup) fprintf(fp, "(%u similar suppressed) ", sup);
            vfprintf(fp, fmt, ap);
            va_end(ap);
            fputc('\n', fp);
            fflush(fp);
            pthread_mutex_unlock(&L.lock);
            return;
        }
    }
    if (L.rl_n < ML_ARRAY_SIZE(L.rl)) {
        snprintf(L.rl[L.rl_n].key, sizeof L.rl[0].key, "%s", key);
        L.rl[L.rl_n].last = now;
        L.rl[L.rl_n].suppressed = 0;
        L.rl_n++;
    }
    pthread_mutex_unlock(&L.lock);
    {
        FILE *fp = L.fp ? L.fp : stderr;
        pthread_mutex_lock(&L.lock);
        va_list ap; va_start(ap, fmt);
        vfprintf(fp, fmt, ap);
        va_end(ap);
        fputc('\n', fp);
        fflush(fp);
        pthread_mutex_unlock(&L.lock);
    }
}
