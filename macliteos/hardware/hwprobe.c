#include "hwprobe.h"
#include "ml/util.h"
#include "ml/log.h"
#include "../compositor/proto.h"
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* ------------------------------------------------------------- small utils -- */
/* basename of a sysfs symlink target, e.g.
 * /sys/bus/pci/devices/0000:01:00.0/driver -> "../../../../bus/pci/drivers/radeon"
 * -> "radeon". v0.1 read these symlinks as *files*, which always yields "" on
 * real hardware; tests/test_hardware.c pins the fix with a fixture symlink. */
static void sys_link_name(const char *dir, const char *link, char *out, size_t outlen)
{
    out[0] = 0;
    char p[512];
    snprintf(p, sizeof p, "%.440s/%.60s", dir, link);
    char buf[PATH_MAX];
    ssize_t n = readlink(p, buf, sizeof buf - 1);
    if (n <= 0) return;
    buf[n] = 0;
    char *t = buf;
    while (*t == '/') t++;
    snprintf(out, outlen, "%s", ml_path_base(t));
}

void ml_bound_driver(const char *sysfs_dev_dir, char *out, size_t outlen)
{
    sys_link_name(sysfs_dev_dir, "driver", out, outlen);
}

static void read_str(const char *dir, const char *file, char *out, size_t outlen)
{
    char p[512];
    snprintf(p, sizeof p, "%.440s/%.60s", dir, file);
    char *s = ml_sysfs_str(p, "");
    snprintf(out, outlen, "%.*s", (int)(outlen ? outlen - 1 : 0), s);
    ml_free(s);
}
static long read_long(const char *dir, const char *file, long def)
{
    char p[512];
    snprintf(p, sizeof p, "%.440s/%.60s", dir, file);
    return ml_sysfs_long(p, def);
}

/* sysfs device attributes (vendor, device, class, flags, bInterfaceClass) are
 * hex strings: "0x1002". ml_sysfs_long parses base 10 and therefore returns 0
 * for every one of them — the v0.1 probe table could never match on real
 * hardware. Everything hex goes through here (regression: test_hardware.c). */
static long read_hex(const char *dir, const char *file, long def)
{
    char p[512];
    snprintf(p, sizeof p, "%.440s/%.60s", dir, file);
    char *t = ml_sysfs_str(p, "");
    if (!t[0]) { ml_free(t); return def; }
    char *end = NULL;
    errno = 0;
    long v = strtol(t, &end, 0);
    ml_free(t);
    if (errno || end == NULL) return def;
    return v;
}

/* ------------------------------------------------------------------- GPU --- */
bool mica_gpu_probe(mica_gpu_info *out)
{
    memset(out, 0, sizeof *out);
    const char *pci_root = hw_sys("bus/pci/devices");
    DIR *d = opendir(pci_root);
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char base[400];
        snprintf(base, sizeof base, "%s/%s", pci_root, e->d_name);
        /* 0x03xxxx = display controller (VGA or 3D); parsed as hex, not text */
        long cls = read_hex(base, "class", -1);
        if (cls < 0 || ((unsigned long)cls >> 16) != 0x03) continue;

        snprintf(out->pci_addr, sizeof out->pci_addr, "%.15s", e->d_name);
        out->present = true;
        found = true;
        out->vendor_id = (uint32_t)read_hex(base, "vendor", 0);
        out->device_id = (uint32_t)read_hex(base, "device", 0);
        sys_link_name(base, "driver", out->driver, sizeof out->driver);

        /* capability DB lookup: model, gallium driver, decode API, codec mask */
        const hw_gpu_cap *cap = hw_gpu_lookup(out->vendor_id, out->device_id);
        if (cap) {
            snprintf(out->device, sizeof out->device, "%s", cap->model);
            snprintf(out->db_model, sizeof out->db_model, "%s", cap->model);
            snprintf(out->accel, sizeof out->accel, "%s", cap->decode_api);
            snprintf(out->accel_api, sizeof out->accel_api, "%s", cap->decode_api);
            snprintf(out->gallium, sizeof out->gallium, "%s", cap->gallium);
            out->decode_mask = cap->decode;
        } else {
            snprintf(out->device, sizeof out->device, "PCI %04x:%04x", out->vendor_id, out->device_id);
            snprintf(out->accel, sizeof out->accel, "unknown");
            snprintf(out->accel_api, sizeof out->accel_api, "none");
        }
        out->pci_revision = (uint32_t)read_hex(base, "revision", 0);
        out->subsystem_vendor = (uint32_t)read_hex(base, "subsystem_vendor", 0);
        out->subsystem_device = (uint32_t)read_hex(base, "subsystem_device", 0);
        /* radeon/amdgpu expose real VRAM through sysfs; others do not */
        long vram = read_long(base, "mem_info_vram_total", -1);
        out->vram_bytes = vram > 0 ? (uint64_t)vram : 0;
        if (out->vram_bytes == 0) {
            long vb = read_long(base, "mem_info_vram_vendor", -1);   /* not a size; ignored */
            (void)vb;
        }
        /* Mesa/DRI/VDPAU presence: file names only. The *version* of Mesa is
         * never inferred from a file — it comes from a real GL query below. */
        if (out->gallium[0]) {
            static const char *dri_dirs[] = {
                "/usr/lib/x86_64-linux-gnu/dri", "/usr/lib/dri", "/usr/lib64/dri",
                "/usr/local/lib/dri",
            };
            char want[64];
            snprintf(want, sizeof want, "%s_dri.so", out->gallium);
            for (size_t k = 0; k < ML_ARRAY_SIZE(dri_dirs); k++) {
                char p[256];
                snprintf(p, sizeof p, "%s/%s", dri_dirs[k], want);
                if (access(p, F_OK) == 0) {
                    out->mesa_present = true;
                    snprintf(out->dri_module, sizeof out->dri_module, "%.63s", want);
                    break;
                }
            }
            if (!out->mesa_present) {
                snprintf(want, sizeof want, "libvdpau_%s.so", out->gallium);
                for (size_t k = 0; k < ML_ARRAY_SIZE(dri_dirs); k++) {
                    char p[256];
                    snprintf(p, sizeof p, "%s/%s", dri_dirs[k], want);
                    if (access(p, F_OK) == 0) {
                        out->mesa_present = true;
                        snprintf(out->vdpau_driver, sizeof out->vdpau_driver, "%.63s", want);
                        break;
                    }
                }
            }
        }
        if (!strcmp(out->driver, "radeon")) {
            out->firmware_required = true;   /* R600+ needs its microcode blobs at KMS time */
            snprintf(out->firmware_note, sizeof out->firmware_note,
                     "radeon loads R600/RV7xx/Redwood microcode from %s/radeon/*.bin at modeset time",
                     hw_fw_root());
        }

        /* DRM nodes bound to this PCI device */
        char drm_root[400];
        snprintf(drm_root, sizeof drm_root, "%s", hw_path(base, "drm"));
        DIR *dd = opendir(drm_root);
        if (dd) {
            struct dirent *de;
            while ((de = readdir(dd))) {
                if (!strncmp(de->d_name, "card", 4) && !strchr(de->d_name, '-')) {
                    out->has_kms = true;
                    snprintf(out->card_node, sizeof out->card_node, "%s", hw_dev(hw_path("dri", de->d_name)));
                } else if (!strncmp(de->d_name, "renderD", 7)) {
                    out->has_render_node = true;
                    snprintf(out->render_node, sizeof out->render_node, "%s", hw_dev(hw_path("dri", de->d_name)));
                }
            }
            closedir(dd);
        }
        if (out->has_kms && !out->card_node[0]) out->has_kms = false;
        break;   /* first display device wins; iMacs drive one GPU at a time */
    }
    closedir(d);
    return found;
}

