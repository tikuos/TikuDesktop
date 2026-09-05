/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_formrow.c - a labelled row splits by the label's width.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_form_row.h"
#include "tiku_measure.h"

#include <stdio.h>

static int fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

int
main(void)
{
    tiku_rect_t r = { 10, 20, 300, 24 }, label, control;
    int w = tiku_measure_width(TIKU_MEASURE_PLAIN, "Temperature");

    printf("a labelled row\n");
    tiku_form_row_split(r, "Temperature", 8, &label, &control);
    check(label.x == 10 && label.w == w,
          "the label takes exactly the width of its words");
    check(control.x == 10 + w + 8 && control.x + control.w == 310,
          "the control takes the rest, past the gap");
    check(label.y == 20 && label.h == 24 && control.h == 24,
          "both keep the row's height");
    printf("%s\n", fails ? "FAILURES" : "the row splits without a pixel named");
    return fails ? 1 : 0;
}
