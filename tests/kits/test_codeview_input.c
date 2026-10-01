/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_input.c - the caret and selection of a code view, moved
 * by keys and by the pointer: characters, rows at a kept x, pages, line
 * and document ends, words, scrolling to the caret, presses, drags,
 * double and triple clicks, and two views of one document.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"

static int fails;

/** @brief Count a failure and say which contract row it belongs to. */
static void
check(int cond, const char *row, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-9s %s\n", row, what);
        fails++;
    }
}

/** @brief A view in a 400 by 200 box at 10, 20 of a new document
 *         holding @p s; the view holds the only reference. */
static tiku_codeview_t *
view_of(const char *s)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;
    tiku_rect_t box = { 10, 20, 400, 200 };

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK) {
        printf("  [FAIL] setup     view\n");
        exit(1);
    }
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
    return v;
}

/** @brief Whether the selection is @p anchor to @p caret. */
static int
is(tiku_codeview_t *v, tiku_edit_pos_t anchor, tiku_edit_pos_t caret)
{
    tiku_edit_pos_t a = -1, c = -1;

    return tiku_codeview_selection(v, &a, &c) == TIKU_EDIT_OK &&
           a == anchor && c == caret;
}

/** @brief The caret alone. */
static tiku_edit_pos_t
caret_of(tiku_codeview_t *v)
{
    tiku_edit_pos_t a = -1, c = -1;

    tiku_codeview_selection(v, &a, &c);
    return c;
}

/** @brief Press @p key with @p mods; what the view answered. */
static int
key(tiku_codeview_t *v, unsigned k, unsigned mods)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_KEY_DOWN;
    e.key = k;
    e.modifiers = mods;
    return tiku_codeview_event(v, &e);
}

/** @brief Press @p k @p n times. */
static void
keys(tiku_codeview_t *v, unsigned k, unsigned mods, int n)
{
    while (n-- > 0)
        (void)key(v, k, mods);
}

/** @brief A pointer event of @p type at @p x, @p y. */
static int
pointer(tiku_codeview_t *v, tiku_event_type_t type, int x, int y,
        unsigned button, unsigned mods, int64_t at)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.button = button;
    e.modifiers = mods;
    e.time_us = at;
    return tiku_codeview_event(v, &e);
}

/** @brief Where the caret at @p pos is drawn, nudged one pixel right so
 *         it is inside the character after it. */
static void
spot(tiku_codeview_t *v, tiku_edit_pos_t pos, int *x, int *y)
{
    int64_t px = 0, py = 0;

    tiku_codeview_point(v, pos, &px, &py);
    *x = (int)px + 1;
    *y = (int)py + 2;
}

/** @brief Left and Right step a character, a CRLF as one, and stop at
 *         the ends; without Shift a selection collapses to its edge. */
static void
characters(void)
{
    tiku_codeview_t *v = view_of("a\xc3\xa9" "b\r\ncd");
    int ok = 1, i;
    static const tiku_edit_pos_t right[] = { 1, 3, 4, 6, 7, 8, 8 };
    static const tiku_edit_pos_t left[] = { 7, 6, 4, 3, 1, 0, 0 };

    for (i = 0; i < 7; i++) {
        if (key(v, TIKU_KEY_RIGHT, 0) != 1 || !is(v, right[i], right[i]))
            ok = 0;
    }
    for (i = 0; i < 7; i++) {
        if (key(v, TIKU_KEY_LEFT, 0) != 1 || !is(v, left[i], left[i]))
            ok = 0;
    }
    check(ok, "NAV-001", "Left and Right step one character, a CRLF as "
          "one, and stop at the ends");
    tiku_codeview_select(v, 1, 6);
    key(v, TIKU_KEY_LEFT, 0);
    check(is(v, 1, 1), "NAV-001", "Left over a selection stops at its "
          "start");
    tiku_codeview_select(v, 6, 1);
    key(v, TIKU_KEY_RIGHT, 0);
    check(is(v, 6, 6), "NAV-001", "and Right at its end");
    tiku_codeview_select(v, 1, 6);
    key(v, TIKU_KEY_LEFT, TIKU_MOD_SHIFT);
    check(is(v, 1, 4), "NAV-001", "Shift keeps the anchor and moves the "
          "caret");
    tiku_codeview_select(v, 0, 0);
    keys(v, TIKU_KEY_RIGHT, TIKU_MOD_SHIFT, 2);
    check(is(v, 0, 3), "NAV-001", "and grows a selection from a caret");
    tiku_codeview_destroy(v);
}

