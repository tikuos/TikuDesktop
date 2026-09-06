/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw_shell.h - the forms road over a link: the board's shell,
 * reached through the session's messages as well as the cable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRAW_SHELL_H_
#define TIKU_DRAW_SHELL_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Text a desk sent for the shell.  The first such message over a
 *        link that is not the console puts the shell on the link beside
 *        the cable; it then answers whoever spoke last.
 */
void tiku_draw_shell_take(const unsigned char *bytes, size_t len);

/** @brief The link is going: the shell is the cable's alone again. */
void tiku_draw_shell_detach(void);

#endif /* TIKU_DRAW_SHELL_H_ */