gl_probe_result mica_gl_probe(mica_gpu_info *g)
{
    /* Never infer GL support from the existence of /dev/dri: only a real query
     * (a GL tool that created a context) may set gl_probed. */
    g->gl_probed = false;
    g->gl_software = false;
    g->gl_renderer[0] = g->gl_version[0] = g->gl_vendor[0] = g->gl_source[0] = 0;

    static const char *tools[] = { "glxinfo -B", "eglinfo", "es2_info" };
    for (size_t t = 0; t < ML_ARRAY_SIZE(tools); t++) {
        char cmd[64];
        snprintf(cmd, sizeof cmd, "%s 2>/dev/null", tools[t]);
        FILE *p = popen(cmd, "r");
        if (!p) continue;
        char line[512];
        char rend[128] = "", ver[64] = "", vend[64] = "";
        while (fgets(line, sizeof line, p)) {
            char *q = strstr(line, "OpenGL renderer string:");
            if (!q) q = strstr(line, "GL_RENDERER:");
            if (!q) q = strstr(line, "GL_RENDERER=");
            if (q) {
                q = strchr(q, ':') ? strchr(q, ':') + 1 : strchr(q, '=') + 1;
                while (*q == ' ' || *q == '\t') q++;
                char *e = strchr(q, '\n');
                if (e) *e = 0;
                snprintf(rend, sizeof rend, "%s", q);
                continue;
            }
            q = strstr(line, "OpenGL version string:");
            if (!q) q = strstr(line, "GL_VERSION:");
            if (q) {
                q = strchr(q, ':') ? strchr(q, ':') + 1 : strchr(q, '=') + 1;
                while (*q == ' ') q++;
                char *e = strchr(q, '\n');
                if (e) *e = 0;
                snprintf(ver, sizeof ver, "%s", q);
                continue;
            }
            q = strstr(line, "OpenGL vendor string:");
            if (!q) q = strstr(line, "GL_VENDOR:");
            if (q) {
                q = strchr(q, ':') ? strchr(q, ':') + 1 : strchr(q, '=') + 1;
                while (*q == ' ') q++;
                char *e = strchr(q, '\n');
                if (e) *e = 0;
                snprintf(vend, sizeof vend, "%s", q);
            }
        }
        pclose(p);
        if (!rend[0]) continue;
        g->gl_probed = true;
        snprintf(g->gl_renderer, sizeof g->gl_renderer, "%s", rend);
        snprintf(g->gl_version, sizeof g->gl_version, "%s", ver);
        snprintf(g->gl_vendor, sizeof g->gl_vendor, "%s", vend);
        snprintf(g->gl_renderer0, sizeof g->gl_renderer0, "%s", rend);
        snprintf(g->gl_source, sizeof g->gl_source, "%s", tools[t]);
        g->gl_software = strcasestr(rend, "llvmpipe") || strcasestr(rend, "softpipe") ||
                         strcasestr(rend, "swrast") || strcasestr(rend, "Software Rasterizer");
        /* Mesa reports its release inside the GL version string ("4.5 (Compatibility
         * Profile) Mesa 23.1.4"). Take it from there when it is there, and leave it
         * empty when it is not — a version is never guessed from a file name. */
        const char *m = strcasestr(ver, "Mesa ") ? strcasestr(ver, "Mesa ")
                                                 : strcasestr(rend, "Mesa ");
        if (m) {
            m += 5;
            size_t k = 0;
            while (m[k] && (isdigit((unsigned char)m[k]) || m[k] == '.') && k < sizeof g->mesa_version - 1) {
                g->mesa_version[k] = m[k];
                k++;
            }
            g->mesa_version[k] = 0;
            g->mesa_present = true;
        }
        return g->gl_software ? GL_SW : GL_HW;
    }
    /* no GL query tool: is there a GL stack at all? */
    static const char *libs[] = { "/usr/lib/libGL.so.1", "/usr/lib/x86_64-linux-gnu/libGL.so.1",
                                  "/usr/lib/libEGL.so.1", "/usr/lib/x86_64-linux-gnu/libEGL.so.1" };
    for (size_t i = 0; i < ML_ARRAY_SIZE(libs); i++)
        if (access(libs[i], F_OK) == 0) return GL_UNKNOWN;
    return GL_NONE;
}

