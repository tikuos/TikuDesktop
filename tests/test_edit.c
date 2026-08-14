/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_edit.c - the in-place editor, and the Select window's persistence.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_edit.h"
#include "tiku_trk_select.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief Type a string one character at a time. */
static void
type(tiku_trk_edit_t *e, const char *s)
{
    while (*s != '\0') {
        (void)tiku_trk_edit_key(e, TIKU_TRK_EDIT_CHAR, 0, *s++);
    }
}

int
main(void)
{
    tiku_trk_edit_t e;
    tiku_trk_select_t w;
    int from, to, x, y;

    printf("in-place editing\n");

    tiku_trk_edit_start(&e, "readme.txt");
    check(e.active && e.len == 10, "MA-043", "the editor opens on the name");
    check(tiku_trk_edit_selection(&e, &from, &to) && from == 0 && to == 10,
          "PVS-014", "and opens with the whole name selected");

    type(&e, "n");
    check(strcmp(e.text, "n") == 0,
          "PVS-014", "so the first keystroke REPLACES rather than appends");

    tiku_trk_edit_start(&e, "readme.txt");
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_END, 0, 0);
    check(!tiku_trk_edit_selection(&e, NULL, NULL) && e.caret == 10,
          "MA-043", "End drops the selection and goes to the end");
    type(&e, "!");
    check(strcmp(e.text, "readme.txt!") == 0,
          "MA-043", "typing then appends");

    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_BACKSPACE, 0, 0);
    check(strcmp(e.text, "readme.txt") == 0,
          "MA-043", "backspace removes one character");

    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_HOME, 0, 0);
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_DELETE, 0, 0);
    check(strcmp(e.text, "eadme.txt") == 0,
          "MA-043", "delete removes forwards");

    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_RIGHT, 1, 0);
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_RIGHT, 1, 0);
    check(tiku_trk_edit_selection(&e, &from, &to) && from == 0 && to == 2,
          "MA-043", "shift+arrow extends a selection from the anchor");
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_LEFT, 1, 0);
    check(tiku_trk_edit_selection(&e, &from, &to) && from == 0 && to == 1,
          "MA-043", "and shrinks it back rather than starting a new one");

    check(tiku_trk_edit_key(&e, TIKU_TRK_EDIT_COMMIT, 0, 0) ==
          TIKU_TRK_EDIT_COMMITTED && !e.active,
          "MA-043", "Return commits and closes");
    tiku_trk_edit_start(&e, "x");
    check(tiku_trk_edit_key(&e, TIKU_TRK_EDIT_CANCEL, 0, 0) ==
          TIKU_TRK_EDIT_CANCELLED && !e.active,
          "MA-043", "Escape abandons and closes");
    check(tiku_trk_edit_key(&e, TIKU_TRK_EDIT_CHAR, 0, 'q') ==
          TIKU_TRK_EDIT_IGNORED,
          "MA-043", "a closed editor takes no more keys");

    printf("\ntext clipboard routing\n");
    tiku_trk_edit_start(&e, "alpha");
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_COPY, 0, 0);
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_END, 0, 0);
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_PASTE, 0, 0);
    check(strcmp(e.text, "alphaalpha") == 0, "PVK-039",
          "copy and paste stay inside the focused text editor");
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_HOME, 0, 0);
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_RIGHT, 1, 0);
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_RIGHT, 1, 0);
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_CUT, 0, 0);
    check(strcmp(e.text, "phaalpha") == 0, "PVK-039",
          "cut removes only the selected text and records it");

    printf("\ncharacters a name cannot contain (MA-046)\n");
    tiku_trk_edit_start(&e, "notes");
    tiku_trk_edit_deny(&e, "/");
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_END, 0, 0);
    type(&e, "a/b");
    check(strcmp(e.text, "notesab") == 0, "MA-046",
          "'/' is swallowed as it is typed, not left in the field to be "
          "refused at commit");
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_CHAR, 0, 0x01);
    check(strcmp(e.text, "notesab") == 0, "MA-046",
          "and so is a control character");
    tiku_trk_edit_start(&e, "x");
    (void)tiku_trk_edit_key(&e, TIKU_TRK_EDIT_END, 0, 0);
    type(&e, "/");
    check(strcmp(e.text, "x/") == 0, "MA-046",
          "a field with no deny set still takes everything printable");

    printf("\nthe Select window\n");
    tiku_trk_select_init(&w);
    check(w.rule == 3 &&
          tiku_trk_select_rule(w.rule) == TIKU_TRK_MATCH_GLOB,
          "AW-080", "wildcard is the default rule");
    check(w.ignore_case == 1 && w.invert == 0,
          "AW-080", "ignore case on, invert off");
    check(tiku_trk_select_rule(0) == TIKU_TRK_MATCH_STARTS &&
          tiku_trk_select_rule(1) == TIKU_TRK_MATCH_ENDS &&
          tiku_trk_select_rule(2) == TIKU_TRK_MATCH_CONTAINS &&
          tiku_trk_select_rule(4) == TIKU_TRK_MATCH_REGEX,
          "Q-047", "the five rules are offered in Tracker's order");

    tiku_trk_select_show(&w, 300, 300, 320, 120, 1000, 700);
    type(&w.expr, "gpio*");
    w.ignore_case = 0;
    tiku_trk_select_hide(&w);
    check(!w.open, "AW-081", "closing hides it");
    tiku_trk_select_show(&w, 300, 300, 320, 120, 1000, 700);
    check(strcmp(w.expr.text, "gpio*") == 0 && w.ignore_case == 0,
          "AW-105", "and the expression and options are still there");

    tiku_trk_select_place(0, 0, 320, 120, 1000, 700, &x, &y);
    check(x == 20 && y == 20,
          "AW-081", "a pointer at the corner still leaves it 20px on screen");
    tiku_trk_select_place(1000, 700, 320, 120, 1000, 700, &x, &y);
    check(x == 1000 - 20 - 320 && y == 700 - 20 - 120,
          "AW-081", "and clamps at the far edge too");
    tiku_trk_select_place(500, 350, 320, 120, 1000, 700, &x, &y);
    check(x == 500 - 160 && y == 350 - 60,
          "AW-081", "otherwise it centres on the pointer");

    printf("\n%s\n", fails ? "FAILURES" : "editing and Select hold");
    return fails ? 1 : 0;
}
