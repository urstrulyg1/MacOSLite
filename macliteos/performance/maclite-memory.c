/* maclite-memory — honest per-component memory report (spec §11, §53).
 *
 * Methodology, stated plainly:
 *   - every MacLiteOS process is included: compositor, shell roles, apps.
 *     Nothing is hidden, nothing is "not really part of the desktop".
 *   - RSS from /proc/<pid>/status. Shared pages are therefore counted once per
 *     process (a conservative over-estimate); PSS via smaps_rollup is used when
 *     the kernel exposes it, and reported separately as the fair number.
 *   - kernel memory is reported from /proc/meminfo, not attributed to us.
 */
#include "perf_common.h"
#include "../hardware/hwprobe.h"

static unsigned long long pss_of(pid_t pid)
{
    char p[64];
    snprintf(p, sizeof p, "/proc/%d/smaps_rollup", (int)pid);
    char *s = ml_read_file(p, NULL);
    if (!s) return 0;
    unsigned long long v = 0;
    char *t = strstr(s, "Pss:");
    if (t) v = strtoull(t + 4, NULL, 10);
    ml_free(s);
    return v;
}

int main(void)
{
    struct { const char *name, *label; } comps[] = {
        { "mica-comp",   "Compositor + WM" },
        { "mica-shell",  "Shell (menu bar / dock / desktop)" },
        { "mica-finder", "Finder" },
        { "mica-terminal", "Terminal" },
        { "mica-",       "Other MacLiteOS processes" },
    };
    mica_mem_info mi;
    mica_mem_probe(&mi);
    printf("MacLiteOS memory report            (method: /proc RSS + PSS where available)\n");
    printf("-----------------------------------------------------------------------------\n");
    printf("Kernel:  total %llu MB   available %llu MB   used %llu MB\n",
           mi.total_kb / 1024, mi.avail_kb / 1024, (mi.total_kb - mi.avail_kb) / 1024);
    unsigned long long grand_rss = 0, grand_pss = 0;
    int grand_n = 0;
    for (size_t i = 0; i < ML_ARRAY_SIZE(comps); i++) {
        char detail[4096] = { 0 };
        unsigned long long kb = 0;
        int n = proc_rss_by_name(comps[i].name, &kb, detail, sizeof detail);
        if (!n) { printf("%-36s not running\n", comps[i].label); continue; }
        unsigned long long pss = 0;
        DIR *d = opendir("/proc");
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            char p[300];
            snprintf(p, sizeof p, "/proc/%s/comm", e->d_name);
            char *comm = ml_sysfs_str(p, "");
            if (strncmp(comm, comps[i].name, strlen(comps[i].name)) == 0)
                pss += pss_of((pid_t)atoi(e->d_name));
            ml_free(comm);
        }
        closedir(d);
        grand_rss += kb; grand_pss += pss; grand_n += n;
        printf("%-36s %3d proc  RSS %6llu kB   PSS %6llu kB\n%s", comps[i].label, n, kb, pss, detail);
    }
    printf("-----------------------------------------------------------------------------\n");
    printf("Desktop total (userspace):  RSS %llu kB (%llu MB)   PSS %llu kB (%llu MB)\n",
           grand_rss, grand_rss / 1024, grand_pss, grand_pss / 1024);
    printf("Budget: idle target 150-300 MB, hard ceiling 500 MB (spec §52)\n");

    msg_stats st;
    if (comp_stats(&st)) {
        printf("\nCompositor caches & state: windows=%u clients=%u mode=%s\n",
               st.nwindows, st.nclients, mica_mode_name(st.mode));
    } else {
        printf("\n(no live compositor session; start one with mica-comp --session)\n");
    }
    return 0;
}
