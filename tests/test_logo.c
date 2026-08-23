/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_logo.c - the mark, at the sizes it has to survive.
 *
 * It is kept as the drawing rather than as a picture so that one artwork
 * covers a 16-pixel Deskbar strip and an 80-pixel About panel, five
 * times apart.  That is the claim, so it is what is tested: that ink
 * lands at every size, that the amber the mark is known by is actually
 * there, that it drains with the rest of the pictures on a palette
 * without hue, and that it stays inside the box it was given -- a logo
 * that paints one pixel past its rect is a logo that scribbles on the
 * Deskbar every time the bar is drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_gfx.h"
#include "tiku_logo.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] TS-LOGO %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

#define S 160
static tiku_surface_t *surf;
static tiku_rgb_t GROUND;

static tiku_surface_t *
fresh(void)
{
    if (surf == NULL) {
        surf = calloc(1u, sizeof *surf);
        surf->px = calloc((size_t)S * S, sizeof *surf->px);
        surf->w = S;
        surf->h = S;
        surf->scale = 1;
    }
    surf->clip = (tiku_rect_t){ 0, 0, S, S };
    tiku_fill(surf, surf->clip, GROUND);
    return surf;
}

/** @brief How many pixels differ from the ground it was drawn on. */
static int
inked(tiku_rect_t r)
{
    int n = 0, x, y;

    for (y = r.y; y < r.y + r.h; y++) {
        for (x = r.x; x < r.x + r.w; x++) {
            if (surf->px[(size_t)y * S + x] != GROUND) {
                n++;
            }
        }
    }
    return n;
}

/** @brief The mean brightness of what was drawn in @p r. */
static int
luminance_of(tiku_rect_t r)
{
    long sum = 0;
    int x, y;

    for (y = r.y; y < r.y + r.h; y++) {
        for (x = r.x; x < r.x + r.w; x++) {
            tiku_rgb_t c = surf->px[(size_t)y * S + x];

            sum += (long)tiku_grey(c) & 0xFFl;
        }
    }
    return (int)(sum / (r.w * r.h));
}

/** @brief The bounding box of everything drawn on the surface. */
static tiku_rect_t
ink_bounds(void)
{
    int x, y, x0 = S, y0 = S, x1 = -1, y1 = -1;
    tiku_rect_t r;

    for (y = 0; y < S; y++) {
        for (x = 0; x < S; x++) {
            if (surf->px[(size_t)y * S + x] != GROUND) {
                if (x < x0) { x0 = x; }
                if (y < y0) { y0 = y; }
                if (x > x1) { x1 = x; }
                if (y > y1) { y1 = y; }
            }
        }
    }
    r.x = x0; r.y = y0;
    r.w = (x1 < x0) ? 0 : x1 - x0 + 1;
    r.h = (y1 < y0) ? 0 : y1 - y0 + 1;
    return r;
}

/** @brief Whether any pixel is recognisably the mark's amber. */
static int
has_amber(void)
{
    int i;

    for (i = 0; i < S * S; i++) {
        tiku_rgb_t c = surf->px[i];
        unsigned r = (c >> 16) & 0xFFu, g = (c >> 8) & 0xFFu;
        unsigned b = c & 0xFFu;

        if (r > 200u && g > 140u && g < 230u && b < 110u) {
            return 1;
        }
    }
    return 0;
}

