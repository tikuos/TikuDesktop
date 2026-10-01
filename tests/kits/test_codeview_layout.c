/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_layout.c - where a code view puts a document's text:
 * every byte kept, widths from the face's own advances, tabs to stops
 * from the line's start, control characters by name, hit tests that
 * land on caret positions, long lines unbroken, and scrolling that
 * follows edits made through another view.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"
#include "tiku_editdoc_private.h"

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

/** @brief A new document holding @p n bytes of @p s. */
static tiku_editdoc_t *
doc_of_n(const char *s, size_t n)
{
    tiku_editdoc_t *doc = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, n) != TIKU_EDIT_OK) {
        printf("  [FAIL] setup     document\n");
        exit(1);
    }
    return doc;
}

static tiku_editdoc_t *
doc_of(const char *s)
{
    return doc_of_n(s, strlen(s));
}

/** @brief A view of @p doc in a 400 by 200 box at 10, 20; the document
 *         keeps its own reference. */
static tiku_codeview_t *
view_of(tiku_editdoc_t *doc)
{
    tiku_codeview_t *v = NULL;
    tiku_rect_t box = { 10, 20, 400, 200 };

    if (tiku_codeview_create(doc, &v) != TIKU_EDIT_OK) {
        printf("  [FAIL] setup     view\n");
        exit(1);
    }
    tiku_codeview_set_bounds(v, box);
    return v;
}

/** @brief Where x 0 of a line is on the surface, read from the caret
 *         at the start of the document. */
static int64_t
origin_x(tiku_codeview_t *v)
{
    int64_t x = -1, y = -1;

    tiku_codeview_point(v, 0, &x, &y);
    return x + tiku_codeview_left(v);
}

/** @brief The line x of a caret at @p pos. */
static int64_t
x_at(tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    int64_t x = -1, y = -1;

    if (tiku_codeview_point(v, pos, &x, &y) != TIKU_EDIT_OK)
        return -1;
    return x - origin_x(v) + tiku_codeview_left(v);
}

/** @brief The width tiku_text_width() gives the first @p n bytes of
 *         @p s. */
static int
prefix_width(const tiku_font_t *f, const char *s, size_t n)
{
    char buf[256];

    if (n >= sizeof buf)
        return -1;
    memcpy(buf, s, n);
    buf[n] = '\0';
    return tiku_text_width(f, buf);
}

/** @brief Text with no tab or control character measures exactly as
 *         the face draws it, at every boundary. */
static void
widths(void)
{
    static const char *const lines[] = {
        "Handgloves 0123456789", "caf\xc3\xa9 \xce\xb1\xce\xb2\xce\xb3 "
        "\xd0\x96\xe2\x80\xa6\xe4\xb8\x80\xf0\x9f\x98\x80!"
    };
    const tiku_font_t *faces[2];
    int i, k;

    faces[0] = tiku_font_mono(0);
    faces[1] = tiku_font_plain();
    for (k = 0; k < 2; k++) {
        for (i = 0; i < 2; i++) {
            tiku_editdoc_t *doc = doc_of(lines[i]);
            tiku_codeview_t *v = view_of(doc);
            size_t n = strlen(lines[i]), p;
            int all = 1;

            tiku_codeview_set_font(v, faces[k]);
            for (p = 0; p <= n; p++) {
                if (!tiku_editdoc_is_boundary(doc, (tiku_edit_pos_t)p))
                    continue;
                if (x_at(v, (tiku_edit_pos_t)p) !=
                    prefix_width(faces[k], lines[i], p))
                    all = 0;
            }
            check(all, "LAY-002",
                  "every boundary lies where the face's width of the text "
                  "before it puts it");
            check(tiku_codeview_line_width(v, 0) ==
                  tiku_text_width(faces[k], lines[i]), "LAY-002",
                  "a line is as wide as the face draws it");
            check((i == 0 || tiku_codeview_point(v, 4, NULL, NULL) ==
                   TIKU_EDIT_INVALID_RANGE) &&
                  tiku_codeview_point(v, (tiku_edit_pos_t)n + 1, NULL,
                                      NULL) == TIKU_EDIT_INVALID_RANGE,
                  "LAY-002", "a position inside a character or past the "
                  "end has no point");
            tiku_codeview_destroy(v);
            tiku_editdoc_release(doc);
        }
    }
}

