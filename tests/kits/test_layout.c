/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_layout.c - a rectangle divided as it was described.
 *
 * The claim worth pinning is not that the arithmetic adds up: it is
 * that the SAME description answers the drawing and the hit test, so
 * the two cannot drift apart -- which is the shape of every layout bug
 * this tree has had.  So the coverage checks below matter more than the
 * spot values: no gap a click can fall into, nothing hanging off the
 * end, and a point in part three landing in part three.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

#include "tiku_layout.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] layout  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

int
main(void)
{
    tiku_rect_t r = { 100, 50, 400, 200 };

    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("a description of fixed parts\n");
    {
        tiku_layout_t l;
        tiku_rect_t a, b, c;

        tiku_layout_init(&l, 1, 0, 0);      /* a row, no gap, no pad */
        check(tiku_layout_add(&l, 100, 0, 0) == 0 &&
              tiku_layout_add(&l, 100, 0, 0) == 1 &&
              tiku_layout_add(&l, 100, 0, 0) == 2,
              "three fixed parts go in and say where they landed");
        a = tiku_layout_slot(&l, r, 0);
        b = tiku_layout_slot(&l, r, 1);
        c = tiku_layout_slot(&l, r, 2);
        check(a.x == 100 && a.w == 100 && a.y == 50 && a.h == 200,
              "the first takes its size and the whole cross extent");
        check(a.x + a.w == b.x && b.x + b.w == c.x,
              "and each begins exactly where the last ended, with no gap "
              "a click could fall into");
        check(tiku_layout_slot(&l, r, 3).w == 0,
              "a part the description does not have is degenerate, not "
              "off the end");
    }

    printf("\nwhat is left is shared by weight\n");
    {
        tiku_layout_t l;
        tiku_rect_t a, b, c;

        tiku_layout_init(&l, 1, 0, 0);
        (void)tiku_layout_add(&l, 100, 0, 0);   /* fixed  */
        (void)tiku_layout_add(&l, 0, 1, 0);     /* one share */
        (void)tiku_layout_add(&l, 0, 3, 0);     /* three shares */
        a = tiku_layout_slot(&l, r, 0);
        b = tiku_layout_slot(&l, r, 1);
        c = tiku_layout_slot(&l, r, 2);
        check(a.w == 100, "the fixed part is untouched by the sharing");
        check(b.w == 75 && c.w == 225,
              "and the rest divides in the shares it asked for");
        check(a.w + b.w + c.w == r.w,
              "the parts cover the run exactly");
        {
            /* 301 does not divide by 4: the shares come to one short,
             * and the last part must swallow it or a pixel column at
             * the end belongs to nobody. */
            tiku_rect_t odd = { 0, 0, 401, 40 };
            tiku_rect_t x0 = tiku_layout_slot(&l, odd, 0);
            tiku_rect_t x1 = tiku_layout_slot(&l, odd, 1);
            tiku_rect_t x2 = tiku_layout_slot(&l, odd, 2);

            check(x0.w + x1.w + x2.w == 401 &&
                  x2.x + x2.w == 401,
                  "and they cover a run the shares do NOT divide, to the "
                  "pixel -- the last takes the remainder");
        }
    }

    printf("\na part is never squeezed under its own least\n");
    {
        tiku_layout_t l;
        tiku_rect_t narrow = { 0, 0, 120, 40 };
        tiku_rect_t a, b;

        tiku_layout_init(&l, 1, 0, 0);
        (void)tiku_layout_add(&l, 0, 1, 80);
        (void)tiku_layout_add(&l, 0, 1, 80);
        a = tiku_layout_slot(&l, narrow, 0);
        b = tiku_layout_slot(&l, narrow, 1);
        check(a.w == 80 && b.w == 80,
              "two parts that will not go under 80 do not, even in 120 -- "
              "a control squeezed under its own word is unreadable, and a "
              "box that overruns is at least visibly wrong");
        check(tiku_layout_least(&l) == 160,
              "and the description says how small it may be asked to be, "
              "which is what a window asks before it opens");
    }

    printf("\ngaps and padding are part of the description\n");
    {
        tiku_layout_t l;
        tiku_rect_t a, b;

        tiku_layout_init(&l, 1, 10, 6);
        (void)tiku_layout_add(&l, 50, 0, 0);
        (void)tiku_layout_add(&l, 0, 1, 0);
        a = tiku_layout_slot(&l, r, 0);
        b = tiku_layout_slot(&l, r, 1);
        check(a.x == 106 && a.y == 56 && a.h == 188,
              "the pad holds the parts off every edge");
        check(b.x == a.x + a.w + 10, "and the gap sits between them");
        check(b.x + b.w == r.x + r.w - 6,
              "with the far pad kept at the end");
        check(tiku_layout_least(&l) == 50 + 10 + 2 * 6,
              "the least counts the gaps and the pad, not only the parts");
    }

    printf("\na stack is the same description laid the other way\n");
    {
        tiku_layout_t l;
        tiku_rect_t a, b;

        tiku_layout_init(&l, 0, 4, 0);
        (void)tiku_layout_add(&l, 0, 1, 0);
        (void)tiku_layout_add(&l, 30, 0, 0);
        a = tiku_layout_slot(&l, r, 0);
        b = tiku_layout_slot(&l, r, 1);
        check(a.y == 50 && a.h == 166 && a.x == 100 && a.w == 400,
              "the body takes what the footer leaves");
        check(b.y == a.y + a.h + 4 && b.h == 30 &&
              b.y + b.h == r.y + r.h,
              "and the footer sits at the foot, exactly");
    }

    printf("\nthe hit test is the same answer read backwards\n");
    {
        tiku_layout_t l;
        int i;

        tiku_layout_init(&l, 1, 8, 4);
        (void)tiku_layout_add(&l, 0, 1, 0);
        (void)tiku_layout_add(&l, 90, 0, 0);
        (void)tiku_layout_add(&l, 70, 0, 0);
        for (i = 0; i < 3; i++) {
            tiku_rect_t s = tiku_layout_slot(&l, r, i);
            char what[96];

            snprintf(what, sizeof what,
                     "a point inside part %d is found in part %d", i, i);
            check(tiku_layout_at(&l, r, s.x + s.w / 2,
                                 s.y + s.h / 2) == i, what);
        }
        check(tiku_layout_at(&l, r, r.x + 1, r.y + 1) == -1,
              "and a point in the pad belongs to nobody, rather than to "
              "whichever part the arithmetic reached first");
        check(tiku_layout_at(&l, r, r.x + r.w + 5, r.y + 10) == -1,
              "as does one outside the rectangle altogether");
    }

    printf("\na spring pushes a row to the far end\n");
    {
        tiku_layout_t l;
        tiku_rect_t b1, b2;

        tiku_layout_init(&l, 1, 6, 0);
        (void)tiku_layout_add(&l, 0, 0, 0);     /* the spring */
        (void)tiku_layout_add(&l, 60, 0, 0);
        (void)tiku_layout_add(&l, 80, 0, 0);
        b1 = tiku_layout_slot(&l, r, 1);
        b2 = tiku_layout_slot(&l, r, 2);
        check(b2.x + b2.w == r.x + r.w,
              "the last button ends at the right edge");
        check(b1.x + b1.w + 6 == b2.x,
              "and the one before it sits a gap away, which is how a "
              "footer is written without counting backwards from a width");
    }

    printf("\n%s\n", fails ? "FAILURES"
                           : "the parts land where they were described");
    return fails ? 1 : 0;
}
