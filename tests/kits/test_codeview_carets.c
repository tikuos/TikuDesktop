/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_carets.c - a view with several selections: set and read,
 * typed at, deleted at and given Return and Tab as one transaction each,
 * copied, cut and pasted, moved by every key, made by Command presses and
 * Option drags, painted, folded over, and left alone by the commands that
 * act on the main selection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"
#include "tiku_ui.h"

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

#define W 360
#define H 220
#define PAPER TIKU_RGB(255, 0, 255)
#define X0 15                   /* text x with no gutter: 10 + 5    */
#define Y0 13                   /* the first row's top: 10 + 3      */

static const tiku_rect_t box = { 10, 10, 320, 200 };

/* The host's clipboard. */
static char clip[256];
static size_t clip_len;
static int clip_sets;

static int
clip_set(void *ctx, const char *text, size_t length)
{
    (void)ctx;
    clip_sets++;
    if (length >= sizeof clip)
        return 1;
    memcpy(clip, text, length);
    clip[length] = '\0';
    clip_len = length;
    return 0;
}

static const char *
clip_text(void *ctx, size_t *length)
{
    (void)ctx;
    *length = clip_len;
    return clip;
}

static const tiku_codeview_host_t host = { NULL, clip_set, clip_text, NULL };

/** @brief A view of a new document holding @p s; the view holds the only
 *         reference. */
static tiku_codeview_t *
view_in(const char *s)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK) {
        printf("  [FAIL] setup     view\n");
        exit(1);
    }
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
    tiku_codeview_set_host(v, &host);
    return v;
}

/** @brief A cell's width in the view's face. */
static int
cell(void)
{
    return tiku_font_mono_cell(0);
}

/** @brief Set @p n selections given as anchor, caret pairs. */
static tiku_edit_status_t
set(tiku_codeview_t *v, const int64_t *pairs, size_t n, size_t main)
{
    tiku_edit_selection_t s[16];
    size_t i;

    for (i = 0; i < n; i++) {
        s[i].anchor = pairs[2 * i];
        s[i].caret = pairs[2 * i + 1];
    }
    return tiku_codeview_select_many(v, s, n, main);
}

/** @brief Whether the selections read back as the @p n pairs, in order,
 *         the main one at @p main. */
static int
has(tiku_codeview_t *v, const int64_t *pairs, size_t n, size_t main)
{
    tiku_edit_selection_t s[16];
    size_t i, at = 99;

    if (tiku_codeview_selections(v, s, 16, &at) != n || at != main)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i].anchor != pairs[2 * i] || s[i].caret != pairs[2 * i + 1])
            return 0;
    return 1;
}

/** @brief Whether the document holds exactly @p s. */
static int
reads(tiku_codeview_t *v, const char *s)
{
    char buf[512];
    size_t n = 0;
    tiku_edit_range_t all = { 0, 0 };

    all.end = tiku_editdoc_length(tiku_codeview_doc(v));
    return tiku_editdoc_read(tiku_codeview_doc(v), all, buf, sizeof buf,
                             &n) == TIKU_EDIT_OK &&
           n == strlen(s) && memcmp(buf, s, n) == 0;
}

/** @brief Press @p key with @p mods; @return whether the view took it. */
static int
key(tiku_codeview_t *v, unsigned key, unsigned mods)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_KEY_DOWN;
    e.key = key;
    e.modifiers = mods;
    return tiku_codeview_event(v, &e);
}

/** @brief Type @p text, a key a character. */
static void
type(tiku_codeview_t *v, const char *text)
{
    tiku_event_t e;

    for (; *text; text++) {
        memset(&e, 0, sizeof e);
        e.type = TIKU_EVENT_KEY_DOWN;
        e.key = (unsigned char)*text;
        e.text[0] = *text;
        (void)tiku_codeview_event(v, &e);
    }
}

