/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_paint.c - a code view's pixels, compared with the same
 * text drawn directly where the layout rules put it: tabs and control
 * names, the selection's ground and ink, the caret, scrolling, a
 * million-byte line, a doubled screen, nothing outside the bounds or
 * the caller's clip, and a recording that plays back to the same
 * pixels after a trip through its wire form.
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

/** @brief A surface of @p w by @p h at @p scale, all PAPER. */
static tiku_surface_t *
surface(int w, int h, int scale)
{
    tiku_surface_t *s = tiku_surface_new(w, h, PAPER);

    if (s == NULL)
        exit(1);
    if (scale > 1)
        (void)tiku_surface_rescale(s, scale, PAPER);
    return s;
}

/** @brief Whether two surfaces hold the same pixels. */
static int
same(const tiku_surface_t *a, const tiku_surface_t *b)
{
    int sc = a->scale > 1 ? a->scale : 1;

    return a->w == b->w && a->h == b->h && a->scale == b->scale &&
           memcmp(a->px, b->px, (size_t)a->w * a->h * (size_t)(sc * sc) *
                  sizeof *a->px) == 0;
}

/** @brief A document holding @p n bytes of @p s, and a view of it in
 *         @p r; the view holds the only reference. */
static tiku_codeview_t *
view_of_n(const char *s, size_t n, tiku_rect_t r)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, n) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
        exit(1);
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, r);
    return v;
}

static tiku_codeview_t *
view_of(const char *s)
{
    return view_of_n(s, strlen(s), box);
}

/** @brief The text area's clip on the reference. */
static void
clip_text(tiku_surface_t *s)
{
    tiku_rect_t r = { x0, box.y, box.x + box.w - x0, box.h };

    tiku_clip_set(s, r);
}

/** @brief A control character's name box, as the rules draw it. */
static void
name_box(tiku_surface_t *s, int x, int y, const char *name,
         tiku_rgb_t ink, tiku_rgb_t ground)
{
    const tiku_font_t *f = tiku_font_mono(0);
    tiku_rect_t r = { x + 1, y + 1, tiku_text_width(f, name) + 2,
                      f->height - 1 };

    tiku_fill(s, r, tiku_dim(ink, ground));
    tiku_text(s, f, x + 2, y + asc, name, ground);
}

/** @brief The sample, unselected and without a caret, as the rules
 *         draw it. */
static void
reference(tiku_surface_t *s)
{
    const tiku_font_t *f = tiku_font_mono(0);

    tiku_fill(s, box, TIKU_C_DOC);
    clip_text(s);
    tiku_text(s, f, x0, y0 + asc, "Hello", TIKU_C_TEXT);
    tiku_text(s, f, x0 + 8 * cell, y0 + asc, "world", TIKU_C_TEXT);
    name_box(s, x0, y0 + step, "^A", TIKU_C_TEXT, TIKU_C_DOC);
    tiku_text(s, f, x0 + ctl, y0 + step + asc, "z", TIKU_C_TEXT);
    tiku_text(s, f, x0, y0 + 2 * step + asc, "second", TIKU_C_TEXT);
    tiku_clip_reset(s);
}

/** @brief Text at its stops, control names, and nothing past the box. */
static void
text_and_bounds(void)
{
    tiku_codeview_t *v = view_of(sample);
    tiku_surface_t *got = surface(W, H, 1), *want = surface(W, H, 1);
    tiku_rect_t corner = { 0, 0, 100, 60 };
    int x, y, outside = 1;

    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, got);
    reference(want);
    check(same(got, want), "PNT-001",
          "text, a tab and a control name land where the layout rules "
          "put them, in the document's ink on its ground");
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            int in = x >= box.x && x < box.x + box.w && y >= box.y &&
                     y < box.y + box.h;

            if (!in && tiku_peek(got, x, y) != PAPER)
                outside = 0;
        }
    }
    check(outside && got->clip.w == W && got->clip.h == H, "PNT-002",
          "nothing is drawn outside the bounds, and the clip is put back");

    tiku_surface_free(got);
    got = surface(W, H, 1);
    tiku_clip_set(got, corner);
    tiku_codeview_paint(v, got);
    outside = 1;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            int in = x < corner.w && y < corner.h && x >= box.x &&
                     y >= box.y;

            if (tiku_peek(got, x, y) != (in ? tiku_peek(want, x, y) : PAPER))
                outside = 0;
        }
    }
    check(outside && got->clip.x == corner.x && got->clip.y == corner.y &&
          got->clip.w == corner.w && got->clip.h == corner.h, "PNT-002",
          "a caller's clip holds, and is the clip afterwards");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief The selection: its ground behind its text, its ink on the
 *         selected characters only, and a selected line break shown as
 *         a space's width after its line. */
