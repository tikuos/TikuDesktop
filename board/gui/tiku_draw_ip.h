/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw_ip.h - the desk the session dials, as a file under /gui.
 *
 * A board with an address can carry its windows to a desktop that is not
 * on the cable.  Writing an address to /gui/desk dials it and moves the
 * session there; writing "off" brings the session back to the console.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRAW_IP_H_
#define TIKU_DRAW_IP_H_

#include <stddef.h>

/** @brief The name the desk answers to under /gui. */
#define TIKU_DRAW_IP_NODE "desk"

/**
 * @brief Dial the desktop @p value names, "a.b.c.d" or "a.b.c.d:port",
 *        and carry the session there; "off" returns it to the console.
 *
 * @return 0, or -1 for an address that will not parse or a link that
 *         will not open
 */
int tiku_draw_ip_write(const char *value, size_t len);

/**
 * @brief What the desk is: "off", or the address, the state and the
 *        messages that have crossed.
 * @return bytes written, or -1
 */
int tiku_draw_ip_read(char *buf, size_t max);

#endif /* TIKU_DRAW_IP_H_ */
