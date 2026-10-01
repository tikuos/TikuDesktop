/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_display.c - what a code view shows besides the text: the
 * line-number gutter, the marks in spaces and tabs, and the scrollbars,
 * drawn where the rules put them and worked by the pointer, the wheel
 * and held arrows; and the widest line, kept through edits, face and tab
 * changes, and a table that could not be allocated.
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

/** @brief Whether two surfaces hold the same pixels. */
static int
same(const tiku_surface_t *a, const tiku_surface_t *b)
{
    return memcmp(a->px, b->px, (size_t)a->w * a->h * sizeof *a->px) == 0;
}

/** @brief A view of a new document holding @p s, in @p r; the view
 *         holds the only reference. */
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

/** @brief @p n lines, each "line" and its number. */
static char *
numbered(int n)
{
    char *s = malloc((size_t)n * 16 + 1);
    int i;

    if (s == NULL)
        exit(1);
    s[0] = '\0';
    for (i = 0; i < n; i++)
        sprintf(s + strlen(s), "line %d%s", i + 1, i + 1 < n ? "\n" : "");
    return s;
}

/** @brief Where the caret at 0 is drawn: line x 0 and the first row. */
static void
origin(tiku_codeview_t *v, int64_t *x, int64_t *y)
{
    tiku_codeview_point(v, 0, x, y);
    *x += tiku_codeview_left(v);
}

/** @brief The gutter: as wide as its digits and twelve pixels, the text
 *         four pixels past it, and the width following the line count. */
static void
gutter_geometry(void)
{
    int digit = tiku_text_advance(tiku_font_mono(0), '0');
    char *nine = numbered(9), *ten = numbered(10), *hundred = numbered(100);
    tiku_codeview_t *v = view_in(nine, box);
    int64_t x, y;

    origin(v, &x, &y);
    check(x == box.x + 5 && tiku_codeview_options(v) == 0, "DSP-001",
          "a new view has no gutter: its text starts 5 pixels in");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_LINE_NUMBERS);
    origin(v, &x, &y);
    check(x == box.x + 12 + digit + 4, "DSP-001",
          "with line numbers, nine lines take one digit's gutter");
    tiku_codeview_destroy(v);
    v = view_in(ten, box);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_LINE_NUMBERS);
    origin(v, &x, &y);
    check(x == box.x + 12 + 2 * digit + 4, "DSP-001", "ten take two");
    tiku_codeview_destroy(v);
    v = view_in(hundred, box);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_LINE_NUMBERS);
    origin(v, &x, &y);
    check(x == box.x + 12 + 3 * digit + 4, "DSP-001", "a hundred three");
    tiku_codeview_set_options(v, 0);
    origin(v, &x, &y);
    check(x == box.x + 5, "DSP-001", "and turning them off takes it away");
    tiku_codeview_destroy(v);
    free(nine);
    free(ten);
    free(hundred);
}

/** @brief The gutter drawn: shaded, ruled, the numbers of the shown
 *         lines set right on the text's baseline in the faint ink. */