/** @brief Tabs reach the next stop counted from their own line's start,
 *         and a tab already at a stop takes a whole interval. */
static void
tabs(void)
{
    tiku_editdoc_t *doc = doc_of("\tx\nab\tx\nabcd\tx\nabc\t\tx\n"
                                 "a\tb\r\n\tc");
    tiku_codeview_t *v = view_of(doc);
    int64_t c = tiku_font_mono_cell(0);
    tiku_edit_pos_t l1 = tiku_editdoc_line_start(doc, 1);
    tiku_edit_pos_t l2 = tiku_editdoc_line_start(doc, 2);
    tiku_edit_pos_t l3 = tiku_editdoc_line_start(doc, 3);
    tiku_edit_pos_t l4 = tiku_editdoc_line_start(doc, 4);
    tiku_edit_pos_t l5 = tiku_editdoc_line_start(doc, 5);

    check(tiku_codeview_tab_width(v) == 4, "LAY-003",
          "tab stops are four spaces apart unless set");
    check(x_at(v, 1) == 4 * c && x_at(v, l1 + 3) == 4 * c, "LAY-003",
          "a tab reaches the next stop from the line's start");
    check(x_at(v, l2 + 5) == 8 * c, "LAY-003",
          "a tab already at a stop takes a whole interval");
    check(x_at(v, l3 + 4) == 4 * c && x_at(v, l3 + 5) == 8 * c, "LAY-003",
          "two tabs reach two stops");
    check(x_at(v, l4 + 2) == 4 * c && x_at(v, l5 + 1) == 4 * c,
          "LAY-003", "after a CRLF, the next line's tab counts from its "
          "own start");
    check(tiku_codeview_set_tab_width(v, 8) == TIKU_EDIT_OK &&
          x_at(v, 1) == 8 * c && x_at(v, l1 + 3) == 8 * c, "LAY-003",
          "a wider setting moves the stops");
    check(tiku_codeview_set_tab_width(v, 0) == TIKU_EDIT_LIMIT &&
          tiku_codeview_set_tab_width(v, TIKU_CODEVIEW_TAB_MAX + 1) ==
          TIKU_EDIT_LIMIT && tiku_codeview_tab_width(v) == 8, "LAY-003",
          "a setting outside 1 to the maximum is refused and the old one "
          "kept");
    check(tiku_codeview_set_tab_width(v, 4) == TIKU_EDIT_OK, "LAY-003",
          "four again");
    tiku_codeview_set_font(v, tiku_font_plain());
    check(x_at(v, 1) == 4 * tiku_text_advance(tiku_font_plain(), ' '),
          "LAY-003", "in a proportional face a column is a space's width");
    tiku_codeview_destroy(v);
    tiku_editdoc_release(doc);
}

/** @brief Control characters are shown by name and are characters like
 *         any other: one caret position before and after each. */
static void
controls(void)
{
    tiku_editdoc_t *doc = doc_of("a\x01" "b\x7f" "c\x1b" "d\xc2\x85" "e");
    tiku_codeview_t *v = view_of(doc);
    const tiku_font_t *f = tiku_font_mono(0);
    int64_t c = tiku_font_mono_cell(0);
    int64_t ctl = tiku_text_width(f, "^A") + 4;
    int64_t del = tiku_text_width(f, "^?") + 4;
    int64_t esc = tiku_text_width(f, "^[") + 4;
    int64_t nel = tiku_text_width(f, "<85>") + 4;

    check(ctl > 2 * c - 1 && x_at(v, 2) == c + ctl, "LAY-004",
          "a C0 control is as wide as its caret name and two pads");
    check(x_at(v, 4) == 2 * c + ctl + del && x_at(v, 6) ==
          3 * c + ctl + del + esc, "LAY-004", "DEL and ESC are named too");
    check(x_at(v, 9) == 4 * c + ctl + del + esc + nel, "LAY-004",
          "a C1 control, two bytes, is named in hexadecimal");
    check(tiku_codeview_line_width(v, 0) ==
          5 * c + ctl + del + esc + nel, "LAY-004",
          "and the line holds every one of them");
    tiku_codeview_destroy(v);
    tiku_editdoc_release(doc);
}

