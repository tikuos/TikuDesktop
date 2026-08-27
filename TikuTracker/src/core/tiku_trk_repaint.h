/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TRK_REPAINT_H_
#define TIKU_TRK_REPAINT_H_

#include <stdint.h>

typedef struct {
    int32_t x, y, w, h;
} tiku_trk_repaint_rect_t;

#define TIKU_TRK_REPAINT_MAX 32

typedef struct {
    tiku_trk_repaint_rect_t rect[TIKU_TRK_REPAINT_MAX];
    int count;
    int scroll_delta;
    /* Pixels that only MOVED: everything from slide_y down goes slide_delta
     * further on, and the caller blits rather than draws them again. */
    int slide_y;
    int slide_delta;
} tiku_trk_repaint_t;

void tiku_trk_repaint_init(tiku_trk_repaint_t *r);
void tiku_trk_repaint_add(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t rect);
/**
 * @brief A row appeared at @p row_y, in the view's own coordinates.
 *
 * Above the view that is a scroll; inside it, a slide plus the one new row.
 */
void tiku_trk_repaint_insert(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t visible,
                             int row_h, int row_y);
void tiku_trk_repaint_remove(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t visible,
                             int row_h, int row_y);
void tiku_trk_repaint_band(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t old_rect,
                           tiku_trk_repaint_rect_t new_rect);
int tiku_trk_repaint_take(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t *out,
                          int max);

/**
 * @brief Everything from @p y down has moved by @p delta pixels.
 *
 * A row inserted or removed inside the view leaves the rows below it
 * unchanged in every way but where they sit, so they are blitted.  Only one
 * slide fits in a batch: a second one gives up and repaints @p visible.
 */
void tiku_trk_repaint_slide(tiku_trk_repaint_t *r, int y, int delta,
                            tiku_trk_repaint_rect_t visible);

/** @brief Take the pending slide, or 0 when there is none. */
int tiku_trk_repaint_take_slide(tiku_trk_repaint_t *r, int *y);

#endif
