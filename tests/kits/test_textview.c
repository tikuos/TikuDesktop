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

    printf("\na selection is a span, and editing over it replaces it\n");
    tiku_textview_set(&tv, "alpha\nbeta\ngamma");
    tiku_textview_place(&tv, 0, 1);           /* after the 'a' of alpha */
    check(!tiku_textview_selection(&tv, NULL, NULL, NULL, NULL),
          "a caret with no anchor is no selection");
    tiku_textview_select_begin(&tv);
    tiku_textview_place(&tv, 2, 3);           /* into 'gam|ma' */
    {
        int y0, x0, y1, x1;

        check(tiku_textview_selection(&tv, &y0, &x0, &y1, &x1) &&
              y0 == 0 && x0 == 1 && y1 == 2 && x1 == 3,
              "the span runs anchor to caret, in document order");
    }
    /* Anchor after the caret answers the same span, normalised. */
    tiku_textview_set(&tv, "alpha\nbeta\ngamma");
    tiku_textview_place(&tv, 2, 3);
    tiku_textview_select_begin(&tv);
    tiku_textview_place(&tv, 0, 1);
    {
        int y0, x0, y1, x1;

        check(tiku_textview_selection(&tv, &y0, &x0, &y1, &x1) &&
              y0 == 0 && x0 == 1 && y1 == 2 && x1 == 3,
              "and the same span whichever end the anchor is at");
    }
    /* Typing over the multi-line span replaces the whole of it. */
    type(&tv, "X");
    check(tiku_textview_lines(&tv) == 1 &&
          strcmp(tiku_textview_line(&tv, 0), "aXma") == 0,
          "a character typed over a selection replaces every line of it");
    check(!tiku_textview_selection(&tv, NULL, NULL, NULL, NULL),
          "and the selection is gone once it has been replaced");
    /* Backspace over a selection is the deletion, taking nothing more. */
    tiku_textview_set(&tv, "keepXXXkeep");
    tiku_textview_place(&tv, 0, 4);
    tiku_textview_select_begin(&tv);
    tiku_textview_place(&tv, 0, 7);
    tiku_textview_backspace(&tv);
    check(strcmp(tiku_textview_line(&tv, 0), "keepkeep") == 0,
          "backspace removes the selection and not the character beyond it");
    /* Select-all spans the document; a plain move lets it go. */
    tiku_textview_set(&tv, "one\ntwo");
    tiku_textview_select_all(&tv);
    {
        int y0, x0, y1, x1;

        check(tiku_textview_selection(&tv, &y0, &x0, &y1, &x1) &&
              y0 == 0 && x0 == 0 && y1 == 1 && x1 == 3,
              "select-all runs from the top to the last line's end");
    }
    tiku_textview_select_none(&tv);
    check(!tiku_textview_selection(&tv, NULL, NULL, NULL, NULL),
          "and letting it go leaves no span");

    printf("\nundo folds a typed word and walks back to the save\n");
    tiku_textview_init(&tv);
    type(&tv, "hello");
    check(tiku_textview_can_undo(&tv), "a typed word is something to undo");
    tiku_textview_undo(&tv);
    check(strcmp(tiku_textview_line(&tv, 0), "") == 0,
          "and ONE undo takes the whole word -- the run was folded into "
          "one edit, not five");
    check(tiku_textview_can_redo(&tv), "the undone word is there to redo");
    tiku_textview_redo(&tv);
    check(strcmp(tiku_textview_line(&tv, 0), "hello") == 0,
          "redo puts it back exactly");
    /* A return breaks the run: the word before it undoes on its own. */
    tiku_textview_init(&tv);
    type(&tv, "one");
    tiku_textview_newline(&tv);
    type(&tv, "two");
    tiku_textview_undo(&tv);
    check(tiku_textview_lines(&tv) == 2 &&
          strcmp(tiku_textview_line(&tv, 1), "") == 0,
          "undo takes the second word but leaves the line break and the "
          "first word standing");
    /* Modified is the DISTANCE from the save, not a one-way flag. */
    tiku_textview_init(&tv);
    type(&tv, "ab");
    tiku_textview_saved(&tv);
    check(!tiku_textview_modified(&tv), "a save clears the modified mark");
    type(&tv, "c");
    check(tiku_textview_modified(&tv), "typing after a save marks it again");
    tiku_textview_undo(&tv);
    check(!tiku_textview_modified(&tv),
          "and undoing back to the save reads as unmodified once more");
    /* Paste is one entry: a single undo removes the whole of it. */
    tiku_textview_init(&tv);
    type(&tv, "XY");
    tiku_textview_place(&tv, 0, 1);
    tiku_textview_insert_text(&tv, "abc");
    check(strcmp(tiku_textview_line(&tv, 0), "XabcY") == 0,
          "a run is inserted whole");
    tiku_textview_undo(&tv);
    check(strcmp(tiku_textview_line(&tv, 0), "XY") == 0,
          "and undone whole, in one step, not a character at a time");

    printf("\na grouped batch of edits undoes as one\n");
    tiku_textview_set(&tv, "a\nb\nc");
    tiku_textview_group_begin(&tv);
    tiku_textview_select_all(&tv);
    tiku_textview_insert_text(&tv, "X\nY\nZ");
    tiku_textview_group_end(&tv);
    check(strcmp(tiku_textview_line(&tv, 2), "Z") == 0,
          "the batch replaced the whole document");
    tiku_textview_undo(&tv);
    check(tiku_textview_lines(&tv) == 3 &&
          strcmp(tiku_textview_line(&tv, 0), "a") == 0 &&
          strcmp(tiku_textview_line(&tv, 2), "c") == 0,
          "and ONE undo took the whole batch back, not a piece of it");
    tiku_textview_redo(&tv);
    check(strcmp(tiku_textview_line(&tv, 0), "X") == 0 &&
          strcmp(tiku_textview_line(&tv, 2), "Z") == 0,
          "and one redo put the whole batch back");

    /* A selection endpoint left past its line after the buffer shrank
     * under it: backspace once fed `len - cx` as a huge size_t and ran
     * a memcpy off a 1024-byte buffer (SIGABRT under FORTIFY).  The
     * caret is clamped now, so the edit is safe. */
    tiku_textview_set(&tv, "0123456789ABCDEF\nshort");
    tv.selecting = 1;
    tv.ay = 0; tv.ax = 16;      /* an anchor at the long line's end */
    tv.cy = 0; tv.cx = 16;
    tv.line[0].len = 3;         /* the line is shortened under the anchor */
    tiku_textview_backspace(&tv);
    check(tv.cx <= tiku_textview_line_len(&tv, tv.cy),
          "a stale selection past a shortened line does not run off it");

    /* An anchor set at the caret encloses nothing, as after a click that
     * does not drag; the edit that follows drops it, so the text it types
     * never becomes a selection for the next edit to replace. */
    tiku_textview_set(&tv, "10 PRINT");
    tiku_textview_place(&tv, 0, 3);
    tiku_textview_select_none(&tv);
    tiku_textview_select_begin(&tv);
    type(&tv, "ab");
    check(strcmp(tiku_textview_line(&tv, 0), "10 abPRINT") == 0,
          "typing after an anchor at the caret keeps every character");
    tiku_textview_place(&tv, 0, 0);
    tiku_textview_select_none(&tv);
    tiku_textview_select_begin(&tv);
    tiku_textview_newline(&tv);
    type(&tv, "x");
    check(tiku_textview_lines(&tv) == 2 &&
          strcmp(tiku_textview_line(&tv, 1), "x10 abPRINT") == 0,
          "and so does a line break typed after one");
    tiku_textview_set(&tv, "abc");
    tiku_textview_place(&tv, 0, 1);
    tiku_textview_select_begin(&tv);
    tiku_textview_move(&tv, TIKU_TEXTVIEW_RIGHT);
    tiku_textview_move(&tv, TIKU_TEXTVIEW_LEFT);
    type(&tv, "xy");
    check(strcmp(tiku_textview_line(&tv, 0), "axybc") == 0,
          "a selection shrunk back to nothing keeps the typing after it");

    /* At the line limits the journal holds exactly what happened, and
     * undo and redo put back exactly what was there. */
    {
        static char full[2 * TIKU_TEXTVIEW_LINE_MAX + 8];
        static char paste[TIKU_TEXTVIEW_LINE_MAX];

        memset(full, 'x', 1023);
        strcpy(full + 1023, "\ny");
        tiku_textview_set(&tv, full);
        tiku_textview_place(&tv, 0, 0);
        tiku_textview_insert_text(&tv, "a");
        tiku_textview_insert_text(&tv, "\x01\x7f");
        check(tiku_textview_line_len(&tv, 0) == 1023 &&
              !tiku_textview_modified(&tv) && !tiku_textview_can_undo(&tv),
              "a paste that lands nothing records nothing");
        tiku_textview_place(&tv, 1, 0);
        tiku_textview_backspace(&tv);
        check(tiku_textview_lines(&tv) == 2 && !tiku_textview_can_undo(&tv),
              "a Backspace join the limit refuses records nothing");
        tiku_textview_place(&tv, 0, 1023);
        tiku_textview_delete(&tv);
        check(tiku_textview_lines(&tv) == 2 && !tiku_textview_can_undo(&tv),
              "nor does a refused Delete join");

        memset(full, 'x', 1021);
        full[1021] = '\0';
        tiku_textview_set(&tv, full);
        tiku_textview_place(&tv, 0, 1021);
        tiku_textview_insert_text(&tv, "\xf0\x9f\x98\x80z");
        check(tiku_textview_line_len(&tv, 0) == 1022 &&
              tiku_textview_line(&tv, 0)[1021] == 'z',
              "a character that does not fit is skipped whole");
        tiku_textview_undo(&tv);
        check(tiku_textview_line_len(&tv, 0) == 1021 &&
              !tiku_textview_modified(&tv),
              "and undo takes back only what landed");

        memset(full, 'a', 1000);
        full[1000] = '\n';
        memset(full + 1001, 'b', 30);
        full[1031] = '\0';
        tiku_textview_set(&tv, full);
        tiku_textview_place(&tv, 0, 501);
        tiku_textview_select_none(&tv);
        tiku_textview_select_begin(&tv);
        tiku_textview_place(&tv, 1, 6);
        tiku_textview_insert(&tv, 'X');
        check(tiku_textview_lines(&tv) == 1 &&
              tiku_textview_line_len(&tv, 0) == 526 &&
              tiku_textview_line(&tv, 0)[501] == 'X',
              "a selection is removed whole when the joined line fits");
        tiku_textview_undo(&tv);
        tiku_textview_undo(&tv);
        check(tiku_textview_lines(&tv) == 2 &&
              tiku_textview_line_len(&tv, 0) == 1000 &&
              tiku_textview_line_len(&tv, 1) == 30 &&
              !tiku_textview_modified(&tv),
              "and undo puts both lines back");

        memset(full + 1001, 'b', 1000);
        full[2001] = '\0';
        tiku_textview_set(&tv, full);
        tiku_textview_place(&tv, 0, 900);
        tiku_textview_select_none(&tv);
        tiku_textview_select_begin(&tv);
        tiku_textview_place(&tv, 1, 100);
        tiku_textview_insert(&tv, 'X');
        check(tiku_textview_lines(&tv) == 2 &&
              tiku_textview_line_len(&tv, 0) == 1000 &&
              tiku_textview_line_len(&tv, 1) == 1000 &&
              !tiku_textview_can_undo(&tv),
              "a selection whose joined line would not fit is refused whole");

        memset(full, 'p', 800);
        full[800] = '\0';
        tiku_textview_set(&tv, full);
        tiku_textview_place(&tv, 0, 800);
        paste[0] = '\n';
        memset(paste + 1, 'c', 500);
        strcpy(paste + 501, "\n");
        tiku_textview_insert_text(&tv, paste);
        check(tiku_textview_lines(&tv) == 3 &&
              tiku_textview_line_len(&tv, 1) == 500,
              "a long line pasted after a long line");
        tiku_textview_undo(&tv);
        check(tiku_textview_lines(&tv) == 1 &&
              tiku_textview_line_len(&tv, 0) == 800 &&
              !tiku_textview_modified(&tv), "is undone whole");
        tiku_textview_redo(&tv);
        check(tiku_textview_lines(&tv) == 3 &&
              tiku_textview_line_len(&tv, 1) == 500, "and redone whole");
        tiku_textview_free(&tv);
    }

    printf("\n%s\n", fails ? "FAILURES" : "the caret stays in the text");
    return fails ? 1 : 0;
}