/** @brief Up and Down keep the x the first of them started from. */
static void
rows(void)
{
    tiku_codeview_t *v = view_of("abcdefgh\nab\n\tabcdefgh\nabcdefghijk");
    int ok = 1, k, gi, aw, want;

    tiku_codeview_select(v, 6, 6);
    key(v, TIKU_KEY_DOWN, 0);
    ok = ok && is(v, 11, 11);
    key(v, TIKU_KEY_DOWN, 0);
    ok = ok && is(v, 15, 15);
    key(v, TIKU_KEY_DOWN, 0);
    ok = ok && is(v, 28, 28);
    key(v, TIKU_KEY_UP, 0);
    ok = ok && is(v, 15, 15);
    key(v, TIKU_KEY_UP, 0);
    ok = ok && is(v, 11, 11);
    key(v, TIKU_KEY_UP, 0);
    check(ok && is(v, 6, 6), "NAV-002",
          "Up and Down keep the x they started from, through a short "
          "line and past a tab");
    key(v, TIKU_KEY_DOWN, 0);
    key(v, TIKU_KEY_LEFT, 0);
    key(v, TIKU_KEY_DOWN, 0);
    check(is(v, 12, 12), "NAV-002", "another move starts a new x");
    tiku_codeview_select(v, 3, 3);
    key(v, TIKU_KEY_UP, 0);
    check(is(v, 3, 3), "NAV-002", "Up on the first line stays");
    tiku_codeview_select(v, 30, 30);
    key(v, TIKU_KEY_DOWN, 0);
    check(is(v, 30, 30), "NAV-002", "and Down on the last");
    tiku_codeview_select(v, 2, 2);
    key(v, TIKU_KEY_DOWN, TIKU_MOD_SHIFT);
    check(is(v, 2, 11), "NAV-002", "Shift extends");
    tiku_codeview_destroy(v);

    v = view_of("iiiiiiii\nWWWW");
    tiku_codeview_set_font(v, tiku_font_plain());
    gi = 8 * tiku_text_advance(tiku_font_plain(), 'i');
    aw = tiku_text_advance(tiku_font_plain(), 'W');
    want = 13;
    for (k = 0; k < 4; k++) {
        if (gi < k * aw + aw / 2) {
            want = 9 + k;
            break;
        }
    }
    tiku_codeview_select(v, 8, 8);
    key(v, TIKU_KEY_DOWN, 0);
    check(want < 13 && is(v, want, want), "NAV-002",
          "the x is pixels, not bytes, in a proportional face");
    tiku_codeview_destroy(v);
}

/** @brief A text of @p n lines "line N". */
static char *
lines_text(int n)
{
    char *s = malloc((size_t)n * 16 + 1);
    int i;

    if (s == NULL)
        exit(1);
    s[0] = '\0';
    for (i = 0; i < n; i++)
        sprintf(s + strlen(s), "line %d%s", i, i + 1 < n ? "\n" : "");
    return s;
}

/** @brief Page Up and Page Down move by the visible rows, and the view
 *         scrolls only as far as shows the caret. */
