#include "kms.h"
#include "hwcap.h"
#include "drm_uapi.h"
#include "ml/log.h"
#include "ml/util.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <poll.h>
#include <errno.h>
#include <linux/fb.h>

const char *ml_disp_kind_name(ml_disp_kind k)
{
    switch (k) {
    case ML_DISP_KMS:   return "kms";
    case ML_DISP_FBDEV: return "fbdev";
    default:            return "headless";
    }
}

/* ------------------------------------------------------------------- KMS --- */
typedef struct {
    int fd;
    uint32_t crtc_id, conn_id, enc_id;
    uint32_t fb[2], handle[2];
    uint8_t *map[2];
    size_t map_size;
    int front;                     /* buffer currently scanned out */
    bool flip_pending;
    struct ml_drm_modeinfo mode;
} kms_priv;

static bool kms_dumb(kms_priv *k, int w, int h, int idx)
{
    struct ml_drm_mode_create_dumb cd = { .height = (uint32_t)h, .width = (uint32_t)w, .bpp = 32 };
    if (ioctl(k->fd, ML_DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) return false;
    k->handle[idx] = cd.handle;
    k->map_size = cd.size;
    struct ml_drm_mode_map_dumb md = { .handle = cd.handle };
    if (ioctl(k->fd, ML_DRM_IOCTL_MODE_MAP_DUMB, &md) < 0) return false;
    k->map[idx] = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, k->fd, (off_t)md.offset);
    if (k->map[idx] == MAP_FAILED) { k->map[idx] = NULL; return false; }
    struct ml_drm_mode_fb_cmd fb = { .width = (uint32_t)w, .height = (uint32_t)h,
                                     .pitch = cd.pitch, .bpp = 32, .depth = 24, .handle = cd.handle };
    if (ioctl(k->fd, ML_DRM_IOCTL_MODE_ADDFB, &fb) < 0) return false;
    k->fb[idx] = fb.fb_id;
    return true;
}

