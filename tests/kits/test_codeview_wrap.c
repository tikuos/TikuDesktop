/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_wrap.c - lines broken into rows at the view's width: where
 * rows break, every byte on one row and back again under the pointer, the
 * scroll range in rows, the caret at a break, Home, End and row moves,
 * each row painted, the rows kept through edits and counted a little at
 * a time, and the view unwrapped when there is no width or no room.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"
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

#define W 400
#define H 200
#define PAPER TIKU_RGB(255, 0, 255)
#define X0 15                   /* text x with no gutter: 10 + 5    */
#define Y0 13                   /* the first row's top: 10 + 3      */

/** @brief One character cell of the monospaced face. */
static int64_t
cell(void)
{
    return tiku_font_mono_cell(0);
}

/** @brief A box whose rows hold @p cells characters: the text starts 5
 *         pixels in and a row keeps 5 more clear, so 2 to spare. */
static tiku_rect_t
wbox(int cells)
{
    tiku_rect_t r = { 10, 10, 10 + cells * (int)cell() + 2, 160 };

    return r;
}

/** @brief A view of a new document holding @p s in @p r, lines wrapping;
 *         the view holds the only reference. */
static tiku_codeview_t *
view_in(const char *s, tiku_rect_t r)
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
    tiku_codeview_set_bounds(v, r);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP);
    return v;
}

/** @brief Rows in all, as the scroll range counts them. */
static int64_t
rows_of(tiku_codeview_t *v)
{
    int64_t total = -1;

    tiku_codeview_extent(v, 0, &total, NULL, NULL);
    return total;
}

/** @brief The row a caret at @p pos is drawn on, from the first shown. */
static int64_t
row_at(tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    int64_t y = 0;

    if (tiku_codeview_point(v, pos, NULL, &y) != TIKU_EDIT_OK)
        return -100;
    return (y - Y0) / tiku_codeview_row_height(v);
}

/** @brief The surface x a caret at @p pos is drawn at. */
static int64_t
x_at(tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    int64_t x = -1;

    (void)tiku_codeview_point(v, pos, &x, NULL);
    return x;
}

/** @brief Whether the selection is the caret alone at @p pos. */
static int
caret_is(tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    tiku_edit_pos_t a = -1, c = -1;

    return tiku_codeview_selection(v, &a, &c) == TIKU_EDIT_OK &&
           a == pos && c == pos;
}

/** @brief Move the caret by @p m. */
static void
go(tiku_codeview_t *v, tiku_codeview_move_t m)
{
    (void)tiku_codeview_move(v, m, 0);
}

/** @brief Put the caret at @p pos. */
static void
put(tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    (void)tiku_codeview_select(v, pos, pos);
}

/** @brief Button 1 pressed and let go at @p x, @p y, at @p time
 *         microseconds. */
static void
click(tiku_codeview_t *v, int x, int y, int64_t time)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_POINTER_DOWN;
    e.button = 1;
    e.x = x;
    e.y = y;
    e.time_us = time;
    (void)tiku_codeview_event(v, &e);
    e.type = TIKU_EVENT_POINTER_UP;
    (void)tiku_codeview_event(v, &e);
}

/** @brief Where rows break: after the last blank that fits, else before
 *         the character that would pass the width, at least one
 *         character a row, blanks hanging past the width, and tab stops
 *         counted from the line's start. */