static void
pages(void)
{
    char *s = lines_text(100);
    tiku_codeview_t *v = view_of(s);
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    int64_t rows = tiku_codeview_rows_visible(v);

    tiku_codeview_select(v, 3, 3);
    key(v, TIKU_KEY_PAGE_DOWN, 0);
    check(caret_of(v) == tiku_editdoc_line_start(doc, rows) + 3 &&
          tiku_codeview_top(v) == 1, "NAV-003",
          "Page Down moves the caret the visible rows and scrolls as "
          "little as shows it");
    key(v, TIKU_KEY_PAGE_DOWN, 0);
    key(v, TIKU_KEY_PAGE_UP, 0);
    check(caret_of(v) == tiku_editdoc_line_start(doc, rows) + 3 &&
          tiku_codeview_top(v) == rows, "NAV-003",
          "Page Up moves it back, scrolling up only to it");
    keys(v, TIKU_KEY_PAGE_DOWN, 0, 20);
    check(tiku_editdoc_line_of(doc, caret_of(v)) == 99, "NAV-003",
          "and they stop at the last line");
    keys(v, TIKU_KEY_PAGE_UP, TIKU_MOD_SHIFT, 20);
    check(tiku_editdoc_line_of(doc, caret_of(v)) == 0 &&
          tiku_codeview_top(v) == 0, "NAV-003",
          "and the first, extending with Shift");
    tiku_codeview_destroy(v);
    free(s);
}

/** @brief Home and End go to the line's ends, with Command the
 *         document's. */
static void
ends(void)
{
    tiku_codeview_t *v = view_of("  abc\r\nxyz");

    tiku_codeview_select(v, 4, 4);
    key(v, TIKU_KEY_HOME, 0);
    check(is(v, 0, 0), "NAV-004", "Home goes to the line's start");
    key(v, TIKU_KEY_END, 0);
    check(is(v, 5, 5), "NAV-004", "End to its end, before the CR");
    key(v, TIKU_KEY_END, TIKU_MOD_CMD);
    check(is(v, 10, 10), "NAV-004", "Command End to the document's end");
    key(v, TIKU_KEY_HOME, TIKU_MOD_CMD);
    check(is(v, 0, 0), "NAV-004", "Command Home to its start");
    tiku_codeview_select(v, 2, 2);
    key(v, TIKU_KEY_END, TIKU_MOD_SHIFT);
    key(v, TIKU_KEY_END, TIKU_MOD_SHIFT | TIKU_MOD_CMD);
    check(is(v, 2, 10), "NAV-004", "Shift extends from the anchor");
    tiku_codeview_destroy(v);
}

/** @brief Word moves: right over one run and the blanks after it, left
 *         over blanks and one run; line breaks are a run of their own. */
static void
words(void)
{
    tiku_codeview_t *v = view_of("foo_bar  baz.qux(1)\n\n  \tnext word");
    static const tiku_edit_pos_t right[] = {
        9, 12, 13, 16, 17, 18, 19, 24, 29, 33, 33
    };
    static const tiku_edit_pos_t left[] = {
        29, 24, 19, 18, 17, 16, 13, 12, 9, 0, 0
    };
    int ok = 1, i;

    for (i = 0; i < 11; i++) {
        key(v, TIKU_KEY_RIGHT, i % 2 ? TIKU_MOD_CMD : TIKU_MOD_OPTION);
        if (!is(v, right[i], right[i]))
            ok = 0;
    }
    check(ok, "NAV-005", "Option or Command with Right goes past one run "
          "of word, punctuation or line breaks, then past blanks");
    ok = 1;
    for (i = 0; i < 11; i++) {
        key(v, TIKU_KEY_LEFT, i % 2 ? TIKU_MOD_OPTION : TIKU_MOD_CMD);
        if (!is(v, left[i], left[i]))
            ok = 0;
    }
    check(ok, "NAV-005", "and with Left back over blanks, then one run");
    tiku_codeview_select(v, 9, 9);
    key(v, TIKU_KEY_RIGHT, TIKU_MOD_OPTION | TIKU_MOD_SHIFT);
    check(is(v, 9, 12), "NAV-005", "Shift extends");
    tiku_codeview_destroy(v);
    v = view_of("ab\xce\xb3\xce\xb4 \xce\xb5");
    key(v, TIKU_KEY_RIGHT, TIKU_MOD_OPTION);
    check(is(v, 7, 7), "NAV-005", "letters past ASCII are word letters, "
          "in one word with ASCII ones");
    tiku_codeview_destroy(v);
}

