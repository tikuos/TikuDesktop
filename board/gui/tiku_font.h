/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_font.h - the desktop's faces, as a board measures them.
 *
 * A face here is a number the desktop resolves and the metrics baked in
 * tiku_metrics.h; text is written as the op that names its face.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FONT_BOARD_
#define TIKU_FONT_BOARD_

#include "tiku_gfx.h"

typedef struct tiku_font {
    int height;                 /**< ascent + descent */
    int ascent;
    int face;                   /**< as the display list numbers faces */
} tiku_font_t;

const tiku_font_t *tiku_font_plain(void);
const tiku_font_t *tiku_font_bold(void);
const tiku_font_t *tiku_font_mono(int bold);
int tiku_text_width(const tiku_font_t *f, const char *text);
int tiku_text_height(const tiku_font_t *f);
void tiku_text(tiku_surface_t *s, const tiku_font_t *f, int x, int y,
               const char *text, tiku_rgb_t c);
int tiku_text_centered(tiku_surface_t *s, const tiku_font_t *f,
                       tiku_rect_t r, const char *text, tiku_rgb_t c);

#endif /* TIKU_FONT_BOARD_ */
