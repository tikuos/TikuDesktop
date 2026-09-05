/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dl.h - the display list, as a board holds it: a window's list.
 *
 * The paint files record a control by name through these; on a board
 * that IS the drawing, so each writes its op into the window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DL_BOARD_
#define TIKU_DL_BOARD_

#include "tiku_gfx.h"

typedef struct tiku_draw_window tiku_dl_t;

int tiku_dl_slider(tiku_dl_t *dl, tiku_rect_t r, int min, int max, int value);
int tiku_dl_tabs(tiku_dl_t *dl, tiku_rect_t r, int count, int current,
                 const char *labels, size_t labels_len);
int tiku_dl_gauge(tiku_dl_t *dl, tiku_rect_t r, int per_mille);
int tiku_dl_list_row(tiku_dl_t *dl, tiku_rect_t r, const char *text,
                     int selected);

#endif /* TIKU_DL_BOARD_ */