/** @brief Select All, and the view's scrolling to the caret. */
static void
select_all_and_reveal(void)
{
    char *s = lines_text(100);
    char line[201];
    tiku_codeview_t *v = view_of(s);
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    int64_t c = tiku_font_mono_cell(0), width = 400 - 5;

    check(tiku_codeview_select_all(v) == TIKU_EDIT_OK &&
          is(v, 0, tiku_editdoc_length(doc)) &&
          tiku_codeview_top(v) == 100 - tiku_codeview_rows_visible(v),
          "NAV-006", "Select All selects everything and shows the caret "
          "at the end");
    tiku_codeview_destroy(v);
    free(s);

    memset(line, 'x', 200);
    line[200] = '\0';
    v = view_of(line);
    key(v, TIKU_KEY_END, 0);
    check(tiku_codeview_left(v) == 200 * c - width + 8, "NAV-007",
          "a caret past the right edge scrolls to 8 pixels inside it");
    key(v, TIKU_KEY_HOME, 0);
    check(tiku_codeview_left(v) == 0, "NAV-007",
          "one past the left edge scrolls to it");
    while (tiku_codeview_left(v) == 0 && caret_of(v) < 200)
        key(v, TIKU_KEY_RIGHT, 0);
    check(caret_of(v) * c >= width - 4 &&
          (caret_of(v) - 1) * c < width - 4 &&
          tiku_codeview_left(v) == caret_of(v) * c - width + 8, "NAV-007",
          "the caret may come within 4 pixels of the right edge first");
    tiku_codeview_scroll_to(v, 0, 0);
    tiku_codeview_reveal(v);
    check(tiku_codeview_left(v) == caret_of(v) * c - width + 8, "NAV-007",
          "reveal brings a scrolled-away caret back");
    tiku_codeview_destroy(v);
}

/** @brief A press puts the caret, a drag extends from it, Shift extends
 *         from the anchor, and only button 1 inside the view counts. */
static void
presses(void)
{
    char *s = lines_text(100);
    tiku_codeview_t *v = view_of(s);
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_rect_t b = tiku_codeview_bounds(v);
    int x, y, x2, y2;

    spot(v, 2, &x, &y);
    check(pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, 0) == 1 &&
          is(v, 2, 2), "NAV-008", "a press puts the caret");
    spot(v, tiku_editdoc_line_start(doc, 1) + 2, &x2, &y2);
    check(pointer(v, TIKU_EVENT_POINTER_MOVE, x2, y2, 0, 0, 0) == 1 &&
          is(v, 2, tiku_editdoc_line_start(doc, 1) + 2), "NAV-008",
          "a drag extends from the press");
    check(pointer(v, TIKU_EVENT_POINTER_UP, x2 + 3000, y2, 1, 0, 0) == 1 &&
          is(v, 2, tiku_editdoc_line_end(doc, 1)), "NAV-008",
          "and the release ends it where it is");
    check(pointer(v, TIKU_EVENT_POINTER_MOVE, x, y, 0, 0, 0) == 0 &&
          is(v, 2, tiku_editdoc_line_end(doc, 1)), "NAV-008",
          "a move with no drag is not taken");
    spot(v, 4, &x, &y);
    check(pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, TIKU_MOD_SHIFT,
                  1000000) == 1 && is(v, 2, 4), "NAV-008",
          "Shift and a press extend from the anchor");
    pointer(v, TIKU_EVENT_POINTER_UP, x, y, 1, 0, 0);
    check(pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 2, 0, 0) == 0 &&
          pointer(v, TIKU_EVENT_POINTER_DOWN, b.x - 1, y, 1, 0, 0) == 0 &&
          pointer(v, TIKU_EVENT_POINTER_DOWN, x, b.y + b.h, 1, 0, 0) == 0 &&
          is(v, 2, 4), "NAV-008",
          "another button, or a press outside the bounds, is not taken");

    tiku_codeview_scroll_to(v, 5, 0);
    spot(v, tiku_editdoc_line_start(doc, 6), &x, &y);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, 5000000);
    pointer(v, TIKU_EVENT_POINTER_MOVE, x, b.y - 300, 0, 0, 0);
    check(tiku_editdoc_line_of(doc, caret_of(v)) == 4 &&
          tiku_codeview_top(v) == 4, "NAV-008",
          "a drag above the view reaches one line above the first row");
    pointer(v, TIKU_EVENT_POINTER_MOVE, x, b.y + b.h + 300, 0, 0, 0);
    check(tiku_editdoc_line_of(doc, caret_of(v)) ==
          4 + tiku_codeview_rows_visible(v) &&
          tiku_codeview_top(v) == 5, "NAV-008",
          "and one below it one line past the last whole row");
    check(pointer(v, TIKU_EVENT_POINTER_UP, x, y, 3, 0, 0) == 0,
          "NAV-008", "another button's release does not end it");
    pointer(v, TIKU_EVENT_DEACTIVATED, 0, 0, 0, 0, 0);
    check(pointer(v, TIKU_EVENT_POINTER_MOVE, x, y, 0, 0, 0) == 0,
          "NAV-008", "deactivation does");
    tiku_codeview_destroy(v);
    free(s);
}

