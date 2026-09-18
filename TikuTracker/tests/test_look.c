/* The soft things: a shadow that falls outside and fades, a rounded
 * fill whose corners are blended and whose body is opaque, a rounded
 * hairline that stays a hairline -- on a plain and a scaled surface. */
#include <assert.h>
#include <stdio.h>
#include "tiku_gfx.h"

static unsigned lum(tiku_rgb_t c) { return ((c >> 16) & 0xff) + ((c >> 8) & 0xff) + (c & 0xff); }

static void shadow_case(int scale)
{
    tiku_surface_t *s = tiku_surface_new(100, 100, TIKU_RGB(200, 200, 200));
    tiku_rect_t r = { 30, 30, 40, 30 };
    unsigned bg = lum(TIKU_RGB(200, 200, 200));

    assert(s != NULL);
    if (scale > 1) { assert(tiku_surface_rescale(s, scale, TIKU_RGB(200, 200, 200)) == 0); }
    tiku_shadow(s, r, 8, 0, 0.5f);
    /* Inside: untouched.  Just below the bottom edge: darkest.  Far
     * out: untouched.  Above the top: lighter than below, since the
     * shadow drops. */
    assert(lum(tiku_peek(s, 50, 45)) == bg);
    assert(lum(tiku_peek(s, 50, 60)) < bg);
    assert(lum(tiku_peek(s, 50, 61)) < bg && lum(tiku_peek(s, 50, 61)) >= lum(tiku_peek(s, 50, 60)));
    assert(lum(tiku_peek(s, 50, 75)) == bg);
    assert(lum(tiku_peek(s, 5, 5)) == bg);
    assert(lum(tiku_peek(s, 50, 28)) > lum(tiku_peek(s, 50, 61)));
    /* Beside the window, at its middle: shaded; at the far corner
     * diagonal, less than beside. */
    assert(lum(tiku_peek(s, 71, 45)) < bg);
    assert(lum(tiku_peek(s, 74, 64)) > lum(tiku_peek(s, 71, 45)));
    tiku_surface_free(s);
}

static void round_case(int scale)
{
    tiku_surface_t *s = tiku_surface_new(60, 60, TIKU_RGB(255, 255, 255));
    tiku_rect_t r = { 10, 10, 40, 30 };
    tiku_rgb_t ink = TIKU_RGB(0, 0, 0);

    assert(s != NULL);
    if (scale > 1) { assert(tiku_surface_rescale(s, scale, TIKU_RGB(255, 255, 255)) == 0); }
    tiku_fill_round(s, r, 6, TIKU_CORNER_ALL, ink);
    /* The body and the straight edges are the colour; the very corner
     * pixel is left nearly white; a pixel along the curve is between. */
    assert(tiku_peek(s, 30, 25) == ink);
    assert(tiku_peek(s, 30, 10) == ink);
    assert(tiku_peek(s, 10, 25) == ink);
    assert(lum(tiku_peek(s, 10, 10)) > 600);
    assert(lum(tiku_peek(s, 49, 39)) > 600);
    assert(lum(tiku_peek(s, 11, 12)) > 0 && lum(tiku_peek(s, 11, 12)) < 765);
    assert(tiku_peek(s, 5, 5) == TIKU_RGB(255, 255, 255));
    tiku_surface_free(s);

    s = tiku_surface_new(60, 60, TIKU_RGB(255, 255, 255));
    if (scale > 1) { assert(tiku_surface_rescale(s, scale, TIKU_RGB(255, 255, 255)) == 0); }
    tiku_frame_round(s, r, 6, TIKU_CORNER_ALL, ink);
    assert(tiku_peek(s, 30, 10) == ink);          /* the top edge      */
    assert(tiku_peek(s, 30, 39) == ink);          /* the bottom edge   */
    assert(tiku_peek(s, 30, 25) == TIKU_RGB(255, 255, 255));   /* hollow */
    assert(lum(tiku_peek(s, 10, 10)) > 600);      /* the corner is cut */
    assert(lum(tiku_peek(s, 12, 12)) < 765);      /* the curve is there */
    tiku_surface_free(s);
}

int main(void)
{
    shadow_case(1);
    shadow_case(2);
    round_case(1);
    round_case(2);
    /* A corner kept square stays square, the others round. */
    {
        tiku_surface_t *s = tiku_surface_new(60, 60, TIKU_RGB(255, 255, 255));
        tiku_fill_round(s, (tiku_rect_t){ 10, 10, 40, 30 }, 6,
                        TIKU_CORNER_ALL & ~TIKU_CORNER_TL, TIKU_RGB(0, 0, 0));
        assert(tiku_peek(s, 10, 10) == TIKU_RGB(0, 0, 0));
        assert(lum(tiku_peek(s, 49, 10)) > 600);
        tiku_surface_free(s);
    }
    /* A grab is the pixels; a blend at half is halfway; scaled, a ghost
     * lands in its rectangle and nowhere else. */
    {
        tiku_surface_t *s = tiku_surface_new(40, 40, TIKU_RGB(0, 0, 0));
        tiku_surface_t *g;
        tiku_fill(s, (tiku_rect_t){ 10, 10, 10, 10 }, TIKU_RGB(200, 100, 0));
        g = tiku_grab(s, (tiku_rect_t){ 8, 8, 14, 14 });
        assert(g != NULL && g->w == 14 && g->h == 14);
        assert(tiku_peek(g, 2, 2) == TIKU_RGB(200, 100, 0));
        assert(tiku_peek(g, 0, 0) == TIKU_RGB(0, 0, 0));
        tiku_fill(s, (tiku_rect_t){ 0, 0, 40, 40 }, TIKU_RGB(0, 0, 0));
        tiku_blend(s, 8, 8, g, 0.5f);
        assert(lum(tiku_peek(s, 12, 12)) > 120 && lum(tiku_peek(s, 12, 12)) < 180);
        assert(tiku_peek(s, 30, 30) == TIKU_RGB(0, 0, 0));
        tiku_fill(s, (tiku_rect_t){ 0, 0, 40, 40 }, TIKU_RGB(0, 0, 0));
        tiku_blend_scaled(s, (tiku_rect_t){ 20, 20, 7, 7 }, g, 1.0f);
        assert(tiku_peek(s, 23, 23) == TIKU_RGB(200, 100, 0));
        assert(tiku_peek(s, 19, 19) == TIKU_RGB(0, 0, 0));
        assert(tiku_peek(s, 27, 27) == TIKU_RGB(0, 0, 0));
        /* Past the edge: trimmed, never NULL for what is there. */
        tiku_surface_free(g);
        g = tiku_grab(s, (tiku_rect_t){ 35, 35, 20, 20 });
        assert(g != NULL && g->w == 5 && g->h == 5);
        tiku_surface_free(g);
        assert(tiku_grab(s, (tiku_rect_t){ 50, 50, 5, 5 }) == NULL);
        tiku_surface_free(s);
    }

    /* Too small to round: a plain fill, nothing lost. */
    {
        tiku_surface_t *s = tiku_surface_new(20, 20, 0);
        tiku_fill_round(s, (tiku_rect_t){ 2, 2, 6, 6 }, 6, TIKU_CORNER_ALL, TIKU_RGB(9, 9, 9));
        assert(tiku_peek(s, 2, 2) == TIKU_RGB(9, 9, 9));
        tiku_surface_free(s);
    }
    puts("PASS: shadows fall outside and fade; rounded fills and frames keep their bodies");
    return 0;
}
