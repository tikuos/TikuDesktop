#include <stdio.h>
#include "tiku_trk_repaint.h"
int main(void) {
    tiku_trk_repaint_t r; tiku_trk_repaint_rect_t out[4];
    tiku_trk_repaint_init(&r);
    tiku_trk_repaint_insert(&r, (tiku_trk_repaint_rect_t){0,0,400,200}, 20, 0);
    if (tiku_trk_repaint_take(&r, out, 4) != 1 || out[0].h != 200) return 1;
    tiku_trk_repaint_insert(&r, (tiku_trk_repaint_rect_t){0,0,400,200}, 20, 1);
    if (r.scroll_delta != 20) return 1;
    tiku_trk_repaint_remove(&r, (tiku_trk_repaint_rect_t){0,0,400,200}, 20, 40);
    if (tiku_trk_repaint_take(&r, out, 4) != 1) return 1;
    /* A band that GREW reports the strips it grew over, not the whole
     * rectangle: the union would repaint every row the band covers, which
     * is the thing the difference exists to avoid (PVL-070). */
    tiku_trk_repaint_band(&r, (tiku_trk_repaint_rect_t){0,20,100,40},
                             (tiku_trk_repaint_rect_t){0,0,100,60});
    if (tiku_trk_repaint_take(&r, out, 4) != 1 ||
        out[0].y != 0 || out[0].h != 20) return 1;
    /* Unmoved is unchanged: nothing to paint again. */
    tiku_trk_repaint_band(&r, (tiku_trk_repaint_rect_t){0,0,100,60},
                             (tiku_trk_repaint_rect_t){0,0,100,60});
    if (tiku_trk_repaint_take(&r, out, 4) != 0) return 1;
    puts("[PASS] repaint invalidation and accumulated scroll"); return 0;
}
