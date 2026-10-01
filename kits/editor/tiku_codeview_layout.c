/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_layout.c - where a line's characters lie: each one's
 * bytes and its x, measured with the advances tiku_text() draws with, so
 * painting, hit testing and the caret use the same positions.  Also where
 * the gutter, the text and the scrollbars lie, and each line's width.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_editdoc_private.h"

#include <stdint.h>
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
tiku_cv_gutter(const tiku_codeview_t *v)
{
    int64_t lines;
    int digits = 1;

    if (!(v->options & TIKU_CODEVIEW_LINE_NUMBERS))
        return 0;
    for (lines = tiku_editdoc_lines(v->doc); lines >= 10; lines /= 10)
        digits++;
    return TIKU_CV_GUTTER_PAD +
           digits * tiku_text_advance(tiku_cv_font(v), '0');
}

tiku_rect_t
tiku_cv_area(const tiku_codeview_t *v)
{
    tiku_rect_t r = v->bounds;
    int gutter = tiku_cv_gutter(v);

    r.x += gutter;
    r.w -= gutter;
    if (v->options & TIKU_CODEVIEW_VSCROLL)
        r.w -= TIKU_CV_BAR;
    if (v->options & TIKU_CODEVIEW_HSCROLL)
        r.h -= TIKU_CV_BAR;
    if (r.w < 0)
        r.w = 0;
    if (r.h < 0)
        r.h = 0;
    return r;
}

int
tiku_cv_text_x(const tiku_codeview_t *v)
{
    int gutter = tiku_cv_gutter(v);

    return v->bounds.x +
           (gutter > 0 ? gutter + TIKU_CV_GUTTER_GAP : TIKU_CV_PAD_X);
}

int64_t
tiku_cv_text_w(const tiku_codeview_t *v)
{
    tiku_rect_t area = tiku_cv_area(v);
    int64_t w = (int64_t)area.x + area.w - tiku_cv_text_x(v);

    return w > 0 ? w : 0;
}

tiku_rect_t
tiku_cv_bar(const tiku_codeview_t *v, int horizontal)
{
    tiku_rect_t r = { 0, 0, 0, 0 };
    tiku_rect_t b = v->bounds;
    int down = (v->options & TIKU_CODEVIEW_VSCROLL) != 0;
    int across = (v->options & TIKU_CODEVIEW_HSCROLL) != 0;

    if (horizontal ? !across : !down)
        return r;
    if (horizontal) {
        r.x = b.x;
        r.y = b.y + b.h - TIKU_CV_BAR;
        r.w = b.w - (down ? TIKU_CV_BAR : 0);
        r.h = TIKU_CV_BAR;
    } else {
        r.x = b.x + b.w - TIKU_CV_BAR;
        r.y = b.y;
        r.w = TIKU_CV_BAR;
        r.h = b.h - (across ? TIKU_CV_BAR : 0);
    }
    if (r.w <= 0 || r.h <= 0 || b.w < TIKU_CV_BAR || b.h < TIKU_CV_BAR)
        r.w = r.h = 0;
    return r;
}

void
tiku_codeview_extent(tiku_codeview_t *view, int across, int64_t *total,
                     int64_t *visible, int64_t *value)
{
    int64_t t = 0, shown = 0, at = 0;

    if (view != NULL && across) {
        t = tiku_cv_widest(view) + TIKU_CV_SLACK;
        shown = tiku_cv_text_w(view);
        at = view->left;
    } else if (view != NULL) {
        t = tiku_editdoc_lines(view->doc);
        shown = tiku_codeview_rows_visible(view);
        at = view->top;
    }
    if (total != NULL)
        *total = t;
    if (visible != NULL)
        *visible = shown;
    if (value != NULL)
        *value = at;
}

void
tiku_cv_bar_place(tiku_codeview_t *v, int horizontal, int *pos, int *frac)
{
    int64_t total, visible, value, most;

    tiku_codeview_extent(v, horizontal, &total, &visible, &value);
    most = total - visible;
    if (most <= 0 || total <= 0) {
        *pos = 0;
        *frac = 1000;
        return;
    }
    if (value > most)
        value = most;
    if (value < 0)
        value = 0;
    *pos = (int)((value * 1000 + most / 2) / most);
    *frac = (int)((visible * 1000 + total / 2) / total);
    if (*frac < 50)
        *frac = 50;
    if (*frac > 1000)
        *frac = 1000;
}

