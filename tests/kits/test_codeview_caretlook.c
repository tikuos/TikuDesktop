/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_caretlook.c - how a code view shows its caret: the main
 * caret's line filled under everything else, and a block caret over the
 * character after each caret, compared with the same calls made
 * directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"
#include "tiku_dl.h"

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

#define W 320
#define H 140
#define PAPER TIKU_RGB(255, 0, 255)

static const tiku_rect_t box = { 10, 10, 300, 110 };
static const char *const sample = "Hello\tworld\n\x01z\nsecond";

/* The layout rules, worked out here rather than asked of the view. */
static int x0, y0, cell, step, asc, ctl;

static void
metrics(void)
{
    const tiku_font_t *f = tiku_font_mono(0);

    x0 = box.x + 5;
    y0 = box.y + 3;
    cell = tiku_font_mono_cell(0);
    step = f->height + 2;
    asc = f->ascent;
    ctl = tiku_text_width(f, "^A") + 4;
}

/** @brief A surface of @p w by @p h, all PAPER. */
static tiku_surface_t *
surface(int w, int h)
{
    tiku_surface_t *s = tiku_surface_new(w, h, PAPER);

    if (s == NULL)
        exit(1);
    return s;
}

/** @brief Whether two surfaces hold the same pixels. */
static int
same(const tiku_surface_t *a, const tiku_surface_t *b)
{
    return a->w == b->w && a->h == b->h &&
           memcmp(a->px, b->px, (size_t)a->w * a->h * sizeof *a->px) == 0;
}

/** @brief A view of a document holding @p s, in @p r. */
static tiku_codeview_t *
view_in(const char *s, tiku_rect_t r)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
        exit(1);
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, r);
    return v;
}

/** @brief The pixel at @p x, @p y without its alpha byte. */
static tiku_rgb_t
pixel(const tiku_surface_t *s, int x, int y)
{
    return tiku_peek(s, x, y) & 0xffffffu;
}

/** @brief Whether the pixel at @p x, @p y is @p c. */
static int
is(const tiku_surface_t *s, int x, int y, tiku_rgb_t c)
{
    return pixel(s, x, y) == (c & 0xffffffu);
}

/** @brief Whether @p v painted while recording leaves a list with no
 *         misses that, flattened and read back, plays to the same
 *         pixels. */
static int
plays_back(tiku_codeview_t *v)
{
    tiku_surface_t *got = surface(W, H), *played = surface(W, H);
    tiku_dl_t *dl = tiku_dl_new(), *back = NULL;
    size_t size, wrote = 0, read = 0;
    void *flat;
    int ok;

    if (dl == NULL)
        exit(1);
    got->record = dl;
    tiku_codeview_paint(v, got);
    got->record = NULL;
    size = tiku_dl_flat_size(dl);
    flat = malloc(size);
    if (flat != NULL && tiku_dl_flatten(dl, flat, size, &wrote))
        back = tiku_dl_unflatten(flat, wrote, &read);
    ok = tiku_dl_misses(dl) == 0 && back != NULL && read == wrote &&
         tiku_dl_play(back, played) > 0 && same(got, played);
    free(flat);
    tiku_dl_free(dl);
    tiku_dl_free(back);
    tiku_surface_free(got);
    tiku_surface_free(played);
    return ok;
}

/** @brief The view painted as it is and unfocused, into @p got and
 *         @p want, the caret line and the caret left to the caller. */
static void
both(tiku_codeview_t *v, tiku_surface_t *got, tiku_surface_t *want)
{
    tiku_codeview_paint(v, got);
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, want);
    tiku_codeview_set_focus(v, 1);
}

/** @brief A block over @p w pixels at @p x, @p y drawn on @p s as the
 *         rules draw one, @p glyph in the ground's colour, inside the
 *         text's clip. */
static void
block(tiku_surface_t *s, int x, int y, int w, const char *glyph)
{
    tiku_rect_t clip = { x0, box.y, box.x + box.w - x0, box.h };
    tiku_rect_t r = { x, y, w, step };

    tiku_clip_set(s, clip);
    tiku_fill(s, r, TIKU_C_TEXT);
    if (glyph != NULL)
        tiku_text(s, tiku_font_mono(0), x, y + asc, glyph, TIKU_C_DOC);
    tiku_clip_reset(s);
}