static void
breaks(void)
{
    int64_t c = cell();
    tiku_codeview_t *v = view_in("aaaa bbbb cccc", wbox(10));

    check(rows_of(v) == 2 && row_at(v, 9) == 0 && x_at(v, 9) == X0 + 9 * c &&
          row_at(v, 10) == 1 && x_at(v, 10) == X0 && x_at(v, 14) == X0 + 4 * c,
          "WRP-001", "a row breaks after the last blank that fits");
    tiku_codeview_destroy(v);
    v = view_in("abcdefghijklmnopqrstuvwxy", wbox(10));
    check(rows_of(v) == 3 && row_at(v, 9) == 0 && row_at(v, 10) == 1 &&
          row_at(v, 20) == 2 && x_at(v, 20) == X0, "WRP-001",
          "a word wider than a row breaks before the character that passes");
    tiku_codeview_destroy(v);
    v = view_in("abcdefghij     klm", wbox(10));
    check(rows_of(v) == 2 && row_at(v, 14) == 0 &&
          x_at(v, 14) == X0 + 14 * c && row_at(v, 15) == 1 &&
          x_at(v, 15) == X0, "WRP-001",
          "blanks past the width stay on their row and none starts one");
    tiku_codeview_destroy(v);
    v = view_in("x\001y", wbox(1));
    check(rows_of(v) == 3 && row_at(v, 1) == 1 && row_at(v, 2) == 2,
          "WRP-001", "a character wider than a row still gets one");
    tiku_codeview_destroy(v);
    v = view_in("x \001", wbox(1));
    check(rows_of(v) == 2 && row_at(v, 2) == 1, "WRP-001",
          "and one after blanks gets one row, not an empty one before it");
    tiku_codeview_destroy(v);
    v = view_in("abcdefghij x\tz", wbox(10));
    check(rows_of(v) == 2 && row_at(v, 11) == 1 && x_at(v, 11) == X0 &&
          x_at(v, 13) == X0 + 5 * c, "WRP-002",
          "tab stops are counted from the line's start, not the row's");
    tiku_codeview_destroy(v);
    v = view_in("\nab\n", wbox(10));
    check(rows_of(v) == 3, "WRP-001", "an empty or short line is one row");
    tiku_codeview_destroy(v);
}

/** @brief The option: off, a row a line and scrolled across; on, rows;
 *         off again, the first line shown is kept. */
static void
option(void)
{
    char text[400] = "";
    tiku_codeview_t *v;
    int i;

    for (i = 0; i < 12; i++)
        strcat(text, "aaaa bbbb cccc dddd eeee\n");
    v = view_in(text, wbox(10));
    tiku_codeview_set_options(v, 0);
    check(rows_of(v) == 13 && row_at(v, 10) == 0, "WRP-001",
          "without the option each line is one row");
    tiku_codeview_scroll_to(v, 5, 0);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP);
    check(rows_of(v) == 37 && tiku_codeview_top(v) == 15 &&
          row_at(v, 5 * 25) == 0 && row_at(v, 5 * 25 + 10) == 1,
          "WRP-001", "with it the lines wrap and the first line shown stays");
    tiku_codeview_scroll_to(v, 16, 0);
    tiku_codeview_set_options(v, 0);
    check(rows_of(v) == 13 && tiku_codeview_top(v) == 5 &&
          row_at(v, 5 * 25) == 0, "WRP-001",
          "turned off, the line the first row was on is shown first");
    tiku_codeview_destroy(v);
}

/** @brief Every caret position is on one row, in order, and a hit on
 *         where it is drawn gives it back. */
static void
lossless(void)
{
    static const char text[] =
        "ab\tcd efgh ijklmnopqrstuvw opq\001rs tu  vwxyz\n\n"
        "short\n\t\t\tdeep tabbed words go on past the width\n"
        "caf\xc3\xa9 na\xc3\xafve \xe2\x82\xac\xe2\x82\xac\xe2\x82\xac end";
    tiku_codeview_t *v = view_in(text, wbox(10));
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_edit_pos_t p, len = tiku_editdoc_length(doc);
    int64_t last_row = 0, last_x = -1, x, y;
    int back = 1, order = 1, rows = 0;

    tiku_codeview_set_bounds(v, (tiku_rect_t){ 10, 10, wbox(10).w, 190 });
    for (p = 0; p <= len; p++) {
        int64_t row;

        if (!tiku_editdoc_is_caret(doc, p))
            continue;
        if (tiku_codeview_point(v, p, &x, &y) != TIKU_EDIT_OK) {
            back = 0;
            continue;
        }
        row = (y - Y0) / tiku_codeview_row_height(v);
        if (row < last_row || (row == last_row && x < last_x))
            order = 0;
        if (row > last_row)
            rows++;
        last_row = row;
        last_x = x;
        if (tiku_codeview_hit(v, (int)x, (int)y) != p)
            back = 0;
    }
    check(order && rows + 1 == rows_of(v), "WRP-002",
          "positions run left to right and down, filling every row");
    check(back, "WRP-002", "a hit where each caret is drawn gives it back");
    tiku_codeview_destroy(v);
}

