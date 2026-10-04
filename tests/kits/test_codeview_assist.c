/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_assist.c - what a code view draws for the application
 * over and behind its text: the rows of marked lines and of the
 * execution line filled, a list of words that inserts the one chosen,
 * and a tip beside it, with the keys and presses each takes and when
 * each goes away.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"
#include "tiku_dl.h"
#include "tiku_editdoc_private.h"
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
#define H 200
#define PAPER TIKU_RGB(255, 0, 255)
#define RED TIKU_RGB(255, 222, 222)
#define BLUE TIKU_RGB(200, 210, 255)
#define YELLOW TIKU_RGB(250, 240, 160)

static const tiku_rect_t box = { 10, 10, 320, 160 };

/** @brief A surface of W by H, all PAPER. */
static tiku_surface_t *
surface(void)
{
    tiku_surface_t *s = tiku_surface_new(W, H, PAPER);

    if (s == NULL)
        exit(1);
    return s;
}

/** @brief A view of a new document holding @p s, in the box; the view
 *         holds the only reference. */
static tiku_codeview_t *
view_of(const char *s)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
        exit(1);
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
    return v;
}

/** @brief Whether the view's document holds @p s. */
static int
holds(tiku_codeview_t *v, const char *s)
{
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    char got[256];
    size_t n = 0;
    tiku_edit_range_t all = { 0, tiku_editdoc_length(doc) };

    return tiku_editdoc_read(doc, all, got, sizeof got, &n) ==
               TIKU_EDIT_OK && strcmp(got, s) == 0;
}

/** @brief Whether the caret is an empty selection at @p pos. */
static int
caret_at(tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    tiku_edit_pos_t a = -1, c = -1;

    return tiku_codeview_selection(v, &a, &c) == TIKU_EDIT_OK &&
           a == pos && c == pos;
}

/** @brief Press key @p k with @p mods, bringing @p text. */
static int
key(tiku_codeview_t *v, unsigned k, unsigned mods, const char *text)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_KEY_DOWN;
    e.key = k;
    e.modifiers = mods;
    if (text != NULL)
        memcpy(e.text, text, strnlen(text, sizeof e.text - 1));
    return tiku_codeview_event(v, &e);
}

/** @brief Press button 1 at @p x, @p y and let it go. */
static int
click(tiku_codeview_t *v, int x, int y)
{
    tiku_event_t e;
    int used;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_POINTER_DOWN;
    e.button = 1;
    e.x = x;
    e.y = y;
    used = tiku_codeview_event(v, &e);
    e.type = TIKU_EVENT_POINTER_UP;
    (void)tiku_codeview_event(v, &e);
    return used;
}

/** @brief The pixel at @p x, @p y, its colour bits only. */
static tiku_rgb_t
at(const tiku_surface_t *s, int x, int y)
{
    return s->px[y * s->w + x] & 0xffffffu;
}

/** @brief The colour bits of @p c. */
static tiku_rgb_t
rgb(tiku_rgb_t c)
{
    return c & 0xffffffu;
}

/** @brief The top of @p line's row on the surface. */
static int
row_top(tiku_codeview_t *v, int64_t line)
{
    int64_t x, y;

    tiku_codeview_point(v, tiku_editdoc_line_start(tiku_codeview_doc(v),
                                                   line), &x, &y);
    return (int)y;
}

/** @brief The colour at the right of @p line's row, past any text. */
static tiku_rgb_t
row_colour(tiku_surface_t *s, tiku_codeview_t *v, int64_t line)
{
    return at(s, box.x + box.w - 3,
              row_top(v, line) + tiku_codeview_row_height(v) / 2);
}

/** @brief Painted anew onto a fresh surface. */
static tiku_surface_t *
painted(tiku_codeview_t *v)
{
    tiku_surface_t *s = surface();

    tiku_codeview_paint(v, s);
    return s;
}

/** @brief Marked rows filled by kind, the execution line over them while
 *         it is current. */