/** @brief The main caret's line is filled across the text, under its
 *         text and selection, while the view has the keys. */
static void
caret_line(void)
{
    tiku_codeview_t *v = view_in("one\ntwo\nthree", box);
    tiku_surface_t *s = surface(W, H);
    int blank = x0 + 200;

    tiku_codeview_set_options(v, TIKU_CODEVIEW_CARET_LINE);
    (void)tiku_codeview_select(v, 5, 5);
    tiku_codeview_paint(v, s);
    check(is(s, blank, y0 + step + 2, TIKU_C_NOTE) &&
          is(s, x0 + 3 * cell + 1, y0 + step + 1, TIKU_C_NOTE) &&
          is(s, box.x + 1, y0 + step + 1, TIKU_C_NOTE) &&
          is(s, blank, y0 + step + step - 1, TIKU_C_NOTE),
          "CLN-001", "the caret's line is filled across the text's area, "
          "the whole row, in the theme's note colour");
    check(is(s, blank, y0 + 2, TIKU_C_DOC) &&
          is(s, blank, y0 + 2 * step + 2, TIKU_C_DOC),
          "CLN-001", "and the other lines are not");
    check(!is(s, x0 + 1, y0 + step + asc - 3, TIKU_C_NOTE) ||
          !is(s, x0 + 2, y0 + step + asc - 3, TIKU_C_NOTE) ||
          !is(s, x0 + 3, y0 + step + asc - 3, TIKU_C_NOTE),
          "CLN-001", "its text is drawn over the fill");
    (void)tiku_codeview_select(v, 4, 7);
    tiku_codeview_paint(v, s);
    check(is(s, x0 + 1, y0 + step + 1, TIKU_C_SELECT) &&
          is(s, blank, y0 + step + 2, TIKU_C_NOTE),
          "CLN-001", "a selection on it is drawn over the fill");
    (void)tiku_codeview_select(v, 1, 10);
    tiku_codeview_paint(v, s);
    check(is(s, blank, y0 + 2 * step + 2, TIKU_C_NOTE) &&
          is(s, blank, y0 + 2, TIKU_C_DOC), "CLN-001",
          "a selection over lines fills the caret's line, not the "
          "anchor's");
    (void)tiku_codeview_select(v, 5, 5);
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, s);
    check(is(s, blank, y0 + step + 2, TIKU_C_DOC), "CLN-001",
          "a view without the keys fills no line");
    tiku_codeview_set_focus(v, 1);
    tiku_codeview_set_options(v, 0);
    tiku_codeview_paint(v, s);
    check(is(s, blank, y0 + step + 2, TIKU_C_DOC), "CLN-001",
          "nor does a view not asked to");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_CARET_LINE |
                              TIKU_CODEVIEW_LINE_NUMBERS);
    tiku_codeview_paint(v, s);
    check(!is(s, box.x + 2, y0 + step + 2, TIKU_C_NOTE) &&
          is(s, blank, y0 + step + 2, TIKU_C_NOTE), "CLN-001",
          "the gutter is left as it is");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief The fill's colour can be set, and a marker's fill or the
 *         execution line's covers it. */
