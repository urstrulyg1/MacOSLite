/* G1OS native installer GUI.
 *
 * The GUI is presentation only: destructive installation work is delegated to
 * maclite-installer-backend. Progress is therefore authoritative and cannot
 * be fabricated by the UI. The backend emits JSON progress events while the
 * GUI remains responsive, shows activity separately from completion, and
 * refuses to show success until the backend exits successfully after its
 * verification phase.
 */
#include "ml/common.h"
#include "ml/log.h"
#include "ml/util.h"
#include "ml/event.h"
#include "ml/surface.h"
#include "ml/raster.h"
#include "ml/font.h"
#include "ml/icon.h"
#include "../compositor/client/mica_client.h"
#include "../compositor/client/shellkit.h"
#include "../hardware/hwprobe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

#define WIN_W 720
#define WIN_H 520
#define GUI_TICK_MS 250
#define ACTIVITY_AFTER_MS 1000
#define MAX_LOG_LINES 48

typedef enum {
    STAGE_WELCOME = 0,
    STAGE_SELECT,
    STAGE_CONFIRM,
    STAGE_INSTALLING,
    STAGE_COMPLETE,
    STAGE_ERROR
} installer_stage;

typedef struct {
    char name[32];
    char devnode[64];
    char model[96];
    uint64_t size_bytes;
    bool is_removable;
    bool is_usb_boot;
    bool is_target;
    int partition_count;
    char partitions[8][96];
} disk_info;

static mica_client *G;
static mica_win *WIN;
static installer_stage STAGE = STAGE_WELCOME;
static disk_info DISKS[8];
static int N_DISKS = 0;
static int TARGET_DISK_IDX = -1;
static bool CONFIRMED_ERASE = false;
static bool SHOW_DETAILS = false;
static bool TEST_MODE = false;

/* Authoritative backend state. */
static pid_t INSTALL_PID = -1;
static int INSTALL_FD = -1;
static ml_source *INSTALL_TIMER = NULL;
static char IPC_BUFFER[4096];
static size_t IPC_USED = 0;
static int INSTALL_PROGRESS = 0;
static int DISPLAY_PROGRESS = 0;
static int LAST_STEP = 0;
static char CURRENT_STEP_TEXT[192] = "Preparing installation...";
static char CURRENT_STAGE_NAME[64] = "Preparing";
static char ERROR_MESSAGE[320] = "";
static char LOG_LINES[MAX_LOG_LINES][192];
static int N_LOG_LINES = 0;
static uint64_t INSTALL_STARTED_MS = 0;
static uint64_t LAST_EVENT_MS = 0;
static unsigned ACTIVITY_FRAME = 0;
static bool BACKEND_ACTIVE = false;
static bool BACKEND_FAILED = false;
static bool BACKEND_VERIFIED = false;

static void draw(void);
static void input(mica_win *w, const msg_input *in);

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

static void add_log(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (N_LOG_LINES < MAX_LOG_LINES) {
        snprintf(LOG_LINES[N_LOG_LINES++], sizeof LOG_LINES[0], "%s", buf);
    } else {
        memmove(LOG_LINES[0], LOG_LINES[1], sizeof(LOG_LINES[0]) * (MAX_LOG_LINES - 1));
        snprintf(LOG_LINES[MAX_LOG_LINES - 1], sizeof LOG_LINES[0], "%s", buf);
    }
}

static bool is_device_live_boot(const char *name)
{
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return false;
    char line[512];
    bool live = false;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, name) &&
            (strstr(line, "/run/maclite-base") || strstr(line, "/run/maclite-live") ||
             strstr(line, "/cdrom") || strstr(line, "iso9660") || strstr(line, "squashfs"))) {
            live = true;
            break;
        }
    }
    fclose(f);

    if (!live) {
        char syspath[256], link[512] = {0};
        snprintf(syspath, sizeof syspath, "/sys/block/%s", name);
        ssize_t n = readlink(syspath, link, sizeof(link) - 1);
        if (n > 0 && (strstr(link, "/usb") || strstr(link, "/USB"))) live = true;
    }
    return live;
}

