/* A shadow is its band: nothing is laid under the box it falls from, a
 * rounded corner's cut is the one exception, and the cost of laying it
 * follows the band's length rather than the box's area.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "tiku_gfx.h"

/** @brief Microseconds now, for a ratio rather than a number. */
static double now_us(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

/** @brief Lay @p r's shadow @p times over a fresh desk; the time per call. */
static double cost(tiku_rect_t r, int scale, int times)
{
    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_RGB(51, 102, 152));
    double t0;
    int i;

    assert(s);
    if (scale > 1) { tiku_surface_rescale(s, scale, TIKU_RGB(51, 102, 152)); }
    tiku_shadow(s, r, 12, 10, 0.5f);
    t0 = now_us();
    for (i = 0; i < times; i++) { tiku_shadow(s, r, 12, 10, 0.5f); }
    t0 = (now_us() - t0) / times;
    tiku_surface_free(s);
    return t0;
}

int main(void)
{
    const tiku_rgb_t desk = TIKU_RGB(51, 102, 152);
    tiku_surface_t *s = tiku_surface_new(400, 300, desk);
    tiku_rect_t r = {100, 80, 200, 120};
    int x, y, under = 0, band = 0, cut = 0;

    assert(s);
    tiku_shadow(s, r, 12, 10, 0.5f);
    for (y = 0; y < 300; y++) {
        for (x = 0; x < 400; x++) {
            int inside = x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
            int touched = tiku_peek(s, x, y) != desk;

            if (inside && touched) {
                /* Only within a corner's square of a corner: the cut. */
                int cx = (x < r.x + 10) || (x >= r.x + r.w - 10);
                int cy = (y < r.y + 10) || (y >= r.y + r.h - 10);

                assert(cx && cy);
                cut++;
            } else if (inside) {
                under++;
            } else if (touched) {
                band++;
            }
        }
    }
    assert(under > 0 && band > 0 && cut > 0);
    /* The interior stays the desk's: a window paints over it anyway. */
    assert(tiku_peek(s, 200, 140) == desk);
    tiku_surface_free(s);
    {
        /* Sixty-four times the area, but the band is only eight times
         * as long: a cost that grew with the area would show here. */
        tiku_rect_t big = {100, 80, 640, 480}, small = {100, 80, 80, 60};
        double b = cost(big, 1, 40), sm = cost(small, 1, 40);

        printf("shadow: 640x480 %.0f us, 80x60 %.0f us at 1x\n", b, sm);
        assert(b < 16 * sm + 200);
        b = cost(big, 2, 20);
        printf("shadow: 640x480 %.0f us at 2x\n", b);
        assert(b < 2000);   /* one shadow leaves the frame its budget */
    }
    puts("PASS: a shadow lays only its band and its corners' cuts, at the band's cost");
    return 0;
}
