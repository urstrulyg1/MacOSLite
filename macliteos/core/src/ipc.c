#include "ml/ipc.h"
#include "ml/log.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

int mlipc_server_open(const char *path)
{
    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa = { 0 };
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0) { close(fd); return -1; }
    chmod(path, 0700);
    if (listen(fd, 16) != 0) { close(fd); return -1; }
    return fd;
}

int mlipc_connect(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa = { 0 };
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) { close(fd); return -1; }
    return fd;
}
void mlipc_close(int fd) { if (fd >= 0) close(fd); }

static bool write_all(int fd, const void *p, size_t n)
{
    const uint8_t *b = p;
    while (n) {
        ssize_t w = send(fd, b, n, MSG_NOSIGNAL);
        if (w < 0) { if (errno == EINTR) continue; return false; }
        b += w; n -= (size_t)w;
    }
    return true;
}
static bool read_all(int fd, void *p, size_t n)
{
    uint8_t *b = p;
    while (n) {
        ssize_t r = recv(fd, b, n, 0);
        if (r < 0) { if (errno == EINTR) continue; return false; }
        if (r == 0) return false;
        b += r; n -= (size_t)r;
    }
    return true;
}

bool mlipc_send(int fd, uint32_t type, const void *payload, uint32_t len)
{
    mlipc_hdr h = { type, len };
    if (!write_all(fd, &h, sizeof h)) return false;
    if (len && !write_all(fd, payload, len)) return false;
    return true;
}

bool mlipc_send_fd(int fd, uint32_t type, const void *payload, uint32_t len, int shm_fd)
{
    mlipc_hdr h = { type, len };
    struct iovec iov[2] = {
        { .iov_base = &h, .iov_len = sizeof h },
        { .iov_base = (void *)payload, .iov_len = len },
    };
    union { char buf[CMSG_SPACE(sizeof(int))]; struct cmsghdr align; } u;
    struct msghdr msg = { 0 };
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof u.buf;
    struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
    cm->cmsg_level = SOL_SOCKET;
    cm->cmsg_type = SCM_RIGHTS;
    cm->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cm), &shm_fd, sizeof(int));
    if (sendmsg(fd, &msg, MSG_NOSIGNAL) < 0) return false;
    return true;
}

uint32_t mlipc_recv(int fd, void *payload, uint32_t cap, uint32_t *len, int *fd_out)
{
    mlipc_hdr h;
    if (!read_all(fd, &h, sizeof h)) return 0;
    if (h.len > MLIPC_MAX || h.len > cap) { ML_WARN("ipc: oversized message %u", h.len); return 0; }
    if (fd_out) *fd_out = -1;
    if (h.len && !read_all(fd, payload, h.len)) return 0;
    if (len) *len = h.len;
    /* drain any passed fd eagerly so the socket does not queue fds we lose */
    struct iovec iov = { .iov_base = NULL, .iov_len = 0 };
    union { char buf[CMSG_SPACE(sizeof(int))]; struct cmsghdr align; } u;
    struct msghdr msg = { 0 };
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof u.buf;
    msg.msg_flags = MSG_DONTWAIT | MSG_PEEK | MSG_CTRUNC;
    ssize_t r = recvmsg(fd, &msg, MSG_DONTWAIT | MSG_PEEK);
    if (r >= 0) {
        struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
        if (cm && cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS && fd_out) {
            /* consume it properly */
            int got = -1;
            msg.msg_flags = 0;
            r = recvmsg(fd, &msg, 0);
            cm = CMSG_FIRSTHDR(&msg);
            if (cm && cm->cmsg_type == SCM_RIGHTS) memcpy(&got, CMSG_DATA(cm), sizeof(int));
            *fd_out = got;
        }
    }
    return h.type;
}

#define IPC_BUF (256 * 1024)
#define IPC_FDS 16