static void
gutter_paint(void)
{
    char *text = numbered(30);
    tiku_codeview_t *v = view_in(text, box);
    tiku_surface_t *got = surface(), *want = surface();
    const tiku_font_t *f = tiku_font_mono(0), *small = tiku_font_at(11);
    int digit = tiku_text_advance(f, '0'), gutter = 12 + 2 * digit;
    int step = f->height + 2, i, y;
    char n[24];

    tiku_codeview_set_options(v, TIKU_CODEVIEW_LINE_NUMBERS);
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_scroll_to(v, 4, 0);
    tiku_codeview_paint(v, got);
    tiku_fill(want, (tiku_rect_t){ box.x + gutter, box.y, box.w - gutter,
                                   box.h }, TIKU_C_DOC);
    tiku_fill(want, (tiku_rect_t){ box.x, box.y, gutter, box.h },
              tiku_tint(TIKU_C_DOC, 1.04f));
    tiku_vline(want, box.x + gutter - 1, box.y, box.h,
               tiku_tint(TIKU_C_DOC, 1.12f));
    tiku_clip_set(want, box);
    for (i = 0, y = box.y + 3; y < box.y + box.h; i++, y += step) {
        snprintf(n, sizeof n, "%d", i + 5);
        tiku_text(want, small, box.x + gutter - 6 - tiku_text_width(small, n),
                  y + f->ascent, n, tiku_ink(TIKU_INK_FAINT));
    }
    tiku_clip_set(want, (tiku_rect_t){ box.x + gutter + 4, box.y,
                                       box.w - gutter - 4, box.h });
    for (i = 0, y = box.y + 3; y < box.y + box.h; i++, y += step) {
        snprintf(n, sizeof n, "line %d", i + 5);
        tiku_text(want, f, box.x + gutter + 4, y + f->ascent, n,
                  TIKU_C_TEXT);
    }
    tiku_clip_reset(want);
    check(same(got, want), "DSP-002",
          "the gutter is shaded and ruled, and numbers the shown lines from "
          "the first row's, right against the rule on the text's baseline");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
    free(text);
}

/** @brief A space shows a dot and a tab a dash, in the middle of their
 *         cells, faint against their ground. */
static void
invisibles(void)
{
    tiku_codeview_t *v = view_in("a b\tc", box);
    tiku_surface_t *got = surface(), *want = surface();
    const tiku_font_t *f = tiku_font_mono(0);
    int c = tiku_font_mono_cell(0), x0 = box.x + 5, y0 = box.y + 3;
    int mid = y0 + f->height / 2;

    tiku_codeview_set_focus(v, 0);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_INVISIBLES);
    tiku_codeview_paint(v, got);
    tiku_fill(want, box, TIKU_C_DOC);
    tiku_clip_set(want, (tiku_rect_t){ x0, box.y, box.w - 5, box.h });
    tiku_hline(want, x0 + c + c / 2, mid, 1,
               tiku_dim(TIKU_C_TEXT, TIKU_C_DOC));
    tiku_text(want, f, x0, y0 + f->ascent, "a b", TIKU_C_TEXT);
    tiku_hline(want, x0 + 3 * c + c / 2, mid, 3,
               tiku_dim(TIKU_C_TEXT, TIKU_C_DOC));
    tiku_text(want, f, x0 + 4 * c, y0 + f->ascent, "c", TIKU_C_TEXT);
    tiku_clip_reset(want);
    check(same(got, want), "DSP-003",
          "a space shows a dot and a tab a three-pixel dash in the middle "
          "of its cell, in the faint ink");
    tiku_codeview_select(v, 1, 4);
    tiku_codeview_paint(v, got);
    check(tiku_peek(got, x0 + c + c / 2, mid) ==
          tiku_dim(TIKU_C_SELTEXT, TIKU_C_SELECT) &&
          tiku_peek(got, x0 + 3 * c + c / 2 + 1, mid) ==
          tiku_dim(TIKU_C_SELTEXT, TIKU_C_SELECT), "DSP-003",
          "and selected, faint against the selection");
    tiku_codeview_set_options(v, 0);
    tiku_codeview_paint(v, got);
    check(tiku_peek(got, x0 + c + c / 2, mid) == TIKU_C_SELECT, "DSP-003",
          "without the option there is no mark");
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_codeview_destroy(v);
}

/** @brief A text of @p n lines of @p w x's. */
static char *
block(int n, int w)
{
    char *s = malloc((size_t)n * (size_t)(w + 1) + 1);
    int i;

    if (s == NULL)
        exit(1);
    for (i = 0; i < n; i++) {
        memset(s + i * (w + 1), 'x', (size_t)w);
        s[i * (w + 1) + w] = '\n';
    }
    s[n * (w + 1) - 1] = '\0';
    return s;
}

/** @brief The scrollbars: down the right and along the foot, thumbs in
 *         thousandths of the scroll, the corner filled, the text kept
 *         clear of them, and the whole recordable. */
