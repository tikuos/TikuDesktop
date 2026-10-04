/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app_cursor.c - the pointer shape a window's content asks for.
 *
 * The request is a message the desktop answers (TIKU_MSG_CURSOR in
 * tiku_remote.h); a file of its own so that an application which never
 * asks for one links nothing of the message kit for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_app.h"
#include "tiku_msg.h"
#include "tiku_remote.h"

struct tiku_msg *
tiku_app_cursor_message(uint32_t window, int shape)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_CURSOR);

    if (m == NULL) {
        return NULL;
    }
    if (!tiku_msg_add_int32(m, "window", (int32_t)window) ||
        !tiku_msg_add_int32(m, "shape", shape)) {
        tiku_msg_free(m);
        return NULL;
    }
    return m;
}

int
tiku_app_cursor_set(const tiku_app_services_t *services, uint32_t window,
                    int shape)
{
    tiku_msg_t *m;
    int sent;

    if (services == NULL || services->send_message == NULL) {
        return 0;
    }
    m = tiku_app_cursor_message(window, shape);
    sent = m != NULL && services->send_message(services->ctx, m);
    tiku_msg_free(m);
    return sent;
}
