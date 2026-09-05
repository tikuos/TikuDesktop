/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_serial.h - a link over a prompt-aware session's frames.
 *
 * The board's console carries typed text and, between 0xC0 delimiters after a
 * 0xF1 marker, the window session's messages, one per frame.  This link is
 * that channel: it routes the session's whole frames to a message callback
 * and sends a message as one frame.  It does not own the session.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LINK_SERIAL_H_
#define TIKU_LINK_SERIAL_H_

#include "tiku_link.h"
#include "tiku_session.h"

/** @brief Open a serial link on @p s.  @return the link, or NULL. */
tiku_link_t *tiku_link_serial_open(tiku_session_t *s);

#endif /* TIKU_LINK_SERIAL_H_ */
