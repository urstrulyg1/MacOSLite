/* Verify that the real graphical G1OS installer is connected to a live compositor.
 * This is a boot-time health gate, not a UI substitute. */
#include "../compositor/proto.h"
#include "ml/ipc.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static int fail(const char *msg)
{
    fprintf(stderr, "G1OS UI HEALTH: FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char path[256];
    if (!rt || !*rt) rt = "/tmp/macliteos-0";
    snprintf(path, sizeof path, "%s/mica-comp.sock", rt);

    int fd = mlipc_connect(path);
    if (fd < 0) return fail("cannot connect to compositor socket");

    msg_hello hello = { .pid = (uint32_t)getpid(), .kind = 0 };
    snprintf(hello.name, sizeof hello.name, "tool:g1os-ui-health");
    if (!mlipc_send(fd, MC_HELLO, &hello, sizeof hello)) {
        mlipc_close(fd);
        return fail("cannot send compositor hello");
    }

    struct pollfd p = { .fd = fd, .events = POLLIN };
    if (poll(&p, 1, 2000) <= 0) {
        mlipc_close(fd);
        return fail("compositor did not answer hello");
    }
    uint8_t buf[1024];
    uint32_t len = 0;
    uint32_t type = mlipc_recv(fd, buf, sizeof buf, &len, NULL);
    if (type != MS_WELCOME || len < sizeof(msg_welcome)) {
        mlipc_close(fd);
        return fail("invalid compositor welcome response");
    }

    if (!mlipc_send(fd, MC_QUERY, NULL, 0)) {
        mlipc_close(fd);
        return fail("cannot query compositor health");
    }
    p.events = POLLIN;
    if (poll(&p, 1, 2000) <= 0) {
        mlipc_close(fd);
        return fail("compositor did not answer health query");
    }
    type = mlipc_recv(fd, buf, sizeof buf, &len, NULL);
    if (type != MS_STATS || len < sizeof(msg_stats)) {
        mlipc_close(fd);
        return fail("invalid compositor health response");
    }

    msg_stats st;
    memcpy(&st, buf, sizeof st);
    mlipc_close(fd);

    if (st.backend == 0) return fail("compositor is headless; no physical display is active");
    if (st.screen_w <= 0 || st.screen_h <= 0) return fail("compositor has no valid display geometry");
    if (st.nwindows == 0) return fail("no graphical installer surface/window is registered");

    printf("G1OS UI HEALTH: PASS: compositor=%s display=%dx%d windows=%u gpu=%s\n",
           st.backend == 1 ? "kms" : "fbdev", st.screen_w, st.screen_h,
           st.nwindows, st.gpu_name[0] ? st.gpu_name : "unknown");
    return 0;
}
