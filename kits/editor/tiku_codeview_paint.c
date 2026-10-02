/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_paint.c - a view drawn with the Interface Kit's calls,
 * from the layout's own walk, so a recording surface carries all of it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_ui.h"

#include <stdio.h>
#include <string.h>

/* Bytes of text handed to tiku_text() at a time.  A piece ends here or
 * where the colour changes; it is never a place where a line breaks. */
#define PIECE_MAX 256

/** @brief One row being painted. */
typedef struct {
    tiku_surface_t *s;
    const tiku_font_t *font;
    int x0;                     /* surface x of line x 0, unscrolled   */
    int y;                      /* the row's top                        */
    int step;                   /* the row's height                     */
    int64_t left;               /* line x at the text's left edge       */
    int64_t right;              /* line x past which nothing shows      */
    tiku_edit_pos_t lo, hi;     /* the selection, in order              */
    char piece[PIECE_MAX + 5];  /* text waiting to be drawn, and a NUL  */
    size_t len;                 /* its bytes                            */
    int64_t piece_x;            /* line x of its first character        */
    tiku_rgb_t ink;             /* its colour                           */
    int stopped;                /* the walk reached the right edge      */
    int invisibles;             /* mark spaces and tabs                 */
    tiku_edit_pos_t start;      /* the line's first byte                */
    const tiku_span_t *runs;    /* its coloured runs; none is plain     */
    size_t nruns;
    size_t run;                 /* the run the walk has reached         */
    int64_t run_end;            /* the line offset where that run ends  */
} row_t;

/** @brief The surface x of line x @p x, which is near the visible part. */
static int
surface_x(const row_t *r, int64_t x)
{
    return r->x0 + (int)(x - r->left);
}

/** @brief The part of @p a inside @p b. */
static tiku_rect_t
meet(tiku_rect_t a, tiku_rect_t b)
{
    int64_t x0 = a.x > b.x ? a.x : b.x;
    int64_t y0 = a.y > b.y ? a.y : b.y;
    int64_t x1 = (int64_t)a.x + a.w < (int64_t)b.x + b.w ?
                 (int64_t)a.x + a.w : (int64_t)b.x + b.w;
    int64_t y1 = (int64_t)a.y + a.h < (int64_t)b.y + b.h ?
                 (int64_t)a.y + a.h : (int64_t)b.y + b.h;
    tiku_rect_t r;

    r.x = (int)x0;
    r.y = (int)y0;
    r.w = x1 > x0 ? (int)(x1 - x0) : 0;
    r.h = y1 > y0 ? (int)(y1 - y0) : 0;
    return r;
}

/** @brief Fill the row from line x @p a to @p b, cut to what shows. */
static void
fill_span(row_t *r, int64_t a, int64_t b, tiku_rgb_t c)
{
    tiku_rect_t box;

    if (a < r->left - 1)
        a = r->left - 1;
    if (b > r->right + 1)
        b = r->right + 1;
    if (b <= a)
        return;
    box.x = surface_x(r, a);
    box.y = r->y;
    box.w = (int)(b - a);
    box.h = r->step;
    tiku_fill(r->s, box, c);
}

/** @brief Draw the text gathered so far. */
static void
flush(row_t *r)
{
    if (r->len == 0)
        return;
    r->piece[r->len] = '\0';
    tiku_text(r->s, r->font, surface_x(r, r->piece_x),
              r->y + r->font->ascent, r->piece, r->ink);
    r->len = 0;
}

/** @brief A control character: its name, light on a dark box. */
static void
paint_name(row_t *r, const tiku_cv_cell_t *c, int selected)
{
    tiku_rgb_t ground = selected ? TIKU_C_SELECT : TIKU_C_DOC;
    tiku_rgb_t ink = selected ? TIKU_C_SELTEXT : TIKU_C_TEXT;
    tiku_rect_t box;
    char name[5];

    (void)tiku_cv_control_name(c->cp, name);
    box.x = surface_x(r, c->x) + 1;
    box.y = r->y + 1;
    box.w = c->adv - 2;
    box.h = r->font->height - 1;
    if (box.w > 0 && box.h > 0)
        tiku_fill(r->s, box, tiku_dim(ink, ground));
    tiku_text(r->s, r->font, surface_x(r, c->x) + TIKU_CV_NAME_PAD,
              r->y + r->font->ascent, name, ground);
}

