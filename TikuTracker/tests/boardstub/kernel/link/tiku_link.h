/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link.h - the link the board library sends its messages to, for a
 * host test.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STUB_LINK_H_
#define TIKU_STUB_LINK_H_

#include <stddef.h>
#include <stdint.h>

typedef struct tiku_link tiku_link_t;
typedef void (*tiku_link_recv_fn)(void *ctx, uint8_t *msg, size_t len);

int  tiku_link_send2(tiku_link_t *l, const void *head, size_t hlen,
                     const void *body, size_t blen);
void tiku_link_on_recv(tiku_link_t *l, tiku_link_recv_fn fn, void *ctx);

#endif /* TIKU_STUB_LINK_H_ */
