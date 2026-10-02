/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_notes.c - the document's indicators drawn under their
 * text in each kind's style, its annotations shown as rows under their
 * lines and passed over by the caret, and the caret taken from one
 * indicator to the next.
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
#define H 200
#define PAPER TIKU_RGB(255, 0, 255)
#define RED TIKU_RGB(200, 0, 0)
#define X0 15                   /* text x with no gutter: 10 + 5    */
#define Y0 13                   /* the first row's top: 10 + 3      */

static const tiku_rect_t box = { 10, 10, 320, 160 };

/** @brief One character cell of the monospaced face. */
static int
cell(void)
{
    return tiku_font_mono_cell(0);
}

/** @brief A view of a new document holding @p s in the box; the view
 *         holds the only reference. */
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
    tiku_codeview_set_focus(v, 0);
    return v;
}

/** @brief A W by H surface of PAPER. */
static tiku_surface_t *
surface(void)
{
    tiku_surface_t *s = tiku_surface_new(W, H, PAPER);

    if (s == NULL)
        exit(1);
    return s;
}

/** @brief The colour bits of the pixel at @p x, @p y. */
static tiku_rgb_t
pixel(const tiku_surface_t *s, int x, int y)
{
    return s->px[y * s->w + x] & 0xffffffu;
}

