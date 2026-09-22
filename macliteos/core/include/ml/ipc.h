#ifndef ML_IPC_H
#define ML_IPC_H
#include "ml/common.h"
#include "ml/event.h"

/* Transport for the compositor protocol: one unix stream socket per client,
 * length-prefixed binary messages, and SCM_RIGHTS for passing shared-memory
 * surface buffers. Everything is fixed-size payloads: no parsing of untrusted
 * variable data, so a misbehaving client cannot corrupt the compositor. */

typedef struct { uint32_t type; uint32_t len; } mlipc_hdr;
#define MLIPC_MAX (1 << 20)

int mlipc_server_open(const char *path);            /* listen socket, -1 on err */
int mlipc_connect(const char *path);                /* client side */
void mlipc_close(int fd);

/* send/recv a message; returns false on peer death / protocol error */
bool mlipc_send(int fd, uint32_t type, const void *payload, uint32_t len);
bool mlipc_send_fd(int fd, uint32_t type, const void *payload, uint32_t len, int shm_fd);
/* recv: payload buffer must be MLIPC_MAX or smaller; *len set. Returns type or 0. */
uint32_t mlipc_recv(int fd, void *payload, uint32_t cap, uint32_t *len, int *fd_out);

/* event-loop integration: fd source that decodes a message and calls cb */
typedef void (*mlipc_cb)(void *ud, int fd, uint32_t type, const void *payload, uint32_t len, int fd_received);
ml_source *mlipc_watch(ml_loop *l, int fd, mlipc_cb cb, void *ud);

/* shared memory helpers for surfaces */
int ml_shm_create(size_t bytes);                    /* memfd, sealed size set */
void *ml_shm_map(int fd, size_t bytes);             /* mmap shared */
#endif