/** @brief The scroll range and position count rows; nothing scrolls
 *         across; the wheel moves by rows; the top stops at the last. */
static void
scrolling(void)
{
    char text[1000] = "";
    tiku_codeview_t *v;
    int64_t total = 0, shown = 0, at = -1;
    int i;

    for (i = 0; i < 30; i++)
        strcat(text, &"\naaaa bbbb cccc dddd eeee"[i == 0]);
    v = view_in(text, wbox(10));
    tiku_codeview_extent(v, 0, &total, &shown, &at);
    check(total == 90 && shown == tiku_codeview_rows_visible(v) && at == 0,
          "WRP-003", "the range counts rows: three a line");
    tiku_codeview_scroll_to(v, 7, 0);
    check(tiku_codeview_top(v) == 7 && row_at(v, 2 * 25 + 10) == 0,
          "WRP-003", "scrolling to row 7 shows line 3's second row first");
    tiku_codeview_scroll_to(v, 1000, 0);
    check(tiku_codeview_top(v) == 90 - tiku_codeview_rows_visible(v),
          "WRP-003", "the top stops where the last row fills the last");
    tiku_codeview_scroll_to(v, 7, 50);
    tiku_codeview_extent(v, 1, &total, &shown, &at);
    check(tiku_codeview_left(v) == 0 && total == shown && at == 0,
          "WRP-003", "wrapped text never scrolls across");
    {
        tiku_event_t e;

        memset(&e, 0, sizeof e);
        e.type = TIKU_EVENT_WHEEL;
        e.dy = 2;
        (void)tiku_codeview_event(v, &e);
        check(tiku_codeview_top(v) == 9, "WRP-003",
              "the wheel scrolls by rows");
    }
    tiku_codeview_destroy(v);
}

/** @brief A caret where a row breaks shows at the next row's start, and
 *         at the end of the row it ends after End, a press past that
 *         row's end or a row move that lands there, until it moves. */
static void
affinity(void)
{
    int64_t c = cell();
    tiku_codeview_t *v = view_in("aaaa bbbb cccc", wbox(10));

    put(v, 0);
    go(v, TIKU_CODEVIEW_LINE_END);
    check(caret_is(v, 10) && row_at(v, 10) == 0 && x_at(v, 10) == X0 + 10 * c,
          "WRP-004", "End stops at the row's end, shown on that row");
    go(v, TIKU_CODEVIEW_CHAR_RIGHT);
    go(v, TIKU_CODEVIEW_CHAR_LEFT);
    check(caret_is(v, 10) && row_at(v, 10) == 1 && x_at(v, 10) == X0,
          "WRP-004", "come back to by a character it shows at the next row");
    put(v, 0);
    go(v, TIKU_CODEVIEW_LINE_END);
    put(v, 10);
    check(caret_is(v, 10) && row_at(v, 10) == 1, "WRP-004",
          "selecting the same place shows it at the next row's start");
    click(v, X0 + 10 * (int)c + 2, Y0 + 1, 1000000);
    check(caret_is(v, 10) && row_at(v, 10) == 0, "WRP-004",
          "a press past a row's end puts the caret at that end");
    click(v, X0 + 1, Y0 + tiku_codeview_row_height(v) + 1, 3000000);
    check(caret_is(v, 10) && row_at(v, 10) == 1, "WRP-004",
          "a press at the next row's start puts it there");
    tiku_codeview_destroy(v);
    v = view_in("aa bbbbbbbbbbbb", wbox(10));
    put(v, 8);
    go(v, TIKU_CODEVIEW_LINE_UP);
    check(caret_is(v, 3) && row_at(v, 3) == 0 && x_at(v, 3) == X0 + 3 * c,
          "WRP-004", "a row move landing past a row's end stays on that row");
    go(v, TIKU_CODEVIEW_LINE_DOWN);
    check(caret_is(v, 8), "WRP-006", "and the x it kept takes it back");
    put(v, 0);
    go(v, TIKU_CODEVIEW_LINE_END);
    (void)tiku_codeview_insert(v, "X", 1, 1);
    check(caret_is(v, 4) && row_at(v, 4) == 1, "WRP-004",
          "typing at a row's end goes on from the caret, which then moves");
    tiku_codeview_destroy(v);
}

/** @brief Home and End go to the row's start and end first, and to the
 *         line's when the caret is already there. */
