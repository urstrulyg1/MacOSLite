/* G1OS native installer GUI — Final Verification & No-Break Guarantee.
 *
 * The GUI is presentation only: destructive installation work is delegated to
 * maclite-installer-backend. Progress is therefore authoritative and cannot
 * be fabricated by the UI. The backend emits JSON progress events while the
 * GUI remains responsive, shows activity separately from completion, and
 * refuses to show success until the backend exits successfully after its
 * verification phase.
 *
 * Verification Flow:
 *   Installation -> Finalizing -> Verification -> All Checks Pass -> 100% -> Summary -> Complete
 * If any required check fails:
 *   Verification -> Fail -> Stop -> Show Exact Problem -> Retry / Repair / View Details
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
#include <limits.h>

#define WIN_W 720
#define WIN_H 520
#define GUI_TICK_MS 250
#define ACTIVITY_AFTER_MS 1000
#define MAX_LOG_LINES 64

typedef enum {
    STAGE_WELCOME = 0,
    STAGE_SELECT,
    STAGE_CONFIRM,
    STAGE_INSTALLING,
    STAGE_VERIFYING,
    STAGE_SUMMARY,
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

typedef struct {
    char name[48];
    bool passed;
} verify_check_item;

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
static char FAILED_CHECK_NAME[64] = "";
static char LOG_LINES[MAX_LOG_LINES][192];
static int N_LOG_LINES = 0;
static uint64_t INSTALL_STARTED_MS = 0;
static uint64_t LAST_EVENT_MS = 0;
static unsigned ACTIVITY_FRAME = 0;
static bool BACKEND_ACTIVE = false;
static bool BACKEND_FAILED = false;
static bool BACKEND_VERIFIED = false;

/* 10 domain summary checklist */
static verify_check_item SUMMARY_CHECKS[10] = {
    { "System files", false },
    { "Filesystem", false },
    { "Bootloader", false },
    { "Kernel", false },
    { "Graphics", false },
    { "Desktop", false },
    { "Drivers", false },
    { "Configuration", false },
    { "Required services", false },
    { "Installation integrity", false }
};

static void draw(void);
static void input(mica_win *w, const msg_input *in);
static bool start_backend(bool repair_flag);

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

static bool forbidden_virtual_name(const char *name)
{
    if (!name || !name[0]) return true;
    return !strncmp(name, "loop", 4) || !strncmp(name, "ram", 3) ||
           !strncmp(name, "zram", 4) || !strncmp(name, "dm-", 3) ||
           !strncmp(name, "md", 2) || !strncmp(name, "sr", 2) ||
           !strncmp(name, "fd", 2) || !strncmp(name, "nbd", 3);
}

static bool read_sysfs_size_bytes(const char *name, uint64_t *out)
{
    char path[256], buf[64];
    snprintf(path, sizeof path, "/sys/block/%s/size", name);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = 0;
    char *end = NULL;
    errno = 0;
    unsigned long long sectors = strtoull(buf, &end, 10);
    if (errno || end == buf || sectors == 0 || sectors > UINT64_MAX / 512ULL) return false;
    *out = (uint64_t)sectors * 512ULL;
    return *out != 0;
}

static bool sysfs_whole_physical_disk(const char *name, const char *devnode, uint64_t *size_bytes)
{
    struct stat st;
    if (!name || !devnode || forbidden_virtual_name(name)) return false;
    if (stat(devnode, &st) != 0 || !S_ISBLK(st.st_mode)) return false;

    char partition[256];
    snprintf(partition, sizeof partition, "/sys/block/%s/partition", name);
    if (access(partition, F_OK) == 0) return false;

    uint64_t bytes = 0;
    if (!read_sysfs_size_bytes(name, &bytes)) return false;

    char device_link[256], resolved[PATH_MAX];
    snprintf(device_link, sizeof device_link, "/sys/block/%s/device", name);
    if (realpath(device_link, resolved) == NULL) return false;

    if (size_bytes) *size_bytes = bytes;
    return true;
}

static const char *transport_name(const char *name)
{
    static char transport[16];
    char link[PATH_MAX];
    char device[256];
    transport[0] = 0;
    snprintf(device, sizeof device, "/sys/block/%s/device", name);
    if (!realpath(device, link)) return "Unknown";
    if (strstr(link, "/usb")) return "USB";
    if (strstr(link, "/mmc")) return "SD/Card";
    if (strstr(link, "/nvme")) return "NVMe";
    if (strstr(link, "/ata")) return "SATA";
    if (strstr(link, "/virtio")) return "VirtIO";
    if (strstr(link, "/firewire")) return "FireWire";
    return "Physical";
}

