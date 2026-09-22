#ifndef MICA_PROTO_H
#define MICA_PROTO_H
#include "ml/common.h"

/* Compositor wire protocol. Fixed-size payloads only.
 * One unix stream socket per client in $XDG_RUNTIME_DIR (or /tmp/macliteos-uid). */

#define MICA_PROTO_VERSION 1u

enum {
    /* client -> compositor */
    MC_HELLO = 1,
    MC_WIN_NEW,        /* + SCM_RIGHTS shm fd */
    MC_WIN_COMMIT,
    MC_WIN_RESIZE,     /* + SCM_RIGHTS shm fd */
    MC_WIN_TITLE,
    MC_WIN_ACTION,
    MC_WIN_DRAG,
    MC_FOCUS,
    MC_WORKSPACE,
    MC_LAUNCH,
    MC_NOTIFY,
    MC_QUERY,
    MC_PING,
    MC_SET_MODE,
    MC_WIN_PLACE,
    MC_SHUTDOWN,

    /* compositor -> client */
    MS_WELCOME = 40,
    MS_WIN_OK,
    MS_INPUT,
    MS_CONFIGURE,
    MS_FRAME,
    MS_WIN_STATE,
    MS_EVENT,
    MS_PONG,
    MS_STATS,
};

/* input kinds */
enum { IN_MOVE = 1, IN_DOWN, IN_UP, IN_CLICK, IN_KEY, IN_SCROLL, IN_ENTER, IN_LEAVE };
/* window actions */
enum { ACT_MINIMIZE = 1, ACT_MAXIMIZE, ACT_CLOSE, ACT_FULLSCREEN, ACT_RAISE,
       ACT_DRAG_BEGIN, ACT_RESIZE_BEGIN, ACT_ICONIFY_DONE };
/* session events broadcast to interested clients (dock, panel) */
enum { EV_WIN_OPEN = 1, EV_WIN_CLOSE, EV_FOCUS, EV_LAUNCH, EV_WORKSPACE, EV_MODE, EV_NOTIFY };
/* window flags at creation */
enum { WIN_F_BORDERLESS = 1 << 0,   /* dock, panel, launcher: no title bar */
       WIN_F_TOPMOST   = 1 << 1,
       WIN_F_BOTTOM    = 1 << 2,   /* desktop wallpaper layer */
       WIN_F_NO_FOCUS  = 1 << 3,
       WIN_F_TRANSIENT = 1 << 4 };
/* window state bits (MS_CONFIGURE / MS_WIN_STATE) */
enum { WS_MINIMIZED = 1 << 0, WS_MAXIMIZED = 1 << 1, WS_FULLSCREEN = 1 << 2, WS_FOCUSED = 1 << 3 };

typedef struct { uint32_t pid; uint32_t kind; char name[48]; } msg_hello;
typedef struct { uint32_t id; int32_t w, h; uint32_t flags; char title[64]; char appid[32]; } msg_win_new;
typedef struct { uint32_t id; uint32_t seq; uint32_t n; int32_t rects[32 * 4]; } msg_win_commit;
typedef struct { uint32_t id; int32_t w, h; } msg_win_resize;
typedef struct { uint32_t id; char title[64]; } msg_win_title;
typedef struct { uint32_t id; uint32_t action; int32_t a, b; } msg_win_action;
typedef struct { uint32_t id; int32_t dx, dy; } msg_win_drag;
typedef struct { uint32_t id; int32_t x, y; } msg_win_place;
typedef struct { uint32_t id; } msg_id;
typedef struct { int32_t dir; } msg_ws;
typedef struct { char cmd[256]; } msg_launch;
typedef struct { char title[64]; char body[160]; char icon[32]; uint32_t timeout_ms; } msg_notify;
typedef struct { uint32_t mode; } msg_mode;

typedef struct { uint32_t version; int32_t screen_w, screen_h; uint32_t scale; uint32_t mode;
                 uint32_t nworkspaces, cur_ws; char name[32]; } msg_welcome;
typedef struct { uint32_t id; int32_t x, y, w, h; uint32_t state; } msg_win_ok;
typedef struct { uint32_t id; uint32_t kind; int32_t x, y, dx, dy; uint32_t button, key, mods;
                 uint64_t time_ns; } msg_input;
typedef struct { uint32_t id; int32_t x, y, w, h; uint32_t state; } msg_configure;
typedef struct { uint32_t id; uint32_t seq; uint64_t ts_ns; } msg_frame;
typedef struct { uint32_t id; uint32_t state; uint32_t focused; } msg_win_state;
typedef struct { uint32_t kind; uint32_t a, b; char text[96]; } msg_event;

typedef struct {
    uint64_t frames, dropped, presents;
    uint32_t fps_x100;          /* fps * 100 */
    uint32_t frame_us;          /* mean composite+present time */
    uint32_t worst_us;
    uint32_t damage_px_last;
    uint32_t mode;
    uint32_t nclients, nwindows;
    uint32_t backend;           /* 0 headless, 1 drm, 2 fbdev */
    uint32_t accel;             /* 0 software, 1 gl */
    int32_t  screen_w, screen_h;
    uint32_t cur_ws, n_ws;
    char gpu_name[48];
} msg_stats;

/* performance modes (spec §8) */
enum { MODE_BEAUTIFUL = 0, MODE_BALANCED = 1, MODE_PERFORMANCE = 2 };
enum { ML_MOD_SHIFT = 1, ML_MOD_CTRL = 2, ML_MOD_META = 4 };
static inline const char *mica_mode_name(uint32_t m)
{
    switch (m) {
    case MODE_BEAUTIFUL: return "beautiful";
    case MODE_BALANCED: return "balanced";
    default: return "performance";
    }
}
#endif