/** @brief A space's dot or a tab's dash in the middle of its cell, its
 *         ink faint against its ground. */
static void
paint_mark(row_t *r, const tiku_cv_cell_t *c, int selected, tiku_rgb_t ink)
{
    tiku_rgb_t ground = selected ? TIKU_C_SELECT : TIKU_C_DOC;

    tiku_hline(r->s, surface_x(r, c->x) + c->adv / 2,
               r->y + r->font->height / 2, c->kind == TIKU_CV_TAB ? 3 : 1,
               tiku_dim(ink, ground));
}

/** @brief The colour of the run holding line offset @p at; the walk asks
 *         in order, so the run only moves forward. */
static tiku_rgb_t
run_ink(row_t *r, int64_t at)
{
    while (r->run < r->nruns && at >= r->run_end) {
        r->run++;
        if (r->run < r->nruns)
            r->run_end += r->runs[r->run].len;
    }
    return r->run < r->nruns ? tiku_ink(r->runs[r->run].ink) : TIKU_C_TEXT;
}

/** @brief Paint one character, gathering text into pieces. */
static int
paint_cell(void *ctx, const tiku_cv_cell_t *c)
{
    row_t *r = ctx;
    int selected = c->pos >= r->lo && c->pos < r->hi;
    tiku_rgb_t ink = run_ink(r, c->pos - r->start);

    if (selected)
        ink = TIKU_C_SELTEXT;

    if (c->x >= r->right) {
        r->stopped = 1;
        return 1;
    }
    if (c->x + c->adv <= r->left)
        return 0;
    if (r->len > 0 && (c->kind != TIKU_CV_GLYPH || ink != r->ink ||
                       r->len + (size_t)c->len > PIECE_MAX))
        flush(r);
    if (c->kind == TIKU_CV_GLYPH) {
        if (r->len == 0) {
            r->piece_x = c->x;
            r->ink = ink;
        }
        memcpy(r->piece + r->len, c->bytes, (size_t)c->len);
        r->len += (size_t)c->len;
    } else if (c->kind == TIKU_CV_CONTROL) {
        paint_name(r, c, selected);
    }
    if (r->invisibles && (c->cp == ' ' || c->kind == TIKU_CV_TAB))
        paint_mark(r, c, selected, ink);
    return 0;
}

/** @brief Paint @p line in the row @p r is set up for: the selection's
 *         ground first, then the text over it, then a selected line
 *         break as one space's width after the text. */
static void
paint_row(tiku_codeview_t *v, row_t *r, int64_t line)
{
    tiku_edit_pos_t start = tiku_editdoc_line_start(v->doc, line);
    tiku_edit_pos_t end = tiku_editdoc_line_end(v->doc, line);
    tiku_edit_pos_t next = tiku_editdoc_line_start(v->doc, line + 1);
    tiku_edit_pos_t upto;
    int64_t width;

    if (r->lo < r->hi && r->lo < end && r->hi > start) {
        tiku_edit_pos_t a = r->lo > start ? r->lo : start;
        tiku_edit_pos_t b = r->hi < end ? r->hi : end;

        fill_span(r, tiku_cv_x_of(v, line, a), tiku_cv_x_of(v, line, b),
                  TIKU_C_SELECT);
    }
    r->len = 0;
    r->stopped = 0;
    r->start = start;
    upto = end;
    if (end - start >= TIKU_CV_LONG) {
        tiku_edit_pos_t at = tiku_cv_char_at(v, line, r->right);

        if (at >= 0)
            upto = at;
    }
    r->runs = tiku_cv_style_line(v, line, upto, &r->nruns);
    r->run = 0;
    r->run_end = r->nruns > 0 ? r->runs[0].len : 0;
    width = tiku_cv_walk_near(v, line, INT64_MAX, r->left, paint_cell, r);
    flush(r);
    if (!r->stopped && next > end && r->lo <= end && r->hi >= next)
        fill_span(r, width, width + tiku_text_advance(r->font, ' '),
                  TIKU_C_SELECT);
}

