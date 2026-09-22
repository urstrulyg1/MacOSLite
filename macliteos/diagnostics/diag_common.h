#ifndef MICA_DIAG_COMMON_H
#define MICA_DIAG_COMMON_H
#include "ml/common.h"
#include "ml/log.h"
#include "ml/util.h"
#include "../hardware/hwcap.h"
#include "../hardware/hwprobe.h"
#include "../hardware/backlight.h"
#include "../hardware/audio.h"
#include "../hardware/media.h"
#include "../hardware/kms.h"
#include "../hardware/edid.h"
#include <unistd.h>
#include <stdarg.h>

/* Shared plumbing for the maclite-* hardware diagnostics.
 *
 * Exit codes (documented in docs/testing.md, asserted by tests/test_hardware.c):
 *   0  every required check PASSED
 *   1  a required check FAILED
 *   2  a required check could NOT be exercised on this host (NOT TESTED)
 *   3  the hardware/backend is absent here (UNSUPPORTED)
 *   4  bad arguments
 */

static inline void diag_title(const char *t)
{
    printf("%s\n", t);
    if (hw_using_fixture())
        printf("(reading fixtures: sysfs=%s proc=%s dev=%s — this is NOT real hardware)\n",
               hw_sysfs_root(), hw_proc_root(), hw_dev_root());
    printf("\n");
}
static inline void diag_section(const char *t) { printf("%s:\n", t); }
static inline void diag_kv(const char *k, const char *fmt, ...) ML_PRINTF_LIKE(2, 3);
static inline void diag_kv(const char *k, const char *fmt, ...)
{
    char v[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v, sizeof v, fmt, ap);
    va_end(ap);
    printf("  %-26s %s\n", k, v);
}
static inline void diag_res(const char *k, hw_result r, const char *fmt, ...) ML_PRINTF_LIKE(3, 4);
static inline void diag_res(const char *k, hw_result r, const char *fmt, ...)
{
    char v[256] = "";
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(v, sizeof v, fmt, ap);
        va_end(ap);
    }
    printf("  %-26s %-10s %s\n", k, hw_result_str(r), v);
}
static inline void diag_note(const char *fmt, ...) ML_PRINTF_LIKE(1, 2);
static inline void diag_note(const char *fmt, ...)
{
    printf("  ");
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}
static inline bool diag_flag(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], flag)) return true;
    return false;
}
static inline const char *diag_opt(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc - 1; i++) if (!strcmp(argv[i], flag)) return argv[i + 1];
    return NULL;
}
static inline const char *diag_yn(bool b) { return b ? "YES" : "NO"; }
static inline const char *or_dash(const char *s) { return s && s[0] ? s : "-"; }
#endif