/** @brief Two clicks select a word, three a line, and a drag after
 *         them extends by words or lines. */
static void
clicks(void)
{
    tiku_codeview_t *v = view_of("alpha beta  (( x\ngamma delta\n\nlast");
    int x, y, x2, y2;
    int64_t t = 10000000;

    spot(v, 7, &x, &y);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t);
    pointer(v, TIKU_EVENT_POINTER_UP, x, y, 1, 0, t);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x + 5, y + 3, 1, 0, t + 399999);
    check(is(v, 6, 10), "NAV-009", "two clicks select the word");
    pointer(v, TIKU_EVENT_POINTER_UP, x, y, 1, 0, t);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 500000);
    check(is(v, 0, 17), "NAV-009",
          "three select the line with its line break");
    pointer(v, TIKU_EVENT_POINTER_UP, x, y, 1, 0, t);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 600000);
    check(is(v, 7, 7), "NAV-009", "and a fourth starts again");
    pointer(v, TIKU_EVENT_POINTER_UP, x, y, 1, 0, t);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 1000000);
    check(is(v, 7, 7), "NAV-009", "a click 400 ms after is a new one");
    pointer(v, TIKU_EVENT_POINTER_UP, x, y, 1, 0, t);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x + 6, y, 1, 0, t + 1000001);
    check(is(v, 8, 8), "NAV-009", "and so is one 6 pixels away");
    pointer(v, TIKU_EVENT_POINTER_UP, x, y, 1, 0, t);

    t += 5000000;
    spot(v, 11, &x, &y);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 1);
    check(is(v, 10, 12), "NAV-009", "two clicks on blanks select them");
    spot(v, 12, &x, &y);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 900000);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 900001);
    check(is(v, 12, 14), "NAV-009", "and on punctuation, its run");
    pointer(v, TIKU_EVENT_POINTER_DOWN, x + 200, y, 1, 0, t + 1900000);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x + 200, y, 1, 0, t + 1900001);
    check(is(v, 15, 16), "NAV-009", "past the end, the line's last run");
    spot(v, 29, &x, &y);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 2900000);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 2900001);
    check(is(v, 29, 29), "NAV-009", "on an empty line, its start");
    pointer(v, TIKU_EVENT_POINTER_UP, x, y, 1, 0, t);

    t += 5000000;
    spot(v, 7, &x, &y);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 1);
    spot(v, 19, &x2, &y2);
    pointer(v, TIKU_EVENT_POINTER_MOVE, x2, y2, 0, 0, 0);
    check(is(v, 6, 22), "NAV-009", "a drag after two clicks extends by "
          "words forward");
    spot(v, 2, &x2, &y2);
    pointer(v, TIKU_EVENT_POINTER_MOVE, x2, y2, 0, 0, 0);
    check(is(v, 10, 0), "NAV-009", "and back, keeping the first word");
    pointer(v, TIKU_EVENT_POINTER_UP, x2, y2, 1, 0, 0);

    t += 5000000;
    spot(v, 19, &x, &y);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 1);
    pointer(v, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, t + 2);
    spot(v, 31, &x2, &y2);
    pointer(v, TIKU_EVENT_POINTER_MOVE, x2, y2, 0, 0, 0);
    check(is(v, 17, 34), "NAV-009", "a drag after three extends by lines");
    spot(v, 1, &x2, &y2);
    pointer(v, TIKU_EVENT_POINTER_MOVE, x2, y2, 0, 0, 0);
    check(is(v, 29, 0), "NAV-009", "both ways");
    pointer(v, TIKU_EVENT_POINTER_UP, x2, y2, 1, 0, 0);
    tiku_codeview_destroy(v);
}