/** @brief A pointer event of @p type at @p x, @p y with @p mods. */
static int
point(tiku_codeview_t *v, tiku_event_type_t type, int x, int y,
      unsigned mods)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = type;
    e.button = 1;
    e.x = x;
    e.y = y;
    e.modifiers = mods;
    e.time_us = 1000000;
    return tiku_codeview_event(v, &e);
}

/** @brief The y of the middle of row @p row. */
static int
row_y(tiku_codeview_t *v, int row)
{
    return Y0 + row * tiku_codeview_row_height(v) +
           tiku_codeview_row_height(v) / 2;
}

/** @brief Selections set and read through the view; one added is the main
 *         one; one set alone keeps it alone. */
static void
setting(void)
{
    tiku_codeview_t *v = view_in("alpha\nbeta\ngamma");

    check(set(v, (const int64_t[]){ 10, 10, 0, 2 }, 2, 0) == TIKU_EDIT_OK &&
          has(v, (const int64_t[]){ 0, 2, 10, 10 }, 2, 1), "CAR-001",
          "selections set are read back in order");
    check(tiku_codeview_add_selection(v, 13, 13) == TIKU_EDIT_OK &&
          has(v, (const int64_t[]){ 0, 2, 10, 10, 13, 13 }, 3, 2), "CAR-001",
          "an added one is the main one");
    check(tiku_codeview_add_selection(v, 1, 1) == TIKU_EDIT_OK &&
          has(v, (const int64_t[]){ 0, 2, 10, 10, 13, 13 }, 3, 0), "CAR-001",
          "one added inside another joins it, which becomes main");
    check(tiku_codeview_select(v, 4, 4) == TIKU_EDIT_OK &&
          has(v, (const int64_t[]){ 4, 4 }, 1, 0), "CAR-001",
          "one selection set keeps it alone");
    tiku_codeview_destroy(v);
}

/** @brief Typing, Backspace, Delete, words, Return and Tab at every
 *         selection, one transaction each. */