static void
fills(void)
{
    tiku_codeview_t *v = view_of("zero\none\ntwo\nthree\nfour\n");
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_edit_replace_t r = { { 0, 0 }, "x", 1 };
    tiku_edit_transaction_t t;
    tiku_surface_t *s;

    tiku_editdoc_marker_add(doc, 1, 2, NULL);
    tiku_editdoc_marker_add(doc, 2, 5, NULL);
    tiku_editdoc_marker_add(doc, 3, 1, NULL);
    tiku_editdoc_marker_add(doc, 3, 2, NULL);
    check(tiku_codeview_set_marker_fill(v, 2, RED) == TIKU_EDIT_OK &&
          tiku_codeview_set_marker_fill(v, 1, BLUE) == TIKU_EDIT_OK,
          "MKP-001", "fills are set for kinds");
    check(tiku_codeview_set_marker_fill(v, TIKU_EDIT_MARKER_KINDS, RED) ==
              TIKU_EDIT_INVALID, "MKP-001", "a kind past the last is refused");
    s = painted(v);
    check(row_colour(s, v, 1) == RED && row_colour(s, v, 0) == rgb(TIKU_C_DOC),
          "MKP-001", "a marked line's row is filled in its kind's colour");
    check(at(s, box.x, row_top(v, 1) + 2) == RED &&
          at(s, box.x - 1, row_top(v, 1) + 2) == PAPER, "MKP-001",
          "across the text's area, from the bounds' left edge");
    check(row_colour(s, v, 2) == rgb(TIKU_C_DOC), "MKP-001",
          "a kind with no fill is not filled");
    check(row_colour(s, v, 3) == RED, "MKP-001",
          "two kinds on one line show the higher kind's fill");
    tiku_surface_free(s);

    tiku_codeview_select(v, tiku_editdoc_line_start(doc, 1),
                         tiku_editdoc_line_end(doc, 1));
    s = painted(v);
    check(at(s, box.x + 6, row_top(v, 1) + 1) == rgb(TIKU_C_SELECT) &&
          row_colour(s, v, 1) == RED, "MKP-001",
          "the selection is drawn over the fill, which shows past it");
    tiku_surface_free(s);
    tiku_codeview_select(v, 0, 0);

    tiku_editdoc_execution_set(doc, 3);
    s = painted(v);
    check(row_colour(s, v, 3) == TIKU_RGB(211, 238, 185), "MKP-002",
          "the current execution line is filled light green over the "
          "markers' fills");
    tiku_surface_free(s);
    tiku_codeview_set_execution_fill(v, YELLOW);
    s = painted(v);
    check(row_colour(s, v, 3) == YELLOW, "MKP-002",
          "in the colour the view is given");
    tiku_surface_free(s);
    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    tiku_editdoc_apply(doc, &t);
    s = painted(v);
    check(row_colour(s, v, 3) == RED, "MKP-002",
          "a stale execution line is not filled");
    tiku_surface_free(s);
    tiku_editdoc_undo(doc);
    s = painted(v);
    check(row_colour(s, v, 3) == YELLOW, "MKP-002",
          "and undo back to its text fills it again");
    tiku_surface_free(s);
    tiku_codeview_clear_marker_fill(v, 2);
    tiku_editdoc_execution_clear(doc);
    s = painted(v);
    check(row_colour(s, v, 1) == rgb(TIKU_C_DOC) && row_colour(s, v, 3) == BLUE,
          "MKP-001", "a cleared kind is no longer filled; a lower one shows");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief A list over "pri" in "10 pri", offering three words. */
static tiku_codeview_list_t
offer(int inserts)
{
    static const char *const words[] = { "PRINT", "PRIVATE", "PROC" };
    tiku_codeview_list_t list;

    memset(&list, 0, sizeof list);
    list.range.start = 3;
    list.range.end = 6;
    list.items = words;
    list.count = 3;
    list.inserts = inserts;
    return list;
}

/** @brief The chosen word's index, or -1 with no list. */
static int
chosen(tiku_codeview_t *v)
{
    size_t current = 0;

    return tiku_codeview_list_shown(v, &current) ? (int)current : -1;
}

/** @brief Keys a list takes: Up and Down round it, Return and Tab to put
 *         the chosen word in, Escape to hide it. */
static void
list_keys(void)
{
    tiku_codeview_t *v = view_of("10 pri\n20 x\n");
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_codeview_list_t list = offer(1);
    tiku_codeview_list_t bad = list;

    tiku_codeview_select(v, 6, 6);
    check(tiku_codeview_list_show(v, &list) == TIKU_EDIT_OK &&
          chosen(v) == 0, "LST-001", "a list is shown, its first word chosen");
    check(key(v, TIKU_KEY_DOWN, 0, NULL) == 1 && chosen(v) == 1 &&
          key(v, TIKU_KEY_DOWN, 0, NULL) == 1 && chosen(v) == 2 &&
          key(v, TIKU_KEY_DOWN, 0, NULL) == 1 && chosen(v) == 0, "LST-002",
          "Down moves the choice, from the last back to the first");
    check(key(v, TIKU_KEY_UP, 0, NULL) == 1 && chosen(v) == 2 &&
          caret_at(v, 6), "LST-002",
          "Up from the first goes to the last, and the caret stays");
    check(key(v, TIKU_KEY_DOWN, TIKU_MOD_CMD, NULL) == 0 && chosen(v) == 2,
          "LST-002", "a Command chord is not the list's");
    check(key(v, TIKU_KEY_RETURN, 0, NULL) == 1 &&
          holds(v, "10 PROC\n20 x\n") && caret_at(v, 7) && chosen(v) == -1,
          "LST-002", "Return puts the chosen word over the range, the caret "
          "after it, and hides the list");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          holds(v, "10 pri\n20 x\n"), "LST-002", "in one undo step");
    tiku_codeview_select(v, 6, 6);
    tiku_codeview_list_show(v, &list);
    check(key(v, TIKU_KEY_TAB, 0, "\t") == 1 &&
          holds(v, "10 PRINT\n20 x\n"), "LST-002", "Tab does the same");
    tiku_editdoc_undo(doc);

    tiku_codeview_select(v, 6, 6);
    tiku_codeview_list_show(v, &list);
    key(v, 'n', 0, "n");
    check(holds(v, "10 prin\n20 x\n") && chosen(v) == 0, "LST-001",
          "other keys reach the text and the list stays");
    check(tiku_codeview_list_insert(v) == TIKU_EDIT_OK &&
          holds(v, "10 PRINT\n20 x\n"), "LST-001",
          "the range follows the edit: typing at its end grows it");
    check(tiku_codeview_list_insert(v) == TIKU_EDIT_NOTHING, "LST-002",
          "with no list, nothing is put in");
    tiku_editdoc_undo(doc);
    tiku_editdoc_undo(doc);

    list.count = 0;
    tiku_codeview_select(v, 6, 6);
    check(tiku_codeview_list_show(v, &list) == TIKU_EDIT_OK &&
          key(v, TIKU_KEY_RETURN, 0, NULL) == 1 &&
          key(v, TIKU_KEY_DOWN, 0, NULL) == 1 &&
          key(v, TIKU_KEY_TAB, 0, "\t") == 1 &&
          holds(v, "10 pri\n20 x\n") && chosen(v) == 0 &&
          tiku_codeview_list_insert(v) == TIKU_EDIT_NOTHING, "LST-002",
          "a list with no words still takes Return, Down and Tab, and "
          "changes nothing");
    list = offer(0);
    check(tiku_codeview_list_show(v, &list) == TIKU_EDIT_OK &&
          key(v, TIKU_KEY_DOWN, 0, NULL) == 1 && chosen(v) == 0 &&
          caret_at(v, 11), "LST-003",
          "a list that does not insert leaves Down to the caret");
    check(key(v, TIKU_KEY_RETURN, 0, NULL) == 1 &&
          holds(v, "10 pri\n20 x\n\n") && chosen(v) == 0, "LST-003",
          "and Return to the text");
    tiku_editdoc_undo(doc);
    check(key(v, TIKU_KEY_ESCAPE, 0, NULL) == 1 && chosen(v) == -1 &&
          key(v, TIKU_KEY_ESCAPE, 0, NULL) == 0, "LST-004",
          "Escape hides the list, and with nothing shown is not taken");

    list = offer(1);
    tiku_codeview_select(v, 6, 6);
    tiku_codeview_list_show(v, &list);
    tiku_editdoc_set_read_only(doc, 1);
    check(key(v, TIKU_KEY_RETURN, 0, NULL) == 1 &&
          tiku_codeview_status(v) == TIKU_EDIT_READ_ONLY && chosen(v) == 0 &&
          holds(v, "10 pri\n20 x\n"), "LST-002",
          "a refused insertion keeps the list and says why");
    tiku_editdoc_set_read_only(doc, 0);

    bad.range.start = 5;
    bad.range.end = 3;
    check(tiku_codeview_list_show(v, &bad) == TIKU_EDIT_INVALID_RANGE,
          "LST-004", "a reversed range is refused");
    bad.range.start = 0;
    bad.range.end = 100;
    check(tiku_codeview_list_show(v, &bad) == TIKU_EDIT_INVALID_RANGE &&
          chosen(v) == 0, "LST-004",
          "so is one past the text, and the list shown stays");
    bad.range.end = 0;
    bad.items = NULL;
    check(tiku_codeview_list_show(v, &bad) == TIKU_EDIT_INVALID, "LST-004",
          "and words that are not there");
    check(tiku_editdoc_load(doc, "new", 3) == TIKU_EDIT_OK &&
          chosen(v) == -1, "LST-004", "a load hides the list");
    tiku_codeview_destroy(v);
}

