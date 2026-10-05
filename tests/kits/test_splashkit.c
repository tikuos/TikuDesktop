/*
 * TikuTracker -- tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_splashkit.c - the splash screen holds its shape.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_splash.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] splash screen  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief How many pixels of @p s differ from @p t. */
static int
differ(const tiku_surface_t *s, const tiku_surface_t *t)
{
    int i, n = 0;

    for (i = 0; i < s->w * s->h; i++) {
        n += (s->px[i] != t->px[i]);
    }
    return n;
}

int
main(void)
{
    static const char *const LINES[] = { "by somebody", "on something" };
    tiku_surface_t *a = tiku_surface_new(400, 230, TIKU_C_DOC);
    tiku_surface_t *b = tiku_surface_new(400, 230, TIKU_C_DOC);
    int y, artful = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);

    tiku_splash_paint(a, "Something", LINES, 2, "V1", "gathering…", NULL);
    /* The band: artwork, not paper.  Every row of the band's middle
     * column should have LEFT the paper colour behind. */
    for (y = 4; y < 80; y += 8) {
        artful += (a->px[y * a->w + 200] != TIKU_C_DOC);
    }
    check(artful == 10,
          "the artwork is banded across the top, and none of it is "
          "bare paper");
    check(a->px[150 * a->w + 200] == TIKU_C_DOC,
          "and below the band the splash screen IS paper");
    tiku_splash_paint(b, "Something", LINES, 2, "V1", NULL, NULL);
    check(differ(a, b) > 0,
          "a splash screen still gathering says so: the status line is the "
          "difference between it and the finished one");
    tiku_splash_paint(b, "Something", LINES, 2, "V1", "gathering…", NULL);
    check(differ(a, b) == 0,
          "and the same words paint the same splash screen, so a stage can be "
          "redrawn without the picture creeping");
    tiku_splash_paint(b, "Something else", LINES, 2, "V1", "gathering…", NULL);
    check(differ(a, b) > 0, "the name is on it");
    {
        /* The person's own banner: one loud colour, and the band wears
         * it edge to edge instead of the sweep. */
        tiku_surface_t *art = tiku_surface_new(2, 2,
                                     TIKU_RGB(200, 30, 30));

        tiku_splash_paint(b, "Something", LINES, 2, "V1", NULL, art);
        check(b->px[40 * b->w + 200] == TIKU_RGB(200, 30, 30) &&
              b->px[40 * b->w + 20] == TIKU_RGB(200, 30, 30),
              "a banner the caller hands in IS the band, edge to edge");
        check(b->px[150 * b->w + 200] == TIKU_C_DOC,
              "and the field below it is still paper");
        tiku_surface_free(art);
    }
    {
        /* The license notice: grey fine print under the status, in the
         * room the credits would have, the band left as it was. */
        tiku_surface_t *c = tiku_surface_new(560, 300, TIKU_C_DOC);
        tiku_surface_t *d = tiku_surface_new(560, 300, TIKU_C_DOC);
        int x, grey = 0, band = 0;

        tiku_splash_paint(c, "Something", NULL, 0, NULL, "gathering…", NULL);
        tiku_splash_paint(d, "Something", NULL, 0, NULL, "gathering…", NULL);
        tiku_splash_license(d);
        for (y = 0; y < 300; y++) {
            for (x = 0; x < 560; x++) {
                if (c->px[y * 560 + x] == d->px[y * 560 + x]) {
                    continue;
                }
                band += y < 150;
                grey += y > 200 && x < 520;
            }
        }
        check(grey > 200 && band == 0,
              "the license notice is fine print under the status, and the "
              "band is untouched");
        tiku_surface_free(c);
        tiku_surface_free(d);
    }
    tiku_surface_free(a);
    tiku_surface_free(b);
    printf("\n%s\n", fails ? "FAILURES" : "the splash screen holds its shape");
    return fails ? 1 : 0;
}
