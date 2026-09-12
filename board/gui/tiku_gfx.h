/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gfx.h - the desktop's drawing surface, as a board records it.
 *
 * The same names the desktop's kit draws with, found first on the
 * board's include path, so the kit's paint files compile here as they
 * are.  Nothing paints: a surface is a window's list, and every call
 * that would put down pixels writes the op it stands for instead, only
 * at the outermost level, the way the desktop's own recorder does.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_GFX_BOARD_
#define TIKU_GFX_BOARD_

#include <stddef.h>
#include <stdint.h>

#include "tiku_geom.h"

/* The R5 tint constants, so the paint files read as they do there. */
#define TIKU_LIGHTEN_MAX  0.00f
#define TIKU_LIGHTEN_2    0.70f
#define TIKU_LIGHTEN_1    0.90f
#define TIKU_NO_TINT      1.00f
#define TIKU_DARKEN_1     1.02f
#define TIKU_DARKEN_2     1.06f
#define TIKU_DARKEN_3     1.07f
#define TIKU_DARKEN_4     1.08f
#define TIKU_DARKEN_MAX   2.00f

struct tiku_draw_window;

/** @brief A surface is a window's list; record_depth keeps the rule
 *         that only the outermost call is written down. */
typedef struct {
    struct tiku_draw_window *record;
    int                      record_depth;
} tiku_surface_t;

/* The desktop's default theme, as the desktop would draw it.  A board
 * writes the ops; the desktop paints them in whatever theme it wears,
 * so these matter only for a colour a program picks by hand. */
#define TIKU_C_PANEL      TIKU_RGB(216, 216, 216)
#define TIKU_C_DOC        TIKU_RGB(255, 255, 255)
#define TIKU_C_TEXT       TIKU_RGB(0, 0, 0)
#define TIKU_C_TAB        TIKU_RGB(255, 203, 0)
#define TIKU_C_TAB_IDLE   TIKU_RGB(232, 232, 232)
#define TIKU_C_SELECT     TIKU_RGB(51, 102, 152)
#define TIKU_C_SELTEXT    TIKU_RGB(255, 255, 255)
#define TIKU_C_FOCUS      TIKU_RGB(0, 0, 229)
#define TIKU_C_BACKDROP   TIKU_RGB(51, 102, 152)
#define TIKU_C_DANGER     TIKU_RGB(203, 48, 48)
#define TIKU_C_NOTE       TIKU_RGB(255, 255, 200)
#define TIKU_C_TABTEXT    TIKU_RGB(0, 0, 0)
/* The four a program's own words are written in, which the desktop
 * reads from its theme and a board carries as the default table's. */
#define TIKU_C_CODE_WORD  TIKU_RGB(0, 0, 176)
#define TIKU_C_CODE_NUM   TIKU_RGB(0, 112, 96)
#define TIKU_C_CODE_STR   TIKU_RGB(160, 32, 32)
#define TIKU_C_CODE_REM   TIKU_RGB(96, 112, 96)

/**
 * @brief What a run of text MEANS, the same nine the desktop names.
 *
 * The values and their order are kits/interface/tiku_gfx.h's, because a
 * file compiled for both ends resolves the name here and the meaning
 * there.  What a board cannot do is follow a theme: the desktop repaints
 * a NAMED op in whatever table it wears, but a colour written into an op
 * is painted as it was written, so a board's page is the light table's
 * ink wherever it is read.
 */
typedef enum {
    TIKU_INK_PLAIN = 0,
    TIKU_INK_FAINT,
    TIKU_INK_WORD,
    TIKU_INK_NUM,
    TIKU_INK_STR,
    TIKU_INK_REMARK,
    TIKU_INK_WRONG,
    TIKU_INK_PATH,
    TIKU_INK_LINK
} tiku_ink_t;

/* The drawing calls the paint files make, each an op when outermost. */
void tiku_fill(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c);
void tiku_frame(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c);
void tiku_hline(tiku_surface_t *s, int x, int y, int len, tiku_rgb_t c);
void tiku_vline(tiku_surface_t *s, int x, int y, int len, tiku_rgb_t c);
void tiku_bevel(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t light,
                tiku_rgb_t shadow);
void tiku_clip_set(tiku_surface_t *s, tiku_rect_t r);
void tiku_clip_reset(tiku_surface_t *s);
tiku_rgb_t tiku_ink(tiku_ink_t which);
tiku_rgb_t tiku_tint(tiku_rgb_t c, float tint);
tiku_rgb_t tiku_dim(tiku_rgb_t ink, tiku_rgb_t ground);
tiku_rect_t tiku_inset(tiku_rect_t r, int n);
int tiku_gfx_rec_enter(tiku_surface_t *s);
void tiku_gfx_rec_leave(tiku_surface_t *s, int on);

/** @brief Bind @p s to @p win: what is drawn on it goes into the list. */
void tiku_surface_on_window(tiku_surface_t *s, struct tiku_draw_window *win);

#endif /* TIKU_GFX_BOARD_ */