/** @brief Put back the clip the caller had. */
static void
restore_clip(tiku_surface_t *s, tiku_rect_t saved)
{
    if (saved.x == 0 && saved.y == 0 && saved.w == s->w && saved.h == s->h)
        tiku_clip_reset(s);
    else
        tiku_clip_set(s, saved);
}

/** @brief The caret: a line the row's height at its x. */
static void
paint_caret(tiku_codeview_t *v, tiku_surface_t *s, int64_t rows)
{
    tiku_edit_pos_t anchor, caret;
    int64_t line, x;
    int step = tiku_codeview_row_height(v);

    if (!v->focused ||
        tiku_codeview_selection(v, &anchor, &caret) != TIKU_EDIT_OK)
        return;
    line = tiku_editdoc_line_of(v->doc, caret);
    if (line < v->top || line >= v->top + rows)
        return;
    x = tiku_cv_x_of(v, line, caret) - v->left;
    if (x < 0 || x > tiku_cv_text_w(v))
        return;
    tiku_vline(s, tiku_cv_text_x(v) + (int)x,
               tiku_cv_text_y(v) + (int)(line - v->top) * step, step,
               TIKU_C_TEXT);
}

/** @brief The gutter: a shaded strip with a rule at its right edge and
 *         each shown line's number right-aligned a few pixels short of
 *         the rule, on the text's baseline. */
static void
paint_gutter(tiku_codeview_t *v, tiku_surface_t *s, int width,
             tiku_rect_t area)
{
    const tiku_font_t *numbers = tiku_font_at(11);
    const tiku_font_t *f = tiku_cv_font(v);
    tiku_rect_t strip = { v->bounds.x, area.y, width, area.h };
    int64_t line, lines = tiku_editdoc_lines(v->doc);
    int y = tiku_cv_text_y(v), step = tiku_codeview_row_height(v);
    char number[24];

    tiku_fill(s, strip, tiku_tint(TIKU_C_DOC, 1.04f));
    tiku_vline(s, strip.x + width - 1, strip.y, strip.h,
               tiku_tint(TIKU_C_DOC, 1.12f));
    for (line = v->top; line < lines && y < area.y + area.h; line++) {
        snprintf(number, sizeof number, "%lld", (long long)(line + 1));
        tiku_text(s, numbers,
                  strip.x + width - 6 - tiku_text_width(numbers, number),
                  y + f->ascent, number, tiku_ink(TIKU_INK_FAINT));
        y += step;
    }
}

/** @brief The rows of marked lines filled with their kinds' colours, the
 *         highest kind's on top, and the execution line's over them
 *         while it is current. */
static void
paint_fills(tiku_codeview_t *v, tiku_surface_t *s, tiku_rect_t area)
{
    int64_t line, exec, lines = tiku_editdoc_lines(v->doc);
    int y = tiku_cv_text_y(v), step = tiku_codeview_row_height(v);

    if (tiku_editdoc_execution(v->doc, &exec) != TIKU_EDIT_OK)
        exec = -1;
    if (v->fill_kinds == 0 && exec < 0)
        return;
    for (line = v->top; line < lines && y < area.y + area.h; line++) {
        uint32_t kinds = v->fill_kinds ?
                         tiku_editdoc_markers_on(v->doc, line) &
                         v->fill_kinds : 0;
        tiku_rect_t row = { area.x, y, area.w, step };
        int kind = TIKU_EDIT_MARKER_KINDS - 1;

        if (line == exec) {
            tiku_fill(s, row, v->exec_fill);
        } else if (kinds != 0) {
            while (!(kinds & ((uint32_t)1 << kind)))
                kind--;
            tiku_fill(s, row, v->fills[kind]);
        }
        y += step;
    }
}