static const char *whole_disk_from_source(const char *src)
{
    static char result[64];
    result[0] = 0;
    if (!src || strncmp(src, "/dev/", 5) != 0) return NULL;

    const char *base = src + 5;
    if (forbidden_virtual_name(base)) {
        if (strncmp(base, "loop", 4) == 0) {
            char backing[PATH_MAX], loop_path[256], mountpoint[PATH_MAX];
            snprintf(loop_path, sizeof loop_path, "/sys/class/block/%s/loop/backing_file", base);
            int fd = open(loop_path, O_RDONLY | O_CLOEXEC);
            if (fd >= 0) {
                ssize_t n = read(fd, backing, sizeof(backing) - 1);
                close(fd);
                if (n > 0) {
                    backing[n] = 0;
                    while (n > 0 && (backing[n-1] == '\n' || backing[n-1] == '\r')) backing[--n] = 0;
                    snprintf(mountpoint, sizeof mountpoint, "%s", backing);
                    char *mp = realpath(mountpoint, NULL);
                    if (mp) {
                        char cmd[PATH_MAX + 64];
                        snprintf(cmd, sizeof cmd, "findmnt -n -o SOURCE -T '%s' 2>/dev/null", mp);
                        FILE *p = popen(cmd, "r");
                        if (p) {
                            char parent[128] = {0};
                            if (fgets(parent, sizeof parent, p)) {
                                char *nl = strpbrk(parent, "\r\n");
                                if (nl) *nl = 0;
                                pclose(p);
                                const char *resolved_parent = whole_disk_from_source(parent);
                                if (resolved_parent) {
                                    snprintf(result, sizeof result, "%s", resolved_parent);
                                    free(mp);
                                    return result;
                                }
                            } else pclose(p);
                        }
                        free(mp);
                    }
                }
            }
        }
        return NULL;
    }

    char part[256];
    snprintf(part, sizeof part, "/sys/class/block/%s/partition", base);
    if (access(part, F_OK) == 0) {
        char pk[256];
        snprintf(pk, sizeof pk, "/sys/class/block/%s/partition", base);
        (void)pk;
        char cmd[256];
        snprintf(cmd, sizeof cmd, "lsblk -ndo PKNAME /dev/%s 2>/dev/null", base);
        FILE *p = popen(cmd, "r");
        if (!p) return NULL;
        if (!fgets(result, sizeof result, p)) { pclose(p); result[0]=0; return NULL; }
        pclose(p);
        char *nl = strpbrk(result, "\r\n");
        if (nl) *nl=0;
        return result[0] ? result : NULL;
    }
    snprintf(result, sizeof result, "%s", base);
    return result;
}

static bool is_device_live_boot(const char *name)
{
    if (!name || !name[0]) return false;
    for (const char *mp = "/run/live"; mp; ) {
        char source[128] = {0};
        char cmd[256];
        snprintf(cmd, sizeof cmd, "findmnt -n -o SOURCE '%s' 2>/dev/null", mp);
        FILE *p = popen(cmd, "r");
        if (p) {
            if (fgets(source, sizeof source, p)) {
                char *nl = strpbrk(source, "\r\n");
                if (nl) *nl=0;
            }
            pclose(p);
        }
        const char *disk = whole_disk_from_source(source);
        if (disk && !strcmp(disk, name)) return true;
        break;
    }

    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (!strstr(line, "/run/maclite-base") && !strstr(line, "/run/maclite-live") &&
            !strstr(line, "/cdrom") && !strstr(line, "/mnt/live")) continue;
        char src[128] = {0}, mp[128] = {0};
        if (sscanf(line, "%127s %127s", src, mp) == 2) {
            const char *disk = whole_disk_from_source(src);
            if (disk && !strcmp(disk, name)) { fclose(f); return true; }
        }
    }
    fclose(f);
    return false;
}

static bool revalidate_target_disk(const disk_info *d)
{
    if (!d || !d->name[0] || !d->devnode[0]) return false;
    if (forbidden_virtual_name(d->name)) return false;
    uint64_t bytes = 0;
    if (!sysfs_whole_physical_disk(d->name, d->devnode, &bytes)) return false;
    if (bytes != d->size_bytes || bytes == 0) return false;
    if (is_device_live_boot(d->name)) return false;
    return true;
}