int
main(void)
{
    static const int sizes[] = { 16, 20, 24, 32, 64, 80 };
    size_t i;
    int rising = 1, last = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("the mark, at the sizes it has to survive (TS-LOGO)\n");
    GROUND = TIKU_C_PANEL;

    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        int n = sizes[i];
        char what[96];
        int ink;

        (void)fresh();
        tiku_logo_paint(surf, (tiku_rect_t){ 20, 20, n, n },
                        TIKU_LOGO_GROUND);
        ink = inked((tiku_rect_t){ 20, 20, n, n });
        snprintf(what, sizeof what,
                 "at %2d px it is drawn, and covers its square", n);
        check(ink > (n * n) / 2, what);
        if (ink <= last) {
            rising = 0;
        }
        last = ink;

        /* Nothing outside the rect it was given. */
        {
            int spill = inked((tiku_rect_t){ 0, 0, S, S }) - ink;

            snprintf(what, sizeof what,
                     "and at %2d px not one pixel lands outside it", n);
            check(spill == 0, what);
        }
    }
    check(rising, "and a bigger square is more ink than a smaller one");

    /* The amber is the thing the mark is known by. */
    (void)fresh();
    tiku_theme_set(NULL);
    tiku_logo_paint(surf, (tiku_rect_t){ 20, 20, 64, 64 },
                    TIKU_LOGO_GROUND);
    check(has_amber(), "the lit edges carry the amber the mark is known by");

    (void)fresh();
    tiku_theme_set(tiku_theme_mono());
    GROUND = TIKU_C_PANEL;
    (void)fresh();
    tiku_logo_paint(surf, (tiku_rect_t){ 20, 20, 64, 64 },
                    TIKU_LOGO_GROUND);
    check(inked((tiku_rect_t){ 20, 20, 64, 64 }) > 64 * 64 / 2 &&
          !has_amber(),
          "and on a palette without hue it is drawn and has none: a "
          "desktop asked for grey does not get an amber logo in it");
    tiku_theme_set(NULL);

    /*
     * The near cube alone fills the square it was given.
     *
     * The artwork is laid out for BOTH cubes across the unit square, and
     * the near one occupies half its width and three fifths of its
     * height.  Drawn on its own -- which is every size below thirty-two,
     * and so every size the Deskbar leaf ever asks for -- a fit made to
     * the whole unit square would put a two-thirds-size mark in the
     * corner of the room and leave the rest empty.  So the near-only
     * rendition is fitted to its OWN bounds, and a caller asking for
     * twenty-six pixels of mark gets twenty-six pixels of mark.
     */
    {
        static const int only_near[] = { 22, 24, 26, 30 };
        size_t j;

        for (j = 0; j < sizeof only_near / sizeof only_near[0]; j++) {
            int n = only_near[j];
            tiku_rect_t b;
            char what[96];

            GROUND = TIKU_C_PANEL;
            (void)fresh();
            tiku_logo_paint(surf, (tiku_rect_t){ 20, 20, n, n }, 0u);
            b = ink_bounds();

            snprintf(what, sizeof what,
                     "at %2d px the cube alone still stands its full "
                     "height", n);
            check(b.h >= n - 2, what);
            snprintf(what, sizeof what,
                     "and is wider than half the square it was handed, "
                     "which fitting it to the whole unit box is not");
            check(b.w > n * 3 / 5, what);
        }
    }

    /* A rect too small to say anything in is left alone rather than
     * filled with a smear. */
    GROUND = TIKU_C_PANEL;
    (void)fresh();
    tiku_logo_paint(surf, (tiku_rect_t){ 20, 20, 3, 3 }, TIKU_LOGO_GROUND);
    check(inked((tiku_rect_t){ 0, 0, S, S }) == 0,
          "and asked for three pixels it draws nothing at all");

    /*---------------------------------------------------------------*/
    /* Colour, at the time of drawing                                  */
    /*---------------------------------------------------------------*/
    {
        tiku_logo_palette_t a, b, half;
        int n = tiku_logo_palette_count();

        check(n >= 3, "there are three or more sets to cycle between");
        tiku_logo_palette(0, 0.0f, &a);
        tiku_logo_palette(1, 0.0f, &b);
        check(a.accent != b.accent && a.ground != b.ground,
              "and they are different sets, not the same one twice");

        /* The ends of a fade are the sets themselves, or the cycle would
         * jump on every wrap. */
        tiku_logo_palette(0, 1.0f, &half);
        check(half.accent == b.accent && half.tint == b.tint,
              "carried all the way, a set becomes the next one exactly");
        tiku_logo_palette(n - 1, 1.0f, &half);
        check(half.accent == a.accent,
              "and the last carries round to the first, so it can run "
              "for ever without a seam");

        tiku_logo_palette(0, 0.5f, &half);
        check(half.accent != a.accent && half.accent != b.accent,
              "and halfway is neither end: the COLOURS are interpolated, "
              "which is why a fade costs what a still costs");
    }

    /* Shade: what puts one mark legibly on three different grounds. */
    {
        tiku_rect_t box = { 20, 20, 48, 48 };
        int plain, lighter, darker;

        GROUND = TIKU_RGB(0x6E, 0x58, 0x0C);
        (void)fresh();
        tiku_logo_paint_with(surf, box, 0u, NULL, 1.0f);
        plain = luminance_of(box);
        (void)fresh();
        tiku_logo_paint_with(surf, box, 0u, NULL, 0.35f);
        lighter = luminance_of(box);
        (void)fresh();
        tiku_logo_paint_with(surf, box, 0u, NULL, 1.15f);
        darker = luminance_of(box);
        check(lighter > plain && darker < plain,
              "under one lightens the mark and over one darkens it, which "
              "is what lets the Deskbar's leaf carry it in all three modes");
        GROUND = TIKU_C_PANEL;
    }

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe mark holds from the Deskbar to the About box\n");
    return 0;
}
