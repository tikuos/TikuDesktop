/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw_desk.h - the desk, as a file under /gui: where the session
 * goes, whichever links this build has.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRAW_DESK_H_
#define TIKU_DRAW_DESK_H_

#include <stddef.h>

/** @brief The name the desk answers to under /gui. */
#define TIKU_DRAW_DESK_NODE "desk"

/**
 * @brief Send the session somewhere: an address dials a desktop over IP,
 *        "ble" advertises for one over the air, "off" returns it to the
 *        console.  Whichever link was up before is closed first.
 * @return 0, or -1 for a word this build has no link for
 */
int tiku_draw_desk_write(const char *value, size_t len);

/**
 * @brief Where the session is: "off", or the active link's own line.
 * @return bytes written, or -1
 */
int tiku_draw_desk_read(char *buf, size_t max);

#endif /* TIKU_DRAW_DESK_H_ */