static void probe_disks(void)
{
    N_DISKS = 0;
    TARGET_DISK_IDX = -1;
    mica_storage_state s;
    mica_storage_probe(&s);

    for (int i = 0; i < s.n && N_DISKS < 8; i++) {
        if (!s.v[i].whole) continue;
        disk_info *d = &DISKS[N_DISKS];
        memset(d, 0, sizeof *d);
        snprintf(d->name, sizeof d->name, "%s", s.v[i].name);
        snprintf(d->devnode, sizeof d->devnode, "%s", s.v[i].devnode);
        snprintf(d->model, sizeof d->model, "%s %s", s.v[i].vendor, s.v[i].model);
        ml_str_trim(d->model);
        if (!d->model[0]) snprintf(d->model, sizeof d->model, "Internal Drive (%s)", d->name);
        d->size_bytes = s.v[i].size_bytes;
        d->is_removable = s.v[i].removable;
        d->is_usb_boot = is_device_live_boot(d->name);

        for (int j = 0; j < s.n && d->partition_count < 8; j++) {
            if (s.v[j].whole) continue;
            if (strncmp(s.v[j].name, d->name, strlen(d->name)) != 0) continue;
            uint64_t mb = s.v[j].size_bytes / (1024ULL * 1024ULL);
            if (mb > 1024)
                snprintf(d->partitions[d->partition_count++], sizeof d->partitions[0], "%s: %.1f GB (%s)",
                         s.v[j].name, (double)mb / 1024.0, s.v[j].fstype[0] ? s.v[j].fstype : "data");
            else
                snprintf(d->partitions[d->partition_count++], sizeof d->partitions[0], "%s: %llu MB (%s)",
                         s.v[j].name, (unsigned long long)mb, s.v[j].fstype[0] ? s.v[j].fstype : "data");
        }
        N_DISKS++;
    }

    /* Test fixture is only available when explicitly requested. Production never invents disks. */
    if (TEST_MODE && N_DISKS == 0) {
        disk_info *d = &DISKS[0];
        memset(d, 0, sizeof *d);
        snprintf(d->name, sizeof d->name, "sda");
        snprintf(d->devnode, sizeof d->devnode, "/dev/sda");
        snprintf(d->model, sizeof d->model, "G1OS Test Disk");
        d->size_bytes = 500ULL * 1000ULL * 1000ULL * 1000ULL;
        d->partition_count = 2;
        snprintf(d->partitions[0], sizeof d->partitions[0], "sda1: 256 MB (EFI)");
        snprintf(d->partitions[1], sizeof d->partitions[1], "sda2: 499 GB (data)");
        N_DISKS = 1;
    }

    /* Prefer a physical, non-removable disk, but never select live media. */
    for (int i = 0; i < N_DISKS; i++) {
        if (!DISKS[i].is_usb_boot && !DISKS[i].is_removable) {
            TARGET_DISK_IDX = i;
            DISKS[i].is_target = true;
            break;
        }
    }
    if (TARGET_DISK_IDX < 0) {
        for (int i = 0; i < N_DISKS; i++) {
            if (!DISKS[i].is_usb_boot) {
                TARGET_DISK_IDX = i;
                DISKS[i].is_target = true;
                break;
            }
        }
    }
}

static const char *backend_path(void)
{
    static const char *paths[] = {
        "/usr/bin/maclite-installer-backend",
        "/usr/local/bin/maclite-installer-backend",
        "/run/maclite-live/usr/bin/maclite-installer-backend",
        "/run/maclite-base/usr/bin/maclite-installer-backend",
        NULL
    };
    for (int i = 0; paths[i]; i++)
        if (access(paths[i], X_OK) == 0) return paths[i];
    return NULL;
}

