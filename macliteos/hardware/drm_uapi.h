#ifndef MICA_DRM_UAPI_H
#define MICA_DRM_UAPI_H
/* Minimal DRM/KMS UAPI, transcribed from the Linux kernel UAPI headers
 * (include/uapi/drm/drm.h and drm_mode.h), which are MIT licensed.
 *
 * Only the structures MacLiteOS actually ioctls are declared, so the compositor
 * can drive a real panel without linking libdrm — that keeps the initrd small
 * and the dependency list at zero (spec §19/§23). The ABI here is frozen by the
 * kernel; it has been stable since 2.6.39.
 *
 * Compile-checked on Debian 12 (gcc 12). NOT runtime-exercised in the sandbox:
 * there is no /dev/dri there. See docs/gpu.md for what is and is not verified. */
#include <stdint.h>
#include <sys/ioctl.h>

#define DRM_IOCTL_BASE 'd'
#define DRM_IO(nr)          _IO(DRM_IOCTL_BASE, (nr))
#define DRM_IOW(nr, type)   _IOW(DRM_IOCTL_BASE, (nr), type)
#define DRM_IOWR(nr, type)  _IOWR(DRM_IOCTL_BASE, (nr), type)

#define DRM_DISPLAY_MODE_LEN 32

struct ml_drm_version {
    uint32_t version_major, version_minor, version_patchlevel;
    uint32_t name_len;  char *name;
    uint32_t date_len;  char *date;
    uint32_t desc_len;  char *desc;
};

struct ml_drm_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[DRM_DISPLAY_MODE_LEN];
};

struct ml_drm_mode_card_res {
    uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
    uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;
    uint32_t min_width, max_width, min_height, max_height;
};

struct ml_drm_mode_get_connector {
    uint64_t encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
    uint32_t count_modes, count_props, count_encoders;
    uint32_t encoder_id, connector_id, connector_type, connector_type_id;
    uint32_t connection, mm_width, mm_height, subpixel, pad;
};

struct ml_drm_mode_get_encoder {
    uint32_t encoder_id, encoder_type, possible_crtcs, crtc_id, possible_clones;
};

struct ml_drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t fb_id, crtc_id, x, y, gamma_size;
    struct ml_drm_modeinfo mode;
    uint32_t mode_valid;
};

struct ml_drm_mode_fb_cmd {
    uint32_t fb_id, width, height, pitch, bpp, depth, handle;
};

struct ml_drm_mode_create_dumb {
    uint32_t height, width, bpp, flags;
    uint32_t handle, pitch;
    uint64_t size;
};
struct ml_drm_mode_map_dumb { uint32_t handle, pad; uint64_t offset; };
struct ml_drm_mode_destroy_dumb { uint32_t handle; };

struct ml_drm_mode_crtc_page_flip {
    uint32_t fb_id, crtc_id, flags, reserved;
    uint64_t user_data;
};
#define ML_DRM_MODE_PAGE_FLIP_EVENT 0x01

struct ml_drm_event { uint32_t type, length; };
struct ml_drm_event_vblank {
    struct ml_drm_event base;
    uint64_t user_data;
    uint32_t tv_sec, tv_usec, sequence, reserved;
};
#define ML_DRM_EVENT_VBLANK        0x01
#define ML_DRM_EVENT_FLIP_COMPLETE 0x02

#define ML_DRM_MODE_CONNECTED    1
#define ML_DRM_MODE_DISCONNECTED 2
#define ML_DRM_MODE_UNKNOWNCONN  3

enum {
    ML_DRM_CONNECTOR_VGA = 1, ML_DRM_CONNECTOR_DVII, ML_DRM_CONNECTOR_DVID,
    ML_DRM_CONNECTOR_DVIA, ML_DRM_CONNECTOR_COMPOSITE, ML_DRM_CONNECTOR_SVIDEO,
    ML_DRM_CONNECTOR_LVDS, ML_DRM_CONNECTOR_COMPONENT, ML_DRM_CONNECTOR_DIN,
    ML_DRM_CONNECTOR_DP, ML_DRM_CONNECTOR_HDMIA, ML_DRM_CONNECTOR_HDMIB,
    ML_DRM_CONNECTOR_TV, ML_DRM_CONNECTOR_EDP, ML_DRM_CONNECTOR_VIRTUAL,
};
#define ML_DRM_MODE_TYPE_PREFERRED (1 << 3)
#define ML_DRM_MODE_TYPE_DRIVER    (1 << 6)

#define ML_DRM_IOCTL_VERSION            DRM_IOWR(0x00, struct ml_drm_version)
#define ML_DRM_IOCTL_MODE_GETRESOURCES  DRM_IOWR(0xA0, struct ml_drm_mode_card_res)
#define ML_DRM_IOCTL_MODE_SETCRTC       DRM_IOWR(0xA2, struct ml_drm_mode_crtc)
#define ML_DRM_IOCTL_MODE_GETCONNECTOR  DRM_IOWR(0xA7, struct ml_drm_mode_get_connector)
#define ML_DRM_IOCTL_MODE_GETENCODER    DRM_IOWR(0xA8, struct ml_drm_mode_get_encoder)
#define ML_DRM_IOCTL_MODE_ADDFB         DRM_IOWR(0xAE, struct ml_drm_mode_fb_cmd)
#define ML_DRM_IOCTL_MODE_RMFB          DRM_IOWR(0xAF, unsigned int)
#define ML_DRM_IOCTL_MODE_PAGE_FLIP     DRM_IOWR(0xB0, struct ml_drm_mode_crtc_page_flip)
#define ML_DRM_IOCTL_MODE_CREATE_DUMB   DRM_IOWR(0xB2, struct ml_drm_mode_create_dumb)
#define ML_DRM_IOCTL_MODE_MAP_DUMB      DRM_IOWR(0xB3, struct ml_drm_mode_map_dumb)
#define ML_DRM_IOCTL_MODE_DESTROY_DUMB  DRM_IOWR(0xB4, struct ml_drm_mode_destroy_dumb)

static inline const char *ml_drm_connector_type_name(uint32_t t)
{
    switch (t) {
    case ML_DRM_CONNECTOR_VGA: return "VGA";
    case ML_DRM_CONNECTOR_DVII: return "DVI-I";
    case ML_DRM_CONNECTOR_DVID: return "DVI-D";
    case ML_DRM_CONNECTOR_DVIA: return "DVI-A";
    case ML_DRM_CONNECTOR_COMPOSITE: return "Composite";
    case ML_DRM_CONNECTOR_SVIDEO: return "S-Video";
    case ML_DRM_CONNECTOR_LVDS: return "LVDS";
    case ML_DRM_CONNECTOR_COMPONENT: return "Component";
    case ML_DRM_CONNECTOR_DIN: return "DIN";
    case ML_DRM_CONNECTOR_DP: return "DP";
    case ML_DRM_CONNECTOR_HDMIA: return "HDMI-A";
    case ML_DRM_CONNECTOR_HDMIB: return "HDMI-B";
    case ML_DRM_CONNECTOR_TV: return "TV";
    case ML_DRM_CONNECTOR_EDP: return "eDP";
    case ML_DRM_CONNECTOR_VIRTUAL: return "Virtual";
    default: return "Unknown";
    }
}
#endif