/** @brief Every caret position of a line is where a hit test at its
 *         point lands. */
static int
round_trips(tiku_codeview_t *v, int64_t line)
{
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_edit_pos_t p = tiku_editdoc_line_start(doc, line);
    tiku_edit_pos_t end = tiku_editdoc_line_end(doc, line);

    for (;; p = tiku_editdoc_next(doc, p)) {
        int64_t x = -1, y = -1;

        if (tiku_codeview_point(v, p, &x, &y) != TIKU_EDIT_OK ||
            tiku_codeview_hit(v, (int)x, (int)y) != p)
            return 0;
        if (p >= end)
            return 1;
    }
}

/** @brief Hit tests: the half of a character decides which side, rows
 *         clamp to the document, and the answer is a caret position. */
static void
hits(void)
{
    tiku_editdoc_t *doc = doc_of("abc\n\tx\xc3\xa9\x01y\nab\r\ncd");
    tiku_codeview_t *v = view_of(doc);
    int64_t c = tiku_font_mono_cell(0), x0 = origin_x(v), y0, y1, xs;
    int step = tiku_codeview_row_height(v);

    tiku_codeview_point(v, 0, &xs, &y0);
    tiku_codeview_point(v, 4, &xs, &y1);
    check(y1 - y0 == step && step == tiku_font_mono(0)->height + 2,
          "LAY-005", "rows are the face's height and two pixels apart");
    check(tiku_codeview_hit(v, (int)(x0 + c + c / 2 - 1), (int)y0) == 1 &&
          tiku_codeview_hit(v, (int)(x0 + c + c / 2), (int)y0) == 2,
          "LAY-005", "the left half of a character is before it, the "
          "middle and the right half after");
    check(tiku_codeview_hit(v, (int)(x0 + 100 * c), (int)y0) == 3 &&
          tiku_codeview_hit(v, (int)(x0 - 40), (int)y0) == 0, "LAY-005",
          "past the end is the line's end, before the start its start");
    check(tiku_codeview_hit(v, (int)(x0 + 2 * c - 1), (int)y1) == 5 - 1 &&
          tiku_codeview_hit(v, (int)(x0 + 2 * c), (int)y1) == 5, "LAY-005",
          "a tab's halves split the same way");
    check(tiku_codeview_hit(v, (int)x0, (int)y0 - 50) == 0 &&
          tiku_codeview_hit(v, (int)(x0 + 1000), (int)y0 + 50 * step) ==
          tiku_editdoc_length(doc), "LAY-005",
          "above the first row is the first line, below the last the "
          "last");
    check(tiku_codeview_hit(v, (int)(x0 + 100 * c), (int)(y0 + 2 * step))
          == tiku_editdoc_line_end(doc, 2), "LAY-005",
          "past the end of a CRLF line is before its CR");
    check(round_trips(v, 0) && round_trips(v, 1) && round_trips(v, 2) &&
          round_trips(v, 3), "LAY-005",
          "a hit at each caret position's point lands on it");
    tiku_codeview_set_tab_width(v, 7);
    tiku_codeview_set_font(v, tiku_font_plain());
    check(round_trips(v, 1), "LAY-005",
          "in another face and tab setting too");
    tiku_codeview_destroy(v);
    tiku_editdoc_release(doc);
}