static void
home_end(void)
{
    tiku_codeview_t *v = view_in("aaaa bbbb cccc dddd eeee\nxy", wbox(10));

    put(v, 13);
    go(v, TIKU_CODEVIEW_LINE_START);
    check(caret_is(v, 10) && row_at(v, 10) == 1, "WRP-005",
          "Home goes to the start of the caret's row");
    go(v, TIKU_CODEVIEW_LINE_START);
    check(caret_is(v, 0), "WRP-005", "and again to the line's start");
    put(v, 13);
    go(v, TIKU_CODEVIEW_LINE_END);
    check(caret_is(v, 20) && row_at(v, 20) == 1, "WRP-005",
          "End goes to the end of the caret's row");
    go(v, TIKU_CODEVIEW_LINE_END);
    check(caret_is(v, 24), "WRP-005", "and again to the line's end");
    put(v, 21);
    go(v, TIKU_CODEVIEW_LINE_END);
    check(caret_is(v, 24), "WRP-005",
          "on a line's last row End goes to the line's end at once");
    put(v, 20);
    go(v, TIKU_CODEVIEW_LINE_START);
    check(caret_is(v, 0), "WRP-005",
          "at a row's start Home goes to the line's");
    tiku_codeview_destroy(v);
}

/** @brief Up, Down and the page keys move by rows at the x kept from the
 *         rows' starts, across lines. */
static void
vertical(void)
{
    char text[1000] = "aaaa bbbb cccc dddd eeee\nxy";
    tiku_codeview_t *v = view_in(text, wbox(10));
    int i;

    put(v, 3);
    go(v, TIKU_CODEVIEW_LINE_DOWN);
    check(caret_is(v, 13), "WRP-006", "Down goes to the next row of a line");
    go(v, TIKU_CODEVIEW_LINE_DOWN);
    check(caret_is(v, 23), "WRP-006", "and to its last row");
    go(v, TIKU_CODEVIEW_LINE_DOWN);
    check(caret_is(v, 27), "WRP-006",
          "and on to the next line, stopping at its end");
    go(v, TIKU_CODEVIEW_LINE_UP);
    go(v, TIKU_CODEVIEW_LINE_UP);
    check(caret_is(v, 13), "WRP-006", "Up comes back at the kept x");
    tiku_codeview_destroy(v);
    strcpy(text, "");
    for (i = 0; i < 20; i++)
        strcat(text, "aaaa bbbb cccc dddd eeee\n");
    v = view_in(text, wbox(10));
    put(v, 2);
    go(v, TIKU_CODEVIEW_PAGE_DOWN);
    check(row_at(v, 2) < 0 && caret_is(v, 25 * (tiku_codeview_rows_visible(v)
                                                / 3) +
                                       10 * (tiku_codeview_rows_visible(v) %
                                             3) + 2),
          "WRP-006", "Page Down moves the rows shown down");
    tiku_codeview_scroll_to(v, 0, 0);
    put(v, 2);
    for (i = 1; i < tiku_codeview_rows_visible(v); i++)
        go(v, TIKU_CODEVIEW_LINE_DOWN);
    check(tiku_codeview_top(v) == 0, "WRP-006",
          "moves within the rows shown do not scroll");
    go(v, TIKU_CODEVIEW_LINE_DOWN);
    check(tiku_codeview_top(v) == 1, "WRP-006",
          "a row move past the last row shown scrolls one row");
    tiku_codeview_destroy(v);
}

/** @brief The colour bits of the pixel at @p x, @p y. */
static tiku_rgb_t
pixel(const tiku_surface_t *s, int x, int y)
{
    return s->px[y * s->w + x] & 0xffffffu;
}

/** @brief Whether @p a and @p b hold the same pixels inside @p r. */
static int
same_in(const tiku_surface_t *a, const tiku_surface_t *b, tiku_rect_t r)
{
    int x, y;

    for (y = r.y; y < r.y + r.h; y++)
        for (x = r.x; x < r.x + r.w; x++)
            if (pixel(a, x, y) != pixel(b, x, y))
                return 0;
    return 1;
}

/** @brief Each row drawn from its own start, the line number on a line's
 *         first row only. */
