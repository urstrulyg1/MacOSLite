#ifndef MICA_KMS_H
#define MICA_KMS_H
#include "ml/common.h"

/* Display present backends (spec §2/§4/§6).
 *
 * v0.1 could only ever render into a memory surface: `mica-comp --drm` was
 * advertised in usage() but never parsed, and MS_STATS hard-coded
 * backend=0/accel=0. This module adds the missing half — a real scanout path —
 * without touching the renderer:
 *
 *   kms      dumb buffers + atomic-free page flips on /dev/dri/cardN. The GPU
 *            scans out and flips; the CPU still rasterises (honest: that is
 *            "hardware scanout", reported as accel=scanout, not accel=gl).
 *   fbdev    /dev/fb0 mmap. Used when the radeon KMS path misbehaves, which is
 *            a documented iMac Mid-2010 failure mode (docs/hardware.md).
 *   headless the v0.1 behaviour: render to RAM, no device. Always available.
 *
 * Everything is event driven: a flip is a non-blocking ioctl and the completion
 * event is read from the DRM fd (epoll-able), never polled.
 */

typedef enum { ML_DISP_HEADLESS = 0, ML_DISP_KMS, ML_DISP_FBDEV } ml_disp_kind;
const char *ml_disp_kind_name(ml_disp_kind k);

typedef struct {
    ml_disp_kind kind;
    int w, h, pitch;
    uint8_t *back;              /* pixels to draw into; NULL when headless */
    size_t back_size;
    bool hw_scanout;            /* a device is scanning out our buffer */
    bool vsync;                 /* flip-completion events are available */
    char node[128];
    char driver[32];
    char connector[32];
    char mode_name[32];
    int refresh_mhz;
    uint64_t flips, flip_errors;
    char note[192];
    void *priv;                 /* backend private state */
} ml_display;

/* requested: "auto" | "kms" | "fbdev" | "headless" (NULL == auto).
 * want_w/want_h are in/out: the backend may replace them with the panel's
 * native mode, and the caller must use the returned size. */
bool ml_display_open(ml_display *d, const char *requested, int *want_w, int *want_h);
void ml_display_close(ml_display *d);

/* back buffer as 32-bit pixels (XRGB8888 in memory, our 0xAARRGGBB layout) */
uint32_t *ml_display_pixels(ml_display *d);
/* copy damaged rects from src into the back buffer and present it */
void ml_display_commit(ml_display *d, const uint32_t *src, const ml_rect *damage, int n);
int ml_display_fd(ml_display *d);                     /* epoll-able, -1 if none */
bool ml_display_wait(ml_display *d, int timeout_ms);  /* wait for flip completion */
int ml_display_refresh_hz(const ml_display *d);

#endif