static bool kms_open(ml_display *d, const char *node, int *want_w, int *want_h)
{
    kms_priv *k = ml_zalloc(sizeof *k);
    k->fd = open(node, O_RDWR | O_CLOEXEC);
    if (k->fd < 0) { ml_free(k); return false; }

    char name[32] = "", date[32] = "", desc[128] = "";
    struct ml_drm_version v = { .name_len = sizeof name, .name = name,
                                .date_len = sizeof date, .date = date,
                                .desc_len = sizeof desc, .desc = desc };
    if (ioctl(k->fd, ML_DRM_IOCTL_VERSION, &v) == 0) snprintf(d->driver, sizeof d->driver, "%s", name);

    struct ml_drm_mode_card_res res;
    memset(&res, 0, sizeof res);
    if (ioctl(k->fd, ML_DRM_IOCTL_MODE_GETRESOURCES, &res) < 0 || !res.count_connectors) {
        snprintf(d->note, sizeof d->note, "GETRESOURCES failed: %s", strerror(errno));
        close(k->fd); ml_free(k);
        return false;
    }
    uint32_t *crtcs = ml_zalloc(sizeof(uint32_t) * (res.count_crtcs ? res.count_crtcs : 1));
    uint32_t *conns = ml_zalloc(sizeof(uint32_t) * res.count_connectors);
    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)conns;
    if (ioctl(k->fd, ML_DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        snprintf(d->note, sizeof d->note, "GETRESOURCES pass 2 failed: %s", strerror(errno));
        ml_free(crtcs); ml_free(conns); close(k->fd); ml_free(k);
        return false;
    }

    /* pick a connector: connected first, otherwise one that offers modes */
    uint32_t chosen = 0;
    struct ml_drm_mode_get_connector best;
    memset(&best, 0, sizeof best);
    struct ml_drm_modeinfo *modes = ml_zalloc(sizeof(*modes) * 64);
    int best_score = -1;
    for (uint32_t i = 0; i < res.count_connectors; i++) {
        struct ml_drm_mode_get_connector c;
        memset(&c, 0, sizeof c);
        c.connector_id = conns[i];
        if (ioctl(k->fd, ML_DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) continue;
        uint32_t nm = c.count_modes > 64 ? 64 : c.count_modes;
        c.modes_ptr = (uint64_t)(uintptr_t)modes;
        c.count_modes = nm;
        if (ioctl(k->fd, ML_DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) continue;
        int score = (c.connection == ML_DRM_MODE_CONNECTED ? 1000 : 0) + (int)nm;
        if (score > best_score) { best_score = score; best = c; chosen = conns[i]; }
    }
    if (!chosen || best.count_modes == 0) {
        snprintf(d->note, sizeof d->note, "no connector offers a mode");
        ml_free(modes); ml_free(crtcs); ml_free(conns); close(k->fd); ml_free(k);
        return false;
    }

    /* mode selection: an exact match for the requested size wins, then the
     * panel's preferred mode, then the largest. Native mode is what the iMac
     * panel wants; scaling looks awful on it. */
    int pick = 0;
    for (uint32_t i = 0; i < best.count_modes; i++)
        if ((int)modes[i].hdisplay == *want_w && (int)modes[i].vdisplay == *want_h) { pick = (int)i; break; }
    if (!pick)
        for (uint32_t i = 0; i < best.count_modes; i++)
            if (modes[i].type & ML_DRM_MODE_TYPE_PREFERRED) { pick = (int)i; break; }
    k->mode = modes[pick];
    ml_free(modes);

    k->conn_id = chosen;
    k->enc_id = best.encoder_id;
    k->crtc_id = res.count_crtcs ? crtcs[0] : 0;
    if (k->enc_id) {
        struct ml_drm_mode_get_encoder e;
        memset(&e, 0, sizeof e);
        e.encoder_id = k->enc_id;
        if (ioctl(k->fd, ML_DRM_IOCTL_MODE_GETENCODER, &e) == 0 && e.crtc_id) k->crtc_id = e.crtc_id;
    }
    ml_free(crtcs); ml_free(conns);

    d->w = k->mode.hdisplay;
    d->h = k->mode.vdisplay;
    *want_w = d->w;
    *want_h = d->h;
    d->refresh_mhz = k->mode.vrefresh ? (int)k->mode.vrefresh * 1000 : 0;
    snprintf(d->mode_name, sizeof d->mode_name, "%dx%d", d->w, d->h);
    snprintf(d->connector, sizeof d->connector, "%s-%u",
             ml_drm_connector_type_name(best.connector_type), best.connector_type_id);
    snprintf(d->node, sizeof d->node, "%s", node);

    if (!kms_dumb(k, d->w, d->h, 0) || !kms_dumb(k, d->w, d->h, 1)) {
        snprintf(d->note, sizeof d->note, "dumb buffer creation failed: %s", strerror(errno));
        close(k->fd); ml_free(k);
        return false;
    }
    d->pitch = (int)(k->map_size / (size_t)d->h);

    struct ml_drm_mode_crtc crtc;
    memset(&crtc, 0, sizeof crtc);
    crtc.crtc_id = k->crtc_id;
    crtc.fb_id = k->fb[0];
    crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&k->conn_id;
    crtc.count_connectors = 1;
    crtc.mode = k->mode;
    crtc.mode_valid = 1;
    /* The iMac Mid-2010 panel is known to come up with no CRTC assigned after
     * KMS takes over; an explicit SETCRTC is what fixes it (docs/hardware.md). */
    if (ioctl(k->fd, ML_DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
        snprintf(d->note, sizeof d->note, "SETCRTC failed: %s", strerror(errno));
        close(k->fd); ml_free(k);
        return false;
    }
    k->front = 0;
    d->priv = k;
    d->kind = ML_DISP_KMS;
    d->hw_scanout = true;
    d->vsync = true;
    snprintf(d->note, sizeof d->note, "%s on %s, %s %dx%d@%d Hz, double buffered dumb buffers",
             d->driver[0] ? d->driver : "drm", node, d->connector, d->w, d->h,
             k->mode.vrefresh);
    return true;
}

static void kms_close(ml_display *d)
{
    kms_priv *k = d->priv;
    if (!k) return;
    for (int i = 0; i < 2; i++) {
        if (k->fb[i]) ioctl(k->fd, ML_DRM_IOCTL_MODE_RMFB, &k->fb[i]);
        if (k->map[i] && k->map_size) munmap(k->map[i], k->map_size);
        if (k->handle[i]) {
            struct ml_drm_mode_destroy_dumb dd = { .handle = k->handle[i] };
            ioctl(k->fd, ML_DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        }
    }
    close(k->fd);
    ml_free(k);
    d->priv = NULL;
}

/* ----------------------------------------------------------------- fbdev --- */
typedef struct { int fd; uint8_t *map; size_t size; int line_length; } fb_priv;

static bool fb_open(ml_display *d, int *want_w, int *want_h)
{
    fb_priv *f = ml_zalloc(sizeof *f);
    f->fd = open(hw_dev("fb0"), O_RDWR);
    if (f->fd < 0) { ml_free(f); return false; }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(f->fd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(f->fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        close(f->fd); ml_free(f);
        return false;
    }
    if (var.bits_per_pixel != 32) {
        snprintf(d->note, sizeof d->note, "fb0 is %u bpp; MacLiteOS needs 32 bpp", var.bits_per_pixel);
        close(f->fd); ml_free(f);
        return false;
    }
    f->size = (size_t)fix.line_length * var.yres_virtual;
    f->map = mmap(0, f->size, PROT_READ | PROT_WRITE, MAP_SHARED, f->fd, 0);
    if (f->map == MAP_FAILED) {
        snprintf(d->note, sizeof d->note, "mmap fb0 failed: %s", strerror(errno));
        close(f->fd); ml_free(f);
        return false;
    }
    f->line_length = (int)fix.line_length;
    d->w = (int)var.xres;
    d->h = (int)var.yres;
    *want_w = d->w;
    *want_h = d->h;
    d->pitch = f->line_length;
    snprintf(d->mode_name, sizeof d->mode_name, "%dx%d", d->w, d->h);
    snprintf(d->node, sizeof d->node, "%s", hw_dev("fb0"));
    snprintf(d->driver, sizeof d->driver, "%.31s", fix.id);
    d->priv = f;
    d->kind = ML_DISP_FBDEV;
    d->hw_scanout = true;
    d->vsync = false;                       /* no flip events on fbdev */
    snprintf(d->note, sizeof d->note, "%s framebuffer %dx%d, %d B/line", fix.id, d->w, d->h, f->line_length);
    return true;
}

static void fb_close(ml_display *d)
{
    fb_priv *f = d->priv;
    if (!f) return;
    if (f->map && f->size) munmap(f->map, f->size);
    close(f->fd);
    ml_free(f);
    d->priv = NULL;
}

/* ---------------------------------------------------------------- public --- */
bool ml_display_open(ml_display *d, const char *requested, int *want_w, int *want_h)
{
    memset(d, 0, sizeof *d);
    d->kind = ML_DISP_HEADLESS;
    char reason[160];
    ml_present_kind pick = hw_pick_present(requested, reason, sizeof reason);

    if (pick == ML_PRESENT_KMS) {
        char node[256] = "";
        /* hw_pick_present already found a card; recover its node */
        for (int i = 0; i < 8; i++) {
            char rel[32];
            snprintf(rel, sizeof rel, "dri/card%d", i);
            if (access(hw_dev(rel), F_OK) == 0) { snprintf(node, sizeof node, "%s", hw_dev(rel)); break; }
        }
        if (node[0] && kms_open(d, node, want_w, want_h)) return true;
        snprintf(d->note, sizeof d->note, "KMS unavailable (%.150s); falling back", d->note[0] ? d->note : reason);
    }
    if (pick != ML_PRESENT_HEADLESS && fb_open(d, want_w, want_h)) return true;

    d->kind = ML_DISP_HEADLESS;
    d->back = NULL;
    snprintf(d->note, sizeof d->note, "%s", reason);
    return true;                              /* headless always succeeds */
}

void ml_display_close(ml_display *d)
{
    if (d->kind == ML_DISP_KMS) kms_close(d);
    else if (d->kind == ML_DISP_FBDEV) fb_close(d);
    d->kind = ML_DISP_HEADLESS;
    d->back = NULL;
}

uint32_t *ml_display_pixels(ml_display *d)
{
    if (d->kind == ML_DISP_KMS) {
        kms_priv *k = d->priv;
        return (uint32_t *)(uintptr_t)k->map[k->front ^ 1];
    }
    if (d->kind == ML_DISP_FBDEV) return (uint32_t *)(uintptr_t)((fb_priv *)d->priv)->map;
    return NULL;
}

void ml_display_commit(ml_display *d, const uint32_t *src, const ml_rect *damage, int n)
{
    if (d->kind == ML_DISP_HEADLESS || !src) return;

    /* KMS uses two scanout buffers. The buffer that was just scanned out is
     * only safe to recycle after the page-flip completion event. Without this
     * synchronization, damage-only updates can be copied into a stale buffer
     * that never received the previous frame, producing persistent cursor
     * trails/ghost windows after mouse movement. DRM explicitly requires
     * userspace to wait for flip completion before recycling the old buffer. */
    if (d->kind == ML_DISP_KMS) {
        kms_priv *k = d->priv;
        if (k && k->flip_pending && !ml_display_wait(d, 100)) {
            ML_WARN("KMS: timed out waiting for previous page flip; skipping frame commit");
            return;
        }
    }

    uint32_t *dst = ml_display_pixels(d);
    if (!dst) return;
    int src_pitch = d->w;
    int dst_pitch = d->kind == ML_DISP_KMS ? d->pitch / 4 : ((fb_priv *)d->priv)->line_length / 4;

    if (d->kind == ML_DISP_KMS) {
        /*
         * KMS has two scanout buffers, but the compositor's damage list is
         * frame-relative. After a flip completes, the buffer being recycled
         * can be one or more frames behind the compositor surface. Copying
         * only the latest cursor rectangle into that stale buffer therefore
         * leaves old cursor images behind.
         *
         * Keep the KMS path deliberately buffer-age independent: once the
         * previous flip has completed, copy the complete compositor surface
         * into the recycled scanout buffer before presenting it. This costs
         * one sequential framebuffer copy per present, but guarantees that
         * every pixel (including the old cursor position) is current on the
         * physical iMac Radeon path.
         *
         * The fbdev path retains damage-only copies because it writes directly
         * to the persistent scanout framebuffer and has no buffer recycling.
         */
        for (int y = 0; y < d->h; y++)
            memcpy(dst + (size_t)y * dst_pitch,
                   src + (size_t)y * src_pitch,
                   (size_t)d->w * 4);
    } else {
        for (int i = 0; i < n; i++) {
            ml_rect r = damage[i];
            if (r.x < 0) { r.w += r.x; r.x = 0; }
            if (r.y < 0) { r.h += r.y; r.y = 0; }
            if (r.x + r.w > d->w) r.w = d->w - r.x;
            if (r.y + r.h > d->h) r.h = d->h - r.y;
            if (r.w <= 0 || r.h <= 0) continue;
            for (int y = 0; y < r.h; y++)
                memcpy(dst + (size_t)(r.y + y) * dst_pitch + r.x,
                       src + (size_t)(r.y + y) * src_pitch + r.x,
                       (size_t)r.w * 4);
        }
    }
    if (d->kind == ML_DISP_KMS) {
        kms_priv *k = d->priv;
        struct ml_drm_mode_crtc_page_flip pf = {
            .fb_id = k->fb[k->front ^ 1], .crtc_id = k->crtc_id,
            .flags = ML_DRM_MODE_PAGE_FLIP_EVENT, .user_data = 0,
        };
        if (ioctl(k->fd, ML_DRM_IOCTL_MODE_PAGE_FLIP, &pf) < 0) {
            d->flip_errors++;
            /* a pending flip blocks the next one: drain it and retry once */
            if (errno == EBUSY) {
                ml_display_wait(d, 50);
                if (ioctl(k->fd, ML_DRM_IOCTL_MODE_PAGE_FLIP, &pf) == 0) { k->flip_pending = true; return; }
            }
            return;
        }
        k->flip_pending = true;
    }
}

int ml_display_fd(ml_display *d) { return d->kind == ML_DISP_KMS ? ((kms_priv *)d->priv)->fd : -1; }

bool ml_display_wait(ml_display *d, int timeout_ms)
{
    if (d->kind != ML_DISP_KMS) return true;
    kms_priv *k = d->priv;
    if (!k->flip_pending) return true;
    struct pollfd pfd = { .fd = k->fd, .events = POLLIN };
    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0) { d->flip_errors++; return false; }
    uint8_t buf[1024];
    for (;;) {
        ssize_t n = read(k->fd, buf, sizeof buf);
        if (n <= 0) break;
        ssize_t off = 0;
        while (off + (ssize_t)sizeof(struct ml_drm_event) <= n) {
            struct ml_drm_event *e = (struct ml_drm_event *)(buf + off);
            if (e->length < sizeof *e || off + e->length > n) break;
            /*
             * We request DRM_MODE_PAGE_FLIP_EVENT for every scanout flip.
             * Only that completion event retires the pending buffer. Do not
             * treat a generic vblank as a flip completion, and do not process
             * another event after the pending flip has been retired: doing so
             * could toggle the front index twice when multiple DRM events are
             * returned by one read().
             */
            if (e->type == ML_DRM_EVENT_FLIP_COMPLETE) {
                k->front ^= 1;
                d->flips++;
                k->flip_pending = false;
                return true;
            }
            off += e->length;
        }
    }
    return !k->flip_pending;
}

int ml_display_refresh_hz(const ml_display *d)
{
    if (d->refresh_mhz) return (int)((d->refresh_mhz + 500) / 1000);
    return d->hw_scanout ? 60 : 0;
}
