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
} tiku_trk_repaint_t;

void tiku_trk_repaint_init(tiku_trk_repaint_t *r);
void tiku_trk_repaint_add(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t rect);
void tiku_trk_repaint_insert(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t visible,
                             int row_h, int above_view);
void tiku_trk_repaint_remove(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t visible,
                             int row_h, int row_y);
void tiku_trk_repaint_band(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t old_rect,
                           tiku_trk_repaint_rect_t new_rect);
int tiku_trk_repaint_take(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t *out,
                          int max);

#endif
