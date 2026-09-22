#ifndef MICA_HWPROBE_H
#define MICA_HWPROBE_H
#include "ml/common.h"
#include "hwcap.h"
#include "edid.h"

/* Hardware probing used by the compositor, System Information, System Settings
 * and every maclite-* diagnostic. Everything is read from sysfs/procfs (or the
 * ML_SYSFS_ROOT / ML_PROC_ROOT / ML_DEV_ROOT fixtures used by the tests): no
 * daemons, no polling, no udev dependency.
 *
 * Probes run at startup or on explicit user action — never in the render path. */

typedef struct {
    char vendor[32], device[64], driver[32];
    uint32_t vendor_id, device_id;
    char pci_addr[16];          /* 0000:01:00.0 */
    uint32_t pci_revision;      /* revision register, 0 when unreadable */
    uint32_t subsystem_vendor, subsystem_device;
    uint64_t vram_bytes;        /* 0 = unknown (mem_info_vram_total) */
    bool present;
    bool has_kms;               /* a /dev/dri/cardN bound to this device */
    char card_node[64];         /* /dev/dri/card0 */
    char render_node[64];       /* /dev/dri/renderD128, "" when absent */
    bool has_render_node;
    char accel[64];             /* legacy human string, kept for existing callers */
    char accel_api[16];         /* "vdpau" | "vaapi" | "none" */
    uint32_t decode_mask;       /* ML_CODEC_* the silicon has (capability DB) */
    char db_model[64];          /* model from the capability DB, "" when unknown */
    char gallium[16];           /* mesa driver name (r600 / radeonsi / ...) */
    /* Mesa presence and version: the version is only ever taken from a real GL
     * query (the version string a GL context reports). File presence proves the
     * driver is installed, not which release it is. */
    bool mesa_present;
    char mesa_version[40];
    char dri_module[64];        /* e.g. "r600_dri.so", "" when not found */
    char vdpau_driver[64];      /* e.g. "libvdpau_r600.so", "" when not found */
    bool firmware_required;     /* the GPU needs a firmware file to initialise */
    char firmware_note[160];
    /* GL facts — only ever filled by a real query, never inferred */
    bool gl_probed;
    char gl_vendor[48], gl_version[32], gl_renderer[96];
    bool gl_software;
    char gl_source[48];         /* "glxinfo" | "eglinfo" | "compositor" | "" */
    char gl_renderer0[64];      /* legacy field name kept for ABI compatibility */
} mica_gpu_info;

typedef struct {
    char model[64];
    uint32_t mhz_max, mhz_cur;
    uint32_t cores, threads;
    bool sse42, avx, avx2, ssse3;
} mica_cpu_info;

typedef struct {
    uint64_t total_kb, avail_kb;
} mica_mem_info;

/* ---- display / DRM --------------------------------------------------- */
typedef struct {
    char card[16];              /* card0 */
    char connector[32];         /* eDP / DP-1 / HDMI-A-1 / LVDS-1 */
    char path[320];             /* sysfs connector directory */
    char status[16];            /* connected | disconnected | unknown */
    char enabled[16];
    char dpms[16];              /* On | Standby | ... */
    bool connected;
    int nmodes;
    int mode_w[24], mode_h[24];
    bool has_edid;
    edid_info edid;
    int cur_w, cur_h, cur_mhz;  /* selected mode (0 when not mode-set) */
} mica_connector;

typedef struct {
    bool any;                   /* a DRM device exists at all */
    char card_node[256];
    bool kms;                   /* card node + connector enumeration worked */
    char driver[32];            /* kernel driver bound to the card */
    mica_connector c[8];
    int n;
    int primary;                /* index of the first connected connector, -1 */
    char note[160];
} mica_drm_state;

/* fbdev fallback (no KMS): /sys/class/graphics/fb0 + /dev/fb0 */
typedef struct {
    bool present;
    char name[16];
    int w, h, bpp;
    char id[48];
} mica_fb_state;

/* ---- network --------------------------------------------------------- */
typedef struct {
    char name[16];
    char kind[16];              /* ethernet | wifi | loopback | other */
    char state[16];             /* operstate */
    char mac[20];
    char driver[32];
    bool up, carrier, wireless;
    bool has_ip4, has_ip6;
    char ip4[48], ip6[80];
    uint64_t rx_bytes, tx_bytes;
    char ssid[64];
    int wifi_level;             /* -1 unknown */
    /* exact chipset: read from the device's own ids, then named by the device
     * database. `chipset` stays empty when the id is unknown — an unnamed chip
     * is reported as unknown rather than assumed to be the AirPort part. */
    bool has_pci_ids;
    uint32_t vendor_id, device_id;
    char chipset[64];
    char chip_driver[24];
    char firmware[192];         /* space separated paths that must exist */
    bool firmware_present;      /* all of them were found under the fw root */
    char chip_note[192];
    int speed_mbps;             /* ethtool-style link speed, -1 unknown */
    char duplex[8];
} mica_net_iface;