static void
bars_paint(void)
{
    char *text = block(100, 80);
    tiku_codeview_t *v = view_in(text, box);
    tiku_surface_t *got = surface(), *want = surface(), *played = surface();
    int c = tiku_font_mono_cell(0), step = tiku_codeview_row_height(v);
    int64_t rows, width = 80 * c + 12, shown = box.w - 15 - 5;
    tiku_rect_t down = { box.x + box.w - 15, box.y, 15, box.h - 15 };
    tiku_rect_t across = { box.x, box.y + box.h - 15, box.w - 15, 15 };
    tiku_dl_t *dl = tiku_dl_new(), *back = NULL;
    unsigned char wire[1 << 16];
    size_t wrote = 0, at = 0;
    int pos, frac, down_pos, across_pos, seen = 0;
    tiku_dl_fact_t fact;

    tiku_codeview_set_focus(v, 0);
    tiku_codeview_set_options(v, TIKU_CODEVIEW_VSCROLL |
                                 TIKU_CODEVIEW_HSCROLL);
    rows = tiku_codeview_rows_visible(v);
    check(rows == (box.h - 15 - 3) / step, "DSP-004",
          "the bar along the foot takes its rows from the text");
    tiku_codeview_scroll_to(v, 41, 3 * c);
    got->record = dl;
    tiku_codeview_paint(v, got);
    got->record = NULL;
    tiku_fill(want, (tiku_rect_t){ box.x, box.y, box.w - 15, box.h - 15 },
              TIKU_C_DOC);
    pos = (int)((41 * 1000 + (100 - rows) / 2) / (100 - rows));
    down_pos = pos;
    frac = (int)((rows * 1000 + 50) / 100);
    tiku_ui_scrollbar(want, down, pos / 1000.0f, (frac < 50 ? 50 : frac) /
                      1000.0f, 0);
    pos = (int)((3 * c * 1000 + (width - shown) / 2) / (width - shown));
    across_pos = pos;
    frac = (int)((shown * 1000 + width / 2) / width);
    tiku_ui_scrollbar(want, across, pos / 1000.0f, frac / 1000.0f, 1);
    tiku_fill(want, (tiku_rect_t){ box.x + box.w - 15, box.y + box.h - 15,
                                   15, 15 }, TIKU_C_PANEL);
    {
        int ok = 1, x, y;

        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                int text_px = x >= box.x + 5 && x < box.x + box.w - 15 &&
                              y >= box.y && y < box.y + box.h - 15;

                if (!text_px && tiku_peek(got, x, y) != tiku_peek(want, x, y))
                    ok = 0;
            }
        }
        check(ok, "DSP-004", "the bars are drawn with their thumbs at the "
              "scroll in thousandths, and the corner between them in the "
              "panel colour");
    }
    if (dl != NULL && tiku_dl_flatten(dl, wire, sizeof wire, &wrote))
        back = tiku_dl_unflatten(wire, wrote, NULL);
    while (tiku_dl_read(wire, wrote, &at, &fact)) {
        if (fact.kind == TIKU_DL_FACT_SCROLLBAR)
            seen |= (fact.v3 ? fact.v1 == across_pos : fact.v1 == down_pos)
                    << (fact.v3 ? 1 : 0);
    }
    check(seen == 3, "DSP-004", "the recording says where each thumb is, in "
          "thousandths of the scroll, rounded");
    check(back != NULL && tiku_dl_misses(dl) == 0 &&
          tiku_dl_play(back, played) > 0 && same(got, played), "DSP-004",
          "a recording of a view with its bars plays back to the same "
          "pixels");
    tiku_dl_free(dl);
    tiku_dl_free(back);
    tiku_surface_free(got);
    tiku_surface_free(want);
    tiku_surface_free(played);
    tiku_codeview_destroy(v);
    free(text);
}

