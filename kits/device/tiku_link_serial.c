/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_serial.c - the serial backend of the link, over a session.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_link_serial.h"

#include <stdlib.h>

struct serial_link {
    tiku_link_t     link;
    tiku_session_t *sess;
};

/** @brief The session's frame handler: the frame is a whole message. */
static void
on_frame(const unsigned char *bytes, size_t len, void *ctx)
{
    tiku_link_deliver(&((struct serial_link *)ctx)->link, bytes, len);
}

static int
serial_send(tiku_link_t *l, const void *msg, size_t len)
{
    struct serial_link *sl = l->ctx;

    return tiku_session_send_frame(sl->sess, msg, len);
}

static void
serial_close(tiku_link_t *l)
{
    struct serial_link *sl = l->ctx;

    tiku_session_on_frame(sl->sess, NULL, NULL);
    free(sl);
}

static const tiku_link_ops_t serial_ops = { serial_send, serial_close };

tiku_link_t *
tiku_link_serial_open(tiku_session_t *s)
{
    struct serial_link *sl;

    if (s == NULL) {
        return NULL;
    }
    sl = calloc(1, sizeof *sl);
    if (sl == NULL) {
        return NULL;
    }
    sl->sess = s;
    sl->link.ops = &serial_ops;
    sl->link.ctx = sl;
    tiku_session_on_frame(s, on_frame, sl);
    return &sl->link;
}