static void
editing(void)
{
    tiku_codeview_t *v = view_in("one\ntwo\nthree");

    (void)set(v, (const int64_t[]){ 3, 3, 7, 7, 13, 13 }, 3, 2);
    type(v, "!");
    check(reads(v, "one!\ntwo!\nthree!") &&
          has(v, (const int64_t[]){ 4, 4, 9, 9, 16, 16 }, 3, 2), "CAR-002",
          "a key types at every caret, each caret after its text");
    check(tiku_codeview_undo(v) == TIKU_EDIT_OK && reads(v, "one\ntwo\nthree") &&
          has(v, (const int64_t[]){ 3, 3, 7, 7, 13, 13 }, 3, 2), "CAR-002",
          "one undo takes it all back, carets and all");
    (void)set(v, (const int64_t[]){ 0, 1, 4, 5 }, 2, 0);
    type(v, "XY");
    check(reads(v, "XYne\nXYwo\nthree"), "CAR-002",
          "typing replaces every selection");
    (void)key(v, TIKU_KEY_BACKSPACE, 0);
    check(reads(v, "Xne\nXwo\nthree") &&
          has(v, (const int64_t[]){ 1, 1, 5, 5 }, 2, 0), "CAR-002",
          "Backspace deletes before every caret");
    (void)key(v, TIKU_KEY_DELETE, 0);
    check(reads(v, "Xe\nXo\nthree"), "CAR-002",
          "Delete deletes after every caret");
    (void)set(v, (const int64_t[]){ 1, 1, 2, 2 }, 2, 1);
    (void)key(v, TIKU_KEY_BACKSPACE, 0);
    (void)key(v, TIKU_KEY_BACKSPACE, 0);
    check(reads(v, "\nXo\nthree") &&
          has(v, (const int64_t[]){ 0, 0 }, 1, 0), "CAR-002",
          "carets that meet become one");
    tiku_codeview_destroy(v);

    v = view_in("  ab cd\n  ef gh");
    (void)set(v, (const int64_t[]){ 7, 7, 15, 15 }, 2, 0);
    (void)key(v, TIKU_KEY_BACKSPACE, TIKU_MOD_CMD);
    check(reads(v, "  ab \n  ef ") &&
          has(v, (const int64_t[]){ 5, 5, 11, 11 }, 2, 0), "CAR-002",
          "Command with Backspace deletes the word before every caret");
    (void)key(v, TIKU_KEY_RETURN, 0);
    check(reads(v, "  ab \n  \n  ef \n  ") &&
          has(v, (const int64_t[]){ 8, 8, 17, 17 }, 2, 0), "CAR-002",
          "Return breaks every line, each with its own indentation");
    (void)key(v, TIKU_KEY_TAB, 0);
    check(reads(v, "  ab \n  \t\n  ef \n  \t"), "CAR-002",
          "Tab types a level at every caret");
    (void)set(v, (const int64_t[]){ 0, 9, 18, 18 }, 2, 0);
    (void)key(v, TIKU_KEY_TAB, 0);
    check(reads(v, "\t\n  ef \n  \t\t"), "CAR-002",
          "and over every selection, one spanning lines too");
    tiku_codeview_destroy(v);

    v = view_in("a {\nb {");
    tiku_codeview_set_language(v, TIKU_SYNTAX_C);
    (void)set(v, (const int64_t[]){ 3, 3, 7, 7 }, 2, 1);
    (void)key(v, TIKU_KEY_RETURN, 0);
    check(reads(v, "a {\n\t\nb {\n\t"), "CAR-002",
          "Return after an opener indents a level more at each");
    tiku_codeview_destroy(v);

    v = view_in("a\rX\nb\rY\nc");
    (void)set(v, (const int64_t[]){ 3, 3, 7, 7 }, 2, 0);
    (void)key(v, TIKU_KEY_BACKSPACE, 0);
    check(reads(v, "a\r\nb\r\nc") &&
          has(v, (const int64_t[]){ 1, 1, 4, 4 }, 2, 0), "CAR-002",
          "a caret a deletion leaves between a CR and its LF goes before "
          "the CR");
    tiku_codeview_destroy(v);
}

/** @brief Copy joins the selections' text by line breaks; Cut removes
 *         them; Paste gives a line to each when the counts match. */
static void
clipboard(void)
{
    tiku_codeview_t *v = view_in("ab cd\nef gh\nij");

    (void)set(v, (const int64_t[]){ 0, 2, 6, 8, 12, 12 }, 3, 0);
    check(tiku_codeview_copy(v) == TIKU_EDIT_OK && strcmp(clip, "ab\nef") == 0,
          "CAR-003", "copy joins the selections' text in order, carets "
          "left out");
    check(tiku_codeview_cut(v) == TIKU_EDIT_OK && reads(v, " cd\n gh\nij") &&
          has(v, (const int64_t[]){ 0, 0, 4, 4, 8, 8 }, 3, 0), "CAR-003",
          "cut removes them, the caret staying");
    (void)set(v, (const int64_t[]){ 0, 0, 4, 4 }, 2, 0);
    check(tiku_codeview_paste(v) == TIKU_EDIT_OK &&
          reads(v, "ab cd\nef gh\nij"), "CAR-003",
          "as many lines as selections paste a line into each");
    (void)set(v, (const int64_t[]){ 0, 0, 6, 6, 12, 12 }, 3, 2);
    check(tiku_codeview_paste(v) == TIKU_EDIT_OK &&
          reads(v, "ab\nefab cd\nab\nefef gh\nab\nefij"), "CAR-003",
          "other counts paste the whole text into each");
    (void)set(v, (const int64_t[]){ 0, 0, 3, 3 }, 2, 0);
    clip_sets = 0;
    check(tiku_codeview_copy(v) == TIKU_EDIT_NOTHING && clip_sets == 0,
          "CAR-003", "carets alone copy nothing");
    tiku_codeview_destroy(v);
}

