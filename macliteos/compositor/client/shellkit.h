#ifndef MICA_SHELLKIT_H
#define MICA_SHELLKIT_H
#include "mica_client.h"
#include "ml/raster.h"
#include "ml/font.h"
#include "ml/icon.h"

/* Shared drawing + behaviour helpers for the MacLiteOS shell components.
 * Deliberately tiny: the shell is ~4 processes of a few hundred kB, not a
 * toolkit-dependent desktop. */

#define PANEL_H   26
#define DOCK_BASE 52

typedef struct { const char *id, *name, *icon, *exec; } mica_app_def;
extern const mica_app_def mica_apps[];
extern const size_t mica_apps_n;
const mica_app_def *mica_app_find(const char *id_or_name);

/* rounded translucent panel background honouring the performance mode */
void shell_panel_bg(ml_ctx *c, ml_rect r, double radius, uint32_t mode, bool dark);
void shell_hairline(ml_ctx *c, ml_rect r, double radius);

/* menu popup helper: a transient window with rows */
typedef struct {
    const char *label;
    const char *icon;
    bool separator;
    bool checked;
    void (*action)(void *ud);
    void *ud;
} shell_menu_item;

typedef struct shell_menu {
    mica_client *c;
    mica_win *win;
    shell_menu_item items[24];
    int n;
    int hover;
    void *ud;
    int x, y;
    struct shell_menu **owner;   /* cleared on self-close so callers never dangle */
} shell_menu;

/* route the menu window's input into shell_menu_input (set automatically) */
void shell_menu_own(shell_menu *m, shell_menu **slot);

shell_menu *shell_menu_open(mica_client *c, int x, int y, void *ud);
void shell_menu_add(shell_menu *m, const char *label, const char *icon,
                  void (*action)(void *ud), void *ud);
void shell_menu_sep(shell_menu *m);
void shell_menu_draw(shell_menu *m);
void shell_menu_input(shell_menu *m, const msg_input *in);
void shell_menu_close(shell_menu *m);

/* status readers (event/on-demand only; see docs/PERFORMANCE.md) */
int shell_battery_pct(void);          /* -1 when no battery */
bool shell_battery_charging(void);
int shell_wifi_level(void);           /* -1 when no wireless iface */
char *shell_wifi_ssid(void);
#endif
