/* Device VFS access through the owning desktop, not a second serial owner.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_VFS_RPC_H
#define TIKU_VFS_RPC_H
#include "tiku_model.h"
#include "tiku_msg.h"

#define TIKU_MSG_VFS_REQUEST 0x76667371u
#define TIKU_MSG_VFS_REPLY   0x76667372u
#define TIKU_VFS_RPC_BYTES 4096
#define TIKU_VFS_RPC_ROWS 129

/* A proxy has one worker caller. pump()/reply() run on the app UI thread;
 * blocking backend operations run only on its worker. */
tiku_backend_t *tiku_vfs_proxy_new(const char *board);
void tiku_vfs_proxy_pump(tiku_backend_t *b,
    int (*send)(void *, const tiku_msg_t *), void *ctx);
void tiku_vfs_proxy_reply(tiku_backend_t *b, const tiku_msg_t *reply);
void tiku_vfs_proxy_cancel(tiku_backend_t *b);

/* Serve on the device owner's serialized worker. Writes require both the
 * bound device identity and an unchanged expected value. The caller owns
 * the returned message and must deliver it only to the requesting session. */
tiku_msg_t *tiku_vfs_serve(tiku_backend_t *backend, const tiku_msg_t *request);
#endif