/** @brief Where the list's box is drawn: under the range start's row,
 *         its edge five pixels left of the text there, a frame round
 *         rows of the interface face's height and four. */
static tiku_rect_t
list_rect(tiku_codeview_t *v, int64_t pos, int rows, int widest)
{
    tiku_rect_t r;
    int64_t x, y;

    tiku_codeview_point(v, pos, &x, &y);
    r.x = (int)x - 5;
    r.y = (int)y + tiku_codeview_row_height(v);
    r.w = widest + 10 < 60 ? 60 : widest + 10;
    r.h = rows * (tiku_font_plain()->height + 4) + 2;
    return r;
}

/** @brief The list drawn and pressed. */
static void
list_presses(void)
{
    static const char *const many[] = {
        "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN"
    };
    const tiku_font_t *f = tiku_font_plain();
    int rh = f->height + 4;
    tiku_codeview_t *v = view_of("10 pri\n20 x\n30 y\n40 z\n50 w\n60 v\n"
                                 "70 u\n80 t\n");
    tiku_codeview_list_t list = offer(1);
    tiku_rect_t r;
    tiku_surface_t *s;
    int widest = tiku_text_width(f, "PRIVATE");

    tiku_codeview_select(v, 6, 6);
    tiku_codeview_list_show(v, &list);
    tiku_codeview_list_hide(v);
    check(chosen(v) == -1, "LST-004", "a list can be hidden");
    tiku_codeview_list_show(v, &list);
    key(v, TIKU_KEY_DOWN, 0, NULL);
    r = list_rect(v, 3, 3, widest);
    s = painted(v);
    check(at(s, r.x, r.y) == rgb(TIKU_C_TEXT) &&
          at(s, r.x + 2, r.y + r.h - 1) == rgb(TIKU_C_TEXT) &&
          at(s, r.x + 2, r.y + r.h) != rgb(TIKU_C_TEXT) &&
          at(s, r.x + r.w - 1, r.y + r.h - 1) == rgb(TIKU_C_TEXT) &&
          at(s, r.x + r.w, r.y + 2) != rgb(TIKU_C_TEXT), "LST-001",
          "the list is framed under the row, sized to its widest word "
          "and its rows");
    check(at(s, r.x + r.w - 3, r.y + 1 + rh + rh / 2) == rgb(TIKU_C_SELECT) &&
          at(s, r.x + r.w - 3, r.y + 1 + rh / 2) == rgb(TIKU_C_DOC),
          "LST-001", "the chosen word's row has the selection's ground");
    tiku_surface_free(s);

    check(click(v, r.x + 4, r.y) == 1 && chosen(v) == 1 &&
          holds(v, "10 pri\n20 x\n30 y\n40 z\n50 w\n60 v\n70 u\n80 t\n"),
          "LST-003", "a press on the frame is taken and chooses nothing");
    check(click(v, r.x + 4, r.y + 1 + 2 * rh + 2) == 1 && chosen(v) == -1 &&
          holds(v, "10 PROC\n20 x\n30 y\n40 z\n50 w\n60 v\n70 u\n80 t\n"),
          "LST-003", "a press on a word in an inserting list puts it in");
    tiku_editdoc_undo(tiku_codeview_doc(v));
    list = offer(0);
    tiku_codeview_select(v, 6, 6);
    tiku_codeview_list_show(v, &list);
    check(click(v, r.x + 4, r.y + 1 + 2 * rh + 2) == 1 && chosen(v) == 2 &&
          holds(v, "10 pri\n20 x\n30 y\n40 z\n50 w\n60 v\n70 u\n80 t\n"),
          "LST-003", "in one that does not insert it only chooses");
    check(click(v, r.x + 4, r.y + r.h + 4) == 1 && chosen(v) == 2,
          "LST-003", "a press outside the list is the text's and keeps it");

    list.items = many;
    list.count = 7;
    list.current = 6;
    list.inserts = 1;
    tiku_codeview_list_show(v, &list);
    r = list_rect(v, 3, 5, tiku_text_width(f, "SEVEN"));
    s = painted(v);
    check(at(s, r.x + r.w - 3, r.y + 1 + rh + rh / 2) == rgb(TIKU_C_SELECT) &&
          at(s, r.x + r.w - 3, r.y + 1 + 2 * rh + rh / 2) == rgb(TIKU_C_DOC),
          "LST-001", "five words at a time: the seventh is the second row "
          "of the second five");
    tiku_surface_free(s);
    check(click(v, r.x + 4, r.y + 1 + 3 * rh + 2) == 1 && chosen(v) == 6,
          "LST-003", "a press below the last word chooses nothing");
    check(click(v, r.x + 4, r.y + 1 + 2) == 1 &&
          holds(v, "10 SIX\n20 x\n30 y\n40 z\n50 w\n60 v\n70 u\n80 t\n"),
          "LST-003", "and one on a word of the page shown puts it in");
    tiku_editdoc_undo(tiku_codeview_doc(v));

    list = offer(1);
    list.range.start = list.range.end =
        tiku_editdoc_line_start(tiku_codeview_doc(v), 7) + 3;
    tiku_codeview_select(v, list.range.start, list.range.start);
    tiku_codeview_list_show(v, &list);
    r = list_rect(v, list.range.start, 3, widest);
    s = painted(v);
    check(at(s, r.x, row_top(v, 7) - 1) == rgb(TIKU_C_TEXT) &&
          at(s, r.x, row_top(v, 7) - r.h) == rgb(TIKU_C_TEXT), "LST-001",
          "with no room below the row, the list is drawn over it");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief The tip: where it is drawn and when it goes. */
static void
tips(void)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_codeview_t *v = view_of("10 pri\n20 x\n");
    tiku_codeview_list_t list = offer(1);
    tiku_surface_t *s;
    tiku_rect_t r;
    int64_t x, y;
    int widest = tiku_text_width(f, "PRIVATE"), bottom, wide_rows;
    char wide[400];

    tiku_codeview_select(v, 6, 6);
    check(tiku_codeview_tip_show(v, 3, "PRINT expression\nPrints it.") ==
              TIKU_EDIT_OK && tiku_codeview_tip_shown(v), "TIP-001",
          "a tip is shown");
    tiku_codeview_point(v, 3, &x, &y);
    s = painted(v);
    check(at(s, (int)x - 6, (int)y + tiku_codeview_row_height(v)) ==
              rgb(TIKU_C_TEXT) &&
          at(s, (int)x - 4, (int)y + tiku_codeview_row_height(v) + 2) ==
              rgb(TIKU_C_NOTE), "TIP-001",
          "framed in the note colour under its position's row");
    for (bottom = (int)y + tiku_codeview_row_height(v) + 2;
         at(s, (int)x - 4, bottom) == rgb(TIKU_C_NOTE); bottom++)
        ;
    check(bottom - ((int)y + tiku_codeview_row_height(v)) ==
              2 * (f->height + 2) + 3, "TIP-001",
          "two lines tall: its text breaks at the LF");
    tiku_surface_free(s);

    tiku_codeview_list_show(v, &list);
    r = list_rect(v, 3, 3, widest);
    s = painted(v);
    check(at(s, r.x + r.w + 2, r.y) == rgb(TIKU_C_TEXT) &&
          at(s, r.x + r.w + 4, r.y + 2) == rgb(TIKU_C_NOTE), "TIP-001",
          "with a list shown, it stands beside the list");
    tiku_surface_free(s);
    check(key(v, TIKU_KEY_DOWN, 0, NULL) == 1 && tiku_codeview_tip_shown(v),
          "TIP-002", "moving the list's choice keeps it");
    check(click(v, r.x + r.w + 6, r.y + 4) == 1 && caret_at(v, 6),
          "TIP-002", "a press on it is taken and moves nothing");
    check(key(v, TIKU_KEY_RETURN, 0, NULL) == 1 &&
          holds(v, "10 PRIVATE\n20 x\n") && tiku_codeview_tip_shown(v),
          "TIP-002", "putting a word in keeps the tip");
    check(key(v, TIKU_KEY_ESCAPE, 0, NULL) == 1 &&
          !tiku_codeview_tip_shown(v), "TIP-002", "Escape hides it");
    tiku_editdoc_undo(tiku_codeview_doc(v));

    tiku_codeview_select(v, 6, 6);
    tiku_codeview_tip_show(v, 3, "tip");
    key(v, TIKU_KEY_LEFT, 0, NULL);
    key(v, TIKU_KEY_LEFT, 0, NULL);
    key(v, TIKU_KEY_LEFT, 0, NULL);
    check(caret_at(v, 3) && tiku_codeview_tip_shown(v), "TIP-002",
          "the caret back at its position keeps it");
    key(v, TIKU_KEY_LEFT, 0, NULL);
    check(!tiku_codeview_tip_shown(v), "TIP-002",
          "the caret before its position hides it");
    tiku_codeview_select(v, 6, 6);
    tiku_codeview_tip_show(v, 3, "tip");
    key(v, TIKU_KEY_DOWN, 0, NULL);
    check(!tiku_codeview_tip_shown(v), "TIP-002",
          "and so does the caret on another line");
    tiku_codeview_select(v, 6, 6);
    tiku_codeview_tip_show(v, 3, "tip");
    tiku_editdoc_load(tiku_codeview_doc(v), "10 pri\n", 7);
    check(!tiku_codeview_tip_shown(v), "TIP-002", "and a load");
    check(tiku_codeview_tip_show(v, 99, "tip") == TIKU_EDIT_INVALID_RANGE &&
          tiku_codeview_tip_show(v, 3, NULL) == TIKU_EDIT_INVALID,
          "TIP-001", "a position outside the text, or no text, is refused");

    memset(wide, 0, sizeof wide);
    for (x = 0; x < 60; x++)
        memcpy(wide + x * 5, "word ", 5);
    tiku_codeview_set_bounds(v, (tiku_rect_t){ 0, 0, W, H });
    tiku_codeview_select(v, 6, 6);
    tiku_codeview_tip_show(v, 3, wide);
    tiku_codeview_point(v, 3, &x, &y);
    s = painted(v);
    {
        int top = (int)y + tiku_codeview_row_height(v) + 2, left, right;

        for (left = 0; left < W && at(s, left, top) != rgb(TIKU_C_NOTE);
             left++)
            ;
        for (right = left; right < W && at(s, right, top) ==
                                        rgb(TIKU_C_NOTE); right++)
            ;
        for (bottom = top; at(s, left, bottom) == rgb(TIKU_C_NOTE); bottom++)
            ;
        wide_rows = (bottom - top + 1) / (f->height + 2);
        check(left < W && wide_rows >= (tiku_text_width(f, wide) + 319) / 320
              && wide_rows <= (tiku_text_width(f, wide) + 319) / 320 + 1,
              "TIP-001", "a long line wraps at 320 pixels");
        check(right - left + 2 <= 320 + 12, "TIP-001",
              "and the box is no wider than that and its margins");
    }
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/* An allocator that refuses while refuse is set. */
static int refuse;

static void *
t_alloc(size_t n)
{
    return refuse ? NULL : malloc(n);
}

static void *
t_realloc(void *p, size_t n)
{
    return refuse ? NULL : realloc(p, n);
}

/** @brief A list or tip that cannot be allocated leaves the one shown. */
static void
memory(void)
{
    tiku_ed_allocator_t failing = { t_alloc, t_realloc, free };
    tiku_codeview_t *v = view_of("10 pri\n");
    tiku_codeview_list_t list = offer(1);

    tiku_codeview_select(v, 6, 6);
    tiku_codeview_list_show(v, &list);
    tiku_codeview_tip_show(v, 3, "first");
    key(v, TIKU_KEY_DOWN, 0, NULL);
    tiku_ed_test_allocator(&failing);
    refuse = 1;
    list.current = 2;
    check(tiku_codeview_list_show(v, &list) == TIKU_EDIT_NO_MEMORY &&
          chosen(v) == 1, "LST-004",
          "a list that cannot be copied leaves the one shown as it was");
    check(tiku_codeview_tip_show(v, 3, "second") == TIKU_EDIT_NO_MEMORY &&
          tiku_codeview_tip_shown(v), "TIP-001",
          "and so does a tip");
    refuse = 0;
    tiku_ed_test_allocator(NULL);
    check(tiku_codeview_list_insert(v) == TIKU_EDIT_OK &&
          holds(v, "10 PRIVATE\n"), "LST-004", "which still works");
    tiku_codeview_destroy(v);
}

/** @brief A recording surface carries the fills, the list and the tip,
 *         and plays back to the pixels painted directly. */
static void
recorded(void)
{
    tiku_codeview_t *v = view_of("10 pri\n20 x\n");
    tiku_codeview_list_t list = offer(1);
    tiku_surface_t *got = surface(), *played = surface();
    tiku_dl_t *dl = tiku_dl_new(), *back = NULL;
    size_t size, wrote = 0, read = 0;
    void *flat;

    if (dl == NULL)
        exit(1);
    tiku_editdoc_marker_add(tiku_codeview_doc(v), 1, 0, NULL);
    tiku_codeview_set_marker_fill(v, 0, RED);
    tiku_editdoc_execution_set(tiku_codeview_doc(v), 0);
    tiku_codeview_select(v, 6, 6);
    tiku_codeview_list_show(v, &list);
    tiku_codeview_tip_show(v, 3, "PRINT expression\nPrints it.");
    got->record = dl;
    tiku_codeview_paint(v, got);
    got->record = NULL;
    size = tiku_dl_flat_size(dl);
    flat = malloc(size);
    if (flat != NULL && tiku_dl_flatten(dl, flat, size, &wrote))
        back = tiku_dl_unflatten(flat, wrote, &read);
    check(tiku_dl_misses(dl) == 0, "LST-001",
          "the fills, list and tip are commands a list can carry");
    check(back != NULL && read == wrote && tiku_dl_play(back, played) > 0 &&
          memcmp(got->px, played->px, sizeof *got->px * W * H) == 0,
          "LST-001", "and they play back to the same pixels");
    free(flat);
    tiku_dl_free(dl);
    tiku_dl_free(back);
    tiku_surface_free(got);
    tiku_surface_free(played);
    tiku_codeview_destroy(v);
}

/** @brief A list with a NULL word, or too many words to copy, is
 *         refused before it is read, and the list shown stays. */
static void
invalid_list(void)
{
    tiku_codeview_t *v = view_of("PR");
    const char *items[] = { "PRINT", NULL };
    tiku_codeview_list_t list;

    memset(&list, 0, sizeof list);
    list.range.end = 2;
    list.items = items;
    list.count = 1;
    list.inserts = 1;
    check(tiku_codeview_list_show(v, &list) == TIKU_EDIT_OK,
          "LST-005", "a list is shown");
    list.count = 2;
    check(tiku_codeview_list_show(v, &list) == TIKU_EDIT_INVALID &&
          tiku_codeview_list_shown(v, NULL), "LST-005",
          "a NULL word is refused, the list shown staying");
    list.count = SIZE_MAX / sizeof *items + 1;
    check(tiku_codeview_list_show(v, &list) == TIKU_EDIT_LIMIT,
          "LST-005", "a count too big to copy is refused before the words "
          "are read");
    check(tiku_codeview_list_insert(v) == TIKU_EDIT_OK && holds(v, "PRINT"),
          "LST-005", "the list shown still puts its word in");
    tiku_codeview_destroy(v);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    fills();
    list_keys();
    list_presses();
    tips();
    memory();
    recorded();
    invalid_list();
    printf("%s\n", fails ? "FAIL: code view assist" :
           "PASS: code view assist -- marker and execution fills, lists "
           "of words, tips, and the keys and presses they take");
    return fails != 0;
}
