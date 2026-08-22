/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_metrics.c - how big a control is.
 *
 * These numbers used to be written into every window that drew one: a
 * 22-pixel row, a 26-pixel button, a 21-pixel title bar.  That was right
 * for a 12-pixel face and for nothing else, and when the face became a
 * preference the windows kept the numbers -- so at 16 px the words left
 * their rows, and one control's halves were split at a pixel the control
 * no longer reached.
 *
 * Two claims are made here, and the FIRST is the one that made the change
 * safe to attempt at all:
 *
 *   at 12 px each answers exactly the number it replaced, so every pinned
 *   pixel in the shell suite goes on meaning what it meant; and
 *
 *   at every other size it moves, in the direction the letters moved.
 *
 * The second without the first would have been a rewrite of the whole
 * interface.  The first without the second would be a rename.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

#include "tiku_font.h"
#include "tiku_ui.h"
#include "tiku_window.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] TS-MET  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

int
main(void)
{
    static const int sizes[] = { 10, 12, 14, 16 };
    int row[4], button[4], tab[4], side[4], step[4];
    int i, rising = 1;

    printf("how big a control is (TS-MET)\n");

    for (i = 0; i < 4; i++) {
        (void)tiku_font_set_size(sizes[i]);
        row[i] = tiku_ui_row_h();
        button[i] = tiku_ui_button_h();
        step[i] = tiku_ui_step_w();
        tab[i] = tiku_window_tab_h();
        side[i] = tiku_window_tab_button();
    }

    /* The face the interface has always defaulted to: 12 px is index 1. */
    check(row[1] == 22, "a list row is the 22 pixels it always was at 12 px");
    check(button[1] == 26, "a button is 26");
    check(tab[1] == 21, "a window's tab is 21");
    check(side[1] == 12, "and its close button 12");
    check(step[1] == 26, "a stepper's half is square with the button");

    for (i = 1; i < 4; i++) {
        if (row[i] <= row[i - 1] || button[i] <= button[i - 1] ||
            tab[i] <= tab[i - 1] || side[i] <= side[i - 1]) {
            rising = 0;
        }
    }
    check(rising, "and every one of them grows with the face, 10 through 16");

    /*
     * A tab has to hold the button in it whatever the face, or the thing
     * that closes a window is drawn through its own edge.
     */
    {
        int fits = 1;

        for (i = 0; i < 4; i++) {
            if (tab[i] < side[i] + 6) {
                fits = 0;
            }
        }
        check(fits, "a tab is always tall enough for the button inside it");
    }

    /* A button must hold its label, in whatever words the catalogue has. */
    (void)tiku_font_set_size(16);
    check(tiku_ui_button_w("Defaults", 112) >= 112,
          "a button never shrinks below the width it was given");
    check(tiku_ui_button_w("a considerably longer label than that", 112)
              > 112,
          "and grows past it for a label that would not have fitted");

    (void)tiku_font_set_size(12);
    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe measurements hold at 12, and move everywhere else\n");
    return 0;
}