static void stop_backend(bool terminate)
{
    if (INSTALL_TIMER) {
        ml_timer_disarm(INSTALL_TIMER);
        INSTALL_TIMER = NULL;
    }
    if (terminate && INSTALL_PID > 0) {
        kill(INSTALL_PID, SIGTERM);
        waitpid(INSTALL_PID, NULL, 0);
    }
    if (INSTALL_FD >= 0) close(INSTALL_FD);
    INSTALL_FD = -1;
    INSTALL_PID = -1;
    BACKEND_ACTIVE = false;
}

static void set_failure(const char *message)
{
    snprintf(ERROR_MESSAGE, sizeof ERROR_MESSAGE, "%s", message);
    BACKEND_FAILED = true;
    BACKEND_ACTIVE = false;
    STAGE = STAGE_ERROR;
    if (INSTALL_TIMER) {
        ml_timer_disarm(INSTALL_TIMER);
        INSTALL_TIMER = NULL;
    }
    if (INSTALL_FD >= 0) {
        close(INSTALL_FD);
        INSTALL_FD = -1;
    }
    if (INSTALL_PID > 0) {
        waitpid(INSTALL_PID, NULL, WNOHANG);
        INSTALL_PID = -1;
    }
    draw();
}

static void apply_progress_event(int step, int progress, const char *message)
{
    if (progress < INSTALL_PROGRESS || progress > 100) return;
    INSTALL_PROGRESS = progress;
    LAST_STEP = step;
    LAST_EVENT_MS = now_ms();
    if (message && message[0]) snprintf(CURRENT_STEP_TEXT, sizeof CURRENT_STEP_TEXT, "%s", message);

    switch (step) {
    case 1: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Preparing"); break;
    case 2: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Preparing"); break;
    case 3: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Partitioning"); break;
    case 4: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Formatting"); break;
    case 5: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Installing"); break;
    case 6: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Bootloader"); break;
    case 7: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Verification"); break;
    case 8: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Verification"); break;
    case 9: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Complete"); break;
    default: break;
    }
    add_log("PROGRESS: %d%% — %s", INSTALL_PROGRESS, CURRENT_STEP_TEXT);
}

static void parse_backend_line(char *line)
{
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (!line[0]) return;
    add_log("%s", line);

    if (strstr(line, "\"status\":\"error\"")) {
        const char *p = strstr(line, "\"message\":\"");
        if (p) {
            p += 11;
            char msg[320] = {0};
            size_t i = 0;
            while (p[i] && p[i] != '"' && i + 1 < sizeof msg) { msg[i] = p[i]; i++; }
            msg[i] = 0;
            set_failure(msg[0] ? msg : "The installation backend reported an error.");
        } else {
            set_failure("The installation backend reported an error.");
        }
        return;
    }

    int step = 0, progress = -1;
    char message[192] = {0};
    if (sscanf(line, "{\"step\":%d,\"progress\":%d,\"message\":\"%191[^\"]", &step, &progress, message) == 3) {
        apply_progress_event(step, progress, message);
        return;
    }

    /* Preserve useful backend errors even when they are emitted as plain stderr. */
    if (strstr(line, "ERROR:")) {
        const char *p = strstr(line, "ERROR:");
        set_failure(p + 6);
    }
}

