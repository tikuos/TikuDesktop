/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_event.h - the desktop's key and modifier names, for the board.
 *
 * The kit's behaviour files name keys and modifiers by these; the
 * event record itself is the kernel's on a board, so only the names
 * come across.  Generated from kits/interface/tiku_event.h by
 * kits/interface/tools/genevent.py; `make check-dl` proves it current.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_EVENT_BOARD_
#define TIKU_EVENT_BOARD_

#define TIKU_MOD_SHIFT  0x01u
#define TIKU_MOD_CMD    0x02u
#define TIKU_MOD_OPTION 0x04u
#define TIKU_MOD_CAPS   0x08u
#define TIKU_KEY_UP       1u
#define TIKU_KEY_DOWN     2u
#define TIKU_KEY_LEFT     3u
#define TIKU_KEY_RIGHT    4u
#define TIKU_KEY_HOME     5u
#define TIKU_KEY_END      6u
#define TIKU_KEY_PAGE_UP  7u
#define TIKU_KEY_PAGE_DOWN 8u
#define TIKU_KEY_BACKSPACE 9u
#define TIKU_KEY_DELETE   10u
#define TIKU_KEY_ESCAPE   11u
#define TIKU_KEY_RETURN   12u
#define TIKU_KEY_TAB      13u
#define TIKU_KEY_MENU     14u
#define TIKU_KEY_F2       15u
#define TIKU_KEY_SHIFT    16u
#define TIKU_KEY_CMD      17u
#define TIKU_KEY_OPTION   18u
#define TIKU_KEY_F1       19u

#endif /* TIKU_EVENT_BOARD_ */