/** @brief Every move key moves every selection; Escape keeps the main
 *         one. */
static void
moves(void)
{
    tiku_codeview_t *v = view_in("abcdefgh\nab\nabcdefgh\nabcdefgh\n"
                                 "abcdefgh");

    (void)set(v, (const int64_t[]){ 2, 2, 14, 14 }, 2, 0);
    (void)key(v, TIKU_KEY_RIGHT, 0);
    check(has(v, (const int64_t[]){ 3, 3, 15, 15 }, 2, 0), "CAR-004",
          "Right moves every caret");
    (void)key(v, TIKU_KEY_RIGHT, TIKU_MOD_SHIFT);
    check(has(v, (const int64_t[]){ 3, 4, 15, 16 }, 2, 0), "CAR-004",
          "with Shift every selection grows");
    (void)key(v, TIKU_KEY_LEFT, 0);
    check(has(v, (const int64_t[]){ 3, 3, 15, 15 }, 2, 0), "CAR-004",
          "Left goes to each selection's start");
    (void)key(v, TIKU_KEY_END, 0);
    check(has(v, (const int64_t[]){ 8, 8, 20, 20 }, 2, 0), "CAR-004",
          "End goes to each line's end");
    (void)set(v, (const int64_t[]){ 6, 6, 27, 27 }, 2, 0);
    (void)key(v, TIKU_KEY_DOWN, 0);
    check(has(v, (const int64_t[]){ 11, 11, 36, 36 }, 2, 0), "CAR-004",
          "Down moves every caret a row");
    (void)key(v, TIKU_KEY_DOWN, 0);
    check(has(v, (const int64_t[]){ 18, 18, 36, 36 }, 2, 0), "CAR-004",
          "the main one keeps its x past a short line");
    (void)set(v, (const int64_t[]){ 6, 6, 23, 23 }, 2, 0);
    (void)key(v, TIKU_KEY_DOWN, 0);
    (void)key(v, TIKU_KEY_DOWN, 0);
    check(has(v, (const int64_t[]){ 18, 18, 32, 32 }, 2, 0), "CAR-004",
          "another caret moves from its own x, not the main one's");
    (void)set(v, (const int64_t[]){ 2, 2, 14, 14 }, 2, 1);
    (void)key(v, TIKU_KEY_HOME, TIKU_MOD_CMD);
    check(has(v, (const int64_t[]){ 0, 0 }, 1, 0), "CAR-004",
          "carets moved to one place become one");
    (void)set(v, (const int64_t[]){ 2, 2, 14, 14 }, 2, 1);
    check(key(v, TIKU_KEY_ESCAPE, 0) == 1 &&
          has(v, (const int64_t[]){ 14, 14 }, 1, 0), "CAR-004",
          "Escape keeps the main one");
    check(key(v, TIKU_KEY_ESCAPE, 0) == 0, "CAR-004",
          "and with one it is left to the application");
    tiku_codeview_destroy(v);
}

/** @brief Command presses add and take away carets; Option drags make a
 *         rectangle by drawn x, tabs and short rows included. */