static void
caret_line_fills(void)
{
    tiku_codeview_t *v = view_in("one\ntwo\nthree", box);
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_surface_t *s = surface(W, H);
    tiku_rgb_t mine = TIKU_RGB(240, 230, 250), mark = TIKU_RGB(200, 220, 255);
    int blank = x0 + 200;

    tiku_codeview_set_options(v, TIKU_CODEVIEW_CARET_LINE);
    tiku_codeview_set_caret_line_fill(v, mine);
    (void)tiku_codeview_select(v, 5, 5);
    tiku_codeview_paint(v, s);
    check(is(s, blank, y0 + step + 2, mine), "CLN-002",
          "a colour set is the fill");
    (void)tiku_editdoc_marker_add(doc, 1, 2, NULL);
    (void)tiku_codeview_set_marker_fill(v, 2, mark);
    tiku_codeview_paint(v, s);
    check(is(s, blank, y0 + step + 2, mark), "CLN-002",
          "a marker's fill covers it");
    (void)tiku_editdoc_execution_set(doc, 1);
    tiku_codeview_paint(v, s);
    check(is(s, blank, y0 + step + 2, TIKU_RGB(211, 238, 185)), "CLN-002",
          "and so does the execution line's");
    tiku_codeview_clear_marker_fill(v, 2);
    tiku_editdoc_execution_clear(doc);
    tiku_codeview_paint(v, s);
    check(is(s, blank, y0 + step + 2, mine), "CLN-002",
          "with neither, the caret line's shows again");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief Only the main caret's line, every text row of it while lines
 *         wrap, and none of its annotation's rows. */
static void
caret_line_rows(void)
{
    tiku_rect_t narrow = { 10, 10, 64, 120 };
    tiku_codeview_t *v = view_in("one\ntwo\nthree", box);
    tiku_edit_selection_t two[2] = { { 1, 1 }, { 10, 10 } };
    tiku_surface_t *s = surface(W, H);
    int blank = x0 + 200, last;

    tiku_codeview_set_options(v, TIKU_CODEVIEW_CARET_LINE);
    (void)tiku_codeview_select_many(v, two, 2, 1);
    tiku_codeview_paint(v, s);
    check(is(s, blank, y0 + 2 * step + 2, TIKU_C_NOTE) &&
          is(s, blank, y0 + 2, TIKU_C_DOC), "CLN-003",
          "of several carets only the main one's line is filled");
    (void)tiku_codeview_select_many(v, two, 2, 0);
    tiku_codeview_paint(v, s);
    check(is(s, blank, y0 + 2, TIKU_C_NOTE) &&
          is(s, blank, y0 + 2 * step + 2, TIKU_C_DOC), "CLN-003",
          "and the fill follows the main one");
    tiku_codeview_destroy(v);

    v = view_in("aaaa bbbb cccc\nnext", narrow);
    last = narrow.x + narrow.w - 2;
    tiku_codeview_set_options(v, TIKU_CODEVIEW_CARET_LINE |
                              TIKU_CODEVIEW_WRAP |
                              TIKU_CODEVIEW_ANNOTATIONS);
    (void)tiku_editdoc_annotation_add(tiku_codeview_doc(v), 0, "note", 0);
    (void)tiku_codeview_select(v, 7, 7);
    tiku_codeview_paint(v, s);
    check(is(s, last, y0 + 2, TIKU_C_NOTE) &&
          is(s, last, y0 + step + 2, TIKU_C_NOTE) &&
          is(s, last, y0 + 2 * step + 2, TIKU_C_NOTE), "CLN-003",
          "a wrapped line is filled on every row of its text");
    check(!is(s, last, y0 + 3 * step + 2, TIKU_C_NOTE) &&
          !is(s, narrow.x + 1, y0 + 3 * step + 2, TIKU_C_NOTE) &&
          is(s, last, y0 + 4 * step + 2, TIKU_C_DOC), "CLN-003",
          "but not on its annotation's row, nor the next line's");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

/** @brief A block caret covers the character after the caret, which is
 *         drawn over it in the ground's colour. */
static void
block_glyph(void)
{
    tiku_codeview_t *v = view_in(sample, box);
    tiku_surface_t *got = surface(W, H), *want = surface(W, H);

    tiku_codeview_set_options(v, TIKU_CODEVIEW_BLOCK_CARET);
    (void)tiku_codeview_select(v, 1, 1);
    both(v, got, want);
    block(want, x0 + cell, y0, cell, "e");
    check(same(got, want), "BLK-001",
          "a block the character's width and the row's height, in the "
          "text's ink, the character over it in the ground's");
    (void)tiku_codeview_select(v, 3, 1);
    both(v, got, want);
    block(want, x0 + cell, y0, cell, "e");
    check(same(got, want), "BLK-001",
          "the block stands where the caret is, not at the selection's "
          "end");
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, got);
    tiku_codeview_set_focus(v, 1);
    tiku_codeview_set_options(v, 0);
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, want);
    tiku_codeview_set_focus(v, 1);
    check(same(got, want), "BLK-001",
          "a view without the keys draws no block");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_BLOCK_CARET |
                              TIKU_CODEVIEW_CARET_LINE);
    check(plays_back(v), "BLK-001",
          "a recording carries the block and the caret line, and plays "
          "to the pixels painted");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief A tab's block is its drawn width, a control character's its
 *         name's cell, and a line's end or the document's end a space's
 *         width. */