/** @brief A number for everything a line's width depends on: the face,
 *         its height and printable advances, and the tab stops. */
static uint32_t
metrics_key(const tiku_codeview_t *v)
{
    const tiku_font_t *f = tiku_cv_font(v);
    uint32_t h = 2166136261u;
    int c;

    h = (h ^ (uint32_t)(uintptr_t)f) * 16777619u;
    h = (h ^ (uint32_t)f->height) * 16777619u;
    h = (h ^ (uint32_t)v->tab_width) * 16777619u;
    for (c = 32; c < 127; c++)
        h = (h ^ (uint32_t)tiku_text_advance(f, (unsigned)c)) * 16777619u;
    return h;
}

void
tiku_cv_widths_free(tiku_codeview_t *v)
{
    tiku_ed_free(v->widths);
    v->widths = NULL;
    v->nwidths = 0;
    v->widths_cap = 0;
    v->widths_ok = 0;
    v->widest = -1;
}

/** @brief Room for @p n entries; 0 when there is none. */
static int
widths_room(tiku_codeview_t *v, int64_t n)
{
    int64_t cap;
    int64_t *grown;

    if (n <= v->widths_cap)
        return 1;
    cap = n + n / 4 + 16;
    if (cap < 0 || (uint64_t)cap > SIZE_MAX / sizeof *grown)
        return 0;
    grown = tiku_ed_realloc(v->widths, (size_t)cap * sizeof *grown);
    if (grown == NULL)
        return 0;
    v->widths = grown;
    v->widths_cap = cap;
    return 1;
}

void
tiku_cv_widths_change(tiku_codeview_t *v, int64_t first, int64_t last_old,
                      int64_t last_new, int64_t lines)
{
    int64_t i, tail;

    if (!v->widths_ok)
        return;
    if (first < 0 || last_old < first || last_new < first ||
        v->nwidths != lines - (last_new - last_old) ||
        last_old >= v->nwidths || !widths_room(v, lines)) {
        tiku_cv_widths_free(v);
        return;
    }
    tail = v->nwidths - (last_old + 1);
    if (tail > 0)
        memmove(&v->widths[last_new + 1], &v->widths[last_old + 1],
                (size_t)tail * sizeof *v->widths);
    for (i = first; i <= last_new; i++)
        v->widths[i] = -1;
    v->nwidths = lines;
    v->widest = -1;
}

/** @brief The widest of the lines the view shows, measured now. */
static int64_t
widest_shown(tiku_codeview_t *v)
{
    int64_t line, last = v->top + tiku_codeview_rows_visible(v);
    int64_t widest = 0;

    for (line = v->top; line <= last; line++) {
        int64_t w = tiku_cv_walk(v, line, NULL, NULL);

        if (w > widest)
            widest = w;
    }
    return widest;
}

int64_t
tiku_cv_widest(tiku_codeview_t *v)
{
    int64_t lines = tiku_editdoc_lines(v->doc), i, widest = 0;
    uint32_t key = metrics_key(v);

    if (v->widths_ok && (v->nwidths != lines || v->widths_key != key))
        tiku_cv_widths_free(v);
    if (!v->widths_ok) {
        if (!widths_room(v, lines))
            return widest_shown(v);
        for (i = 0; i < lines; i++)
            v->widths[i] = -1;
        v->nwidths = lines;
        v->widths_ok = 1;
        v->widths_key = key;
        v->widest = -1;
    }
    if (v->widest >= 0)
        return v->widest;
    for (i = 0; i < lines; i++) {
        if (v->widths[i] < 0)
            v->widths[i] = tiku_cv_walk(v, i, NULL, NULL);
        if (v->widths[i] > widest)
            widest = v->widths[i];
    }
    v->widest = widest;
    return widest;
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