static void
selection(void)
{
    tiku_codeview_t *v = view_of(sample);
    tiku_surface_t *got = surface(W, H, 1), *want = surface(W, H, 1);
    const tiku_font_t *f = tiku_font_mono(0);
    tiku_rect_t ground = { x0 + cell, y0, 8 * cell, step };

    tiku_codeview_set_focus(v, 0);
    tiku_codeview_select(v, 1, 7);
    tiku_codeview_paint(v, got);
    tiku_fill(want, box, TIKU_C_DOC);
    clip_text(want);
    tiku_fill(want, ground, TIKU_C_SELECT);
    tiku_text(want, f, x0, y0 + asc, "H", TIKU_C_TEXT);
    tiku_text(want, f, x0 + cell, y0 + asc, "ello", TIKU_C_SELTEXT);
    tiku_text(want, f, x0 + 8 * cell, y0 + asc, "w", TIKU_C_SELTEXT);
    tiku_text(want, f, x0 + 9 * cell, y0 + asc, "orld", TIKU_C_TEXT);
    name_box(want, x0, y0 + step, "^A", TIKU_C_TEXT, TIKU_C_DOC);
    tiku_text(want, f, x0 + ctl, y0 + step + asc, "z", TIKU_C_TEXT);
    tiku_text(want, f, x0, y0 + 2 * step + asc, "second", TIKU_C_TEXT);
    tiku_clip_reset(want);
    check(same(got, want), "PNT-003",
          "a selection inside a line: its ground behind, its ink on the "
          "selected characters, a tab's width included");

    tiku_surface_free(got);
    tiku_surface_free(want);
    got = surface(W, H, 1);
    want = surface(W, H, 1);
    tiku_codeview_select(v, 13, 2);
    tiku_codeview_paint(v, got);
    tiku_fill(want, box, TIKU_C_DOC);
    clip_text(want);
    ground.x = x0 + 2 * cell;
    ground.w = 12 * cell;
    tiku_fill(want, ground, TIKU_C_SELECT);
    ground.x = x0;
    ground.y = y0 + step;
    ground.w = ctl;
    tiku_fill(want, ground, TIKU_C_SELECT);
    tiku_text(want, f, x0, y0 + asc, "He", TIKU_C_TEXT);
    tiku_text(want, f, x0 + 2 * cell, y0 + asc, "llo", TIKU_C_SELTEXT);
    tiku_text(want, f, x0 + 8 * cell, y0 + asc, "world", TIKU_C_SELTEXT);
    name_box(want, x0, y0 + step, "^A", TIKU_C_SELTEXT, TIKU_C_SELECT);
    tiku_text(want, f, x0 + ctl, y0 + step + asc, "z", TIKU_C_TEXT);
    tiku_text(want, f, x0, y0 + 2 * step + asc, "second", TIKU_C_TEXT);
    tiku_clip_reset(want);
    check(same(got, want), "PNT-003",
          "a selection over a line break: a space's width after the line, "
          "and a selected control name inverted");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief The caret: a line the row's height at its point, only while
 *         the view has focus. */
static void
caret(void)
{
    tiku_codeview_t *v = view_of(sample);
    tiku_surface_t *got = surface(W, H, 1), *want = surface(W, H, 1);
    int gap = y0 + step - 1;

    tiku_codeview_select(v, 3, 3);
    tiku_codeview_paint(v, got);
    reference(want);
    tiku_vline(want, x0 + 3 * cell, y0, step, TIKU_C_TEXT);
    check(same(got, want), "PNT-004",
          "a focused view draws its caret down the whole row");
    tiku_codeview_select(v, 21, 21);
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, got);
    check(tiku_peek(got, x0 + 3 * cell, gap) == TIKU_C_DOC &&
          tiku_peek(got, x0 + 6 * cell, y0 + 3 * step - 1) == TIKU_C_DOC,
          "PNT-004", "an unfocused view draws none");
    tiku_codeview_set_focus(v, 1);
    tiku_codeview_paint(v, got);
    check(tiku_peek(got, x0 + 6 * cell, y0 + 3 * step - 1) == TIKU_C_TEXT,
          "PNT-004", "and at a line's end it stands after the text");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief Scrolling moves the picture and nothing else. */
static void
scrolling(void)
{
    tiku_codeview_t *v = view_of("Hello\tworld\n\x01z\nsecond\nthird\n"
                                 "fourth\nfifth\nsixth\n\tseventh\n"
                                 "eighth\nninth\n");
    tiku_surface_t *flat = surface(W, H, 1), *moved = surface(W, H, 1);
    int x, y, ok = 1;

    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, flat);
    tiku_codeview_scroll_to(v, 1, 3 * cell);
    tiku_codeview_paint(v, moved);
    check(tiku_codeview_top(v) == 1 && tiku_codeview_left(v) == 3 * cell,
          "PNT-005", "a view can be scrolled down a line and right three "
          "columns");
    for (y = y0; y < box.y + box.h - step; y++) {
        for (x = x0; x < box.x + box.w - 3 * cell; x++) {
            if (tiku_peek(moved, x, y) !=
                tiku_peek(flat, x + 3 * cell, y + step))
                ok = 0;
        }
    }
    check(ok, "PNT-005", "and shows the same pixels a row up and three "
          "columns left");
    tiku_surface_free(flat);
    tiku_surface_free(moved);
    tiku_codeview_destroy(v);
}

