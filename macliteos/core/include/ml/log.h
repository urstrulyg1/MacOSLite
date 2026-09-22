#ifndef ML_LOG_H
#define ML_LOG_H
#include "ml/common.h"

typedef enum { ML_LOG_ERROR = 0, ML_LOG_WARN, ML_LOG_INFO, ML_LOG_DEBUG, ML_LOG_TRACE } ml_loglevel;

void ml_log_init(const char *progname, ml_loglevel level, const char *path_or_null);
void ml_log_set_level(ml_loglevel l);
ml_loglevel ml_log_level(void);
void ml_log(ml_loglevel l, const char *fmt, ...) ML_PRINTF_LIKE(2, 3);
/* Rate limited: at most one message per `per_ms` for a given key. Used by the
 * compositor so a broken client cannot flood the log. */
void ml_log_ratelimit(ml_loglevel l, const char *key, uint64_t per_ms, const char *fmt, ...) ML_PRINTF_LIKE(4, 5);
void ml_log_close(void);

#define ML_ERR(...)  ml_log(ML_LOG_ERROR, __VA_ARGS__)
#define ML_WARN(...) ml_log(ML_LOG_WARN,  __VA_ARGS__)
#define ML_INFO(...) ml_log(ML_LOG_INFO,  __VA_ARGS__)
#define ML_DBG(...)  ml_log(ML_LOG_DEBUG, __VA_ARGS__)
#endif