const char *gl_probe_name(gl_probe_result r)
{
    switch (r) {
    case GL_HW:      return "hardware";
    case GL_SW:      return "software";
    case GL_UNKNOWN: return "not tested";
    default:         return "no GL stack";
    }
}

/* ------------------------------------------------------------------- CPU --- */
bool mica_cpu_probe(mica_cpu_info *out)
{
    memset(out, 0, sizeof *out);
    char *info = ml_read_file(hw_proc("cpuinfo"), NULL);
    if (!info) return false;
    char *line = strtok(info, "\n");
    uint32_t phys = 0;
    while (line) {
        if (!strncmp(line, "model name", 10)) {
            char *v = strchr(line, ':');
            if (v) snprintf(out->model, sizeof out->model, "%s", ml_str_trim(v + 1));
        } else if (!strncmp(line, "cpu MHz", 7)) {
            char *v = strchr(line, ':');
            if (v) out->mhz_cur = (uint32_t)atof(v + 1);
        } else if (!strncmp(line, "flags", 5)) {
            out->sse42 = strstr(line, "sse4_2") != NULL;
            out->ssse3 = strstr(line, "ssse3") != NULL;
            out->avx = strstr(line, " avx") != NULL;
            out->avx2 = strstr(line, "avx2") != NULL;
        } else if (!strncmp(line, "siblings", 8)) {
            char *v = strchr(line, ':');
            if (v) out->threads = (uint32_t)atoi(v + 1);
        } else if (!strncmp(line, "cpu cores", 9)) {
            char *v = strchr(line, ':');
            if (v) phys = (uint32_t)atoi(v + 1);
        }
        line = strtok(NULL, "\n");
    }
    out->cores = phys ? phys : ML_MAX(1u, out->threads);
    if (!out->threads) out->threads = ML_MAX(1u, out->cores);
    out->mhz_max = (uint32_t)(read_long(hw_sys("devices/system/cpu/cpu0/cpufreq"), "cpuinfo_max_freq", 0) / 1000);
    ml_free(info);
    return true;
}

bool mica_mem_probe(mica_mem_info *out)
{
    memset(out, 0, sizeof *out);
    char *m = ml_read_file(hw_proc("meminfo"), NULL);
    if (!m) return false;
    char *t = strstr(m, "MemTotal:");
    if (t) out->total_kb = strtoull(t + 9, NULL, 10);
    t = strstr(m, "MemAvailable:");
    if (t) out->avail_kb = strtoull(t + 13, NULL, 10);
    ml_free(m);
    return true;
}