typedef struct {
    mica_net_iface v[8];
    int n;
    int neth, nwifi;
    char gateway[48];
    bool have_gateway;
    char dns[8][64];
    int ndns;
    char resolv_source[128];
    int primary;                /* first non-loopback iface with an address, -1 */
} mica_net_state;

/* One honest answer to "is this machine on the network?", shared by
 * maclite-network and maclite-hardware. Link-layer facts come from sysfs and are
 * assertable under fixtures; an IPv4 address needs a live socket, so
 * address_readable is false under fixture roots and callers must report the
 * address row NOT TESTED instead of borrowing this host's address. */
typedef struct {
    bool have_iface;
    bool link_up;                /* interface up and carrier detected */
    bool address_readable;       /* an address lookup was actually possible */
    bool has_ip4;
    char iface[16], state[16], ip4[48], evidence[176];
} ml_link_status;
ml_link_status ml_net_link_status(const mica_net_state *n);

/* Basename of the driver bound to a sysfs device directory ("" when unbound).
 * Public because the driver resolver asks the same question of parts the GPU
 * probe never looks at (SATA, card reader, FireWire, audio codec controller). */
void ml_bound_driver(const char *sysfs_dev_dir, char *out, size_t outlen);

/* ---- USB ------------------------------------------------------------- */
typedef struct {
    char port[24];              /* "1-1", "2-1.3" */
    uint32_t vid, pid;
    char product[64], manufacturer[48];
    char kind[20];              /* keyboard | mouse | storage | hub | audio | other */
    char driver[32];
    int speed_mbps;
    char blockdev[16];          /* sdb for mass storage, "" otherwise */
    char mountpoint[128];
    bool mounted;
} mica_usb_dev;

typedef struct {
    mica_usb_dev v[32];
    int n;
    int nkey, nmouse, nstorage, nhub;
    bool present;               /* /sys/bus/usb exists */
    char note[128];
} mica_usb_state;

/* ---- storage --------------------------------------------------------- */
typedef struct {
    char name[16];              /* sda, sda1, nvme0n1p2 */
    char devnode[24];
    bool whole, removable, ro;
    uint64_t size_bytes;
    char model[48], vendor[24];
    char fstype[16], mountpoint[128];
    bool mounted;
    uint64_t fs_total_kb, fs_free_kb;
    int use_pct;                /* -1 when not mounted */
} mica_disk;

typedef struct {
    mica_disk v[40];
    int n, nwhole, nremovable, nmounted;
    char low_space[128];        /* set when any mounted fs is <10% free */
} mica_storage_state;

/* ---- power ----------------------------------------------------------- */
typedef struct {
    bool state_node;            /* /sys/power/state exists */
    bool writable;
    char mem_modes[64];         /* contents of /sys/power/mem_sleep */
    bool s2idle, shallow, deep, disk, freeze;
    bool lid;
    bool logind;                /* systemd-logind socket present */
    bool battery;
    int battery_pct;
    char note[192];             /* documented platform caveat (see docs/hardware.md) */
} mica_power_state;

/* ---- input ----------------------------------------------------------- */
typedef struct {
    bool keyboard, mouse, media_keys;
    int nkey, nmouse;
    char names[8][48];
    int n;
    bool present;               /* /proc/bus/input/devices readable */
} mica_input_state;

/* ---- probes ---------------------------------------------------------- */
bool mica_gpu_probe(mica_gpu_info *out);
bool mica_cpu_probe(mica_cpu_info *out);
bool mica_mem_probe(mica_mem_info *out);
bool mica_drm_probe(mica_drm_state *out);
bool mica_fb_probe(mica_fb_state *out);
bool mica_net_probe(mica_net_state *out);
bool mica_usb_probe(mica_usb_state *out);
bool mica_storage_probe(mica_storage_state *out);
bool mica_power_probe(mica_power_state *out);
bool mica_input_probe(mica_input_state *out);

/* GL verification. Returns without lying:
 *   GL_HW      a real GL context reported a hardware renderer
 *   GL_SW      a real GL context reported llvmpipe/softpipe/swrast
 *   GL_NONE    no GL stack on this host
 *   GL_UNKNOWN a GL stack exists but no context could be created here
 *              (NOT TESTED — never reported as PASS) */
typedef enum { GL_HW = 0, GL_SW, GL_NONE, GL_UNKNOWN } gl_probe_result;
gl_probe_result mica_gl_probe(mica_gpu_info *g);
const char *gl_probe_name(gl_probe_result r);

/* Suggests a performance mode from real capability (spec §18).
 * Returns MODE_* from compositor/proto.h (0/1/2). */
uint32_t mica_pick_mode(const mica_gpu_info *g, const mica_cpu_info *c);
const char *mica_gpu_accel_name(const mica_gpu_info *g);
/* quick benchmark: software blend throughput in MB/s of a 1920x1080 RGBA
 * buffer; used as a tie-breaker when the GPU is unknown. */
double mica_cpu_blend_benchmark_mb_s(void);

#endif