static void
paint_rows(void)
{
    tiku_codeview_t *v = view_in("aaaa bbbb cccc\nxy", wbox(10));
    tiku_surface_t *got = tiku_surface_new(W, H, PAPER);
    tiku_surface_t *want = tiku_surface_new(W, H, PAPER);
    const tiku_font_t *f = tiku_font_mono(0), *small = tiku_font_at(11);
    int step = tiku_codeview_row_height(v), gutter, tx;
    tiku_rect_t box = wbox(10), text;

    if (got == NULL || want == NULL)
        exit(1);
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, got);
    tiku_fill(want, box, TIKU_C_DOC);
    text = (tiku_rect_t){ X0, box.y, box.w - 5, box.h };
    tiku_clip_set(want, text);
    tiku_text(want, f, X0, Y0 + f->ascent, "aaaa bbbb ", TIKU_C_TEXT);
    tiku_text(want, f, X0, Y0 + step + f->ascent, "cccc", TIKU_C_TEXT);
    tiku_text(want, f, X0, Y0 + 2 * step + f->ascent, "xy", TIKU_C_TEXT);
    tiku_clip_reset(want);
    check(same_in(got, want, box), "WRP-007",
          "each row is drawn from its own first character");
    gutter = 12 + tiku_text_advance(f, '0');
    tx = box.x + gutter + 4;
    box.w += gutter - 1;
    tiku_codeview_set_bounds(v, box);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP |
                              TIKU_CODEVIEW_LINE_NUMBERS);
    tiku_codeview_paint(v, got);
    tiku_fill(want, box, PAPER);
    tiku_fill(want, (tiku_rect_t){ box.x, box.y, gutter, box.h },
              tiku_tint(TIKU_C_DOC, 1.04f));
    tiku_vline(want, box.x + gutter - 1, box.y, box.h,
               tiku_tint(TIKU_C_DOC, 1.12f));
    tiku_text(want, small, box.x + gutter - 6 - tiku_text_width(small, "1"),
              Y0 + f->ascent, "1", tiku_ink(TIKU_INK_FAINT));
    tiku_text(want, small, box.x + gutter - 6 - tiku_text_width(small, "2"),
              Y0 + 2 * step + f->ascent, "2", tiku_ink(TIKU_INK_FAINT));
    check(same_in(got, want, (tiku_rect_t){ box.x, box.y, gutter, box.h }) &&
          row_at(v, 9) == 0 && row_at(v, 10) == 1 && x_at(v, 10) == tx,
          "WRP-007", "a line's number is on its first row only, and the "
          "rows start past the gutter");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief The selection, a marked line's fill, the bracket by the caret
 *         and the caret itself are drawn on the rows they fall in. */
static void
paint_marks(void)
{
    int64_t c = cell();
    tiku_codeview_t *v = view_in("aaaa bbbb (ccc)\nxy", wbox(10));
    tiku_surface_t *s = tiku_surface_new(W, H, PAPER);
    tiku_rgb_t sel = TIKU_C_SELECT & 0xffffffu, doc = TIKU_C_DOC & 0xffffffu;
    tiku_rgb_t mark = TIKU_RGB(200, 220, 255) & 0xffffffu;
    tiku_rgb_t pair = tiku_dim(TIKU_C_SELECT, TIKU_C_DOC) & 0xffffffu;
    int step = tiku_codeview_row_height(v), foot = Y0 + step - 1;

    if (s == NULL)
        exit(1);
    tiku_codeview_set_focus(v, 0);
    (void)tiku_codeview_select(v, 8, 12);
    tiku_codeview_paint(v, s);
    check(pixel(s, X0 + 8 * (int)c, foot) == sel &&
          pixel(s, X0 + 7 * (int)c, foot) == doc &&
          pixel(s, X0, foot + step) == sel &&
          pixel(s, X0 + 2 * (int)c, foot + step) == doc, "WRP-007",
          "a selection over a break fills the end of one row and the start "
          "of the next");
    (void)tiku_editdoc_marker_add(tiku_codeview_doc(v), 0, 2, NULL);
    (void)tiku_codeview_set_marker_fill(v, 2, TIKU_RGB(200, 220, 255));
    put(v, 0);
    tiku_codeview_paint(v, s);
    check(pixel(s, X0 + 9 * (int)c, foot + step) == mark &&
          pixel(s, X0 + 9 * (int)c, foot + 2 * step) == doc, "WRP-007",
          "a marked line's fill covers all its rows and no more");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP | TIKU_CODEVIEW_BRACES);
    put(v, 11);
    tiku_codeview_paint(v, s);
    check(pixel(s, X0, foot + step) == pair &&
          pixel(s, X0 + 4 * (int)c, foot + step) == pair, "WRP-007",
          "brackets on a wrapped row are marked where that row draws them");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP);
    tiku_codeview_set_focus(v, 1);
    put(v, 0);
    go(v, TIKU_CODEVIEW_LINE_END);
    tiku_codeview_paint(v, s);
    check(pixel(s, X0 + 10 * (int)c, Y0 + 2) == (TIKU_C_TEXT & 0xffffffu),
          "WRP-007", "after End the caret is drawn at the row's end");
    go(v, TIKU_CODEVIEW_CHAR_RIGHT);
    go(v, TIKU_CODEVIEW_CHAR_LEFT);
    tiku_codeview_paint(v, s);
    check(pixel(s, X0 + 10 * (int)c, Y0 + 2) != (TIKU_C_TEXT & 0xffffffu),
          "WRP-007", "and at the next row's start otherwise");
    tiku_codeview_destroy(v);
    tiku_surface_free(s);
}

