/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_layout.c - where a line's characters lie: each one's
 * bytes and its x, measured with the advances tiku_text() draws with, so
 * painting, hit testing and the caret use the same positions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"

#include <string.h>

/** @brief A walk in progress: the character being assembled from the
 *         document's bytes, which may arrive in two runs. */
typedef struct {
    const tiku_font_t *font;
    int tab;                    /* pixels between stops                 */
    tiku_cv_cell_fn fn;
    void *ctx;
    tiku_cv_cell_t cell;        /* the character being read             */
    int need;                   /* its bytes still to come              */
    int stopped;                /* fn asked to stop                     */
} walk_t;

const tiku_font_t *
tiku_cv_font(const tiku_codeview_t *v)
{
    return v->font != NULL ? v->font : tiku_font_mono(0);
}

int
tiku_cv_tab_px(const tiku_codeview_t *v)
{
    int space = tiku_text_advance(tiku_cv_font(v), ' ');

    return (space > 0 ? space : 1) * v->tab_width;
}

int
tiku_cv_control_name(unsigned cp, char *name)
{
    static const char hex[] = "0123456789ABCDEF";

    if ((cp < 0x20u && cp != '\t') || cp == 0x7Fu) {
        name[0] = '^';
        name[1] = cp == 0x7Fu ? '?' : (char)(cp + 0x40u);
        name[2] = '\0';
        return 1;
    }
    if (cp >= 0x80u && cp <= 0x9Fu) {
        name[0] = '<';
        name[1] = hex[cp >> 4];
        name[2] = hex[cp & 15u];
        name[3] = '>';
        name[4] = '\0';
        return 1;
    }
    return 0;
}

/** @brief Give a whole character its kind and width at its x. */
static void
measure(const walk_t *w, tiku_cv_cell_t *c)
{
    char name[5];

    if (c->cp == '\t') {
        c->kind = TIKU_CV_TAB;
        c->adv = w->tab - (int)(c->x % w->tab);
    } else if (tiku_cv_control_name(c->cp, name)) {
        c->kind = TIKU_CV_CONTROL;
        c->adv = tiku_text_width(w->font, name) + 2 * TIKU_CV_NAME_PAD;
    } else {
        c->kind = TIKU_CV_GLYPH;
        c->adv = tiku_text_advance(w->font, c->cp);
    }
    if (c->adv < 0)
        c->adv = 0;
}

/** @brief Decode one run of the line's bytes, finishing a character the
 *         previous run began, and hand each whole one to the visitor. */
static int
walk_bytes(void *ctx, const char *bytes, size_t n)
{
    walk_t *w = ctx;
    tiku_cv_cell_t *c = &w->cell;
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned b = (unsigned char)bytes[i];

        if (w->need == 0) {
            c->len = 0;
            w->need = b < 0x80u ? 0 : b >= 0xF0u ? 3 : b >= 0xE0u ? 2 : 1;
            c->cp = w->need ? b & (0x3Fu >> w->need) : b;
        } else {
            c->cp = (c->cp << 6) | (b & 0x3Fu);
            w->need--;
        }
        c->bytes[c->len++] = (char)b;
        if (w->need == 0) {
            c->bytes[c->len] = '\0';
            measure(w, c);
            if (w->fn != NULL && w->fn(w->ctx, c)) {
                w->stopped = 1;
                return 1;
            }
            c->pos += c->len;
            c->x += c->adv;
        }
    }
    return 0;
}

int64_t
tiku_cv_walk(tiku_codeview_t *v, int64_t line, tiku_cv_cell_fn fn,
             void *ctx)
{
    tiku_edit_range_t r;
    walk_t w;

    r.start = tiku_editdoc_line_start(v->doc, line);
    r.end = tiku_editdoc_line_end(v->doc, line);
    if (r.start < 0 || r.end < r.start)
        return -1;
    memset(&w, 0, sizeof w);
    w.font = tiku_cv_font(v);
    w.tab = tiku_cv_tab_px(v);
    w.fn = fn;
    w.ctx = ctx;
    w.cell.pos = r.start;
    if (r.end > r.start)
        (void)tiku_editdoc_each(v->doc, r, walk_bytes, &w);
    return w.cell.x;
}