typedef struct {
    int fd;
    mlipc_cb cb;
    void *ud;
    uint8_t *buf;
    size_t fill;
    size_t cap;
    int fds[IPC_FDS];
    uint64_t fd_off[IPC_FDS];  /* cumulative byte offset at which each fd arrived */
    int nfd;
    uint64_t total_read;
    bool dead;
} ipc_src;

static void ipc_drain(ipc_src *s)
{
    size_t off = 0;
    for (;;) {
        if (s->fill - off < sizeof(mlipc_hdr)) break;
        mlipc_hdr h;
        memcpy(&h, s->buf + off, sizeof h);
        if (h.len > MLIPC_MAX) { s->dead = true; break; }
        if (s->fill - off - sizeof h < h.len) break;
        int passed = -1;
        uint64_t msg_end = s->total_read - (uint64_t)(s->fill - off) + sizeof h + h.len;
        for (int i = 0; i < s->nfd; i++)
            if (s->fd_off[i] <= msg_end) {
                passed = s->fds[i];
                for (int k = i; k + 1 < s->nfd; k++) { s->fds[k] = s->fds[k + 1]; s->fd_off[k] = s->fd_off[k + 1]; }
                s->nfd--;
                i--;
            }
        s->cb(s->ud, s->fd, h.type, s->buf + off + sizeof h, h.len, passed);
        off += sizeof h + h.len;
        if (s->dead) break;
    }
    if (off) {
        memmove(s->buf, s->buf + off, s->fill - off);
        s->fill -= off;
    }
}

static void on_readable(void *ud, uint32_t events)
{
    ipc_src *s = ud;
    if (events & (EPOLLHUP | EPOLLERR)) { s->cb(s->ud, s->fd, 0, NULL, 0, -1); s->dead = true; return; }
    for (;;) {
        if (s->fill == s->cap) {
            ML_WARN("ipc: buffer overflow on fd %d, dropping peer", s->fd);
            s->cb(s->ud, s->fd, 0, NULL, 0, -1);
            s->dead = true;
            return;
        }
        struct iovec iov = { .iov_base = s->buf + s->fill, .iov_len = s->cap - s->fill };
        union { char buf[CMSG_SPACE(sizeof(int) * 4)]; struct cmsghdr align; } u;
        struct msghdr msg = { 0 };
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = u.buf;
        msg.msg_controllen = sizeof u.buf;
        ssize_t r = recvmsg(s->fd, &msg, MSG_DONTWAIT);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            s->cb(s->ud, s->fd, 0, NULL, 0, -1);
            s->dead = true;
            return;
        }
        if (r == 0) { s->cb(s->ud, s->fd, 0, NULL, 0, -1); s->dead = true; return; }
        s->fill += (size_t)r;
        s->total_read += (uint64_t)r;
        for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
            if (cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS) continue;
            int n = (int)((cm->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            for (int i = 0; i < n && s->nfd < IPC_FDS; i++) {
                int fd;
                memcpy(&fd, CMSG_DATA(cm) + (size_t)i * sizeof(int), sizeof(int));
                s->fds[s->nfd] = fd;
                s->fd_off[s->nfd] = s->total_read;
                s->nfd++;
            }
        }
        ipc_drain(s);
        if (s->dead) return;
    }
}

ml_source *mlipc_watch(ml_loop *l, int fd, mlipc_cb cb, void *ud)
{
    ipc_src *s = ml_zalloc(sizeof *s);
    s->fd = fd;
    s->cb = cb;
    s->ud = ud;
    s->cap = IPC_BUF;
    s->buf = ml_alloc(s->cap);
    return ml_loop_add_fd(l, fd, EPOLLIN, on_readable, s);
}


int ml_shm_create(size_t bytes)
{
    int fd = memfd_create("mica-surface", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)bytes) != 0) { close(fd); return -1; }
    return fd;
}
void *ml_shm_map(int fd, size_t bytes)
{
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return p == MAP_FAILED ? NULL : p;
}
