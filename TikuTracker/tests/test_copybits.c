/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_copybits.c - moving a block of pixels within the surface.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

#include "tiku_gfx.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static tiku_rgb_t
at(tiku_surface_t *s, int x, int y)
{
    return s->px[(size_t)y * (size_t)s->w + (size_t)x] & 0xffffffu;
}

int
main(void)
{
    tiku_surface_t *s = tiku_surface_new(40, 40,
                                                   TIKU_RGB(0, 0, 0));
    tiku_rgb_t red = TIKU_RGB(255, 0, 0);
    tiku_rgb_t blue = TIKU_RGB(0, 0, 255);

    printf("moving a block of pixels (PVL-045, PVL-048)\n");
    tiku_fill(s, (tiku_rect_t){ 0, 10, 40, 10 }, red);
    tiku_fill(s, (tiku_rect_t){ 0, 20, 40, 10 }, blue);

    tiku_copy_bits(s, (tiku_rect_t){ 0, 10, 40, 20 }, 0, 10);
    check(at(s, 5, 20) == red && at(s, 5, 30) == blue, "PVL-045",
          "a block moved down arrives where it was sent");
    check(at(s, 5, 10) == red, "PVL-045",
          "and what it came from is left alone -- moving pixels is not "
          "erasing them, which is what the caller repaints over");

    tiku_copy_bits(s, (tiku_rect_t){ 0, 20, 40, 20 }, 0, -10);
    check(at(s, 5, 10) == red && at(s, 5, 20) == blue, "PVL-048",
          "and a block moved up over its own source keeps its order: the "
          "scanlines are walked in the direction that does not eat its "
          "own tail");

    tiku_fill(s, (tiku_rect_t){ 0, 0, 40, 40 },
                   TIKU_RGB(0, 0, 0));
    tiku_fill(s, (tiku_rect_t){ 0, 0, 40, 5 }, red);
    tiku_copy_bits(s, (tiku_rect_t){ 0, 0, 40, 5 }, 0, -10);
    check(at(s, 5, 0) == red, "PVL-045",
          "a move that would land off the top moves nothing rather than "
          "writing outside the surface");
    tiku_copy_bits(s, (tiku_rect_t){ 0, 0, 40, 5 }, 0, 38);
    check(at(s, 5, 38) == red && at(s, 5, 39) == red, "PVL-045",
          "and one that would run off the bottom moves the part that fits");

    {   /* The clip holds a moved block in, the way it holds every other
         * kind of drawing in: a list sliding its rows must not carry them
         * over its own border. */
        tiku_fill(s, (tiku_rect_t){ 0, 0, 40, 40 },
                       TIKU_RGB(0, 0, 0));
        tiku_fill(s, (tiku_rect_t){ 0, 10, 40, 10 }, red);
        tiku_clip_set(s, (tiku_rect_t){ 0, 0, 40, 25 });
        tiku_copy_bits(s, (tiku_rect_t){ 0, 10, 40, 10 }, 0, 10);
        tiku_clip_reset(s);
        check(at(s, 5, 24) == red && at(s, 5, 25) != red, "PVL-048",
              "a block moved past the clip carries only as far as the clip");
    }

    {   /* A window hands the frame the pixels it kept: they land where it
         * sits, and stop at the edge of what the frame will take. */
        tiku_surface_t *kept = tiku_surface_new(10, 10, blue);

        tiku_fill(s, (tiku_rect_t){ 0, 0, 40, 40 },
                       TIKU_RGB(0, 0, 0));
        tiku_blit(s, 5, 6, kept);
        check(at(s, 5, 6) == blue && at(s, 14, 15) == blue &&
              at(s, 4, 6) != blue && at(s, 15, 15) != blue, "PVL-045",
              "kept pixels arrive at the corner they are given, and take "
              "up their own size");
        tiku_blit(s, 35, 35, kept);
        check(at(s, 39, 39) == blue, "PVL-045",
              "a window at the edge hands over the part that fits rather "
              "than writing past the frame");
        tiku_clip_set(s, (tiku_rect_t){ 0, 0, 40, 20 });
        tiku_blit(s, 0, 18, kept);
        check(at(s, 2, 19) == blue && at(s, 2, 20) != blue, "PVL-045",
              "and what the frame has clipped away it does not take");
        tiku_clip_reset(s);
        /* A window half off the left edge hands over its RIGHT half: the
         * pixels trimmed away are the ones nobody can see, not the ones at
         * the front of every scanline. */
        tiku_fill(kept, (tiku_rect_t){ 0, 0, 4, 10 }, red);
        tiku_fill(s, (tiku_rect_t){ 0, 0, 40, 40 },
                       TIKU_RGB(0, 0, 0));
        tiku_blit(s, -4, 0, kept);
        check(at(s, 0, 3) == blue, "PVL-045",
              "and one over the left edge hands over the columns that are "
              "on the frame, not the ones that are off it");
        tiku_surface_free(kept);
    }

    tiku_surface_free(s);
    printf("\n%s\n", fails ? "FAILURES" : "the pixels move");
    return fails ? 1 : 0;
}
