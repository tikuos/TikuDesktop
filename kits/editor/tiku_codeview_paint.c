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

/** @brief Paint one character, gathering text into pieces. */
static int
paint_cell(void *ctx, const tiku_cv_cell_t *c)
{
    row_t *r = ctx;
    int selected = c->pos >= r->lo && c->pos < r->hi;
    tiku_rgb_t ink = selected ? TIKU_C_SELTEXT : TIKU_C_TEXT;

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
    int64_t width;

    if (r->lo < r->hi && r->lo < end && r->hi > start) {
        tiku_edit_pos_t a = r->lo > start ? r->lo : start;
        tiku_edit_pos_t b = r->hi < end ? r->hi : end;

        fill_span(r, tiku_cv_x_of(v, line, a), tiku_cv_x_of(v, line, b),
                  TIKU_C_SELECT);
    }
    r->len = 0;
    r->stopped = 0;
    width = tiku_cv_walk(v, line, paint_cell, r);
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
    if (x < 0 || x > v->bounds.w)
        return;
    tiku_vline(s, tiku_cv_text_x(v) + (int)x,
               tiku_cv_text_y(v) + (int)(line - v->top) * step, step,
               TIKU_C_TEXT);
}

void
tiku_codeview_paint(tiku_codeview_t *view, tiku_surface_t *s)
{
    tiku_rect_t saved, area, text;
    tiku_edit_pos_t anchor = 0, caret = 0;
    int64_t lines, rows;
    row_t r;

    if (view == NULL || s == NULL)
        return;
    saved = s->clip;
    area = meet(view->bounds, saved);
    if (area.w <= 0 || area.h <= 0)
        return;
    tiku_cv_clamp(view);
    tiku_clip_set(s, area);
    tiku_fill(s, view->bounds, TIKU_C_DOC);

    memset(&r, 0, sizeof r);
    r.s = s;
    r.font = tiku_cv_font(view);
    r.x0 = tiku_cv_text_x(view);
    r.step = tiku_codeview_row_height(view);
    r.left = view->left;
    r.right = view->left + (view->bounds.x + view->bounds.w - r.x0);
    (void)tiku_codeview_selection(view, &anchor, &caret);
    r.lo = anchor < caret ? anchor : caret;
    r.hi = anchor < caret ? caret : anchor;
    text.x = r.x0;
    text.y = view->bounds.y;
    text.w = view->bounds.x + view->bounds.w - r.x0;
    text.h = view->bounds.h;
    text = meet(text, saved);
    lines = tiku_editdoc_lines(view->doc);
    rows = 0;
    if (text.w > 0 && text.h > 0) {
        tiku_clip_set(s, text);
        for (;; rows++) {
            r.y = tiku_cv_text_y(view) + (int)rows * r.step;
            if (view->top + rows >= lines ||
                r.y >= view->bounds.y + view->bounds.h)
                break;
            paint_row(view, &r, view->top + rows);
        }
        paint_caret(view, s, rows);
    }
    restore_clip(s, saved);
}