static void
gestures(void)
{
    tiku_codeview_t *v = view_in("abcdefgh\na\tbcdef\nab\nabcdefgh");
    int c = cell();

    tiku_codeview_set_focus(v, 1);
    (void)tiku_codeview_select(v, 1, 1);
    (void)point(v, TIKU_EVENT_POINTER_DOWN, X0 + 5 * c, row_y(v, 0),
                TIKU_MOD_CMD);
    (void)point(v, TIKU_EVENT_POINTER_UP, X0 + 5 * c, row_y(v, 0),
                TIKU_MOD_CMD);
    check(has(v, (const int64_t[]){ 1, 1, 5, 5 }, 2, 1), "CAR-005",
          "a Command press adds a caret, the main one");
    (void)point(v, TIKU_EVENT_POINTER_DOWN, X0 + 1 * c, row_y(v, 0),
                TIKU_MOD_CMD);
    (void)point(v, TIKU_EVENT_POINTER_UP, X0 + 1 * c, row_y(v, 0),
                TIKU_MOD_CMD);
    check(has(v, (const int64_t[]){ 5, 5 }, 1, 0), "CAR-005",
          "a Command press on a caret takes it away");
    (void)point(v, TIKU_EVENT_POINTER_DOWN, X0 + 1 * c, row_y(v, 3),
                TIKU_MOD_CMD);
    (void)point(v, TIKU_EVENT_POINTER_MOVE, X0 + 4 * c, row_y(v, 3),
                TIKU_MOD_CMD);
    (void)point(v, TIKU_EVENT_POINTER_UP, X0 + 4 * c, row_y(v, 3),
                TIKU_MOD_CMD);
    check(has(v, (const int64_t[]){ 5, 5, 21, 24 }, 2, 1), "CAR-005",
          "a drag from it selects without touching the others");
    (void)point(v, TIKU_EVENT_POINTER_DOWN, X0 + 3 * c, row_y(v, 0),
                TIKU_MOD_OPTION);
    (void)point(v, TIKU_EVENT_POINTER_MOVE, X0 + 6 * c, row_y(v, 3),
                TIKU_MOD_OPTION);
    (void)point(v, TIKU_EVENT_POINTER_UP, X0 + 6 * c, row_y(v, 3),
                TIKU_MOD_OPTION);
    check(has(v, (const int64_t[]){ 3, 6, 11, 13, 19, 19, 23, 26 }, 4, 3),
          "CAR-005", "an Option drag selects a rectangle by drawn x: a tab "
          "by its width, a short row to its end");
    (void)point(v, TIKU_EVENT_POINTER_DOWN, X0 + 6 * c, row_y(v, 3),
                TIKU_MOD_OPTION);
    (void)point(v, TIKU_EVENT_POINTER_MOVE, X0 + 2 * c, row_y(v, 2),
                TIKU_MOD_OPTION);
    (void)point(v, TIKU_EVENT_POINTER_UP, X0 + 2 * c, row_y(v, 2),
                TIKU_MOD_OPTION);
    check(has(v, (const int64_t[]){ 19, 19, 26, 22 }, 2, 0), "CAR-005",
          "dragged up and left, each faces the drag's way, the pointer's "
          "row main");
    tiku_codeview_destroy(v);

    v = view_in("a {\n  b\n}  xyz\nend");
    tiku_codeview_set_language(v, TIKU_SYNTAX_C);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_ANNOTATIONS);
    (void)tiku_editdoc_annotation_add(tiku_codeview_doc(v), 2, "note", 0);
    (void)tiku_codeview_fold(v, 0, 0);
    (void)point(v, TIKU_EVENT_POINTER_DOWN, X0 + 1 * c, row_y(v, 0),
                TIKU_MOD_OPTION);
    (void)point(v, TIKU_EVENT_POINTER_MOVE, X0 + 1 * c, row_y(v, 3),
                TIKU_MOD_OPTION);
    (void)point(v, TIKU_EVENT_POINTER_UP, X0 + 1 * c, row_y(v, 3),
                TIKU_MOD_OPTION);
    check(has(v, (const int64_t[]){ 1, 1, 9, 9, 16, 16 }, 3, 2), "CAR-005",
          "folded lines and annotation rows take no part of it");
    tiku_codeview_destroy(v);
}

/** @brief The colour bits of the pixel at @p x, @p y. */
static tiku_rgb_t
pixel(const tiku_surface_t *s, int x, int y)
{
    return s->px[y * s->w + x] & 0xffffffu;
}

