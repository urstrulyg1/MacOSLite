#ifndef ML_UTIL_H
#define ML_UTIL_H
#include "ml/common.h"

/* file / path helpers */
bool ml_file_exists(const char *path);
bool ml_is_dir(const char *path);
char *ml_read_file(const char *path, size_t *out_len);      /* NUL-terminated, NULL on error */
bool ml_write_file(const char *path, const void *data, size_t len);
bool ml_mkdirs(const char *path, int mode);
char *ml_path_join(const char *a, const char *b);
const char *ml_path_base(const char *p);
char *ml_path_dir(const char *p);
char *ml_expand_home(const char *p);
const char *ml_home(void);
char *ml_config_dir(void);       /* $XDG_CONFIG_HOME or ~/.config/macliteos */
char *ml_cache_dir(void);
char *ml_data_dir(void);
char *ml_runtime_dir(void);      /* $XDG_RUNTIME_DIR or /tmp/macliteos-<uid> */
char *ml_find_in_path(const char *exe);

/* read a single value out of sysfs/procfs; returns def on any failure */
long ml_sysfs_long(const char *path, long def);
char *ml_sysfs_str(const char *path, const char *def);
bool ml_sysfs_write(const char *path, const char *value);

/* string helpers */
bool ml_str_endswith(const char *s, const char *suf);
bool ml_str_startswith(const char *s, const char *pre);
char *ml_str_trim(char *s);
int ml_str_ieq(const char *a, const char *b);
char **ml_str_split(const char *s, char sep, int *out_n);

/* growable string builder (no per-append allocation storms) */
typedef struct { char *p; size_t n, cap; } ml_str;
void ml_str_init(ml_str *s);
void ml_str_free(ml_str *s);
void ml_str_reset(ml_str *s);
void ml_str_reserve(ml_str *s, size_t extra);
void ml_str_append(ml_str *s, const char *txt);
void ml_str_appendf(ml_str *s, const char *fmt, ...) ML_PRINTF_LIKE(2, 3);
void ml_str_append_char(ml_str *s, char c);

/* monotonic deadline helper used by benchmarks */
double ml_elapsed_ms(uint64_t start_ns);

/* human readable bytes */
void ml_format_bytes(uint64_t bytes, char *out, size_t outlen);
#endif
