/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_textview.c - many lines of text with a caret in them, and the
 * palette grid beside it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_textview.h"
#include "tiku_colors.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] text    %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

static void
type(tiku_textview_t *tv, const char *s)
{
    while (*s != '\0') {
        tiku_textview_insert(tv, *s++);
    }
}

int
main(void)
{
    tiku_textview_t tv;
    char buf[512];
    int line, col;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("a document is never fewer than one line\n");

    tiku_textview_init(&tv);
    check(tiku_textview_lines(&tv) == 1 &&
          tiku_textview_line_len(&tv, 0) == 0,
          "an empty document is one empty line, not none");
    check(!tiku_textview_modified(&tv), "and is not modified yet");

    printf("\nsetting text splits on newlines, and the last one closes\n");
    tiku_textview_set(&tv, "alpha\nbeta\ngamma\n");
    check(tiku_textview_lines(&tv) == 3,
          "a trailing newline ends the last line rather than starting a "
          "blank one -- or a file grows a line each time it is saved");
    check(strcmp(tiku_textview_line(&tv, 1), "beta") == 0,
          "and the lines are the text between the newlines");
    check(!tiku_textview_modified(&tv),
          "setting the text is not an edit of it");
    check(strcmp(tiku_textview_line(&tv, 99), "") == 0,
          "a line that does not exist reads empty, not off the end");

    printf("\nthe caret walks the text as one run\n");
    tiku_textview_place(&tv, 1, 0);
    tiku_textview_move(&tv, TIKU_TEXTVIEW_LEFT);
    tiku_textview_caret(&tv, &line, &col);
    check(line == 0 && col == 5,
          "left at a line's head goes to the END of the one above");
    tiku_textview_move(&tv, TIKU_TEXTVIEW_RIGHT);
    tiku_textview_caret(&tv, &line, &col);
    check(line == 1 && col == 0,
          "and right at a line's end goes to the head of the next");
    tiku_textview_place(&tv, 2, 5);
    tiku_textview_move(&tv, TIKU_TEXTVIEW_UP);
    tiku_textview_caret(&tv, &line, &col);
    check(line == 1 && col == 4,
          "moving onto a shorter line clamps the column to its end");
    tiku_textview_place(&tv, 0, 99);
    tiku_textview_caret(&tv, &line, &col);
    check(col == 5, "and the caret can never be placed past a line's end");

    printf("\ntyping, breaking and rubbing out\n");
    tiku_textview_init(&tv);
    type(&tv, "abc");
    check(strcmp(tiku_textview_line(&tv, 0), "abc") == 0 &&
          tiku_textview_modified(&tv), "typing appends and marks modified");
    tiku_textview_place(&tv, 0, 1);
    tiku_textview_newline(&tv);
    check(tiku_textview_lines(&tv) == 2 &&
          strcmp(tiku_textview_line(&tv, 0), "a") == 0 &&
          strcmp(tiku_textview_line(&tv, 1), "bc") == 0,
          "Return breaks the line and the tail becomes the next one");
    tiku_textview_caret(&tv, &line, &col);
    check(line == 1 && col == 0, "with the caret at the new line's head");
    tiku_textview_backspace(&tv);
    check(tiku_textview_lines(&tv) == 1 &&
          strcmp(tiku_textview_line(&tv, 0), "abc") == 0,
          "and backspace there folds the two back together");
    tiku_textview_caret(&tv, &line, &col);
    check(line == 0 && col == 1,
          "leaving the caret at the SEAM, not at the line's start");

    printf("\ndelete is the same fold, from the other side\n");
    tiku_textview_set(&tv, "one\ntwo");
    tiku_textview_place(&tv, 0, 3);
    tiku_textview_delete(&tv);
    check(tiku_textview_lines(&tv) == 1 &&
          strcmp(tiku_textview_line(&tv, 0), "onetwo") == 0,
          "delete at a line's end pulls the next line up");
    tiku_textview_caret(&tv, &line, &col);
    check(line == 0 && col == 3,
          "and the caret stays where it was, not where the fold put it");

    printf("\nthe view follows the caret, and never past the end\n");
    tiku_textview_set(&tv, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10");
    tiku_textview_place(&tv, 9, 0);
    check(tiku_textview_reveal(&tv, 4) &&
          tiku_textview_top(&tv) == 6,
          "a caret below the screen scrolls just enough to show it");
    tiku_textview_place(&tv, 0, 0);
    check(tiku_textview_reveal(&tv, 4) && tiku_textview_top(&tv) == 0,
          "and one above scrolls back to it");
    (void)tiku_textview_scroll_to(&tv, 500, 4);
    check(tiku_textview_top(&tv) == 6,
          "scrolling past the end stops at the last screenful, so the "
          "document cannot be scrolled out of sight");

    printf("\nthe text comes back out as it went in\n");
    tiku_textview_set(&tv, "alpha\nbeta");
    {
        size_t need = tiku_textview_text(&tv, buf, sizeof buf);

        check(need == 11 && strncmp(buf, "alpha\nbeta\n", 11) == 0,
              "every line, joined by newlines, with a closing one");
        check(tiku_textview_text(&tv, NULL, 0) == need,
              "and the size can be asked for without a buffer");
    }
    tiku_textview_saved(&tv);
    check(!tiku_textview_modified(&tv), "saying it was saved clears the flag");
    tiku_textview_free(&tv);
    check(tiku_textview_lines(&tv) == 1,
          "freeing leaves an empty document, not an invalid one");

    printf("\na palette is a grid one colour is picked from\n");
    {
        tiku_colors_t c;
        tiku_rect_t grid = { 0, 0, 160, 80 };

        tiku_colors_default(&c);
        check(tiku_colors_count(&c) == 32 && tiku_colors_current(&c) == 0,
              "the default palette is four rows of eight");
        check(tiku_colors_select(&c, 9) == 9 &&
              tiku_colors_select(&c, 999) == 31,
              "choosing clamps to what the palette holds");
        {
            tiku_rect_t sw = tiku_colors_rect(&c, grid, 9);

            check(sw.x == 20 && sw.y == 20,
                  "swatch nine is the second of the second row");
            check(tiku_colors_hit(&c, grid, sw.x + 2, sw.y + 2) == 9,
                  "and a click inside it lands on it");
        }
        check(tiku_colors_hit(&c, grid, 500, 500) == -1,
              "a click outside the grid lands on nothing");
        /* Pinned to the SWATCH, not to another call of the same
         * function: comparing two nearest() answers passes just as
         * happily when both are wrong. */
        check(tiku_colors_select_nearest(&c, TIKU_RGB(255, 203, 0)) == 11,
              "an exact colour lands on its own swatch");
        check(tiku_colors_select_nearest(&c, TIKU_RGB(254, 202, 1)) == 11,
              "and one the palette has no exact match for still lands on "
              "the swatch a person would call that one");
        check(tiku_colors_select_nearest(&c, TIKU_RGB(2, 2, 2)) == 0,
              "a near-black lands on black, not on whatever comes first");
    }

    printf("\n%s\n", fails ? "FAILURES" : "the caret stays in the text");
    return fails ? 1 : 0;
}