/** @brief A line of 1,000,000 bytes is one row, measured whole. */
static void
long_line(void)
{
    static const tiku_edit_pos_t cuts[] = {
        255, 256, 257, 511, 512, 4096, 65536, 399999, 400000, 400001,
        777777, 999999, 1000000
    };
    size_t n = 1000000, i;
    char *s = malloc(n);
    const tiku_font_t *f = tiku_font_mono(0);
    int64_t c = tiku_font_mono_cell(0), y0, y1, x;
    tiku_editdoc_t *doc;
    tiku_codeview_t *v;
    int same_row = 1;

    if (s == NULL)
        exit(1);
    for (i = 0; i < 400000; i++)
        s[i] = (char)('a' + i % 26);
    s[400000] = '\t';
    for (i = 400001; i + 1 < n; i += 2) {
        s[i] = '\xc3';
        s[i + 1] = '\xa9';
    }
    s[n - 1] = 'z';
    doc = doc_of_n(s, n);
    v = view_of(doc);
    check(tiku_editdoc_lines(doc) == 1 && tiku_codeview_line_width(v, 0) ==
          400000 * c + 4 * c + 299999 * tiku_text_advance(f, 0xE9) +
          c, "LAY-006",
          "a line of a million bytes is measured whole, its tab at its "
          "stop");
    tiku_codeview_point(v, 0, &x, &y0);
    for (i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
        tiku_edit_pos_t p = cuts[i];

        while (!tiku_editdoc_is_boundary(doc, p))
            p--;
        if (tiku_codeview_point(v, p, &x, &y1) != TIKU_EDIT_OK || y1 != y0)
            same_row = 0;
    }
    check(same_row, "LAY-006", "and every part of it is on the first row");
    check(x_at(v, 400001) == 400004 * c &&
          x_at(v, 600001) == 400004 * c + 100000 * tiku_text_advance(f, 0xE9),
          "LAY-006", "positions far along it are where the text puts them");
    tiku_codeview_scroll_to(v, 0, 400000 * c);
    x = origin_x(v) + 4 * c;
    check(tiku_codeview_hit(v, (int)x, (int)y0) == 400001, "LAY-006",
          "a hit far along it, scrolled there, lands after the tab");
    tiku_codeview_destroy(v);
    tiku_editdoc_release(doc);
    free(s);
}

/** @brief The face can change: widths and rows follow it. */
static void
fonts(void)
{
    const char *text = "Wide words, thin ones";
    tiku_editdoc_t *doc = doc_of(text);
    tiku_codeview_t *v = view_of(doc);
    int step12, step16;

    check(tiku_codeview_font(v) == tiku_font_mono(0) &&
          tiku_codeview_line_width(v, 0) ==
          tiku_text_width(tiku_font_mono(0), text), "LAY-007",
          "a new view measures in the monospaced face");
    step12 = tiku_codeview_row_height(v);
    (void)tiku_font_set_size(16);
    step16 = tiku_codeview_row_height(v);
    check(step16 == tiku_font_mono(0)->height + 2 && step16 > step12 &&
          tiku_codeview_line_width(v, 0) ==
          tiku_text_width(tiku_font_mono(0), text), "LAY-007",
          "and follows the interface's size when it changes");
    (void)tiku_font_set_size(12);
    tiku_codeview_set_font(v, tiku_font_plain());
    check(tiku_codeview_line_width(v, 0) ==
          tiku_text_width(tiku_font_plain(), text) &&
          tiku_codeview_row_height(v) == tiku_font_plain()->height + 2,
          "LAY-007", "a proportional face gives its own widths and rows");
    tiku_codeview_set_font(v, NULL);
    check(tiku_codeview_font(v) == tiku_font_mono(0) &&
          tiku_codeview_row_height(v) == step12, "LAY-007",
          "and no face goes back to the monospaced one");
    tiku_codeview_destroy(v);
    tiku_editdoc_release(doc);
}