static void drain_backend_output(void)
{
    if (INSTALL_FD < 0) return;
    char buf[1024];
    for (;;) {
        ssize_t n = read(INSTALL_FD, buf, sizeof buf);
        if (n > 0) {
            if (IPC_USED + (size_t)n > sizeof IPC_BUFFER) {
                size_t keep = sizeof IPC_BUFFER / 2;
                memmove(IPC_BUFFER, IPC_BUFFER + IPC_USED - keep, keep);
                IPC_USED = keep;
            }
            memcpy(IPC_BUFFER + IPC_USED, buf, (size_t)n);
            IPC_USED += (size_t)n;

            size_t start = 0;
            for (size_t i = 0; i < IPC_USED; i++) {
                if (IPC_BUFFER[i] != '\n') continue;
                IPC_BUFFER[i] = 0;
                parse_backend_line(IPC_BUFFER + start);
                start = i + 1;
            }
            if (start) {
                memmove(IPC_BUFFER, IPC_BUFFER + start, IPC_USED - start);
                IPC_USED -= start;
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n == 0 || (n < 0 && errno != EINTR)) return;
        if (n < 0 && errno == EINTR) continue;
        return;
    }
}

static void finish_backend_if_needed(void)
{
    if (INSTALL_PID <= 0) return;
    int status = 0;
    pid_t r = waitpid(INSTALL_PID, &status, WNOHANG);
    if (r != INSTALL_PID) return;

    drain_backend_output();
    if (INSTALL_FD >= 0) {
        if (IPC_USED) {
            IPC_BUFFER[IPC_USED] = 0;
            parse_backend_line(IPC_BUFFER);
            IPC_USED = 0;
        }
        close(INSTALL_FD);
        INSTALL_FD = -1;
    }
    INSTALL_PID = -1;
    BACKEND_ACTIVE = false;

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && INSTALL_PROGRESS >= 100) {
        BACKEND_VERIFIED = true;
        INSTALL_PROGRESS = 100;
        DISPLAY_PROGRESS = 100;
        snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Complete");
        snprintf(CURRENT_STEP_TEXT, sizeof CURRENT_STEP_TEXT, "Installation verified successfully.");
        add_log("SUCCESS: backend exited 0 after verification; 100%% is now authoritative.");
        STAGE = STAGE_COMPLETE;
        if (INSTALL_TIMER) {
            ml_timer_disarm(INSTALL_TIMER);
            INSTALL_TIMER = NULL;
        }
    } else if (!BACKEND_FAILED) {
        char msg[320];
        if (WIFEXITED(status))
            snprintf(msg, sizeof msg, "Installation stopped before verification completed (exit status %d).", WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            snprintf(msg, sizeof msg, "Installation process stopped unexpectedly (signal %d).", WTERMSIG(status));
        else
            snprintf(msg, sizeof msg, "Installation process stopped unexpectedly.");
        set_failure(msg);
        return;
    }
    draw();
}

static void install_gui_tick(void *ud)
{
    (void)ud;
    if (STAGE != STAGE_INSTALLING) return;

    ACTIVITY_FRAME++;
    drain_backend_output();
    finish_backend_if_needed();

    uint64_t now = now_ms();
    if (BACKEND_ACTIVE && now - LAST_EVENT_MS >= ACTIVITY_AFTER_MS) {
        /* Activity is visual only. INSTALL_PROGRESS is deliberately unchanged. */
        LAST_EVENT_MS = LAST_EVENT_MS; /* explicit: do not advance authoritative progress */
    }

    /* Smooth interpolation toward the last authoritative value; never beyond it. */
    if (DISPLAY_PROGRESS < INSTALL_PROGRESS) {
        int delta = INSTALL_PROGRESS - DISPLAY_PROGRESS;
        DISPLAY_PROGRESS += delta > 3 ? 3 : 1;
        if (DISPLAY_PROGRESS > INSTALL_PROGRESS) DISPLAY_PROGRESS = INSTALL_PROGRESS;
    } else if (DISPLAY_PROGRESS > INSTALL_PROGRESS) {
        DISPLAY_PROGRESS = INSTALL_PROGRESS;
    }
    draw();
}

static bool start_installation(void)
{
    if (TARGET_DISK_IDX < 0 || !CONFIRMED_ERASE) return false;
    const char *backend = backend_path();
    if (!backend) {
        set_failure("G1OS installer backend is not installed or executable.");
        return false;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        set_failure("Unable to create the installer progress channel.");
        return false;
    }
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    const char *target = DISKS[TARGET_DISK_IDX].devnode;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        set_failure("Unable to start the installation backend.");
        return false;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        if (TEST_MODE)
            execl(backend, backend, "--target", target, "--dry-run", "--json", (char *)NULL);
        else
            execl(backend, backend, "--target", target, "--json", (char *)NULL);
        dprintf(STDERR_FILENO, "ERROR: failed to exec G1OS installer backend: %s\n", strerror(errno));
        _exit(127);
    }

    close(pipefd[1]);
    INSTALL_PID = pid;
    INSTALL_FD = pipefd[0];
    IPC_USED = 0;
    INSTALL_PROGRESS = 0;
    DISPLAY_PROGRESS = 0;
    LAST_STEP = 0;
    INSTALL_STARTED_MS = now_ms();
    LAST_EVENT_MS = INSTALL_STARTED_MS;
    ACTIVITY_FRAME = 0;
    BACKEND_ACTIVE = true;
    BACKEND_FAILED = false;
    BACKEND_VERIFIED = false;
    N_LOG_LINES = 0;
    ERROR_MESSAGE[0] = 0;
    snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Preparing");
    snprintf(CURRENT_STEP_TEXT, sizeof CURRENT_STEP_TEXT, "Starting verified G1OS installation...");
    add_log("INSTALL: target=%s", target);
    add_log("INSTALL: authoritative progress channel connected");

    STAGE = STAGE_INSTALLING;
    INSTALL_TIMER = mica_add_timer(G, GUI_TICK_MS, true, install_gui_tick, NULL);
    draw();
    return true;
}