/** @brief A line of a million bytes, scrolled to its middle on a wide
 *         surface, shows exactly the text there. */
static void
long_line(void)
{
    size_t n = 1000000, i;
    char *s = malloc(n + 6), shown[400];
    const int wide = 2600;
    tiku_rect_t r = { 0, 0, wide, 40 };
    tiku_surface_t *got = surface(wide, 40, 1), *want = surface(wide, 40, 1);
    tiku_rect_t text = { 5, 0, wide - 5, 40 };
    tiku_codeview_t *v;

    if (s == NULL)
        exit(1);
    for (i = 0; i < n; i++)
        s[i] = (char)('0' + i % 10);
    memcpy(s + n, "\nnext", 5);
    v = view_of_n(s, n + 5, r);
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_scroll_to(v, 0, 500003 * (int64_t)cell);
    tiku_codeview_paint(v, got);
    memcpy(shown, s + 500003, sizeof shown - 1);
    shown[sizeof shown - 1] = '\0';
    tiku_fill(want, r, TIKU_C_DOC);
    tiku_clip_set(want, text);
    tiku_text(want, tiku_font_mono(0), 5, 3 + asc, shown, TIKU_C_TEXT);
    tiku_clip_reset(want);
    check((wide - 5) / cell > 256 && same(got, want), "PNT-006",
          "the middle of a million-byte line shows the text there, "
          "unbroken across more than 256 bytes of it");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
    free(s);
}

/** @brief On a doubled screen the same logical layout is drawn. */
static void
doubled(void)
{
    tiku_codeview_t *v = view_of(sample);
    tiku_surface_t *got = surface(W, H, 2), *want = surface(W, H, 2);

    tiku_codeview_set_focus(v, 0);
    tiku_codeview_paint(v, got);
    reference(want);
    check(same(got, want), "PNT-007",
          "a doubled surface gets the same layout, drawn at its scale");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief A recording surface carries the whole view: the list has no
 *         misses and plays back, after flattening and reading back as a
 *         wire would carry it, to the pixels painted directly. */
static void
recorded(void)
{
    tiku_codeview_t *v = view_of(sample);
    tiku_surface_t *got = surface(W, H, 1), *played = surface(W, H, 1);
    tiku_dl_t *dl = tiku_dl_new(), *back = NULL;
    size_t size, wrote = 0, read = 0;
    void *flat;

    if (dl == NULL)
        exit(1);
    tiku_codeview_select(v, 13, 2);
    got->record = dl;
    tiku_codeview_paint(v, got);
    got->record = NULL;
    size = tiku_dl_flat_size(dl);
    flat = malloc(size);
    if (flat != NULL && tiku_dl_flatten(dl, flat, size, &wrote))
        back = tiku_dl_unflatten(flat, wrote, &read);
    check(tiku_dl_misses(dl) == 0 && tiku_dl_count(dl) > 0, "PNT-008",
          "everything a view paints is a command a list can carry");
    check(back != NULL && read == wrote &&
          tiku_dl_play(back, played) > 0 && same(got, played), "PNT-008",
          "and the list, sent and read back, plays to the same pixels");
    free(flat);
    tiku_dl_free(dl);
    tiku_dl_free(back);
    tiku_surface_free(got);
    tiku_surface_free(played);
    tiku_codeview_destroy(v);
}

/** @brief Bounds too small for the margins, empty, or partly off the
 *         surface paint only what is inside them. */
static void
small_bounds(void)
{
    tiku_codeview_t *v = view_of(sample);
    tiku_surface_t *s = surface(W, H, 1);
    tiku_rect_t tiny = { 5, 5, 3, 2 }, none = { 5, 5, 0, 0 };
    tiku_rect_t hanging = { W - 40, H - 30, 100, 100 };
    int x, y, ok = 1;

    tiku_codeview_set_bounds(v, tiny);
    tiku_codeview_paint(v, s);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            int in = x >= 5 && x < 8 && y >= 5 && y < 7;

            if (tiku_peek(s, x, y) != (in ? TIKU_C_DOC : PAPER))
                ok = 0;
        }
    }
    tiku_codeview_set_bounds(v, none);
    tiku_codeview_paint(v, s);
    tiku_codeview_set_bounds(v, hanging);
    tiku_codeview_paint(v, s);
    check(ok && tiku_peek(s, W - 1, H - 1) == TIKU_C_DOC &&
          tiku_peek(s, W - 41, H - 1) == PAPER && s->clip.w == W &&
          s->clip.h == H, "PNT-009",
          "a box smaller than its margins, an empty one, and one hanging "
          "off the surface paint only inside themselves");
    tiku_surface_free(s);
    tiku_codeview_destroy(v);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    metrics();
    text_and_bounds();
    selection();
    caret();
    scrolling();
    long_line();
    doubled();
    recorded();
    small_bounds();
    printf("%s\n", fails ? "FAIL: code view painting" :
           "PASS: code view painting -- text at its stops, selection, "
           "caret, scrolling, long lines, doubled screens, recordings");
    return fails != 0;
}