/** @brief Stop at the character starting at or after a position. */
static int
reach_pos(void *ctx, const tiku_cv_cell_t *c)
{
    return c->pos >= *(const tiku_edit_pos_t *)ctx;
}

int64_t
tiku_cv_x_of(tiku_codeview_t *v, int64_t line, tiku_edit_pos_t pos)
{
    return tiku_cv_walk(v, line, reach_pos, &pos);
}

/** @brief A hit test in progress. */
typedef struct {
    int64_t x;                  /* the line's own x asked about         */
    tiku_edit_pos_t pos;        /* the answer, once found               */
} hit_t;

/** @brief Stop before the character whose left half holds the x. */
static int
reach_x(void *ctx, const tiku_cv_cell_t *c)
{
    hit_t *h = ctx;

    if (h->x < c->x + c->adv / 2) {
        h->pos = c->pos;
        return 1;
    }
    return 0;
}

tiku_edit_pos_t
tiku_cv_pos_at(tiku_codeview_t *v, int64_t line, int64_t x)
{
    hit_t h;

    h.x = x;
    h.pos = tiku_editdoc_line_end(v->doc, line);
    (void)tiku_cv_walk(v, line, reach_x, &h);
    return h.pos;
}

/** @brief Stop at the character whose cell holds the x. */
static int
reach_cell(void *ctx, const tiku_cv_cell_t *c)
{
    hit_t *h = ctx;

    if (h->x < c->x + c->adv) {
        h->pos = c->pos;
        return 1;
    }
    return 0;
}

tiku_edit_pos_t
tiku_cv_char_at(tiku_codeview_t *v, int64_t line, int64_t x)
{
    hit_t h;

    h.x = x;
    h.pos = -1;
    (void)tiku_cv_walk(v, line, reach_cell, &h);
    return h.pos;
}

int
tiku_cv_text_x(const tiku_codeview_t *v)
{
    return v->bounds.x + TIKU_CV_PAD_X;
}

int
tiku_cv_text_y(const tiku_codeview_t *v)
{
    return v->bounds.y + TIKU_CV_PAD_Y;
}

int64_t
tiku_codeview_line_width(tiku_codeview_t *view, int64_t line)
{
    if (view == NULL)
        return -1;
    return tiku_cv_walk(view, line, NULL, NULL);
}

tiku_edit_status_t
tiku_codeview_point(tiku_codeview_t *view, tiku_edit_pos_t pos, int64_t *x,
                    int64_t *y)
{
    int64_t line;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    if (!tiku_editdoc_is_boundary(view->doc, pos))
        return TIKU_EDIT_INVALID_RANGE;
    line = tiku_editdoc_line_of(view->doc, pos);
    if (pos > tiku_editdoc_line_end(view->doc, line))
        pos = tiku_editdoc_line_end(view->doc, line);
    if (x != NULL)
        *x = tiku_cv_text_x(view) + tiku_cv_x_of(view, line, pos) -
             view->left;
    if (y != NULL)
        *y = tiku_cv_text_y(view) +
             (line - view->top) * tiku_codeview_row_height(view);
    return TIKU_EDIT_OK;
}

tiku_edit_pos_t
tiku_codeview_hit(tiku_codeview_t *view, int x, int y)
{
    int64_t row, line, lines;
    int step;

    if (view == NULL)
        return -1;
    step = tiku_codeview_row_height(view);
    row = (int64_t)y - tiku_cv_text_y(view);
    row = row >= 0 ? row / step : -((-row + step - 1) / step);
    lines = tiku_editdoc_lines(view->doc);
    line = view->top + row;
    if (line < 0)
        line = 0;
    if (line >= lines)
        line = lines - 1;
    return tiku_cv_pos_at(view, line,
                          (int64_t)x - tiku_cv_text_x(view) + view->left);
}
