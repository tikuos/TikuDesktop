/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clipboard.h - the desk's clipboard, which a board does not hold.
 *
 * A field's cut and paste ask here; the board keeps one line of text
 * of its own until milestone 4 carries the desk's over the session.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CLIPBOARD_BOARD_
#define TIKU_CLIPBOARD_BOARD_

void tiku_clipboard_set(const char *text);
const char *tiku_clipboard_text(void);
unsigned tiku_clipboard_stamp(void);

#endif /* TIKU_CLIPBOARD_BOARD_ */