/** @brief Send button 1 down, a move, or up at @p x, @p y. */
static int
pointer(tiku_codeview_t *v, tiku_event_type_t type, int x, int y)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.button = type == TIKU_EVENT_POINTER_MOVE ? 0 : 1;
    return tiku_codeview_event(v, &e);
}

/** @brief Send @p n ticks; how many the view used. */
static int
ticks(tiku_codeview_t *v, int n)
{
    tiku_event_t e;
    int used = 0;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_TICK;
    while (n-- > 0)
        used += tiku_codeview_event(v, &e);
    return used;
}

/** @brief Whether the bar @p down is drawn at the view's scroll with the
 *         arrow @p held names pressed, -1 back, +1 on or 0 neither. */
static int
down_drawn(tiku_codeview_t *v, tiku_rect_t down, int held)
{
    tiku_surface_t *got = surface(), *want = surface();
    int64_t total, shown, at;
    int pos, frac, ok = 1, x, y;

    tiku_codeview_extent(v, 0, &total, &shown, &at);
    pos = (int)((at * 1000 + (total - shown) / 2) / (total - shown));
    frac = (int)((shown * 1000 + total / 2) / total);
    tiku_codeview_paint(v, got);
    tiku_ui_scrollbar_held(want, down, pos / 1000.0f,
                           (frac < 50 ? 50 : frac) / 1000.0f, 0, held);
    for (y = down.y; y < down.y + down.h; y++) {
        for (x = down.x; x < down.x + down.w; x++) {
            if (tiku_peek(got, x, y) != tiku_peek(want, x, y))
                ok = 0;
        }
    }
    tiku_surface_free(got);
    tiku_surface_free(want);
    return ok;
}

/** @brief The bars worked by the pointer: arrows step and repeat while
 *         held, the track pages, the thumb drags, and the caret stays. */
