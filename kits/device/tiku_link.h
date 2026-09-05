/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link.h - a link: whole messages to and from one board, on any medium.
 *
 * The desktop's mirror of the kernel's link.  A channel adopts a link and
 * neither knows nor cares whether it is a serial cable, a socket or a radio;
 * a serial link is the first, over a prompt-aware session's frames.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LINK_H_
#define TIKU_LINK_H_

#include <stddef.h>

typedef struct tiku_link tiku_link_t;

/** @brief One whole message from the board, its escapes already undone. */
typedef void (*tiku_link_msg_fn)(const unsigned char *msg, size_t len,
                                 void *ctx);

/** @brief What a backend supplies: send one message, and close. */
typedef struct {
    int  (*send)(tiku_link_t *l, const void *msg, size_t len);
    void (*close)(tiku_link_t *l);
} tiku_link_ops_t;

/** @brief A link.  A backend fills ops and ctx; a consumer installs recv. */
struct tiku_link {
    const tiku_link_ops_t *ops;
    void                  *ctx;
    tiku_link_msg_fn       recv;
    void                  *recv_ctx;
};

/** @brief Send one whole message.  @return 0, or -1 on a broken link. */
int  tiku_link_send(tiku_link_t *l, const void *msg, size_t len);

/** @brief Where whole messages from the board go; NULL drops them. */
void tiku_link_on_message(tiku_link_t *l, tiku_link_msg_fn fn, void *ctx);

/** @brief For a backend: hand a whole message to the consumer, if any. */
void tiku_link_deliver(tiku_link_t *l, const unsigned char *msg, size_t len);

/** @brief Release the backend; the link delivers nothing after this. */
void tiku_link_close(tiku_link_t *l);

#endif /* TIKU_LINK_H_ */
