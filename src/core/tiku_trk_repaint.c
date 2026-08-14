/* SPDX-License-Identifier: Apache-2.0 */
#include "tiku_trk_repaint.h"
#include <string.h>

static int max_i(int a, int b) { return a > b ? a : b; }
static int min_i(int a, int b) { return a < b ? a : b; }

static tiku_trk_repaint_rect_t
union_rect(tiku_trk_repaint_rect_t a, tiku_trk_repaint_rect_t b)
{
    int ax = min_i(a.x, b.x), ay = min_i(a.y, b.y);
    int ar = max_i(a.x + a.w, b.x + b.w);
    int ab = max_i(a.y + a.h, b.y + b.h);
    return (tiku_trk_repaint_rect_t){ ax, ay, ar - ax, ab - ay };
}

void
tiku_trk_repaint_init(tiku_trk_repaint_t *r)
{
    if (r != NULL) memset(r, 0, sizeof *r);
}

void
tiku_trk_repaint_add(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t rect)
{
    int i;
    if (r == NULL || rect.w <= 0 || rect.h <= 0) return;
    for (i = 0; i < r->count; i++) {
        tiku_trk_repaint_rect_t old = r->rect[i];
        int touch = !(old.x + old.w < rect.x || rect.x + rect.w < old.x ||
                       old.y + old.h < rect.y || rect.y + rect.h < old.y);
        if (touch) {
            r->rect[i] = union_rect(old, rect);
            return;
        }
    }
    if (r->count < TIKU_TRK_REPAINT_MAX) r->rect[r->count++] = rect;
    else r->rect[0] = union_rect(r->rect[0], rect);
}

void
tiku_trk_repaint_insert(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t visible,
                        int row_h, int above_view)
{
    if (r == NULL || row_h <= 0) return;
    if (above_view) r->scroll_delta += row_h;
    else tiku_trk_repaint_add(r, (tiku_trk_repaint_rect_t){visible.x, visible.y,
                                                   visible.w, visible.h});
}

void
tiku_trk_repaint_remove(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t visible,
                        int row_h, int row_y)
{
    if (r == NULL || row_h <= 0) return;
    if (row_y < visible.y) r->scroll_delta -= row_h;
    else if (row_y < visible.y + visible.h)
        tiku_trk_repaint_add(r, (tiku_trk_repaint_rect_t){visible.x, row_y,
                                                  visible.w,
                                                  visible.y + visible.h - row_y});
}

void
tiku_trk_repaint_band(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t old_rect,
                      tiku_trk_repaint_rect_t new_rect)
{
    if (r == NULL) return;
    /* The union is the conservative difference region.  It is still a
     * sliver rather than a whole-window invalidation and works with the
     * surface clip used by both shells. */
    tiku_trk_repaint_add(r, union_rect(old_rect, new_rect));
}

int
tiku_trk_repaint_take(tiku_trk_repaint_t *r, tiku_trk_repaint_rect_t *out, int max)
{
    int n, i;
    if (r == NULL || out == NULL || max <= 0) return 0;
    n = r->count < max ? r->count : max;
    for (i = 0; i < n; i++) out[i] = r->rect[i];
    if (n < r->count) memmove(r->rect, r->rect + n,
                              (size_t)(r->count - n) * sizeof r->rect[0]);
    r->count -= n;
    return n;
}
