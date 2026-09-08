/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw_basic.h - the interpreter's own stream over a link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRAW_BASIC_H_
#define TIKU_DRAW_BASIC_H_

#include <stddef.h>

/**
 * @brief Text a desk sent for the interpreter, on the link's own stream.
 *
 * The mode is entered on the first of it and driven from here after, so
 * the console is never taken: the shell keeps its line, and a program
 * runs while the namespace still answers.
 */
void tiku_draw_basic_take(const unsigned char *bytes, size_t len);

/** @brief Give the console back: the link is gone. */
void tiku_draw_basic_detach(void);

#endif /* TIKU_DRAW_BASIC_H_ */