/** @brief Keys the view does not move by are left to the application. */
static void
ignored(void)
{
    tiku_codeview_t *v = view_of("abc\ndef");
    tiku_event_t up;

    tiku_codeview_select(v, 1, 2);
    memset(&up, 0, sizeof up);
    up.type = TIKU_EVENT_KEY_UP;
    up.key = TIKU_KEY_LEFT;
    check(key(v, 'a', 0) == 0 && key(v, 'a', TIKU_MOD_CMD) == 0 &&
          key(v, TIKU_KEY_UP, TIKU_MOD_OPTION) == 0 &&
          key(v, TIKU_KEY_DOWN, TIKU_MOD_CMD) == 0 &&
          key(v, TIKU_KEY_HOME, TIKU_MOD_OPTION) == 0 &&
          key(v, TIKU_KEY_RETURN, 0) == 0 &&
          tiku_codeview_event(v, &up) == 0 && is(v, 1, 2), "NAV-010",
          "a letter, a command, Option with Up or Home, Command with "
          "Down, Return and key releases are not taken");
    check(tiku_codeview_event(v, NULL) == 0 &&
          tiku_codeview_move(v, (tiku_codeview_move_t)99, 0) ==
          TIKU_EDIT_INVALID && is(v, 1, 2), "NAV-010",
          "and an unknown move changes nothing");
    tiku_codeview_destroy(v);
}

/** @brief Two views move independently; a change ends a drag and the
 *         kept x. */
static void
two_views(void)
{
    tiku_codeview_t *a = view_of("abcdefgh\nab\nabcdefgh");
    tiku_editdoc_t *doc = tiku_codeview_doc(a);
    tiku_codeview_t *b = NULL;
    tiku_edit_replace_t r = { { 0, 0 }, "x", 1 };
    tiku_edit_transaction_t t;
    int x, y;

    tiku_codeview_create(doc, &b);
    tiku_codeview_set_bounds(b, tiku_codeview_bounds(a));
    tiku_codeview_select(a, 6, 6);
    key(a, TIKU_KEY_DOWN, 0);
    check(is(a, 11, 11) && is(b, 0, 0), "NAV-011",
          "moving one view leaves the other's caret");
    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    tiku_editdoc_apply(doc, &t);
    key(a, TIKU_KEY_DOWN, 0);
    check(is(a, 15, 15), "NAV-011",
          "a change ends the x the rows were keeping");
    spot(a, 2, &x, &y);
    pointer(a, TIKU_EVENT_POINTER_DOWN, x, y, 1, 0, 0);
    tiku_editdoc_apply(doc, &t);
    check(pointer(a, TIKU_EVENT_POINTER_MOVE, x + 30, y, 0, 0, 0) == 0,
          "NAV-011", "and a drag in progress");
    tiku_codeview_destroy(b);
    tiku_codeview_destroy(a);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    characters();
    rows();
    pages();
    ends();
    words();
    select_all_and_reveal();
    presses();
    clicks();
    ignored();
    two_views();
    printf("%s\n", fails ? "FAIL: code view input" :
           "PASS: code view input -- characters, rows, pages, ends, words, "
           "reveal, presses, drags, clicks, two views");
    return fails != 0;
}