/** @brief A scrollbar placed in thousandths, so a recording carries the
 *         thumb exactly where it is drawn here; an arrow held down is
 *         drawn pressed. */
static void
paint_bar(tiku_codeview_t *v, tiku_surface_t *s, int horizontal)
{
    tiku_rect_t bar = tiku_cv_bar(v, horizontal);
    int pos, frac;

    if (bar.w <= 0 || bar.h <= 0)
        return;
    tiku_cv_bar_place(v, horizontal, &pos, &frac);
    tiku_ui_scrollbar_held(s, bar, (float)pos / 1000.0f,
                           (float)frac / 1000.0f, horizontal,
                           v->arrow_bar == horizontal + 1 ? v->arrow_dir
                                                          : 0);
}

void
tiku_codeview_paint(tiku_codeview_t *view, tiku_surface_t *s)
{
    tiku_rect_t saved, full, area, text;
    tiku_edit_pos_t anchor = 0, caret = 0;
    int64_t lines, rows;
    int gutter;
    row_t r;

    if (view == NULL || s == NULL)
        return;
    saved = s->clip;
    full = meet(view->bounds, saved);
    if (full.w <= 0 || full.h <= 0)
        return;
    if (view->options & TIKU_CODEVIEW_HSCROLL)
        tiku_cv_clamp_across(view);
    else
        tiku_cv_clamp(view);
    area = tiku_cv_area(view);
    gutter = tiku_cv_gutter(view);
    tiku_clip_set(s, full);
    tiku_fill(s, area, TIKU_C_DOC);
    if (gutter > 0)
        paint_gutter(view, s, gutter, area);
    tiku_clip_set(s, meet(area, full));
    paint_fills(view, s, area);
    tiku_clip_set(s, full);

    memset(&r, 0, sizeof r);
    r.s = s;
    r.font = tiku_cv_font(view);
    r.x0 = tiku_cv_text_x(view);
    r.step = tiku_codeview_row_height(view);
    r.left = view->left;
    r.right = view->left + tiku_cv_text_w(view);
    r.invisibles = (view->options & TIKU_CODEVIEW_INVISIBLES) != 0;
    (void)tiku_codeview_selection(view, &anchor, &caret);
    r.lo = anchor < caret ? anchor : caret;
    r.hi = anchor < caret ? caret : anchor;
    text.x = r.x0;
    text.y = area.y;
    text.w = (int)tiku_cv_text_w(view);
    text.h = area.h;
    text = meet(text, saved);
    lines = tiku_editdoc_lines(view->doc);
    rows = 0;
    if (text.w > 0 && text.h > 0) {
        tiku_clip_set(s, text);
        for (;; rows++) {
            r.y = tiku_cv_text_y(view) + (int)rows * r.step;
            if (view->top + rows >= lines || r.y >= area.y + area.h)
                break;
            paint_row(view, &r, view->top + rows);
        }
        paint_caret(view, s, rows);
        tiku_clip_set(s, full);
    }
    tiku_clip_set(s, meet(area, full));
    tiku_cv_assist_paint(view, s);
    tiku_clip_set(s, full);
    paint_bar(view, s, 0);
    paint_bar(view, s, 1);
    if ((view->options & TIKU_CODEVIEW_VSCROLL) &&
        (view->options & TIKU_CODEVIEW_HSCROLL)) {
        tiku_rect_t corner = { view->bounds.x + view->bounds.w - TIKU_CV_BAR,
                               view->bounds.y + view->bounds.h - TIKU_CV_BAR,
                               TIKU_CV_BAR, TIKU_CV_BAR };

        tiku_fill(s, corner, TIKU_C_PANEL);
    }
    restore_clip(s, saved);
}