/* ------------------------------------------------------------------- DRM --- */
static void probe_connector(const char *conn_name, const char *conn_dir, mica_connector *c)
{
    memset(c, 0, sizeof *c);
    snprintf(c->connector, sizeof c->connector, "%.31s", conn_name);
    snprintf(c->path, sizeof c->path, "%.319s", conn_dir);
    read_str(conn_dir, "status", c->status, sizeof c->status);
    read_str(conn_dir, "enabled", c->enabled, sizeof c->enabled);
    read_str(conn_dir, "dpms", c->dpms, sizeof c->dpms);
    c->connected = !strcmp(c->status, "connected");
    if (!c->status[0]) snprintf(c->status, sizeof c->status, "unknown");

    char *modes = ml_read_file(hw_path(conn_dir, "modes"), NULL);
    if (modes) {
        char *l = strtok(modes, "\n");
        while (l && c->nmodes < (int)ML_ARRAY_SIZE(c->mode_w)) {
            int w = 0, h = 0;
            if (sscanf(l, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                c->mode_w[c->nmodes] = w;
                c->mode_h[c->nmodes] = h;
                c->nmodes++;
            }
            l = strtok(NULL, "\n");
        }
        ml_free(modes);
    }
    c->has_edid = edid_read_file(hw_path(conn_dir, "edid"), &c->edid);
    if (!c->has_edid) edid_info_init(&c->edid);
}

bool mica_drm_probe(mica_drm_state *out)
{
    memset(out, 0, sizeof *out);
    out->primary = -1;
    const char *drm_dir = hw_sys("class/drm");
    DIR *d = opendir(drm_dir);
    if (!d) {
        /* class symlinks absent: fall back to /dev/dri nodes */
        for (int i = 0; i < 8; i++) {
            char rel[32];
            snprintf(rel, sizeof rel, "dri/card%d", i);
            if (access(hw_dev(rel), F_OK) == 0) {
                out->any = true;
                snprintf(out->card_node, sizeof out->card_node, "%s", hw_dev(rel));
                snprintf(out->note, sizeof out->note, "%.54s present but %.70s unreadable", out->card_node, drm_dir);
                return true;
            }
        }
        snprintf(out->note, sizeof out->note, "no DRM device under %s", hw_dev_root());
        return false;
    }
    struct dirent *e;
    while ((e = readdir(d)) && out->n < (int)ML_ARRAY_SIZE(out->c)) {
        if (e->d_name[0] == '.') continue;
        char *dash = strchr(e->d_name, '-');
        if (!dash) {
            /* the card itself */
            snprintf(out->card_node, sizeof out->card_node, "%s", hw_dev(hw_path("dri", e->d_name)));
            out->any = true;
            char card_dir[400];
            snprintf(card_dir, sizeof card_dir, "%s", hw_path(drm_dir, e->d_name));
            sys_link_name(hw_path(card_dir, "device"), "driver", out->driver, sizeof out->driver);
            continue;
        }
        char conn_dir[400];
        snprintf(conn_dir, sizeof conn_dir, "%s/%s", drm_dir, e->d_name);
        probe_connector(dash + 1, conn_dir, &out->c[out->n]);
        snprintf(out->c[out->n].card, sizeof out->c[out->n].card, "%.15s", e->d_name);
        size_t cl = (size_t)(dash - e->d_name);
        if (cl >= sizeof out->c[out->n].card) cl = sizeof out->c[out->n].card - 1;
        memcpy(out->c[out->n].card, e->d_name, cl);
        out->c[out->n].card[cl] = 0;
        if (out->c[out->n].connected && out->primary < 0) out->primary = out->n;
        out->n++;
    }
    closedir(d);
    out->kms = out->any && out->n > 0;
    if (!out->any)
        snprintf(out->note, sizeof out->note, "connectors listed under %s but no card node in %s",
                 drm_dir, hw_dev_root());
    return out->any;
}

bool mica_fb_probe(mica_fb_state *out)
{
    memset(out, 0, sizeof *out);
    const char *fb = hw_sys("class/graphics/fb0");
    if (!ml_is_dir(fb)) return false;
    out->present = true;
    snprintf(out->name, sizeof out->name, "fb0");
    char *vs = ml_sysfs_str(hw_path(fb, "virtual_size"), "");
    sscanf(vs, "%d,%d", &out->w, &out->h);
    ml_free(vs);
    out->bpp = (int)read_long(fb, "bits_per_pixel", 0);
    read_str(fb, "name", out->id, sizeof out->id);
    return true;
}

/* --------------------------------------------------------------- network --- */
static bool iface_ip(const char *name, char *ip4, size_t l4, char *ip6, size_t l6)
{
    bool got4 = false, got6 = false;
    /* Under fixture roots the interface list is the fixture's, so an address
     * lookup would have to ask the *running* kernel — and would happily report
     * the sandbox NIC's address for the modelled machine (observed: a fixture
     * eth0 inheriting the container's 0.21.0.0). A fixture tree cannot express
     * an IPv4 address, so the honest answer is "not readable here": callers
     * label address-dependent checks NOT TESTED instead of inventing one.
     * Real hosts take the ioctl path below. */
    if (hw_using_fixture()) return false;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s >= 0) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof ifr);
        snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", name);
        if (ioctl(s, SIOCGIFADDR, &ifr) == 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *)&ifr.ifr_addr.sa_data;
            snprintf(ip4, l4, "%s", inet_ntoa(sa->sin_addr));
            got4 = true;
        }
        close(s);
    }
    /* IPv6 from /proc/net/if_inet6: 32 hex chars, no colons; rebuild and pretty
     * print the first global-scope address (scope 00). */
    char *t = ml_read_file(hw_proc("net/if_inet6"), NULL);
    if (t) {
        char *l = strtok(t, "\n");
        while (l) {
            char addr[64], ifn[32];
            int idx = 0, prefix = 0, scope = 0, flags = 0;
            if (sscanf(l, "%63s %x %x %x %x %31s", addr, &idx, &prefix, &scope, &flags, ifn) == 6 &&
                strlen(addr) == 32 && !strcmp(ifn, name) && scope == 0) {
                char rebuilt[48];
                for (int i = 0, j = 0; i < 32; i += 4) {
                    memcpy(rebuilt + j, addr + i, 4);
                    j += 4;
                    if (i < 28) rebuilt[j++] = ':';
                }
                rebuilt[39] = 0;
                struct in6_addr bin;
                char tmp[INET6_ADDRSTRLEN];
                if (inet_pton(AF_INET6, rebuilt, &bin) == 1 && inet_ntop(AF_INET6, &bin, tmp, sizeof tmp))
                    snprintf(ip6, l6, "%s", tmp);
                else
                    snprintf(ip6, l6, "%s", rebuilt);
                got6 = true;
                break;
            }
            l = strtok(NULL, "\n");
        }
        ml_free(t);
    }
    return got4 || got6;
}

static void read_route_gateway(char *out, size_t outlen)
{
    out[0] = 0;
    char *t = ml_read_file(hw_proc("net/route"), NULL);
    if (!t) return;
    char *l = strtok(t, "\n");
    while (l) {
        char iface[32];
        unsigned long dest = 0, gw = 0;
        if (sscanf(l, "%31s %lx %lx", iface, &dest, &gw) == 3 && dest == 0 && gw != 0) {
            struct in_addr a = { .s_addr = htonl((in_addr_t)gw) };  /* route file is big-endian hex */
            snprintf(out, outlen, "%s", inet_ntoa(a));
            break;
        }
        l = strtok(NULL, "\n");
    }
    ml_free(t);
}