static void probe_disks(void)
{
    N_DISKS = 0;
    TARGET_DISK_IDX = -1;
    mica_storage_state s;
    mica_storage_probe(&s);

    for (int i = 0; i < s.n && N_DISKS < 8; i++) {
        if (!s.v[i].whole || forbidden_virtual_name(s.v[i].name)) continue;
        disk_info *d = &DISKS[N_DISKS];
        memset(d, 0, sizeof *d);
        snprintf(d->name, sizeof d->name, "%s", s.v[i].name);
        snprintf(d->devnode, sizeof d->devnode, "%s", s.v[i].devnode);

        uint64_t authoritative_size = 0;
        if (!sysfs_whole_physical_disk(d->name, d->devnode, &authoritative_size))
            continue; /* cannot prove this is a real whole disk: never guess */
        if (authoritative_size == 0) continue;

        char vendor[48] = {0}, model[96] = {0};
        snprintf(vendor, sizeof vendor, "%s", s.v[i].vendor);
        snprintf(model, sizeof model, "%s", s.v[i].model);
        snprintf(d->model, sizeof d->model, "%s %s", vendor, model);
        ml_str_trim(d->model);
        if (!d->model[0]) snprintf(d->model, sizeof d->model, "%s disk", transport_name(d->name));
        d->size_bytes = authoritative_size;
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

    if (TEST_MODE && N_DISKS == 0) {
        disk_info *d = &DISKS[0];
        memset(d, 0, sizeof *d);
        snprintf(d->name, sizeof d->name, "sda");
        snprintf(d->devnode, sizeof d->devnode, "/dev/sda");
        snprintf(d->model, sizeof d->model, "G1OS Internal SSD");
        d->size_bytes = 500ULL * 1000ULL * 1000ULL * 1000ULL;
        d->partition_count = 2;
        snprintf(d->partitions[0], sizeof d->partitions[0], "sda1: 256 MB (EFI)");
        snprintf(d->partitions[1], sizeof d->partitions[1], "sda2: 499 GB (data)");
        N_DISKS = 1;
    }

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

static void apply_progress_event(int step, int progress, const char *message, const char *state)
{
    if (progress < INSTALL_PROGRESS || progress > 100) return;
    INSTALL_PROGRESS = progress;
    LAST_STEP = step;
    LAST_EVENT_MS = now_ms();
    if (message && message[0]) snprintf(CURRENT_STEP_TEXT, sizeof CURRENT_STEP_TEXT, "%s", message);

    if (state && !strcmp(state, "VERIFYING")) {
        snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Verification");
        if (STAGE == STAGE_INSTALLING) STAGE = STAGE_VERIFYING;
    } else {
        switch (step) {
        case 1:
        case 2: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Preparing"); break;
        case 3: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Partitioning"); break;
        case 4: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Formatting"); break;
        case 5: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Installing"); break;
        case 6: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Bootloader"); break;
        case 7: snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Finalizing"); break;
        default:
            if (progress >= 80 && progress < 100) {
                snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Verification");
                if (STAGE == STAGE_INSTALLING) STAGE = STAGE_VERIFYING;
            }
            break;
        }
    }
    add_log("PROGRESS: %d%% — %s", INSTALL_PROGRESS, CURRENT_STEP_TEXT);
}

static void mark_summary_check(const char *domain, bool passed)
{
    for (int i = 0; i < 10; i++) {
        if (strstr(SUMMARY_CHECKS[i].name, domain) || strstr(domain, SUMMARY_CHECKS[i].name)) {
            SUMMARY_CHECKS[i].passed = passed;
            break;
        }
    }
}

static void parse_backend_line(char *line)
{
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (!line[0]) return;
    add_log("%s", line);

    if (strstr(line, "\"status\":\"error\"") || strstr(line, "Verification FAIL:")) {
        const char *p = strstr(line, "\"message\":\"");
        if (p) {
            p += 11;
            char msg[320] = {0};
            size_t i = 0;
            while (p[i] && p[i] != '"' && i + 1 < sizeof msg) { msg[i] = p[i]; i++; }
            msg[i] = 0;
            set_failure(msg[0] ? msg : "The installation backend reported an error.");
        } else if (strstr(line, "Verification FAIL:")) {
            const char *vf = strstr(line, "Verification FAIL:");
            set_failure(vf + 18);
        } else {
            set_failure("The installation backend reported an error.");
        }
        return;
    }

    if (strstr(line, "\"type\":\"state\"")) {
        if (strstr(line, "\"state\":\"VERIFYING\"")) {
            STAGE = STAGE_VERIFYING;
            snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Verification");
        } else if (strstr(line, "\"state\":\"COMPLETED\"")) {
            BACKEND_VERIFIED = true;
        }
        return;
    }

    if (strstr(line, "\"type\":\"verify\"")) {
        STAGE = STAGE_VERIFYING;
        snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, "Verification");
        int progress = -1;
        char domain[64] = {0}, status[32] = {0}, msg[192] = {0};
        if (sscanf(line, "{\"type\":\"verify\",\"num\":%*d,\"progress\":%d,\"domain\":\"%63[^\"]\",\"status\":\"%31[^\"]\",\"message\":\"%191[^\"]",
                   &progress, domain, status, msg) >= 3) {
            if (progress > INSTALL_PROGRESS) INSTALL_PROGRESS = progress;
            bool ok = strcmp(status, "PASS") == 0;
            mark_summary_check(domain, ok);
            snprintf(CURRENT_STEP_TEXT, sizeof CURRENT_STEP_TEXT, "%s: %s", domain, msg);
            add_log("VERIFY: [%s] %s — %s", status, domain, msg);
            if (!ok) {
                snprintf(FAILED_CHECK_NAME, sizeof FAILED_CHECK_NAME, "%s", domain);
                set_failure(msg);
            }
        }
        return;
    }

    if (strstr(line, "\"type\":\"verification_summary\"")) {
        for (int i = 0; i < 10; i++) SUMMARY_CHECKS[i].passed = true;
        BACKEND_VERIFIED = true;
        return;
    }

    int step = 0, progress = -1;
    char message[192] = {0}, state[64] = {0};
    if (sscanf(line, "{\"step\":%d,\"progress\":%d,\"message\":\"%191[^\"]\",\"state\":\"%63[^\"]", &step, &progress, message, state) >= 3) {
        apply_progress_event(step, progress, message, state);
        return;
    } else if (sscanf(line, "{\"step\":%d,\"progress\":%d,\"message\":\"%191[^\"]", &step, &progress, message) == 3) {
        apply_progress_event(step, progress, message, "INSTALLING");
        return;
    }

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
        snprintf(CURRENT_STEP_TEXT, sizeof CURRENT_STEP_TEXT, "All required verification checks passed.");
        add_log("SUCCESS: backend verified healthy; transitioning to summary screen.");
        STAGE = STAGE_SUMMARY;
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
    if (STAGE != STAGE_INSTALLING && STAGE != STAGE_VERIFYING) return;

    ACTIVITY_FRAME++;
    drain_backend_output();
    finish_backend_if_needed();

    if (DISPLAY_PROGRESS < INSTALL_PROGRESS) {
        int delta = INSTALL_PROGRESS - DISPLAY_PROGRESS;
        DISPLAY_PROGRESS += delta > 3 ? 3 : 1;
        if (DISPLAY_PROGRESS > INSTALL_PROGRESS) DISPLAY_PROGRESS = INSTALL_PROGRESS;
    } else if (DISPLAY_PROGRESS > INSTALL_PROGRESS) {
        DISPLAY_PROGRESS = INSTALL_PROGRESS;
    }
    draw();
}

static bool start_backend(bool repair_flag)
{
    /* Re-enumerate and revalidate immediately before the destructive backend is spawned.
     * Device nodes can disappear/reappear between selection and installation. */
    if (TARGET_DISK_IDX < 0 || !CONFIRMED_ERASE) return false;
    if (!revalidate_target_disk(&DISKS[TARGET_DISK_IDX])) {
        set_failure("The selected disk changed, disappeared, became unavailable, or is the live installation media. Please rescan and select it again.");
        probe_disks();
        return false;
    }
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
        if (TEST_MODE) {
            if (repair_flag)
                execl(backend, backend, "--target", target, "--dry-run", "--json", "--repair", (char *)NULL);
            else
                execl(backend, backend, "--target", target, "--dry-run", "--json", (char *)NULL);
        } else {
            if (repair_flag)
                execl(backend, backend, "--target", target, "--json", "--repair", (char *)NULL);
            else
                execl(backend, backend, "--target", target, "--json", (char *)NULL);
        }
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
    FAILED_CHECK_NAME[0] = 0;
    for (int i = 0; i < 10; i++) SUMMARY_CHECKS[i].passed = false;
    snprintf(CURRENT_STAGE_NAME, sizeof CURRENT_STAGE_NAME, repair_flag ? "Repairing" : "Preparing");
    snprintf(CURRENT_STEP_TEXT, sizeof CURRENT_STEP_TEXT, repair_flag ? "Repairing and re-verifying installation..." : "Starting verified G1OS installation...");
    add_log("INSTALL: target=%s (repair=%d)", target, repair_flag ? 1 : 0);

    STAGE = STAGE_INSTALLING;
    INSTALL_TIMER = mica_add_timer(G, GUI_TICK_MS, true, install_gui_tick, NULL);
    draw();
    return true;
}

static void draw_progress_bar(ml_ctx *c, int x, int y, int w, int h, int progress, ml_color fill_col)
{
    ml_fill_rounded(c, ml_rect_make(x, y, w, h), h / 2, ml_rgba(255, 255, 255, 28));
    if (progress > 0) {
        int fill = (w * progress) / 100;
        if (fill > 0) ml_fill_rounded(c, ml_rect_make(x, y, fill, h), h / 2, fill_col);
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

    /* Base modal container */
    ml_fill_rounded(&c, ml_rect_make(18, 18, s->w - 36, s->h - 36), 18, ml_rgba(22, 25, 34, 238));
    ml_stroke_rounded(&c, ml_rect_make(18, 18, s->w - 36, s->h - 36), 18, 1.0, ml_rgba(255, 255, 255, 24));

    int cx = s->w / 2;
    ml_draw_text(&c, fb, cx - 28, 54, "G1OS", 24, ml_rgb(248, 250, 255));
    ml_draw_text(&c, f, cx - 86, 76, "Giving life to older machines", 12, ml_rgb(155, 181, 220));

    if (STAGE == STAGE_WELCOME) {
        ml_draw_text(&c, fb, cx - 126, 146, "Install G1OS", 22, ml_rgb(248, 250, 255));
        ml_draw_text(&c, f, cx - 205, 174, "A lightweight system designed for older hardware.", 13, ml_rgb(190, 198, 214));
        ml_draw_text(&c, f, cx - 215, 197, "Includes mandatory Final Verification & No-Break Guarantee.", 12, ml_rgb(125, 190, 255));
        ml_rect b = ml_rect_make(cx - 82, 262, 164, 42);
        ml_fill_rounded(&c, b, 12, ml_rgb(52, 133, 242));
        ml_draw_text(&c, fb, cx - 34, 288, "Continue", 13, ml_rgb(255, 255, 255));

    } else if (STAGE == STAGE_SELECT) {
        ml_draw_text(&c, fb, 48, 114, "Choose where to install G1OS", 18, ml_rgb(245, 248, 255));
        ml_draw_text(&c, f, 48, 136, "Live USB media is automatically protected.", 12, ml_rgb(160, 169, 188));
        int y = 158;
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
            const char *transport = transport_name(d->name);
            const char *kind = d->is_usb_boot ? "Protected live media" :
                               d->is_removable ? transport :
                               transport;
            snprintf(detail, sizeof detail, "%s · %.1f GB · %s", d->devnode, gb, kind);
            ml_draw_text(&c, f, 108, y + 45, detail, 11, d->is_usb_boot ? ml_rgb(240, 185, 105) : ml_rgb(160, 170, 190));
            y += 72;
        }
        ml_rect b = ml_rect_make(s->w - 156, s->h - 68, 108, 36);
        ml_fill_rounded(&c, b, 10, TARGET_DISK_IDX >= 0 ? ml_rgb(52, 133, 242) : ml_rgba(255, 255, 255, 20));
        ml_draw_text(&c, fb, s->w - 132, s->h - 45, "Continue", 12, ml_rgb(255, 255, 255));

    } else if (STAGE == STAGE_CONFIRM) {
        ml_draw_text(&c, fb, cx - 158, 116, "Confirm installation", 19, ml_rgb(245, 248, 255));
        if (TARGET_DISK_IDX >= 0) {
            disk_info *d = &DISKS[TARGET_DISK_IDX];
            ml_draw_text(&c, fb, 64, 154, d->model, 14, ml_rgb(235, 240, 250));
            ml_draw_text(&c, f, 64, 176, d->devnode, 12, ml_rgb(155, 166, 185));
            ml_fill_rounded(&c, ml_rect_make(52, 200, s->w - 104, 70), 12, ml_rgba(220, 60, 55, 42));
            ml_draw_text(&c, fb, 68, 224, "This will erase the selected disk.", 14, ml_rgb(255, 120, 115));
            ml_draw_text(&c, f, 68, 246, "All existing partitions and data will be permanently removed.", 12, ml_rgb(238, 200, 200));
            ml_stroke_rounded(&c, ml_rect_make(56, 290, 20, 20), 5, 1.5, ml_rgba(255, 255, 255, 120));
            if (CONFIRMED_ERASE) {
                ml_fill_rounded(&c, ml_rect_make(58, 292, 16, 16), 4, ml_rgb(52, 133, 242));
                ml_icon_draw(&c, "check", ml_rect_make(58, 292, 16, 16), ml_rgb(255, 255, 255));
            }
            ml_draw_text(&c, f, 86, 306, "I understand this disk will be erased.", 12, ml_rgb(205, 212, 225));
        }
        ml_rect b = ml_rect_make(s->w - 238, s->h - 68, 190, 36);
        ml_fill_rounded(&c, b, 10, CONFIRMED_ERASE ? ml_rgb(214, 61, 55) : ml_rgba(255, 255, 255, 18));
        ml_draw_text(&c, fb, s->w - 214, s->h - 45, "Erase & Install", 12, ml_rgb(255, 255, 255));

    } else if (STAGE == STAGE_INSTALLING || STAGE == STAGE_VERIFYING) {
        bool is_verifying = STAGE == STAGE_VERIFYING || INSTALL_PROGRESS >= 80;
        ml_draw_text(&c, fb, cx - (is_verifying ? 80 : 102), 114, is_verifying ? "Verifying G1OS" : "Installing G1OS", 22, ml_rgb(248, 250, 255));
        ml_draw_text(&c, f, cx - 140, 138, is_verifying ? "Running mandatory system health & bootability checks" : CURRENT_STAGE_NAME, 12, is_verifying ? ml_rgb(110, 215, 160) : ml_rgb(125, 183, 255));

        int bar_x = 76, bar_y = 176, bar_w = s->w - 152;
        draw_progress_bar(&c, bar_x, bar_y, bar_w, 12, DISPLAY_PROGRESS, is_verifying ? ml_rgb(80, 200, 140) : ml_rgb(78, 157, 255));
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", INSTALL_PROGRESS);
        ml_draw_text(&c, fb, cx - 18, 218, pct, 24, ml_rgb(245, 248, 255));
        ml_draw_text(&c, f, cx - 180, 248, CURRENT_STEP_TEXT, 13, ml_rgb(198, 205, 220));

        uint64_t elapsed = now_ms() - INSTALL_STARTED_MS;
        char elapsed_text[64];
        snprintf(elapsed_text, sizeof elapsed_text, "Elapsed %02llu:%02llu", (unsigned long long)(elapsed / 60000ULL), (unsigned long long)((elapsed / 1000ULL) % 60ULL));
        ml_draw_text(&c, f, cx - 45, 272, elapsed_text, 11, ml_rgb(125, 134, 151));

        bool active = BACKEND_ACTIVE && !BACKEND_FAILED;
        if (active) {
            const char *dots[] = {"•", "••", "•••"};
            const char *d = dots[ACTIVITY_FRAME % 3];
            ml_draw_text(&c, f, cx - 15, 300, d, 14, is_verifying ? ml_rgb(90, 220, 150) : ml_rgb(110, 175, 245));
        }
        ml_draw_text(&c, f, cx - 165, 324, is_verifying ? "Authoritative pre-flight check in progress. No fake 100%." : "Progress is based on verified operations.", 11, ml_rgb(120, 130, 148));

        ml_rect toggle = ml_rect_make(48, 356, s->w - 96, 28);
        ml_fill_rounded(&c, toggle, 8, ml_rgba(255, 255, 255, 9));
        ml_draw_text(&c, fb, 60, 375, SHOW_DETAILS ? "▼ Installation Details" : "▶ Installation Details", 10, ml_rgb(155, 180, 215));
        if (SHOW_DETAILS) {
            ml_rect logbox = ml_rect_make(48, 390, s->w - 96, 96);
            ml_fill_rounded(&c, logbox, 8, ml_rgba(5, 7, 10, 210));
            int first = N_LOG_LINES > 5 ? N_LOG_LINES - 5 : 0;
            for (int i = first; i < N_LOG_LINES; i++)
                ml_draw_text(&c, f, 58, 410 + (i - first) * 16, LOG_LINES[i], 9, ml_rgb(160, 170, 188));
        }

    } else if (STAGE == STAGE_SUMMARY) {
        /* Section 19: Verification Summary Screen */
        ml_draw_text(&c, fb, cx - 100, 112, "G1OS Installation", 20, ml_rgb(248, 250, 255));
        ml_draw_text(&c, f, cx - 120, 134, "All mandatory verification checks passed", 12, ml_rgb(90, 215, 145));

        /* 2-column checklist grid */
        int col1_x = 76, col2_x = cx + 24, start_y = 164;
        for (int i = 0; i < 5; i++) {
            int y = start_y + i * 26;
            ml_icon_draw(&c, "check", ml_rect_make(col1_x, y, 16, 16), ml_rgb(70, 210, 130));
            ml_draw_text(&c, f, col1_x + 24, y + 13, SUMMARY_CHECKS[i].name, 12, ml_rgb(230, 238, 248));
        }
        for (int i = 5; i < 10; i++) {
            int y = start_y + (i - 5) * 26;
            ml_icon_draw(&c, "check", ml_rect_make(col2_x, y, 16, 16), ml_rgb(70, 210, 130));
            ml_draw_text(&c, f, col2_x + 24, y + 13, SUMMARY_CHECKS[i].name, 12, ml_rgb(230, 238, 248));
        }

        ml_draw_text(&c, fb, cx - 64, 320, "Everything is ready.", 14, ml_rgb(120, 220, 160));

        /* Details Toggle */
        ml_rect toggle = ml_rect_make(48, 350, s->w - 96, 26);
        ml_fill_rounded(&c, toggle, 8, ml_rgba(255, 255, 255, 9));
        ml_draw_text(&c, fb, 60, 368, SHOW_DETAILS ? "▼ View Details (Log)" : "▶ View Details (Log)", 10, ml_rgb(155, 180, 215));
        if (SHOW_DETAILS) {
            ml_rect logbox = ml_rect_make(48, 382, s->w - 96, 68);
            ml_fill_rounded(&c, logbox, 8, ml_rgba(5, 7, 10, 210));
            int first = N_LOG_LINES > 3 ? N_LOG_LINES - 3 : 0;
            for (int i = first; i < N_LOG_LINES; i++)
                ml_draw_text(&c, f, 58, 402 + (i - first) * 16, LOG_LINES[i], 9, ml_rgb(160, 170, 188));
        }

        ml_rect b = ml_rect_make(cx - 100, s->h - 64, 200, 38);
        ml_fill_rounded(&c, b, 12, ml_rgb(52, 133, 242));
        ml_draw_text(&c, fb, cx - 44, s->h - 40, "Complete", 13, ml_rgb(255, 255, 255));

    } else if (STAGE == STAGE_COMPLETE) {
        ml_icon_draw(&c, "check", ml_rect_make(cx - 32, 124, 64, 64), ml_rgb(80, 210, 135));
        ml_draw_text(&c, fb, cx - 124, 218, "Installation Complete", 20, ml_rgb(245, 250, 248));
        ml_draw_text(&c, f, cx - 155, 243, "G1OS has been successfully installed and verified.", 12, ml_rgb(180, 192, 205));
        ml_draw_text(&c, f, cx - 140, 266, "Remove the USB installer before restarting.", 12, ml_rgb(150, 163, 180));
        
        /* Reboot Safety Gate: Button is only enabled if installation is verified 100% */
        bool can_restart = BACKEND_VERIFIED && INSTALL_PROGRESS >= 100;
        ml_rect b = ml_rect_make(cx - 112, 314, 224, 42);
        ml_fill_rounded(&c, b, 12, can_restart ? ml_rgb(52, 133, 242) : ml_rgba(255, 255, 255, 20));
        ml_draw_text(&c, fb, cx - 55, 340, "Restart", 13, can_restart ? ml_rgb(255, 255, 255) : ml_rgba(255, 255, 255, 80));

    } else if (STAGE == STAGE_ERROR) {
        /* Section 20: Failure Summary Screen */
        ml_icon_draw(&c, "close", ml_rect_make(cx - 28, 108, 56, 56), ml_rgb(245, 80, 75));
        ml_draw_text(&c, fb, cx - 138, 190, "Installation could not be completed.", 17, ml_rgb(255, 105, 100));

        char fail_header[96];
        snprintf(fail_header, sizeof fail_header, "✗ %s", FAILED_CHECK_NAME[0] ? FAILED_CHECK_NAME : "Verification failure");
        ml_draw_text(&c, fb, 60, 222, fail_header, 13, ml_rgb(255, 140, 135));
        ml_draw_text(&c, f, 60, 244, ERROR_MESSAGE, 12, ml_rgb(238, 190, 190));

        ml_rect logbox = ml_rect_make(52, 274, s->w - 104, 116);
        ml_fill_rounded(&c, logbox, 10, ml_rgba(5, 7, 10, 220));
        int first = N_LOG_LINES > 6 ? N_LOG_LINES - 6 : 0;
        for (int i = first; i < N_LOG_LINES; i++)
            ml_draw_text(&c, f, 64, 296 + (i - first) * 16, LOG_LINES[i], 9, ml_rgb(205, 155, 155));

        /* Buttons: [Retry] [Repair] [View Details] */
        ml_rect btn_retry = ml_rect_make(cx - 150, 420, 90, 36);
        ml_fill_rounded(&c, btn_retry, 10, ml_rgb(52, 133, 242));
        ml_draw_text(&c, fb, cx - 132, 443, "Retry", 12, ml_rgb(255, 255, 255));

        ml_rect btn_repair = ml_rect_make(cx - 45, 420, 95, 36);
        ml_fill_rounded(&c, btn_repair, 10, ml_rgb(40, 160, 100));
        ml_draw_text(&c, fb, cx - 28, 443, "Repair", 12, ml_rgb(255, 255, 255));

        ml_rect btn_details = ml_rect_make(cx + 65, 420, 105, 36);
        ml_fill_rounded(&c, btn_details, 10, ml_rgba(255, 255, 255, 20));
        ml_draw_text(&c, fb, cx + 76, 443, "View Details", 11, ml_rgb(220, 230, 245));
    }

    mica_win_commit(WIN);
}

static void input(mica_win *w, const msg_input *in)
{
    (void)w;
    if (in->kind == IN_KEY) {
        if ((in->key == 0xff1b || in->key == 'q') && STAGE != STAGE_INSTALLING && STAGE != STAGE_VERIFYING)
            mica_quit(G, 0);
        return;
    }
    if (in->kind != IN_DOWN && in->kind != IN_CLICK) return;
    int x = in->x, y = in->y;
    int cx = WIN_W / 2;

    if (STAGE == STAGE_WELCOME) {
        if (x >= cx - 82 && x <= cx + 82 && y >= 262 && y <= 304) {
            STAGE = STAGE_SELECT;
            probe_disks();
            draw();
        }
    } else if (STAGE == STAGE_SELECT) {
        int dy = 158;
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
        if (x >= WIN_W - 156 && x <= WIN_W - 48 && y >= WIN_H - 68 && y <= WIN_H - 32 && TARGET_DISK_IDX >= 0) {
            STAGE = STAGE_CONFIRM;
            draw();
        }
    } else if (STAGE == STAGE_CONFIRM) {
        if (x >= 48 && x <= WIN_W - 48 && y >= 280 && y <= 320) {
            CONFIRMED_ERASE = !CONFIRMED_ERASE;
            draw();
            return;
        }
        if (x >= WIN_W - 238 && x <= WIN_W - 48 && y >= WIN_H - 68 && y <= WIN_H - 32 && CONFIRMED_ERASE)
            start_backend(false);
    } else if (STAGE == STAGE_INSTALLING || STAGE == STAGE_VERIFYING) {
        if (x >= 48 && x <= WIN_W - 48 && y >= 356 && y <= 384) {
            SHOW_DETAILS = !SHOW_DETAILS;
            draw();
        }
    } else if (STAGE == STAGE_SUMMARY) {
        if (x >= 48 && x <= WIN_W - 48 && y >= 350 && y <= 376) {
            SHOW_DETAILS = !SHOW_DETAILS;
            draw();
            return;
        }
        if (x >= cx - 100 && x <= cx + 100 && y >= WIN_H - 64 && y <= WIN_H - 26) {
            STAGE = STAGE_COMPLETE;
            draw();
        }
    } else if (STAGE == STAGE_COMPLETE) {
        /* Section 24: Reboot Safety Gate - only allow restart if verified 100% */
        if (x >= cx - 112 && x <= cx + 112 && y >= 314 && y <= 356) {
            if (BACKEND_VERIFIED && INSTALL_PROGRESS >= 100) {
                add_log("RESTART: user requested reboot after verified installation.");
                if (!TEST_MODE) system("reboot 2>/dev/null || systemctl reboot 2>/dev/null || shutdown -r now 2>/dev/null");
                mica_quit(G, 0);
            } else {
                add_log("REBOOT BLOCKED: Installation is not verified complete.");
            }
        }
    } else if (STAGE == STAGE_ERROR) {
        /* [Retry] */
        if (x >= cx - 150 && x <= cx - 60 && y >= 420 && y <= 456) {
            stop_backend(true);
            BACKEND_FAILED = false;
            start_backend(false);
        }
        /* [Repair] */
        else if (x >= cx - 45 && x <= cx + 50 && y >= 420 && y <= 456) {
            stop_backend(true);
            BACKEND_FAILED = false;
            start_backend(true); /* invoke automatic repair and re-verification */
        }
        /* [View Details] */
        else if (x >= cx + 65 && x <= cx + 170 && y >= 420 && y <= 456) {
            SHOW_DETAILS = !SHOW_DETAILS;
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