/** @brief An indicator of @p kind over [@p a, @p b). */
static void
mark(tiku_codeview_t *v, int64_t a, int64_t b, unsigned kind)
{
    tiku_edit_range_t r = { a, b };

    if (tiku_editdoc_indicator_add(tiku_codeview_doc(v), r, kind, NULL) !=
        TIKU_EDIT_OK)
        exit(1);
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

/** @brief Rows in all, as the scroll range counts them. */
static int64_t
rows_of(tiku_codeview_t *v)
{
    int64_t total = -1;

    tiku_codeview_extent(v, 0, &total, NULL, NULL);
    return total;
}

/** @brief Whether the selection is @p anchor to @p caret. */
static int
is(tiku_codeview_t *v, tiku_edit_pos_t anchor, tiku_edit_pos_t caret)
{
    tiku_edit_pos_t a = -1, c = -1;

    return tiku_codeview_selection(v, &a, &c) == TIKU_EDIT_OK &&
           a == anchor && c == caret;
}

/** @brief Each style drawn under its range and nowhere else; a kind
 *         with no style, or a style refused, draws nothing. */
static void
styles(void)
{
    tiku_codeview_t *v = view_in("int x = nope;\nnext");
    tiku_surface_t *s = surface(), *plain = surface();
    tiku_rgb_t red = RED & 0xffffffu;
    int c = cell(), base = Y0 + tiku_font_mono(0)->height;
    int a = X0 + 8 * c, b = X0 + 12 * c, i, only = 1;

    mark(v, 8, 12, 0);
    tiku_codeview_paint(v, plain);
    check(tiku_codeview_set_mark(v, TIKU_EDIT_INDICATOR_KINDS,
                                 TIKU_CODEVIEW_MARK_LINE, RED) ==
              TIKU_EDIT_INVALID &&
          tiku_codeview_set_mark(v, 0, (tiku_codeview_mark_t)9, RED) ==
              TIKU_EDIT_INVALID, "IMK-001",
          "a kind or style that is not there is refused");
    tiku_codeview_paint(v, s);
    check(!memcmp(s->px, plain->px, sizeof *s->px * W * H), "IMK-001",
          "a kind with no style is not drawn");
    tiku_codeview_set_mark(v, 0, TIKU_CODEVIEW_MARK_SQUIGGLE, RED);
    tiku_codeview_paint(v, s);
    check(pixel(s, a, base) == red && pixel(s, a + 2, base + 1) == red &&
          pixel(s, a, base + 1) != red && pixel(s, a + 2, base) != red,
          "IMK-001", "a squiggle goes up and down every 2 pixels under the "
          "range");
    for (i = X0; i < X0 + 20 * c; i++)
        if ((i < a || i >= b) && (pixel(s, i, base) == red ||
                                  pixel(s, i, base + 1) == red))
            only = 0;
    check(only, "IMK-001", "and nowhere outside it");
    tiku_codeview_set_mark(v, 0, TIKU_CODEVIEW_MARK_LINE, RED);
    tiku_codeview_paint(v, s);
    for (i = a, only = 1; i < b; i++)
        if (pixel(s, i, base + 1) != red)
            only = 0;
    check(only && pixel(s, a - 1, base + 1) != red &&
          pixel(s, b, base + 1) != red, "IMK-001",
          "a line runs under the whole range");
    tiku_codeview_set_mark(v, 0, TIKU_CODEVIEW_MARK_BOX, RED);
    tiku_codeview_paint(v, s);
    check(pixel(s, a, Y0 + 3) == red && pixel(s, b - 1, Y0 + 3) == red &&
          pixel(s, a + c, Y0) == red && pixel(s, a - 1, Y0 + 3) != red,
          "IMK-001", "a box frames the range's cells");
    tiku_surface_free(s);
    tiku_surface_free(plain);
    tiku_codeview_destroy(v);
}

/** @brief A mark over a row break is drawn on both rows, where each row
 *         puts the text. */
static void
wrapped_marks(void)
{
    tiku_codeview_t *v = view_in("aaaa bbbb cccc");
    tiku_surface_t *s = surface();
    tiku_rgb_t red = RED & 0xffffffu;
    int c = cell(), step, base;
    tiku_rect_t narrow = { 10, 10, 10 + 10 * c + 2, 160 };

    tiku_codeview_set_bounds(v, narrow);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_WRAP);
    tiku_codeview_set_mark(v, 1, TIKU_CODEVIEW_MARK_LINE, RED);
    mark(v, 7, 12, 1);
    tiku_codeview_paint(v, s);
    step = tiku_codeview_row_height(v);
    base = Y0 + tiku_font_mono(0)->height + 1;
    check(pixel(s, X0 + 7 * c, base) == red &&
          pixel(s, X0 + 9 * c, base) == red &&
          pixel(s, X0 + 6 * c, base) != red &&
          pixel(s, X0 + 10 * c + 2, base) != red &&
          pixel(s, X0, base + step) == red &&
          pixel(s, X0 + c, base + step) == red &&
          pixel(s, X0 + 2 * c + 1, base + step) != red, "IMK-002",
          "the mark ends one row at the break and starts the next");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief Annotations take rows under their line while the option is on,
 *         each LF a row, drawn on a ground of their kind's colour. */
static void
annotation_rows(void)
{
    tiku_codeview_t *v = view_in("int x = nope;\nnext\nlast");
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_surface_t *s = surface(), *want = surface();
    const tiku_font_t *f = tiku_font_mono(0);
    tiku_rgb_t ground = tiku_dim(RED, TIKU_C_DOC) & 0xffffffu;
    int step = tiku_codeview_row_height(v);
    tiku_rect_t row;

    (void)tiku_editdoc_annotation_add(doc, 0, "error: nope\nhint", 0);
    check(rows_of(v) == 3 && row_at(v, 14) == 1, "ANV-001",
          "without the option annotations take no rows");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_ANNOTATIONS);
    check(rows_of(v) == 5 && row_at(v, 14) == 3 && row_at(v, 19) == 4,
          "ANV-001", "with it a line's annotation takes a row for each LF "
          "and one more");
    tiku_codeview_paint(v, s);
    check(pixel(s, X0 + 250, Y0 + step + 2) == (TIKU_C_PANEL & 0xffffffu),
          "ANV-002", "a kind with no style has the panel's ground");
    tiku_codeview_set_mark(v, 0, TIKU_CODEVIEW_MARK_SQUIGGLE, RED);
    tiku_codeview_paint(v, s);
    row = (tiku_rect_t){ X0, Y0 + step, box.w - 5, step };
    tiku_fill(want, row, ground);
    tiku_clip_set(want, row);
    tiku_text(want, f, X0 + 4, Y0 + step + f->ascent, "error: nope",
              TIKU_C_TEXT);
    tiku_clip_reset(want);
    {
        int x, y, same = 1;

        for (y = row.y; y < row.y + row.h; y++)
            for (x = row.x; x < row.x + row.w; x++)
                if (pixel(s, x, y) != pixel(want, x, y))
                    same = 0;
        check(same && pixel(s, X0 + 250, Y0 + 2 * step + 2) == ground,
              "ANV-002", "each row of an annotation is its text on its "
              "kind's colour pulled to the page");
    }
    (void)tiku_codeview_set_options(v, TIKU_CODEVIEW_ANNOTATIONS |
                                    TIKU_CODEVIEW_LINE_NUMBERS);
    check(row_at(v, 14) == 3, "ANV-001",
          "the gutter numbers text rows, the annotation's rows count");
    tiku_surface_free(s);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief Up and Down pass over annotation rows, and a press on one puts
 *         the caret at its line's end. */
static void
caret_moves(void)
{
    tiku_codeview_t *v = view_in("int x = nope;\nnext\nlast");
    int step;

    tiku_codeview_set_options(v, TIKU_CODEVIEW_ANNOTATIONS);
    (void)tiku_editdoc_annotation_add(tiku_codeview_doc(v), 0, "a\nb", 0);
    step = tiku_codeview_row_height(v);
    (void)tiku_codeview_select(v, 2, 2);
    (void)tiku_codeview_move(v, TIKU_CODEVIEW_LINE_DOWN, 0);
    check(is(v, 16, 16), "ANV-003",
          "Down from the annotated line goes to the next line's text");
    (void)tiku_codeview_move(v, TIKU_CODEVIEW_LINE_UP, 0);
    check(is(v, 2, 2), "ANV-003", "Up comes back to the annotated line");
    (void)tiku_codeview_select(v, 16, 16);
    (void)tiku_codeview_move(v, TIKU_CODEVIEW_PAGE_UP, 0);
    check(is(v, 2, 2), "ANV-003", "a page up stops on text");
    {
        tiku_event_t e;

        memset(&e, 0, sizeof e);
        e.type = TIKU_EVENT_POINTER_DOWN;
        e.button = 1;
        e.x = X0 + 2;
        e.y = Y0 + step + 2;
        (void)tiku_codeview_event(v, &e);
        e.type = TIKU_EVENT_POINTER_UP;
        (void)tiku_codeview_event(v, &e);
        check(is(v, 13, 13) && tiku_codeview_hit(v, X0 + 50,
                                                 Y0 + 2 * step + 2) == 13,
              "ANV-003", "a press on an annotation row puts the caret at "
              "its line's end");
    }
    tiku_codeview_destroy(v);
}

/** @brief The rows follow annotations added and removed, their lines
 *         through edits, and wrapping. */
static void
row_updates(void)
{
    tiku_codeview_t *v = view_in("aaaa bbbb cccc\nxy");
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    int c = cell();
    tiku_rect_t narrow = { 10, 10, 10 + 10 * c + 2, 160 };

    tiku_codeview_set_options(v, TIKU_CODEVIEW_ANNOTATIONS);
    check(rows_of(v) == 2, "ANV-004", "no annotations, a row a line");
    (void)tiku_editdoc_annotation_add(doc, 1, "note", 0);
    check(rows_of(v) == 3, "ANV-004", "an annotation added adds its row");
    (void)tiku_editdoc_annotation_add(doc, 0, "a\nb", 0);
    check(rows_of(v) == 5, "ANV-004", "and another its rows, with the "
          "first still shown");
    (void)tiku_editdoc_annotations_clear(doc, 0, 0xff);
    {
        tiku_edit_replace_t r = { { 0, 0 }, "new\n", 4 };
        tiku_edit_transaction_t t;

        memset(&t, 0, sizeof t);
        t.edits = &r;
        t.count = 1;
        (void)tiku_editdoc_apply(doc, &t);
    }
    check(rows_of(v) == 4 && row_at(v, 19) == 2, "ANV-004",
          "the annotation's row moves down with its line");
    tiku_codeview_set_bounds(v, narrow);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_ANNOTATIONS |
                              TIKU_CODEVIEW_WRAP);
    check(rows_of(v) == 5 && row_at(v, 14) == 2, "ANV-004",
          "wrapped, a line has its text's rows, then its annotation's");
    (void)tiku_editdoc_annotations_clear(doc, -1, 0xff);
    check(rows_of(v) == 4, "ANV-004", "cleared, their rows go");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_ANNOTATIONS);
    check(rows_of(v) == 3, "ANV-004",
          "and with nothing to wrap a line is a row again");
    tiku_codeview_destroy(v);
}