bool mica_net_probe(mica_net_state *out)
{
    memset(out, 0, sizeof *out);
    out->primary = -1;
    const char *net_dir = hw_sys("class/net");
    DIR *d = opendir(net_dir);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) && out->n < (int)ML_ARRAY_SIZE(out->v)) {
        if (e->d_name[0] == '.') continue;
        mica_net_iface *i = &out->v[out->n];
        memset(i, 0, sizeof *i);
        i->wifi_level = -1;
        snprintf(i->name, sizeof i->name, "%.15s", e->d_name);
        char base[400];
        snprintf(base, sizeof base, "%.300s/%.60s", net_dir, e->d_name);
        read_str(base, "operstate", i->state, sizeof i->state);
        read_str(base, "address", i->mac, sizeof i->mac);
        sys_link_name(base, "device/driver", i->driver, sizeof i->driver);
        i->up = read_hex(base, "flags", 0) & 1;
        i->carrier = read_long(base, "carrier", -1) == 1;
        i->wireless = ml_is_dir(hw_path(base, "wireless"));
        i->rx_bytes = (uint64_t)read_long(base, "statistics/rx_bytes", 0);
        i->tx_bytes = (uint64_t)read_long(base, "statistics/tx_bytes", 0);
        if (!strcmp(i->name, "lo")) snprintf(i->kind, sizeof i->kind, "loopback");
        else if (i->wireless) snprintf(i->kind, sizeof i->kind, "wifi");
        else if (!strncmp(i->name, "wl", 2) || !strncmp(i->name, "wlan", 4)) snprintf(i->kind, sizeof i->kind, "wifi");
        else snprintf(i->kind, sizeof i->kind, "ethernet");
        if (!strcmp(i->kind, "wifi")) {
            char *w = ml_sysfs_str(hw_path(base, "wireless/link"), "");
            i->wifi_level = atoi(w);
            ml_free(w);
            out->nwifi++;
        } else if (!strcmp(i->kind, "ethernet")) out->neth++;

        /* Exact chipset: the device's own PCI (or USB) ids, read from sysfs,
         * then named by the device database. An id the table does not know is
         * reported as an unknown chip — never assumed to be the target part. */
        i->speed_mbps = -1;
        unsigned long v = (unsigned long)read_hex(base, "device/vendor", 0);
        unsigned long dd = (unsigned long)read_hex(base, "device/device", 0);
        if (!v) {   /* USB network adapters expose their ids at the interface */
            v = (unsigned long)read_hex(base, "device/idVendor", 0);
            dd = (unsigned long)read_hex(base, "device/idProduct", 0);
        }
        if (v && dd) {
            i->has_pci_ids = true;
            i->vendor_id = (uint32_t)v;
            i->device_id = (uint32_t)dd;
            hw_dev_role role = !strcmp(i->kind, "wifi") ? HW_DEV_WIFI : HW_DEV_ETHERNET;
            const hw_dev_cap *cap = hw_dev_lookup(i->vendor_id, i->device_id, role);
            if (cap) {
                snprintf(i->chipset, sizeof i->chipset, "%s", cap->model);
                snprintf(i->chip_driver, sizeof i->chip_driver, "%s", cap->driver);
                snprintf(i->firmware, sizeof i->firmware, "%s", cap->firmware);
                snprintf(i->chip_note, sizeof i->chip_note, "%s", cap->note);
                i->firmware_present = true;
                if (cap->firmware[0]) {
                    char list[192], *save = NULL;
                    snprintf(list, sizeof list, "%s", cap->firmware);
                    for (char *tok = strtok_r(list, " ", &save); tok; tok = strtok_r(NULL, " ", &save))
                        if (access(hw_fw(tok), F_OK) != 0) i->firmware_present = false;
                }
            } else {
                snprintf(i->chipset, sizeof i->chipset, "unknown %04x:%04x", i->vendor_id, i->device_id);
            }
        }
        {
            /* link speed: only the kernel knows it, and only when a link is up
             * (ethtool or the sysfs attribute, whichever this kernel offers) */
            long sp = read_long(base, "speed", -1);
            if (sp > 0) i->speed_mbps = (int)sp;
            char dup[16];
            read_str(base, "duplex", dup, sizeof dup);
            snprintf(i->duplex, sizeof i->duplex, "%.7s", dup);
        }
        i->has_ip4 = iface_ip(i->name, i->ip4, sizeof i->ip4, i->ip6, sizeof i->ip6);
        i->has_ip6 = i->ip6[0] != 0;
        /* the interface that carries this machine: first non-loopback one that
         * is administratively up (address state is reported separately, since
         * it may be unreadable here) */
        if (out->primary < 0 && strcmp(i->name, "lo") && i->up) out->primary = out->n;
        out->n++;
    }
    closedir(d);
    if (!out->have_gateway) {
        read_route_gateway(out->gateway, sizeof out->gateway);
        out->have_gateway = out->gateway[0] != 0;
    }
    char *res = ml_read_file(hw_etc("resolv.conf"), NULL);
    if (res) {
        snprintf(out->resolv_source, sizeof out->resolv_source, "%s", hw_etc("resolv.conf"));
        char *l = strtok(res, "\n");
        while (l && out->ndns < 8) {
            while (*l == ' ' || *l == '\t') l++;
            if (!strncmp(l, "nameserver", 10)) {
                char *v = l + 10;
                while (*v == ' ' || *v == '\t') v++;
                char *sp = strpbrk(v, " \t\n#");
                if (sp) *sp = 0;
                if (*v) snprintf(out->dns[out->ndns++], 64, "%s", v);
            }
            l = strtok(NULL, "\n");
        }
        ml_free(res);
    } else {
        snprintf(out->resolv_source, sizeof out->resolv_source, "%s (absent)", hw_etc("resolv.conf"));
    }
    return out->n > 0;
}