static void
bars_input(void)
{
    char *text = block(100, 80);
    tiku_codeview_t *v = view_in(text, box);
    int c = tiku_font_mono_cell(0);
    int64_t rows, total, shown, at;
    tiku_rect_t down = { box.x + box.w - 15, box.y, 15, box.h - 15 };
    tiku_rect_t across = { box.x, box.y + box.h - 15, box.w - 15, 15 };
    tiku_rect_t thumb;
    tiku_edit_pos_t anchor, caret;
    int frac;

    tiku_codeview_set_options(v, TIKU_CODEVIEW_VSCROLL |
                                 TIKU_CODEVIEW_HSCROLL);
    rows = tiku_codeview_rows_visible(v);
    check(pointer(v, TIKU_EVENT_POINTER_DOWN, down.x + 7,
                  down.y + down.h - 3) == 1 && tiku_codeview_top(v) == 1,
          "DSP-005", "the down arrow steps a line");
    check(ticks(v, 11) == 0 && tiku_codeview_top(v) == 1 &&
          ticks(v, 1) == 1 && tiku_codeview_top(v) == 2 &&
          ticks(v, 3) == 0 && ticks(v, 1) == 1 && tiku_codeview_top(v) == 3,
          "DSP-005", "held, it waits twelve ticks and then steps every "
          "four");
    check(down_drawn(v, down, 1), "DSP-005",
          "a held arrow is drawn pressed");
    check(pointer(v, TIKU_EVENT_POINTER_UP, down.x + 7, down.y + down.h - 3)
          == 1 && ticks(v, 40) == 0 && tiku_codeview_top(v) == 3, "DSP-005",
          "and letting go stops it");
    check(down_drawn(v, down, 0), "DSP-005",
          "and draws it raised again");
    pointer(v, TIKU_EVENT_POINTER_DOWN, down.x + 7, down.y + down.h - 20);
    pointer(v, TIKU_EVENT_POINTER_UP, down.x + 7, down.y + down.h - 20);
    check(tiku_codeview_top(v) == 3 + rows - 1, "DSP-005",
          "the track below the thumb pages down the rows shown less one");
    pointer(v, TIKU_EVENT_POINTER_DOWN, across.x + across.w - 3,
            across.y + 7);
    pointer(v, TIKU_EVENT_POINTER_UP, across.x + across.w - 3, across.y + 7);
    check(tiku_codeview_left(v) == c, "DSP-005",
          "the right arrow steps a column across");
    tiku_codeview_scroll_to(v, 0, 0);
    tiku_codeview_extent(v, 0, &total, &shown, &at);
    frac = (int)((rows * 1000 + 50) / 100);
    thumb = tiku_ui_scrollbar_thumb(down, 0.0f, frac / 1000.0f, 0);
    pointer(v, TIKU_EVENT_POINTER_DOWN, down.x + 7, thumb.y + 2);
    pointer(v, TIKU_EVENT_POINTER_MOVE, down.x + 7,
            thumb.y + 2 + (down.h - 30 - thumb.h));
    check(tiku_codeview_top(v) == total - shown, "DSP-005",
          "dragging the thumb to the end of its track scrolls to the end");
    pointer(v, TIKU_EVENT_POINTER_MOVE, down.x + 7,
            thumb.y + 2 + (down.h - 30 - thumb.h) / 2);
    pointer(v, TIKU_EVENT_POINTER_UP, down.x + 7,
            thumb.y + 2 + (down.h - 30 - thumb.h) / 2);
    check(tiku_codeview_top(v) >= (total - shown) / 2 - 1 &&
          tiku_codeview_top(v) <= (total - shown) / 2 + 1, "DSP-005",
          "and to the middle, the middle");
    check(pointer(v, TIKU_EVENT_POINTER_MOVE, down.x + 7, down.y) == 0,
          "DSP-005", "a move after the release is not taken");
    tiku_codeview_selection(v, &anchor, &caret);
    check(anchor == 0 && caret == 0, "DSP-005",
          "working the bars leaves the caret where it was");
    pointer(v, TIKU_EVENT_POINTER_DOWN, down.x + 7, down.y + 3);
    {
        tiku_event_t e;

        memset(&e, 0, sizeof e);
        e.type = TIKU_EVENT_DEACTIVATED;
        tiku_codeview_event(v, &e);
    }
    at = tiku_codeview_top(v);
    check(ticks(v, 40) == 0 && tiku_codeview_top(v) == at, "DSP-005",
          "deactivation lets go of a held arrow");
    tiku_codeview_destroy(v);
    free(text);
}

/** @brief The wheel scrolls rows, or columns with Shift or sideways, and
 *         leaves the caret; a press in the gutter is the application's. */
static void
wheel_and_gutter(void)
{
    char *text = block(100, 80);
    tiku_codeview_t *v = view_in(text, box);
    int c = tiku_font_mono_cell(0);
    tiku_event_t e;
    tiku_edit_pos_t anchor, caret;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_WHEEL;
    e.dy = 3;
    check(tiku_codeview_event(v, &e) == 1 && tiku_codeview_top(v) == 3,
          "DSP-006", "the wheel scrolls rows");
    e.modifiers = TIKU_MOD_SHIFT;
    e.dy = 2;
    tiku_codeview_event(v, &e);
    e.modifiers = 0;
    e.dy = 0;
    e.dx = 1;
    tiku_codeview_event(v, &e);
    tiku_codeview_selection(v, &anchor, &caret);
    check(tiku_codeview_top(v) == 3 && tiku_codeview_left(v) == 3 * c &&
          anchor == 0 && caret == 0, "DSP-006",
          "with Shift or sideways it scrolls columns, and the caret stays");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_LINE_NUMBERS);
    check(pointer(v, TIKU_EVENT_POINTER_DOWN, box.x + 3, box.y + 20) == 0,
          "DSP-007", "a press in the gutter is not taken");
    tiku_codeview_set_options(v, TIKU_CODEVIEW_VSCROLL |
                                 TIKU_CODEVIEW_HSCROLL);
    check(pointer(v, TIKU_EVENT_POINTER_DOWN, box.x + box.w - 5,
                  box.y + box.h - 5) == 0, "DSP-007",
          "nor one in the corner between the bars");
    tiku_codeview_destroy(v);
    free(text);
}

