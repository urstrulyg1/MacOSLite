#ifndef MICA_CLIENT_H
#define MICA_CLIENT_H
#include "ml/common.h"
#include "ml/event.h"
#include "ml/surface.h"
#include "ml/region.h"
#include "../proto.h"
#include "ml/anim.h"

/* Thin client runtime for MacLiteOS components and applications.
 * A process gets: one socket, N shared-memory window surfaces, an event loop
 * that sleeps until input arrives, and damage-based commits. Nothing here
 * polls. */

typedef struct mica_client mica_client;
typedef struct mica_win mica_win;

struct mica_win {
    mica_client *c;
    uint32_t id;
    ml_surface *surf;         /* wraps the shm buffer; draw into it with ml_ctx */
    int w, h;
    uint32_t flags;
    uint32_t state;
    bool focused;
    bool mapped;
    void *ud;
    void (*on_input)(mica_win *, const msg_input *);
    void (*on_configure)(mica_win *, const msg_configure *);
    void (*on_state)(mica_win *);
    void (*on_close)(mica_win *);
};

struct mica_client {
    int fd;
    ml_loop *loop;
    char name[48];
    msg_welcome info;
    void *ud;
    void (*on_event)(mica_client *, const msg_event *);
    void (*on_pong)(mica_client *, uint64_t sent_ns);
};

mica_client *mica_connect(const char *name);       /* NULL if compositor absent */
void mica_disconnect(mica_client *c);
int mica_run(mica_client *c);                       /* block until quit */
int mica_run_for(mica_client *c, uint64_t ms);
void mica_quit(mica_client *c, int code);
ml_loop *mica_loop(mica_client *c);
ml_source *mica_add_timer(mica_client *c, uint64_t ms, bool repeat, ml_void_fn fn, void *ud);
ml_anim_engine *mica_anims(mica_client *c);   /* process-wide animation engine */

mica_win *mica_win_new(mica_client *c, int w, int h, const char *title, const char *appid, uint32_t flags);
void mica_win_destroy(mica_win *win);
void mica_win_commit(mica_win *win);                /* commit accumulated damage */
void mica_win_commit_rects(mica_win *win, const ml_region *r);
void mica_win_set_title(mica_win *win, const char *title);
void mica_win_action(mica_win *win, uint32_t action, int a, int b);
void mica_win_drag(mica_win *win, int dx, int dy);
void mica_win_place(mica_win *win, int x, int y);   /* request screen position */
void mica_win_focus(mica_win *win);
void mica_win_resize(mica_win *win, int w, int h);
void mica_launch(mica_client *c, const char *cmdline);
void mica_notify(mica_client *c, const char *title, const char *body, const char *icon, uint32_t timeout_ms);
void mica_workspace(mica_client *c, int dir);
void mica_set_mode(mica_client *c, uint32_t mode);
void mica_ping(mica_client *c);
#endif