/** @brief Every selection's ground and every caret are painted. */
static void
painting(void)
{
    tiku_codeview_t *v = view_in("abcdef\nabcdef\nabcdef");
    tiku_surface_t *s = tiku_surface_new(W, H, PAPER);
    tiku_rgb_t sel = TIKU_C_SELECT & 0xffffffu, doc = TIKU_C_DOC & 0xffffffu;
    tiku_rgb_t text = TIKU_C_TEXT & 0xffffffu;
    int c = cell(), step = tiku_codeview_row_height(v);

    if (s == NULL)
        exit(1);
    tiku_codeview_set_focus(v, 1);
    (void)set(v, (const int64_t[]){ 1, 3, 8, 10, 18, 18 }, 3, 0);
    tiku_codeview_paint(v, s);
    check(pixel(s, X0 + 1 * c + 1, Y0 + step - 1) == sel &&
          pixel(s, X0 + 1 * c + 1, Y0 + 2 * step - 1) == sel &&
          pixel(s, X0 + 4 * c, Y0 + step - 1) == doc &&
          pixel(s, X0 + 4 * c, Y0 + 2 * step - 1) == doc, "CAR-006",
          "every selection is filled, and nothing past it");
    check(pixel(s, X0 + 4 * c, Y0 + 2 * step + 2) == text &&
          pixel(s, X0 + 3 * c, Y0 + 2) == text, "CAR-006",
          "every caret is drawn");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief A caret at each line's end; folding moves those inside. */
static void
line_ends(void)
{
    tiku_codeview_t *v = view_in("one\ntwo\nthree\nfour");

    (void)tiku_codeview_select(v, 1, 14);
    check(tiku_codeview_carets_to_line_ends(v) == TIKU_EDIT_OK &&
          has(v, (const int64_t[]){ 3, 3, 7, 7, 13, 13 }, 3, 2), "CAR-007",
          "a caret at the end of each line touched, the last main, a "
          "selection ending at a line's start leaving it out");
    (void)tiku_codeview_select(v, 9, 10);
    check(tiku_codeview_carets_to_line_ends(v) == TIKU_EDIT_OK &&
          has(v, (const int64_t[]){ 13, 13 }, 1, 0), "CAR-007",
          "within one line, its end");
    tiku_codeview_destroy(v);

    v = view_in("a {\n  b\n  c\n}");
    tiku_codeview_set_language(v, TIKU_SYNTAX_C);
    (void)set(v, (const int64_t[]){ 6, 6, 10, 10, 12, 12 }, 3, 2);
    (void)tiku_codeview_fold(v, 0, 0);
    check(has(v, (const int64_t[]){ 3, 3, 12, 12 }, 2, 1), "FLD-005",
          "folding puts every caret inside at the head's end");
    tiku_codeview_destroy(v);
}

/** @brief The commands of one selection act on the main one alone, and
 *         undo gives the others back. */
static void
others(void)
{
    tiku_codeview_t *v = view_in("one\ntwo");

    tiku_codeview_set_language(v, TIKU_SYNTAX_C);
    (void)set(v, (const int64_t[]){ 1, 1, 5, 5 }, 2, 1);
    (void)key(v, '/', TIKU_MOD_CMD);
    check(reads(v, "one\n// two") && has(v, (const int64_t[]){ 8, 8 }, 1, 0),
          "CAR-008", "a line command acts on the main selection alone");
    check(tiku_codeview_undo(v) == TIKU_EDIT_OK &&
          has(v, (const int64_t[]){ 1, 1, 5, 5 }, 2, 1), "CAR-008",
          "and undo gives the others back");
    tiku_codeview_destroy(v);
}

int
main(void)
{
    setting();
    editing();
    clipboard();
    moves();
    gestures();
    painting();
    line_ends();
    others();
    if (fails) {
        printf("FAIL: %d checks\n", fails);
        return 1;
    }
    printf("PASS: code view carets -- set and read, typing, deleting, "
           "Return and Tab at each, the clipboard, moves, Escape, Command "
           "presses, Option rectangles, painting, line ends, folding, "
           "commands of one\n");
    return 0;
}