/** @brief The rows a box shows, and how scrolling moves points and hits. */
static void
scrolling(void)
{
    char text[2000];
    tiku_editdoc_t *doc;
    tiku_codeview_t *v;
    int i, step;
    int64_t x, y, x0, y0;
    tiku_rect_t tiny = { 0, 0, 3, 2 };
    tiku_rect_t box = { 10, 20, 400, 200 };

    text[0] = '\0';
    for (i = 0; i < 100; i++)
        strcat(text, "0123456789\n");
    doc = doc_of(text);
    v = view_of(doc);
    step = tiku_codeview_row_height(v);
    check(tiku_codeview_rows_visible(v) == (200 - 3) / step, "LAY-008",
          "the box shows its whole rows below a three pixel margin");
    tiku_codeview_point(v, 0, &x0, &y0);
    check(x0 == 10 + 5 && y0 == 20 + 3, "LAY-008",
          "the text starts five pixels in and three down");
    tiku_codeview_scroll_to(v, 30, 21);
    tiku_codeview_point(v, tiku_editdoc_line_start(doc, 31) + 3, &x, &y);
    check(tiku_codeview_top(v) == 30 && tiku_codeview_left(v) == 21 &&
          y == y0 + step && x == x0 + 3 * tiku_font_mono_cell(0) - 21,
          "LAY-008", "a scrolled view moves its points");
    check(tiku_codeview_hit(v, (int)x, (int)y) ==
          tiku_editdoc_line_start(doc, 31) + 3, "LAY-008",
          "and its hits");
    tiku_codeview_scroll_to(v, 1000, -5);
    check(tiku_codeview_top(v) == 101 - tiku_codeview_rows_visible(v) &&
          tiku_codeview_left(v) == 0, "LAY-008",
          "the top stops where the last line fills the last row, and "
          "neither goes below 0");
    tiku_codeview_set_bounds(v, tiny);
    tiku_codeview_scroll_to(v, 1000, 0);
    check(tiku_codeview_rows_visible(v) == 1 &&
          tiku_codeview_top(v) == 100, "LAY-008",
          "a box too small for a row still shows one");
    tiku_codeview_set_bounds(v, box);
    check(tiku_codeview_top(v) == 101 - tiku_codeview_rows_visible(v),
          "LAY-008", "growing the box brings the top back");
    tiku_codeview_destroy(v);
    tiku_editdoc_release(doc);
}

/** @brief Replace [s, e) with @p text as one transaction. */
static tiku_edit_status_t
put(tiku_editdoc_t *doc, int64_t s, int64_t e, const char *text)
{
    tiku_edit_replace_t r = { { s, e }, text, strlen(text) };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief A view keeps its top row on its text while another view edits
 *         above it, and each keeps its own selection. */
static void
two_views(void)
{
    char text[2000];
    tiku_editdoc_t *doc;
    tiku_codeview_t *a, *b;
    tiku_edit_pos_t anchor = -1, caret = -1;
    int i;

    text[0] = '\0';
    for (i = 0; i < 100; i++) {
        char line[24];

        snprintf(line, sizeof line, "line %d\n", i);
        strcat(text, line);
    }
    doc = doc_of(text);
    a = view_of(doc);
    b = view_of(doc);
    tiku_codeview_scroll_to(b, 50, 0);
    check(put(doc, tiku_editdoc_line_start(doc, 10),
              tiku_editdoc_line_start(doc, 10), "x\ny\nz\n") ==
          TIKU_EDIT_OK && tiku_codeview_top(b) == 53 &&
          tiku_codeview_top(a) == 0, "LAY-009",
          "lines added above a view's top keep the same text at its top");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          tiku_codeview_top(b) == 50, "LAY-009", "and an undo puts it back");
    check(put(doc, tiku_editdoc_line_start(doc, 45),
              tiku_editdoc_line_start(doc, 60), "") == TIKU_EDIT_OK &&
          tiku_codeview_top(b) == 45, "LAY-009",
          "a deletion over the top line leaves the top at the change");
    check(put(doc, tiku_editdoc_line_start(doc, 70),
              tiku_editdoc_line_start(doc, 70), "w\n") == TIKU_EDIT_OK &&
          tiku_codeview_top(b) == 45, "LAY-009",
          "a change below the top moves nothing");
    tiku_codeview_scroll_to(b, 40, 30);
    check(tiku_editdoc_load(doc, "new\ntext", 8) == TIKU_EDIT_OK &&
          tiku_codeview_top(b) == 0 && tiku_codeview_left(b) == 0,
          "LAY-009", "a load starts every view at the top left");
    check(tiku_codeview_select(a, 1, 3) == TIKU_EDIT_OK &&
          tiku_codeview_selection(b, &anchor, &caret) == TIKU_EDIT_OK &&
          anchor == 0 && caret == 0 &&
          tiku_codeview_selection(a, &anchor, &caret) == TIKU_EDIT_OK &&
          anchor == 1 && caret == 3, "LAY-009",
          "each view keeps its own selection");
    check(tiku_codeview_select(a, 1, 3 + 100) == TIKU_EDIT_INVALID_RANGE,
          "LAY-009", "a selection outside the text is refused");
    tiku_codeview_destroy(a);
    tiku_codeview_destroy(b);
    tiku_editdoc_release(doc);
}

