/*
 * TikuTracker -- tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_plaque.c - the plaque holds its shape.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_plaque.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] plaque  %s\n", cond ? "PASS" : "FAIL", what);
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

    tiku_plaque_paint(a, "Something", LINES, 2, "V1", "gathering…");
    /* The band: artwork, not paper.  Every row of the band's middle
     * column should have LEFT the paper colour behind. */
    for (y = 4; y < 80; y += 8) {
        artful += (a->px[y * a->w + 200] != TIKU_C_DOC);
    }
    check(artful == 10,
          "the artwork is banded across the top, and none of it is "
          "bare paper");
    check(a->px[150 * a->w + 200] == TIKU_C_DOC,
          "and below the band the plaque IS paper");
    tiku_plaque_paint(b, "Something", LINES, 2, "V1", NULL);
    check(differ(a, b) > 0,
          "a plaque still gathering says so: the status line is the "
          "difference between it and the finished one");
    tiku_plaque_paint(b, "Something", LINES, 2, "V1", "gathering…");
    check(differ(a, b) == 0,
          "and the same words paint the same plaque, so a stage can be "
          "redrawn without the picture creeping");
    tiku_plaque_paint(b, "Something else", LINES, 2, "V1", "gathering…");
    check(differ(a, b) > 0, "the name is on it");
    tiku_surface_free(a);
    tiku_surface_free(b);
    printf("\n%s\n", fails ? "FAILURES" : "the plaque holds its shape");
    return fails ? 1 : 0;
}