static void draw_progress_bar(ml_ctx *c, int x, int y, int w, int h, int progress)
{
    ml_fill_rounded(c, ml_rect_make(x, y, w, h), h / 2, ml_rgba(255, 255, 255, 28));
    if (progress > 0) {
        int fill = (w * progress) / 100;
        if (fill > 0) ml_fill_rounded(c, ml_rect_make(x, y, fill, h), h / 2, ml_rgb(78, 157, 255));
    }
}

static void draw(void)
{
    ml_surface *s = WIN->surf;
    ml_ctx c;
    ml_ctx_init(&c, s, ml_rect_make(0, 0, s->w, s->h));
    shell_panel_bg(&c, ml_rect_make(0, 0, s->w, s->h), 0, G->info.mode, true);
    ml_font *f = ml_font_get("mica-sans");
    ml_font *fb = ml_font_get("mica-sans-bold");

    /* Minimal native G1OS installer surface. */
    ml_fill_rounded(&c, ml_rect_make(18, 18, s->w - 36, s->h - 36), 18, ml_rgba(22, 25, 34, 238));
    ml_stroke_rounded(&c, ml_rect_make(18, 18, s->w - 36, s->h - 36), 18, 1.0, ml_rgba(255, 255, 255, 24));

    int cx = s->w / 2;
    ml_draw_text(&c, fb, cx - 28, 66, "G1OS", 24, ml_rgb(248, 250, 255));
    ml_draw_text(&c, f, cx - 86, 88, "Giving life to older machines", 12, ml_rgb(155, 181, 220));

    if (STAGE == STAGE_WELCOME) {
        ml_draw_text(&c, fb, cx - 126, 156, "Install G1OS", 22, ml_rgb(248, 250, 255));
        ml_draw_text(&c, f, cx - 205, 184, "A lightweight system designed for older hardware.", 13, ml_rgb(190, 198, 214));
        ml_draw_text(&c, f, cx - 196, 207, "Your existing data is protected until you explicitly confirm erasure.", 12, ml_rgb(145, 153, 170));
        ml_rect b = ml_rect_make(cx - 82, 272, 164, 42);
        ml_fill_rounded(&c, b, 12, ml_rgb(52, 133, 242));
        ml_draw_text(&c, fb, cx - 34, 298, "Continue", 13, ml_rgb(255, 255, 255));

    } else if (STAGE == STAGE_SELECT) {
        ml_draw_text(&c, fb, 48, 124, "Choose where to install G1OS", 18, ml_rgb(245, 248, 255));
        ml_draw_text(&c, f, 48, 146, "Live USB media is automatically protected.", 12, ml_rgb(160, 169, 188));
        int y = 170;
        for (int i = 0; i < N_DISKS; i++) {
            disk_info *d = &DISKS[i];
            bool sel = i == TARGET_DISK_IDX;
            ml_rect r = ml_rect_make(48, y, s->w - 96, 62);
            ml_fill_rounded(&c, r, 12, d->is_usb_boot ? ml_rgba(80, 70, 50, 80) : (sel ? ml_rgba(50, 110, 210, 90) : ml_rgba(255, 255, 255, 10)));
            ml_stroke_rounded(&c, r, 12, 1.0, d->is_usb_boot ? ml_rgba(255, 180, 80, 90) : (sel ? ml_rgba(90, 170, 255, 180) : ml_rgba(255, 255, 255, 18)));
            ml_icon_draw(&c, d->is_usb_boot ? "usb" : "drive", ml_rect_make(62, y + 14, 34, 34), d->is_usb_boot ? ml_rgb(230, 180, 100) : ml_rgb(130, 190, 255));
            ml_draw_text(&c, fb, 108, y + 25, d->model, 13, ml_rgb(242, 245, 252));
            char detail[160];
            double gb = (double)d->size_bytes / 1e9;
            snprintf(detail, sizeof detail, "%s · %.1f GB · %s", d->devnode, gb, d->is_usb_boot ? "Protected live media" : (d->is_removable ? "Removable" : "Internal disk"));
            ml_draw_text(&c, f, 108, y + 45, detail, 11, d->is_usb_boot ? ml_rgb(240, 185, 105) : ml_rgb(160, 170, 190));
            y += 72;
        }
        ml_rect b = ml_rect_make(s->w - 156, s->h - 74, 108, 36);
        ml_fill_rounded(&c, b, 10, TARGET_DISK_IDX >= 0 ? ml_rgb(52, 133, 242) : ml_rgba(255, 255, 255, 20));
        ml_draw_text(&c, fb, s->w - 132, s->h - 51, "Continue", 12, ml_rgb(255, 255, 255));

    } else if (STAGE == STAGE_CONFIRM) {
        ml_draw_text(&c, fb, cx - 158, 126, "Confirm installation", 19, ml_rgb(245, 248, 255));
        if (TARGET_DISK_IDX >= 0) {
            disk_info *d = &DISKS[TARGET_DISK_IDX];
            ml_draw_text(&c, fb, 64, 166, d->model, 14, ml_rgb(235, 240, 250));
            ml_draw_text(&c, f, 64, 188, d->devnode, 12, ml_rgb(155, 166, 185));
            ml_fill_rounded(&c, ml_rect_make(52, 214, s->w - 104, 70), 12, ml_rgba(220, 60, 55, 42));
            ml_draw_text(&c, fb, 68, 238, "This will erase the selected disk.", 14, ml_rgb(255, 120, 115));
            ml_draw_text(&c, f, 68, 260, "All existing partitions and data will be permanently removed.", 12, ml_rgb(238, 200, 200));
            ml_stroke_rounded(&c, ml_rect_make(56, 304, 20, 20), 5, 1.5, ml_rgba(255, 255, 255, 120));
            if (CONFIRMED_ERASE) {
                ml_fill_rounded(&c, ml_rect_make(58, 306, 16, 16), 4, ml_rgb(52, 133, 242));
                ml_icon_draw(&c, "check", ml_rect_make(58, 306, 16, 16), ml_rgb(255, 255, 255));
            }
            ml_draw_text(&c, f, 86, 320, "I understand this disk will be erased.", 12, ml_rgb(205, 212, 225));
        }
        ml_rect b = ml_rect_make(s->w - 238, s->h - 74, 190, 36);
        ml_fill_rounded(&c, b, 10, CONFIRMED_ERASE ? ml_rgb(214, 61, 55) : ml_rgba(255, 255, 255, 18));
        ml_draw_text(&c, fb, s->w - 214, s->h - 51, "Erase & Install", 12, ml_rgb(255, 255, 255));

    } else if (STAGE == STAGE_INSTALLING) {
        ml_draw_text(&c, fb, cx - 102, 126, "Installing G1OS", 22, ml_rgb(248, 250, 255));
        ml_draw_text(&c, f, cx - 120, 150, CURRENT_STAGE_NAME, 12, ml_rgb(125, 183, 255));

        int bar_x = 76, bar_y = 190, bar_w = s->w - 152;
        draw_progress_bar(&c, bar_x, bar_y, bar_w, 12, DISPLAY_PROGRESS);
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", INSTALL_PROGRESS);
        ml_draw_text(&c, fb, cx - 18, 234, pct, 24, ml_rgb(245, 248, 255));
        ml_draw_text(&c, f, cx - 170, 266, CURRENT_STEP_TEXT, 13, ml_rgb(198, 205, 220));

        uint64_t elapsed = now_ms() - INSTALL_STARTED_MS;
        char elapsed_text[64];
        snprintf(elapsed_text, sizeof elapsed_text, "Elapsed %02llu:%02llu", (unsigned long long)(elapsed / 60000ULL), (unsigned long long)((elapsed / 1000ULL) % 60ULL));
        ml_draw_text(&c, f, cx - 45, 290, elapsed_text, 11, ml_rgb(125, 134, 151));

        bool active = BACKEND_ACTIVE && !BACKEND_FAILED;
        if (active) {
            const char *dots[] = {"•", "••", "•••"};
            const char *d = dots[ACTIVITY_FRAME % 3];
            ml_draw_text(&c, f, cx - 15, 322, d, 14, ml_rgb(110, 175, 245));
        }
        ml_draw_text(&c, f, cx - 165, 348, active ? "The installer is working. Progress is based on verified operations." : "Waiting for verified installer state...", 11, ml_rgb(120, 130, 148));

        ml_rect toggle = ml_rect_make(48, 378, s->w - 96, 28);
        ml_fill_rounded(&c, toggle, 8, ml_rgba(255, 255, 255, 9));
        ml_draw_text(&c, fb, 60, 397, SHOW_DETAILS ? "▼ Installation Details" : "▶ Installation Details", 10, ml_rgb(155, 180, 215));
        if (SHOW_DETAILS) {
            ml_rect logbox = ml_rect_make(48, 410, s->w - 96, 82);
            ml_fill_rounded(&c, logbox, 8, ml_rgba(5, 7, 10, 210));
            int first = N_LOG_LINES > 4 ? N_LOG_LINES - 4 : 0;
            for (int i = first; i < N_LOG_LINES; i++)
                ml_draw_text(&c, f, 58, 428 + (i - first) * 16, LOG_LINES[i], 9, ml_rgb(160, 170, 188));
        }

    } else if (STAGE == STAGE_COMPLETE) {
        ml_icon_draw(&c, "check", ml_rect_make(cx - 32, 132, 64, 64), ml_rgb(80, 210, 135));
        ml_draw_text(&c, fb, cx - 124, 230, "Installation Complete", 20, ml_rgb(245, 250, 248));
        ml_draw_text(&c, f, cx - 155, 255, "G1OS has been successfully installed and verified.", 12, ml_rgb(180, 192, 205));
        ml_draw_text(&c, f, cx - 140, 278, "Remove the USB installer before restarting.", 12, ml_rgb(150, 163, 180));
        ml_rect b = ml_rect_make(cx - 112, 322, 224, 42);
        ml_fill_rounded(&c, b, 12, ml_rgb(52, 133, 242));
        ml_draw_text(&c, fb, cx - 55, 348, "Restart", 13, ml_rgb(255, 255, 255));

    } else if (STAGE == STAGE_ERROR) {
        ml_icon_draw(&c, "close", ml_rect_make(cx - 30, 122, 60, 60), ml_rgb(245, 80, 75));
        ml_draw_text(&c, fb, cx - 105, 214, "Installation Failed", 19, ml_rgb(255, 105, 100));
        ml_draw_text(&c, f, 60, 244, ERROR_MESSAGE, 12, ml_rgb(238, 190, 190));
        ml_draw_text(&c, f, 60, 270, "The progress value has been stopped at the last verified point.", 11, ml_rgb(150, 158, 175));
        ml_rect logbox = ml_rect_make(52, 294, s->w - 104, 112);
        ml_fill_rounded(&c, logbox, 10, ml_rgba(5, 7, 10, 220));
        int first = N_LOG_LINES > 6 ? N_LOG_LINES - 6 : 0;
        for (int i = first; i < N_LOG_LINES; i++)
            ml_draw_text(&c, f, 64, 314 + (i - first) * 16, LOG_LINES[i], 9, ml_rgb(205, 155, 155));
        ml_rect b = ml_rect_make(cx - 70, 430, 140, 36);
        ml_fill_rounded(&c, b, 10, ml_rgb(52, 133, 242));
        ml_draw_text(&c, fb, cx - 34, 453, "Try Again", 12, ml_rgb(255, 255, 255));
    }

    mica_win_commit(WIN);
}

