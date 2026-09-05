/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_console.h - the console link the board library opens, for a
 * host test.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STUB_LINK_CONSOLE_H_
#define TIKU_STUB_LINK_CONSOLE_H_

#include "tiku_link.h"

#define TIKU_LINK_CONSOLE_SESSION 0xF1u

typedef struct { int used; } tiku_link_console_t;

tiku_link_t *tiku_link_console_open(tiku_link_console_t *lc, uint8_t marker,
                                    uint8_t *buf, size_t cap);

#endif /* TIKU_STUB_LINK_CONSOLE_H_ */