/** @brief The rows one line of ASCII letters, blanks and tabs breaks
 *         into by the rule, in face @p f, for checking. */
static int64_t
model_rows(const tiku_font_t *f, const char *s, int64_t n, int64_t width,
           int64_t tab)
{
    int64_t x = 0, x0 = 0, start = 0, opp = -1, opp_x = 0;
    int64_t rows = 1, i;
    int blank_before = 0;

    for (i = 0; i < n; i++) {
        int blank = s[i] == ' ' || s[i] == '\t';
        int64_t adv = s[i] == '\t' ? tab - x % tab :
                      tiku_text_advance(f, (unsigned char)s[i]);

        if (!blank && blank_before && i > start) {
            opp = i;
            opp_x = x;
        }
        blank_before = blank;
        if (!blank && i > start && x + adv - x0 > width) {
            if (opp > start) {
                start = opp;
                x0 = opp_x;
                rows++;
            }
            if (i > start && x + adv - x0 > width) {
                start = i;
                x0 = x;
                rows++;
            }
            opp = -1;
        }
        x += adv;
    }
    return rows;
}

/** @brief Whether the view's rows match the rule's for every line, the
 *         first rows of each line where the rule puts them. */
static int
rows_match(tiku_codeview_t *v)
{
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    int64_t lines = tiku_editdoc_lines(doc), line, row = 0;
    int64_t width = wbox(10).w - 10, tab = 4 * cell();
    char buf[4096];
    int ticks;

    for (ticks = 0; ticks < 100 && tiku_codeview_wrapping(v); ticks++) {
        tiku_event_t e;

        memset(&e, 0, sizeof e);
        e.type = TIKU_EVENT_TICK;
        (void)tiku_codeview_event(v, &e);
    }
    tiku_codeview_scroll_to(v, 0, 0);
    for (line = 0; line < lines; line++) {
        tiku_edit_range_t r;
        size_t n = 0;

        r.start = tiku_editdoc_line_start(doc, line);
        r.end = tiku_editdoc_line_end(doc, line);
        if (tiku_editdoc_read(doc, r, buf, sizeof buf, &n) != TIKU_EDIT_OK ||
            row_at(v, r.start) != row)
            return 0;
        row += model_rows(tiku_font_mono(0), buf, (int64_t)n, width, tab);
    }
    return rows_of(v) == row;
}

/** @brief Replace [@p s, @p e) with @p text as one edit. */
static void
replace(tiku_editdoc_t *doc, int64_t s, int64_t e, const char *text)
{
    tiku_edit_replace_t r = { { s, e }, text, strlen(text) };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    (void)tiku_editdoc_apply(doc, &t);
}

/** @brief Edits re-break the lines they touch and keep the text the
 *         first row shows; the rows stay right through random edits. */