/** @brief The widest line by brute force. */
static int64_t
widest_now(tiku_codeview_t *v)
{
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    int64_t line, widest = 0;

    for (line = 0; line < tiku_editdoc_lines(doc); line++) {
        int64_t w = tiku_codeview_line_width(v, line);

        if (w > widest)
            widest = w;
    }
    return widest;
}

/** @brief The extent across. */
static int64_t
across_total(tiku_codeview_t *v)
{
    int64_t total = 0;

    tiku_codeview_extent(v, 1, &total, NULL, NULL);
    return total;
}

/** @brief Replace [s, e) with @p text. */
static void
put(tiku_editdoc_t *doc, int64_t s, int64_t e, const char *text)
{
    tiku_edit_replace_t r = { { s, e }, text, strlen(text) };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    (void)tiku_editdoc_apply(doc, &t);
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

/** @brief The widest line, kept: through random edits, undo, a face and
 *         tab change, and a table that could not be allocated. */
static void
widths(void)
{
    static const char *const pieces[] = {
        "", "x", "wide wide wide wide", "\n", "\n\n", "a\tb", "\r\n", "q\nr"
    };
    tiku_ed_allocator_t failing = { t_alloc, t_realloc, free };
    char *text = numbered(200);
    tiku_codeview_t *v = view_in(text, box);
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    unsigned seed = 12345;
    int i, kept = 1;

    for (i = 0; i < 600; i++) {
        int64_t len = tiku_editdoc_length(doc), s, e;

        seed = seed * 1103515245u + 12345u;
        s = (int64_t)(seed >> 8) % (len + 1);
        seed = seed * 1103515245u + 12345u;
        e = s + (int64_t)(seed >> 8) % 12;
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
            put(doc, s, e, pieces[(seed >> 12) % 8]);
        if (i % 3 == 0 && across_total(v) != widest_now(v) + 12)
            kept = 0;
    }
    check(kept, "DSP-008", "the widest line is right after every few of "
          "600 random edits and undos");
    tiku_codeview_set_tab_width(v, 9);
    check(across_total(v) == widest_now(v) + 12, "DSP-008",
          "and after the tab stops change");
    tiku_codeview_set_font(v, tiku_font_plain());
    check(across_total(v) == widest_now(v) + 12, "DSP-008",
          "and after the face changes");
    tiku_codeview_set_font(v, NULL);
    (void)tiku_font_set_size(16);
    check(across_total(v) == widest_now(v) + 12, "DSP-008",
          "and after the interface's size does");
    (void)tiku_font_set_size(12);

    tiku_editdoc_load(doc, text, strlen(text));
    put(doc, tiku_editdoc_length(doc), tiku_editdoc_length(doc),
        "\na line longer than all the others, and out of view");
    {
        int64_t line, shown = 0;

        for (line = 0; line <= tiku_codeview_rows_visible(v); line++) {
            if (tiku_codeview_line_width(v, line) > shown)
                shown = tiku_codeview_line_width(v, line);
        }
        tiku_ed_test_allocator(&failing);
        refuse = 1;
        check(across_total(v) == shown + 12 && shown < widest_now(v),
              "DSP-008", "with no room for the table, the widest line "
              "shown stands in for the widest");
        refuse = 0;
        tiku_ed_test_allocator(NULL);
    }
    check(across_total(v) == widest_now(v) + 12, "DSP-008",
          "and once there is room, the table is built again and right");
    tiku_codeview_destroy(v);
    free(text);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    gutter_geometry();
    gutter_paint();
    invisibles();
    bars_paint();
    bars_input();
    wheel_and_gutter();
    widths();
    printf("%s\n", fails ? "FAIL: code view display" :
           "PASS: code view display -- gutter, whitespace marks, "
           "scrollbars, wheel, widest line");
    return fails != 0;
}
