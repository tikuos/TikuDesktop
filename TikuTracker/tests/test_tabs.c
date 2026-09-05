/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_tabs.c - the tab strip widget: what it holds, where it lays tabs
 * out, which one a click lands on, and how the arrow moves between them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

#include "tiku_tabs.h"
#include "tiku_tabs_paint.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] tabs    %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

int
main(void)
{
    tiku_tabs_t t;
    tiku_rect_t strip = { 100, 20, 300, 24 };
    tiku_rect_t r0, r1, r2;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("a tab strip owns its tabs and its current one\n");

    tiku_tabs_init(&t);
    check(tiku_tabs_count(&t) == 0 && tiku_tabs_current(&t) == -1,
          "starts empty, with nothing current");
    check(tiku_tabs_add(&t, "Info") == 0 && tiku_tabs_current(&t) == 0,
          "the first tab added becomes current");
    check(tiku_tabs_add(&t, "Permissions") == 1 &&
          tiku_tabs_add(&t, "Attributes") == 2 &&
          tiku_tabs_count(&t) == 3, "and the rest queue after it");
    check(tiku_tabs_current(&t) == 0,
          "adding a tab does not move the current one");

    printf("\nselection clamps, and the arrow wraps\n");
    check(tiku_tabs_select(&t, 1) == 1, "select lands on the asked tab");
    check(tiku_tabs_select(&t, 99) == 2,
          "and clamps past the end to the last, not off it");
    check(tiku_tabs_select(&t, -4) == 0, "and past the start to the first");
    check(tiku_tabs_next(&t, 1) == 1 && tiku_tabs_next(&t, 1) == 2,
          "the arrow steps forward");
    check(tiku_tabs_next(&t, 1) == 0,
          "and past the last tab wraps to the first, never onto nothing");
    check(tiku_tabs_next(&t, -1) == 2,
          "back past the first wraps to the last -- a plain %% would go "
          "negative here");

    printf("\nlayout divides the strip, and the last tab eats the remainder\n");
    r0 = tiku_tabs_rect(&t, strip, 0);
    r1 = tiku_tabs_rect(&t, strip, 1);
    r2 = tiku_tabs_rect(&t, strip, 2);
    check(r0.x == 100 && r0.w == 100, "the first tab is a clean third");
    check(r1.x == 200 && r2.x == 300,
          "and the second and third follow it exactly");
    check(r0.x + r0.w == r1.x && r1.x + r1.w == r2.x &&
          r2.x + r2.w == strip.x + strip.w,
          "the three cover the strip with no gap a click could fall into");
    check(tiku_tabs_rect(&t, strip, 3).w == 0,
          "an index the strip does not have is degenerate, not off the end");

    {
        /* 301 does not divide by 3: three 100s leave one over, and the
         * last tab must swallow it so the strip is covered exactly. */
        tiku_rect_t odd = { 0, 0, 301, 24 };
        tiku_rect_t a = tiku_tabs_rect(&t, odd, 2);

        check(a.x + a.w == 301,
              "an odd strip width is covered to the pixel by the last tab");
    }

    printf("\na click resolves to the tab under it\n");
    check(tiku_tabs_hit(&t, strip, 150, 30) == 0,
          "a point in the first third hits the first tab");
    check(tiku_tabs_hit(&t, strip, 250, 30) == 1, "the middle, the second");
    check(tiku_tabs_hit(&t, strip, 380, 30) == 2, "the right, the third");
    check(tiku_tabs_hit(&t, strip, 150, 5) == -1,
          "a point above the strip hits no tab");
    check(tiku_tabs_hit(&t, strip, 500, 30) == -1,
          "and one past its right edge hits none either");

    /* The draw path must at least not fault on a real surface; what it
     * paints is a pixel claim the shell scripts make, not this. */
    {
        tiku_surface_t *s = tiku_surface_new(420, 60, TIKU_C_PANEL);

        if (s != NULL) {
            tiku_tabs_draw(&t, s, strip);
            check(1, "the strip draws onto a surface without faulting");
            tiku_surface_free(s);
        }
    }

    printf("\n%s\n", fails ? "FAILURES" : "the tabs keep their places");
    return fails ? 1 : 0;
}