static void
edits(void)
{
    static const char *const pieces[] = {
        "", "x", "wide wide wide wide", "\n", "\n\n", "a\tb", "zz zz zz",
        "q\nr", "abcdefghijklmnop"
    };
    char text[2000] = "";
    tiku_codeview_t *v = view_in("aaaa bbbb", wbox(10));
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    unsigned seed = 4242;
    int i, kept = 1;

    put(v, 9);
    (void)tiku_codeview_insert(v, " cccc", 5, 1);
    check(rows_of(v) == 2 && row_at(v, 10) == 1, "WRP-008",
          "typing past the width breaks the line");
    (void)tiku_codeview_undo(v);
    check(rows_of(v) == 1, "WRP-008", "undoing it joins the rows again");
    tiku_codeview_destroy(v);
    for (i = 0; i < 20; i++)
        strcat(text, "aaaa bbbb cccc dddd eeee\n");
    v = view_in(text, wbox(10));
    doc = tiku_codeview_doc(v);
    tiku_codeview_scroll_to(v, 7, 0);
    replace(doc, 0, 24, "a");
    check(row_at(v, 2 * 25 - 23 + 10) == 0 && tiku_codeview_top(v) == 5,
          "WRP-008", "a line above the first row losing rows leaves the "
          "text shown where it was");
    replace(doc, 0, 1, "aaaa bbbb cccc dddd eeee ffff gggg");
    check(row_at(v, 2 * 25 + 10 + 10) == 0 && tiku_codeview_top(v) == 8,
          "WRP-008", "and gaining rows too");
    replace(doc, tiku_editdoc_line_start(doc, 1),
            tiku_editdoc_line_start(doc, 4), "xxxx yyyy xxxx yyyy xxxx\n");
    check(row_at(v, tiku_editdoc_line_start(doc, 1)) == 0 &&
          tiku_codeview_top(v) == 4, "WRP-008",
          "a change over the first row shown puts its first row there");
    for (i = 0; i < 400; i++) {
        int64_t len = tiku_editdoc_length(doc), s, e;

        seed = seed * 1103515245u + 12345u;
        s = (int64_t)(seed >> 8) % (len + 1);
        seed = seed * 1103515245u + 12345u;
        e = s + (int64_t)(seed >> 8) % 14;
        if (e > len)
            e = len;
        while (!tiku_editdoc_is_caret(doc, s))
            s--;
        while (!tiku_editdoc_is_caret(doc, e))
            e--;
        if (e < s)
            e = s;
        seed = seed * 1103515245u + 12345u;
        if ((seed >> 9) % 7 == 0)
            (void)tiku_editdoc_undo(doc);
        else
            replace(doc, s, e, pieces[(seed >> 12) % 9]);
        if (i % 4 == 0 && !rows_match(v))
            kept = 0;
    }
    check(kept, "WRP-008", "every line's rows are where the rule puts them "
          "after every few of 400 random edits and undos");
    tiku_codeview_destroy(v);
}

/** @brief A long document: the rows shown are counted at once, the rest
 *         a part at a time, and the view says so until they all are. */
static void
counting(void)
{
    int lines = 200000, i, ticks = 0, pending;
    char *text = malloc((size_t)lines * 25 + 1), *p = text;
    tiku_codeview_t *v;
    tiku_event_t e;

    if (text == NULL)
        exit(1);
    for (i = 0; i < lines; i++, p += 25)
        memcpy(p, "aaaa bbbb cccc dddd eeee\n", 25);
    *p = '\0';
    v = view_in(text, wbox(10));
    check(tiku_codeview_wrapping(v) && rows_of(v) < 3 * lines + 1 &&
          rows_of(v) > lines + 1, "WRP-009",
          "the lines past what one layout counts take one row until counted");
    tiku_codeview_scroll_to(v, 3 * (int64_t)lines, 0);
    check(row_at(v, (int64_t)25 * (lines - 1) + 20) ==
              tiku_codeview_rows_visible(v) - 2, "WRP-009",
          "the rows shown are counted when shown");
    tiku_codeview_scroll_to(v, rows_of(v) - 50000, 0);
    {
        tiku_editdoc_t *doc = tiku_codeview_doc(v);
        tiku_edit_pos_t at = tiku_codeview_hit(v, X0, Y0 + 1);
        int64_t line = tiku_editdoc_line_of(doc, at);
        int64_t sub = (at - tiku_editdoc_line_start(doc, line)) / 10;

        check(line > lines / 2 &&
              row_at(v, tiku_editdoc_line_start(doc, line + 2)) == 6 - sub &&
              row_at(v, tiku_editdoc_line_start(doc, line + 3)) == 9 - sub,
              "WRP-009", "and so are rows shown among lines not counted");
    }
    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_TICK;
    pending = tiku_codeview_wrapping(v);
    for (i = 0; i < 100 && tiku_codeview_wrapping(v); i++)
        ticks += tiku_codeview_event(v, &e);
    check(pending && !tiku_codeview_wrapping(v) && ticks > 0 &&
          rows_of(v) == 3 * (int64_t)lines + 1, "WRP-009",
          "ticks count the rest, then the view says it is done");
    check(tiku_codeview_event(v, &e) == 0, "WRP-009",
          "a tick with nothing to count is not taken");
    tiku_codeview_destroy(v);
    free(text);
}