static void
block_widths(void)
{
    tiku_codeview_t *v = view_in(sample, box);
    tiku_surface_t *got = surface(W, H), *want = surface(W, H);
    int space = tiku_text_advance(tiku_font_mono(0), ' ');

    tiku_codeview_set_options(v, TIKU_CODEVIEW_BLOCK_CARET);
    (void)tiku_codeview_select(v, 5, 5);
    both(v, got, want);
    block(want, x0 + 5 * cell, y0, 3 * cell, NULL);
    check(same(got, want), "BLK-002", "a tab's block is the tab's width");
    (void)tiku_codeview_select(v, 12, 12);
    both(v, got, want);
    block(want, x0, y0 + step, ctl, NULL);
    tiku_text(want, tiku_font_mono(0), x0 + 2, y0 + step + asc, "^A",
              TIKU_C_DOC);
    check(same(got, want), "BLK-002",
          "a control character's is its name's cell, the name over it");
    (void)tiku_codeview_select(v, 11, 11);
    both(v, got, want);
    block(want, x0 + 13 * cell, y0, space, NULL);
    check(same(got, want), "BLK-002", "a line's end is a space's width");
    (void)tiku_codeview_select(v, 21, 21);
    both(v, got, want);
    block(want, x0 + 6 * cell, y0 + 2 * step, space, NULL);
    check(same(got, want), "BLK-002", "and so is the document's end");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief Every caret has its block; where a row breaks the block covers
 *         the next row's first character, or stands a space wide at the
 *         row's end when the caret is shown there. */
static void
block_rows(void)
{
    tiku_rect_t narrow = { 10, 10, 64, 120 };
    tiku_codeview_t *v = view_in(sample, box);
    tiku_edit_selection_t two[2] = { { 1, 1 }, { 17, 17 } };
    tiku_surface_t *got = surface(W, H), *want = surface(W, H);
    int space = tiku_text_advance(tiku_font_mono(0), ' ');

    tiku_codeview_set_options(v, TIKU_CODEVIEW_BLOCK_CARET);
    (void)tiku_codeview_select_many(v, two, 2, 0);
    both(v, got, want);
    block(want, x0 + cell, y0, cell, "e");
    block(want, x0 + 2 * cell, y0 + 2 * step, cell, "c");
    check(same(got, want), "BLK-003", "each caret has its block");
    tiku_codeview_destroy(v);

    v = view_in("aaaa bbbb cccc", narrow);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_BLOCK_CARET |
                              TIKU_CODEVIEW_WRAP);
    (void)tiku_codeview_select(v, 5, 5);
    both(v, got, want);
    block(want, x0, y0 + step, cell, "b");
    check(same(got, want), "BLK-003",
          "a caret where a row breaks covers the next row's first "
          "character");
    (void)tiku_codeview_move(v, TIKU_CODEVIEW_LINE_UP, 0);
    (void)tiku_codeview_move(v, TIKU_CODEVIEW_LINE_END, 0);
    both(v, got, want);
    block(want, x0 + 5 * cell, y0, space, NULL);
    check(same(got, want), "BLK-003",
          "and shown at the row's end it is a space wide there");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    metrics();
    caret_line();
    caret_line_fills();
    caret_line_rows();
    block_glyph();
    block_widths();
    block_rows();
    printf("%s\n", fails ? "FAIL: code view caret looks" :
           "PASS: code view caret looks -- the caret line under fills and "
           "text, block carets over glyphs, tabs, names and line ends");
    return fails != 0;
}