ml_link_status ml_net_link_status(const mica_net_state *n)
{
    ml_link_status s;
    memset(&s, 0, sizeof s);
    s.address_readable = !hw_using_fixture();
    int pi = n->primary;
    if (pi < 0) {
        snprintf(s.evidence, sizeof s.evidence, "no non-loopback interface is up under %s", hw_sys("class/net"));
        return s;
    }
    const mica_net_iface *i = &n->v[pi];
    s.have_iface = true;
    snprintf(s.iface, sizeof s.iface, "%s", i->name);
    snprintf(s.state, sizeof s.state, "%s", i->state);
    s.link_up = i->up && i->carrier;
    s.has_ip4 = i->has_ip4;
    snprintf(s.ip4, sizeof s.ip4, "%s", i->ip4);
    snprintf(s.evidence, sizeof s.evidence, "%s: operstate=%s carrier=%s driver=%s ip=%s",
             i->name, i->state[0] ? i->state : "unknown", i->carrier ? "yes" : "no",
             i->driver[0] ? i->driver : "?", i->has_ip4 ? i->ip4 : "(unreadable here)");
    return s;
}

/* ------------------------------------------------------------------- USB --- */
static void usb_kind_of(const char *dev_dir, mica_usb_dev *u)
{
    snprintf(u->kind, sizeof u->kind, "other");
    /* interfaces live in subdirectories like 1-1:1.0 */
    DIR *d = opendir(dev_dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strchr(e->d_name, ':')) continue;
        char sub[420];
        snprintf(sub, sizeof sub, "%.300s/%.100s", dev_dir, e->d_name);
        long cls = read_hex(sub, "bInterfaceClass", -1);
        long proto = read_hex(sub, "bInterfaceProtocol", -1);
        sys_link_name(sub, "driver", u->driver, sizeof u->driver);
        if (cls == 0x03) {
            /* HID: protocol 1 = keyboard, 2 = mouse (boot protocol) */
            snprintf(u->kind, sizeof u->kind, proto == 1 ? "keyboard" : proto == 2 ? "mouse" : "hid");
        } else if (cls == 0x08) snprintf(u->kind, sizeof u->kind, "storage");
        else if (cls == 0x09) snprintf(u->kind, sizeof u->kind, "hub");
        else if (cls == 0x01) snprintf(u->kind, sizeof u->kind, "audio");
        else if (cls == 0x0e) snprintf(u->kind, sizeof u->kind, "camera");
    }
    closedir(d);
}

static void usb_block_dev(const char *dev_dir, mica_usb_dev *u)
{
    DIR *d = opendir(dev_dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "host", 4)) continue;
        char scsi[420];
        snprintf(scsi, sizeof scsi, "%.300s/%.100s", dev_dir, e->d_name);
        DIR *sd = opendir(scsi);
        if (!sd) continue;
        struct dirent *se;
        while ((se = readdir(sd))) {
            if (se->d_name[0] < '0' || se->d_name[0] > '9') continue;
            char tgt[460];
            snprintf(tgt, sizeof tgt, "%.340s/%.100s", scsi, se->d_name);
            DIR *td = opendir(tgt);
            if (!td) continue;
            struct dirent *te;
            while ((te = readdir(td))) {
                if (strncmp(te->d_name, "block:", 6)) continue;
                snprintf(u->blockdev, sizeof u->blockdev, "%s", te->d_name + 6);
            }
            closedir(td);
        }
        closedir(sd);
    }
    closedir(d);
}

bool mica_usb_probe(mica_usb_state *out)
{
    memset(out, 0, sizeof *out);
    const char *root = hw_sys("bus/usb/devices");
    DIR *d = opendir(root);
    if (!d) { snprintf(out->note, sizeof out->note, "%s not present", root); return false; }
    out->present = true;
    struct dirent *e;
    while ((e = readdir(d)) && out->n < (int)ML_ARRAY_SIZE(out->v)) {
        if (e->d_name[0] == '.') continue;
        /* skip usbN root hubs' interface dirs */
        if (strchr(e->d_name, ':')) continue;
        char base[420];
        snprintf(base, sizeof base, "%s/%s", root, e->d_name);
        mica_usb_dev *u = &out->v[out->n];
        memset(u, 0, sizeof *u);
        snprintf(u->port, sizeof u->port, "%.23s", e->d_name);
        /* sysfs writes these as bare hex ("05ac"); base-10 parsing gave 0 for
         * every real device (fix pinned by test_hardware.c). */
        u->vid = (uint32_t)read_hex(base, "idVendor", 0);
        u->pid = (uint32_t)read_hex(base, "idProduct", 0);
        read_str(base, "product", u->product, sizeof u->product);
        read_str(base, "manufacturer", u->manufacturer, sizeof u->manufacturer);
        u->speed_mbps = (int)(read_long(base, "speed", 0));
        usb_kind_of(base, u);
        if (!strcmp(u->kind, "storage")) usb_block_dev(base, u);
        if (!strcmp(u->kind, "keyboard")) out->nkey++;
        else if (!strcmp(u->kind, "mouse")) out->nmouse++;
        else if (!strcmp(u->kind, "storage")) out->nstorage++;
        else if (!strcmp(u->kind, "hub")) out->nhub++;
        out->n++;
    }
    closedir(d);
    return out->present;
}

