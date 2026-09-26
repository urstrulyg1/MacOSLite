#ifndef MICA_PERF_COMMON_H
#define MICA_PERF_COMMON_H
#include "ml/common.h"
#include "ml/util.h"
#include "ml/ipc.h"
#include "../compositor/proto.h"
#include <dirent.h>
#include <unistd.h>

/* Ask the running compositor for its frame statistics. Returns false when no
 * session is live (tools then report "no session"). */
static inline bool comp_stats(msg_stats *out)
{
    char *rt = ml_runtime_dir();
    char *path = ml_path_join(rt, "mica-comp.sock");
    ml_free(rt);
    int fd = mlipc_connect(path);
    ml_free(path);
    if (fd < 0) return false;
    msg_hello h = { .pid = (uint32_t)getpid() };
    snprintf(h.name, sizeof h.name, "tool:%s", ml_path_base((char *)__builtin_return_address(0) ? "maclite" : "maclite"));
    if (!mlipc_send(fd, MC_HELLO, &h, sizeof h)) { close(fd); return false; }
    uint8_t buf[512];
    uint32_t len = 0;
    uint32_t t = mlipc_recv(fd, buf, sizeof buf, &len, NULL);
    if (t != MS_WELCOME) { close(fd); return false; }
    mlipc_send(fd, MC_QUERY, NULL, 0);
    t = mlipc_recv(fd, buf, sizeof buf, &len, NULL);
    close(fd);
    if (t != MS_STATS || len < sizeof *out) return false;
    memcpy(out, buf, sizeof *out);
    return true;
}

typedef struct { unsigned long long utime, ntime, cputime_total; unsigned long long idle; } cpu_sample;
static inline void cpu_sample_take(cpu_sample *s)
{
    char *st = ml_read_file("/proc/stat", NULL);
    if (!st) { memset(s, 0, sizeof *s); return; }
    unsigned long long user, nice, sys, idle, iow, irq, sirq, steal;
    sscanf(st, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
           &user, &nice, &sys, &idle, &iow, &irq, &sirq, &steal);
    s->utime = user + nice;
    s->ntime = sys + irq + sirq + steal;
    s->idle = idle + iow;
    s->cputime_total = s->utime + s->ntime + s->idle;
    ml_free(st);
}
static inline double cpu_percent(const cpu_sample *a, const cpu_sample *b)
{
    unsigned long long dt = b->cputime_total - a->cputime_total;
    if (!dt) return 0;
    unsigned long long busy = (b->utime + b->ntime) - (a->utime + a->ntime);
    return 100.0 * (double)busy / (double)dt;
}

/* RSS of every process whose comm matches prefix; returns count and total kB */
static inline int proc_rss_by_name(const char *prefix, unsigned long long *total_kb, char *detail, size_t detail_len)
{
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    int n = 0;
    unsigned long long tot = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char p[300];
        snprintf(p, sizeof p, "/proc/%s/comm", e->d_name);
        char *comm = ml_sysfs_str(p, "");
        if (strncmp(comm, prefix, strlen(prefix)) != 0) { ml_free(comm); continue; }
        snprintf(p, sizeof p, "/proc/%s/status", e->d_name);
        char *status = ml_read_file(p, NULL);
        if (status) {
            char *v = strstr(status, "VmRSS:");
            if (v) {
                unsigned long long kb = strtoull(v + 6, NULL, 10);
                tot += kb;
                n++;
                char *role = strstr(status, "Name:");
                (void)role;
                if (detail) {
                    char one[96];
                    snprintf(one, sizeof one, "    pid %-7s %-16s %6llu kB\n", e->d_name, comm, kb);
                    if (strlen(detail) + strlen(one) < detail_len) strcat(detail, one);
                }
            }
            ml_free(status);
        }
        ml_free(comm);
    }
    closedir(d);
    if (total_kb) *total_kb = tot;
    return n;
}
#endif