/** @brief The rows break again when the width, the face or the tab
 *         stops change. */
static void
metrics(void)
{
    int64_t c = cell();
    tiku_codeview_t *v = view_in("aaaa bbbb cccc dddd", wbox(10));

    check(rows_of(v) == 2, "WRP-010", "two rows at ten characters");
    tiku_codeview_set_bounds(v, wbox(5));
    check(rows_of(v) == 4 && row_at(v, 15) == 3, "WRP-010",
          "four at five characters");
    tiku_codeview_set_bounds(v, wbox(20));
    check(rows_of(v) == 1, "WRP-010", "one at twenty");
    tiku_codeview_set_bounds(v, wbox(6));
    tiku_codeview_set_font(v, tiku_font_plain());
    check(rows_of(v) == model_rows(tiku_font_plain(), "aaaa bbbb cccc dddd",
                                   19, wbox(6).w - 10,
                                   4 * tiku_text_advance(tiku_font_plain(),
                                                         ' ')),
          "WRP-010", "another face breaks by its own widths");
    tiku_codeview_set_font(v, NULL);
    tiku_codeview_destroy(v);
    v = view_in("ab\tcd\tef", wbox(10));
    check(rows_of(v) == 1, "WRP-010", "tabs of four fit one row");
    (void)tiku_codeview_set_tab_width(v, 8);
    check(rows_of(v) == 2 && row_at(v, 6) == 1 && x_at(v, 6) == X0 &&
          x_at(v, 5) == X0 + 10 * c, "WRP-010", "tabs of eight break it");
    tiku_codeview_destroy(v);
}

/* An allocator whose every allocation can be made to fail. */
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

/** @brief No width to wrap at, or no room for the rows: the lines are
 *         shown a row each until there is. */
static void
unwrapped(void)
{
    tiku_ed_allocator_t failing = { t_alloc, t_realloc, free };
    tiku_codeview_t *v = view_in("aaaa bbbb cccc\nxy", wbox(10));

    tiku_codeview_set_bounds(v, (tiku_rect_t){ 10, 10, 10, 160 });
    check(rows_of(v) == 2, "WRP-011", "with no width each line is a row");
    tiku_codeview_set_bounds(v, wbox(10));
    check(rows_of(v) == 3, "WRP-011", "given width again the lines wrap");
    tiku_codeview_set_options(v, 0);
    tiku_ed_test_allocator(&failing);
    refuse = 1;
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP);
    check(rows_of(v) == 2 && row_at(v, 10) == 0, "WRP-011",
          "with no room for the rows each line is a row");
    refuse = 0;
    tiku_codeview_set_bounds(v, wbox(10));
    check(rows_of(v) == 3 && row_at(v, 10) == 1, "WRP-011",
          "and once there is room they wrap");
    tiku_ed_test_allocator(NULL);
    tiku_codeview_destroy(v);
}

int
main(void)
{
    breaks();
    option();
    lossless();
    scrolling();
    affinity();
    home_end();
    vertical();
    paint_rows();
    paint_marks();
    edits();
    counting();
    metrics();
    unwrapped();
    if (fails) {
        printf("FAIL: %d checks\n", fails);
        return 1;
    }
    printf("PASS: code view wrapping -- breaks, every byte on a row, rows "
           "scrolled, the caret at a break, Home and End, row moves, rows "
           "painted, edits, counting, widths, no room\n");
    return 0;
}