/* --------------------------------------------------------------- storage --- */
bool mica_storage_probe(mica_storage_state *out)
{
    memset(out, 0, sizeof *out);
    /* mount table first, so devices can be annotated */
    struct { char dev[64], mp[128], fs[16]; } mounts[64];
    int nmounts = 0;
    char *mi = ml_read_file(hw_proc("self/mountinfo"), NULL);
    if (mi) {
        char *l = strtok(mi, "\n");
        while (l && nmounts < 64) {
            /* fields: id parent major:minor root mp opts ... - fstype source superopts */
            char mp[128], fs[16], src[128];
            char *dash = strstr(l, " - ");
            if (dash) {
                if (sscanf(dash + 3, "%15s %127s", fs, src) == 2) {
                    /* mount point is field 5 */
                    char *f = l;
                    for (int i = 0; i < 4 && f; i++) f = strchr(f, ' ') ? strchr(f, ' ') + 1 : NULL;
                    if (f) {
                        char *sp = strchr(f, ' ');
                        if (sp) {
                            size_t n = (size_t)(sp - f);
                            if (n >= sizeof mp) n = sizeof mp - 1;
                            memcpy(mp, f, n);
                            mp[n] = 0;
                            snprintf(mounts[nmounts].dev, sizeof mounts[0].dev, "%.63s", src);
                            snprintf(mounts[nmounts].mp, sizeof mounts[0].mp, "%s", mp);
                            snprintf(mounts[nmounts].fs, sizeof mounts[0].fs, "%s", fs);
                            nmounts++;
                        }
                    }
                }
            }
            l = strtok(NULL, "\n");
        }
        ml_free(mi);
    }

    /* /sys/block is the legacy alias; class/block is the canonical one. Accept
     * whichever exists (fixtures and older kernels may only have one). */
    const char *blk = hw_sys("class/block");
    DIR *d = opendir(blk);
    if (!d) {
        blk = hw_sys("block");
        d = opendir(blk);
    }
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) && out->n < (int)ML_ARRAY_SIZE(out->v)) {
        if (e->d_name[0] == '.') continue;
        char base[420];
        snprintf(base, sizeof base, "%s/%s", blk, e->d_name);
        mica_disk *k = &out->v[out->n];
        memset(k, 0, sizeof *k);
        k->use_pct = -1;
        snprintf(k->name, sizeof k->name, "%.15s", e->d_name);
        snprintf(k->devnode, sizeof k->devnode, "%s", hw_dev(e->d_name));
        k->removable = read_long(base, "removable", 0) == 1;
        k->ro = (int)read_long(base, "ro", 0);
        k->size_bytes = (uint64_t)read_long(base, "size", 0) * 512ull;
        /* a partition has a `partition` attribute; a whole disk does not */
        k->whole = !ml_file_exists(hw_path(base, "partition"));
        read_str(base, "device/model", k->model, sizeof k->model);
        read_str(base, "device/vendor", k->vendor, sizeof k->vendor);
        for (int m = 0; m < nmounts; m++) {
            const char *b = ml_path_base(mounts[m].dev);
            if (!strcmp(b, k->name)) {
                k->mounted = true;
                snprintf(k->mountpoint, sizeof k->mountpoint, "%s", mounts[m].mp);
                snprintf(k->fstype, sizeof k->fstype, "%s", mounts[m].fs);
                struct statvfs sv;
                if (statvfs(mounts[m].mp, &sv) == 0) {
                    k->fs_total_kb = (uint64_t)sv.f_blocks * (sv.f_frsize / 1024);
                    k->fs_free_kb = (uint64_t)sv.f_bavail * (sv.f_frsize / 1024);
                    if (k->fs_total_kb)
                        k->use_pct = (int)(100 - (k->fs_free_kb * 100) / k->fs_total_kb);
                }
                out->nmounted++;
                if (k->use_pct >= 90 && !out->low_space[0])
                    snprintf(out->low_space, sizeof out->low_space,
                             "%s is %d%% full (%llu MB free)", k->mountpoint, k->use_pct,
                             (unsigned long long)(k->fs_free_kb / 1024));
                break;
            }
        }
        if (k->whole) out->nwhole++;
        if (k->removable) out->nremovable++;
        out->n++;
    }
    closedir(d);
    return out->n > 0;
}

/* ----------------------------------------------------------------- power --- */
bool mica_power_probe(mica_power_state *out)
{
    memset(out, 0, sizeof *out);
    out->battery_pct = -1;
    const char *pwr = hw_sys("power");
    char *st = ml_read_file(hw_path(pwr, "state"), NULL);
    if (st) {
        out->state_node = true;
        out->disk = strstr(st, "disk") != NULL;
        out->freeze = strstr(st, "freeze") != NULL;
        out->deep = strstr(st, "mem") != NULL;
        ml_free(st);
    }
    char *ms = ml_read_file(hw_path(pwr, "mem_sleep"), NULL);
    if (ms) {
        snprintf(out->mem_modes, sizeof out->mem_modes, "%s", ml_str_trim(ms));
        out->s2idle = strstr(ms, "s2idle") != NULL;
        out->shallow = strstr(ms, "shallow") != NULL;
        /* the bracketed entry is the active one */
        char *ob = strchr(ms, '[');
        char *cb = ob ? strchr(ob, ']') : NULL;
        if (ob && cb) {
            *cb = 0;
            out->deep = strstr(ob + 1, "deep") != NULL;
        }
        ml_free(ms);
    }
    out->writable = access(hw_path(pwr, "state"), W_OK) == 0;
    out->lid = ml_file_exists(hw_proc("acpi/button/lid/LID0/state")) ||
               ml_file_exists(hw_proc("acpi/button/lid/LID/state"));
    out->logind = ml_file_exists("/run/systemd/system");
    out->battery = ml_is_dir(hw_sys("class/power_supply/BAT0")) || ml_is_dir(hw_sys("class/power_supply/BAT1"));
    if (out->battery) {
        long cap = read_long(hw_sys("class/power_supply/BAT0"), "capacity", -1);
        if (cap < 0) cap = read_long(hw_sys("class/power_supply/BAT1"), "capacity", -1);
        out->battery_pct = (int)cap;
    }
    snprintf(out->note, sizeof out->note,
             "iMac11,x: suspend-to-RAM is a documented failure on this platform "
             "(panel does not re-light after resume) — see docs/hardware.md");
    return out->state_node;
}

