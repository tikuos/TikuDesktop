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

    /* A rect too small to say anything in is left alone rather than
     * filled with a smear. */
    GROUND = TIKU_C_PANEL;
    (void)fresh();
    tiku_logo_paint(surf, (tiku_rect_t){ 20, 20, 3, 3 }, TIKU_LOGO_GROUND);
    check(inked((tiku_rect_t){ 0, 0, S, S }) == 0,
          "and asked for three pixels it draws nothing at all");

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe mark holds from the Deskbar to the About box\n");
    return 0;
}