/* An allocator that fails its n-th attempt and counts live blocks. */
static long live, attempts, fail_at;

static void *
t_alloc(size_t n)
{
    size_t *p;

    attempts++;
    if (fail_at && attempts == fail_at)
        return NULL;
    p = malloc(n + 16);
    if (p != NULL) {
        live++;
        return (char *)p + 16;
    }
    return NULL;
}

static void *
t_realloc(void *old, size_t n)
{
    char *p;

    if (old == NULL)
        return t_alloc(n);
    attempts++;
    if (fail_at && attempts == fail_at)
        return NULL;
    p = realloc((char *)old - 16, n + 16);
    return p != NULL ? p + 16 : NULL;
}

static void
t_free(void *p)
{
    if (p != NULL) {
        live--;
        free((char *)p - 16);
    }
}

/** @brief A view holds its document and lets it go; a creation that
 *         cannot allocate leaves nothing behind. */
static void
lifetime(void)
{
    tiku_ed_allocator_t counted = { t_alloc, t_realloc, t_free };
    tiku_editdoc_t *doc;
    tiku_codeview_t *v = (tiku_codeview_t *)&v;
    tiku_edit_view_t handle;
    tiku_edit_pos_t anchor, caret;
    int n, refused = 1;

    check(tiku_codeview_create(NULL, &v) == TIKU_EDIT_INVALID && v == NULL &&
          tiku_codeview_create(NULL, NULL) == TIKU_EDIT_INVALID, "LAY-010",
          "a view needs a document and somewhere to put itself");
    tiku_ed_test_allocator(&counted);
    doc = doc_of("held");
    for (n = 1;; n++) {
        tiku_edit_status_t st;

        attempts = 0;
        fail_at = n;
        st = tiku_codeview_create(doc, &v);
        fail_at = 0;
        if (st == TIKU_EDIT_OK)
            break;
        if (st != TIKU_EDIT_NO_MEMORY || v != NULL)
            refused = 0;
    }
    check(refused && n > 1, "LAY-010",
          "a creation whose allocation fails returns NO_MEMORY and holds "
          "nothing");
    handle = tiku_codeview_doc_view(v);
    tiku_editdoc_retain(doc);
    tiku_editdoc_release(doc);
    tiku_editdoc_release(doc);
    check(tiku_editdoc_length(doc) == 4, "LAY-010",
          "the view keeps its document alive");
    tiku_editdoc_retain(doc);
    tiku_codeview_destroy(v);
    check(tiku_editdoc_view_selection(doc, handle, &anchor, &caret) !=
          TIKU_EDIT_OK, "LAY-010", "a destroyed view's selection is gone");
    tiku_editdoc_release(doc);
    check(live == 0, "LAY-010", "and the last reference frees everything");
    tiku_ed_test_allocator(NULL);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    widths();
    tabs();
    controls();
    hits();
    long_line();
    fonts();
    scrolling();
    two_views();
    lifetime();
    printf("%s\n", fails ? "FAIL: code view layout" :
           "PASS: code view layout -- face widths, tab stops, named "
           "controls, hits, long lines, scrolling, two views");
    return fails != 0;
}