/* ----------------------------------------------------------------- input --- */
bool mica_input_probe(mica_input_state *out)
{
    memset(out, 0, sizeof *out);
    char *t = ml_read_file(hw_proc("bus/input/devices"), NULL);
    if (!t) return false;
    out->present = true;
    char *save = t;
    char name[64] = "", handlers[96] = "", caps_key[128] = "";
    for (char *l = strsep(&save, "\n"); l; l = strsep(&save, "\n")) {
        if (l[0] == 'I') { name[0] = handlers[0] = caps_key[0] = 0; }
        else if (!strncmp(l, "N: Name=", 8)) {
            char *q = strchr(l, '"');
            char *q2 = q ? strchr(q + 1, '"') : NULL;
            if (q && q2) {
                size_t n = (size_t)(q2 - q - 1);
                if (n >= sizeof name) n = sizeof name - 1;
                memcpy(name, q + 1, n);
                name[n] = 0;
            }
        } else if (!strncmp(l, "H: Handlers=", 12)) {
            sscanf(l + 12, "%95[^\n]", handlers);
        } else if (!strncmp(l, "B: KEY=", 7)) {
            /* keep the whole bitmap: a second word means keycodes above 255 are
             * in use, which is how volume/brightness keys show up */
            sscanf(l + 7, "%127[^\n]", caps_key);
        }
        /* a blank line ends the record */
        if (l[0] == 0) {
            if (name[0]) {
                bool kbd = strstr(handlers, "kbd") != NULL || strstr(handlers, "sysrq") != NULL;
                bool mouse = strstr(handlers, "mouse") != NULL;
                if (kbd) { out->nkey++; out->keyboard = true; }
                if (mouse) { out->nmouse++; out->mouse = true; }
                /* KEY_VOLUMEUP/KEY_BRIGHTNESSUP land in the high KEY words */
                if (strchr(caps_key, ' ') != NULL && caps_key[0] != '0') out->media_keys = true;
                if (out->n < 8 && (kbd || mouse)) snprintf(out->names[out->n++], 48, "%s", name);
            }
            name[0] = handlers[0] = caps_key[0] = 0;
        }
    }
    /* flush the last record */
    if (name[0]) {
        if (strstr(handlers, "kbd")) { out->nkey++; out->keyboard = true; }
        if (strstr(handlers, "mouse")) { out->nmouse++; out->mouse = true; }
        if (out->n < 8) snprintf(out->names[out->n++], 48, "%s", name);
    }
    ml_free(t);
    return out->present;
}

/* ------------------------------------------------------------------ mode --- */
uint32_t mica_pick_mode(const mica_gpu_info *g, const mica_cpu_info *c)
{
    mica_mem_info m;
    mica_mem_probe(&m);
    char reason[160];
    /* hardware rendering is only claimed when a real GL context said so */
    bool hw_render = g->gl_probed && !g->gl_software;
    uint32_t mode = hw_pick_mode_caps(g->has_kms, hw_render, c ? c->cores : 1,
                                      (uint32_t)(m.total_kb / 1024), reason, sizeof reason);
    ML_DBG("mode pick: %s (%s)", mica_mode_name(mode), reason);
    return mode;
}

const char *mica_gpu_accel_name(const mica_gpu_info *g) { return g->accel[0] ? g->accel : "none"; }

double mica_cpu_blend_benchmark_mb_s(void)
{
    /* one 1920x1080 alpha-over pass, timed; honest, small, bounded */
    size_t n = (size_t)1920 * 1080;
    uint32_t *a = ml_alloc(n * 4), *b = ml_alloc(n * 4);
    for (size_t i = 0; i < n; i++) { a[i] = 0x80404040u; b[i] = 0xff102030u; }
    uint64_t t0 = ml_now_ns();
    uint64_t acc = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t s = a[i], d = b[i], al = (s >> 24) + 1;
        uint32_t ia = 256 - al;
        uint32_t rb = (((s & 0x00FF00FFu) * al + (d & 0x00FF00FFu) * ia) >> 8) & 0x00FF00FFu;
        uint32_t gg = (((s & 0x0000FF00u) * al + (d & 0x0000FF00u) * ia) >> 8) & 0x0000FF00u;
        acc += rb | gg | 0xff000000u;
    }
    double ms = ml_elapsed_ms(t0);
    (void)acc;
    ml_free(a); ml_free(b);
    if (ms <= 0) ms = 0.001;
    return ((double)n * 8.0 / (1024.0 * 1024.0)) / (ms / 1000.0);
}