/** @brief A marked line's fill covers its text's rows and not its
 *         annotation's. */
static void
fills(void)
{
    tiku_codeview_t *v = view_in("one\ntwo");
    tiku_surface_t *s = surface();
    tiku_rgb_t fill = TIKU_RGB(200, 220, 255);
    int step = tiku_codeview_row_height(v);

    tiku_codeview_set_options(v, TIKU_CODEVIEW_ANNOTATIONS);
    (void)tiku_editdoc_annotation_add(tiku_codeview_doc(v), 0, "a", 0);
    (void)tiku_editdoc_marker_add(tiku_codeview_doc(v), 0, 1, NULL);
    (void)tiku_codeview_set_marker_fill(v, 1, fill);
    tiku_codeview_paint(v, s);
    check(pixel(s, X0 + 200, Y0 + 2) == (fill & 0xffffffu) &&
          pixel(s, 11, Y0 + 2) == (fill & 0xffffffu) &&
          pixel(s, X0 + 200, Y0 + step + 2) == (TIKU_C_PANEL & 0xffffffu) &&
          pixel(s, 11, Y0 + step + 2) == (TIKU_C_DOC & 0xffffffu),
          "ANV-002", "a marked line's fill leaves its annotation's rows");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief Going to the next indicator, or back to the one before, round
 *         the document's ends, by kind; with none nothing changes. */
static void
going(void)
{
    tiku_codeview_t *v = view_in("aa bb cc dd ee");

    check(tiku_codeview_goto_indicator(v, 0xff, 0) == TIKU_EDIT_NOTHING &&
          is(v, 0, 0), "IMK-003", "with none nothing changes");
    mark(v, 3, 5, 0);
    mark(v, 9, 11, 1);
    check(tiku_codeview_goto_indicator(v, 0xff, 0) == TIKU_EDIT_OK &&
          is(v, 3, 5), "IMK-003", "the next one is selected");
    check(tiku_codeview_goto_indicator(v, 0xff, 0) == TIKU_EDIT_OK &&
          is(v, 9, 11), "IMK-003", "and the one after it");
    check(tiku_codeview_goto_indicator(v, 0xff, 0) == TIKU_EDIT_OK &&
          is(v, 3, 5), "IMK-003", "past the last it starts again");
    check(tiku_codeview_goto_indicator(v, 0xff, 1) == TIKU_EDIT_OK &&
          is(v, 9, 11), "IMK-003", "back past the first it goes to the "
          "last");
    (void)tiku_codeview_select(v, 0, 0);
    check(tiku_codeview_goto_indicator(v, 0x2, 0) == TIKU_EDIT_OK &&
          is(v, 9, 11), "IMK-003", "only the kinds asked for count");
    tiku_codeview_destroy(v);
}

int
main(void)
{
    styles();
    wrapped_marks();
    annotation_rows();
    caret_moves();
    row_updates();
    fills();
    going();
    if (fails) {
        printf("FAIL: %d checks\n", fails);
        return 1;
    }
    printf("PASS: code view notes -- indicator styles, marks on wrapped "
           "rows, annotation rows and their ground, caret moves past them, "
           "rows following them, fills, going to the next\n");
    return 0;
}