static void input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (in->kind == IN_KEY) {
        if ((in->key == 0xff1b || in->key == 'q') && STAGE != STAGE_INSTALLING) mica_quit(G, 0);
        return;
    }
    if (in->kind != IN_DOWN && in->kind != IN_CLICK) return;
    int x = in->x, y = in->y;
    int cx = WIN_W / 2;

    if (STAGE == STAGE_WELCOME) {
        if (x >= cx - 82 && x <= cx + 82 && y >= 272 && y <= 314) {
            STAGE = STAGE_SELECT;
            probe_disks();
            draw();
        }
    } else if (STAGE == STAGE_SELECT) {
        int dy = 170;
        for (int i = 0; i < N_DISKS; i++) {
            if (x >= 48 && x <= WIN_W - 48 && y >= dy && y <= dy + 62 && !DISKS[i].is_usb_boot) {
                TARGET_DISK_IDX = i;
                for (int j = 0; j < N_DISKS; j++) DISKS[j].is_target = j == i;
                CONFIRMED_ERASE = false;
                draw();
                return;
            }
            dy += 72;
        }
        if (x >= WIN_W - 156 && x <= WIN_W - 48 && y >= WIN_H - 74 && y <= WIN_H - 38 && TARGET_DISK_IDX >= 0) {
            STAGE = STAGE_CONFIRM;
            draw();
        }
    } else if (STAGE == STAGE_CONFIRM) {
        if (x >= 48 && x <= WIN_W - 48 && y >= 296 && y <= 334) {
            CONFIRMED_ERASE = !CONFIRMED_ERASE;
            draw();
            return;
        }
        if (x >= WIN_W - 238 && x <= WIN_W - 48 && y >= WIN_H - 74 && y <= WIN_H - 38 && CONFIRMED_ERASE)
            start_installation();
    } else if (STAGE == STAGE_INSTALLING) {
        if (x >= 48 && x <= WIN_W - 48 && y >= 378 && y <= 406) {
            SHOW_DETAILS = !SHOW_DETAILS;
            draw();
        }
    } else if (STAGE == STAGE_COMPLETE) {
        if (x >= cx - 112 && x <= cx + 112 && y >= 322 && y <= 364) {
            add_log("RESTART: user requested reboot after verified installation.");
            if (!TEST_MODE) system("reboot 2>/dev/null || systemctl reboot 2>/dev/null || shutdown -r now 2>/dev/null");
            mica_quit(G, 0);
        }
    } else if (STAGE == STAGE_ERROR) {
        if (x >= cx - 70 && x <= cx + 70 && y >= 430 && y <= 466) {
            stop_backend(true);
            STAGE = STAGE_SELECT;
            CONFIRMED_ERASE = false;
            BACKEND_FAILED = false;
            probe_disks();
            draw();
        }
    }
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--test") || !strcmp(argv[i], "--dry-run")) TEST_MODE = true;

    ml_log_init("mica-installer", ML_LOG_WARN, getenv("MICA_LOG"));
    G = mica_connect("installer");
    if (!G) return 1;
    WIN = mica_win_new(G, WIN_W, WIN_H, "G1OS Installer", "installer", 0);
    if (!WIN) { mica_quit(G, 1); return 1; }
    mica_win_place(WIN, (G->info.screen_w - WIN_W) / 2, (G->info.screen_h - WIN_H) / 2);
    WIN->on_input = input;
    probe_disks();
    draw();
    return mica_run(G);
}
