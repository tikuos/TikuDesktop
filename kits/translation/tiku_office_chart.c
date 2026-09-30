/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart.c - a chart in a slide or a Word drawing, drawn from
 * the values its part keeps: read, then laid out in its frame -- the
 * title at the top, the legend on its side, the plot in what is left
 * less the room its axes' labels take, or where the file places them --
 * and drawn: grounds, gridlines, series, axes, labels, legend, title.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CT_PAD   7.0f               /* inside the chart's edge           */
#define CT_GAP   6.0f               /* between its title, legend, plot   */
#define LG_PAD   3.0f               /* inside the legend's edge          */
#define LG_SEP   10.0f              /* between entries along a row       */
#define LG_MAX   256                /* entries a legend shows, at most   */

/** @brief One legend entry: a series, or one point of a series whose
 *         points each have a colour. */
typedef struct {
    const char        *name;        /* its words                         */
    const ch_group_t  *g;
    const ch_series_t *s;
    const ch_trend_t  *t;           /* a trendline of the series, or NULL */
    int                i;           /* the point, or -1 for the series   */
    float              w;           /* its key and words across          */
} lg_entry_t;

/** @brief The legend: its entries and its place. */
typedef struct {
    lg_entry_t e[LG_MAX];
    int        n;
    int        across;              /* entries along rows, not down      */
    ch_box_t   box;
} lg_t;

/* ------------------------------------------------------------------ axes  */

/** @brief Which axes the drawn groups measure by, which way each lies,
 *         and which it crosses.  @return how many groups can be drawn. */
static int
axes_begin(ch_chart_t *k)
{
    int g, i, drawn = 0;

    for (i = 0; i < k->nax; i++) {
        k->ax[i].partner = -1;
    }
    for (g = 0; g < k->ngrp; g++) {
        const ch_group_t *gr = &k->grp[g];
        ch_axis_t *c, *v;

        if (gr->kind == CH_PIE || gr->kind == CH_DOUGHNUT) {
            drawn++;
            continue;
        }
        if (gr->cat_ax < 0 || gr->val_ax < 0) {
            continue;
        }
        if (gr->kind == CH_RADAR) {
            drawn++;                /* its axes are its own: round */
            continue;
        }
        c = &k->ax[gr->cat_ax];
        v = &k->ax[gr->val_ax];
        if (!c->used) {
            c->horiz = v->used ? !v->horiz
                               : !(gr->kind == CH_BAR && gr->across);
        }
        v->horiz = v->used ? v->horiz : !c->horiz;
        c->used = v->used = 1;
        c->partner = gr->val_ax;
        v->partner = gr->cat_ax;
        drawn++;
    }
    for (i = 0; i < k->nax; i++) {
        ch_axis_t *a = &k->ax[i];

        a->pos = a->horiz ? (a->pos == 't' || a->pos == 'r' ? 't' : 'b')
                          : (a->pos == 'r' || a->pos == 't' ? 'r' : 'l');
    }
    return drawn;
}

/** @brief Every used axis laid along plot box @p p and scaled to it. */
static void
axes_span(ch_chart_t *k, const ch_box_t *p)
{
    int i;

    for (i = 0; i < k->nax; i++) {
        ch_axis_t *a = &k->ax[i];

        a->from = a->horiz ? p->x : p->y + p->h;
        a->to = a->horiz ? p->x + p->w : p->y;
    }
    for (i = 0; i < k->nax; i++) {
        if (k->ax[i].used) {
            ch_scale(k, &k->ax[i], k->ax[i].horiz ? p->w : p->h);
        }
    }
}

/**
 * @brief The plot box inside @p box: less the room its axes' labels and
 *        titles take when @p reserve (they are inside the box the file
 *        gives, or the room left), measured twice, as the scale their
 *        labels show changes with the room.  Each axis' line placed.
 */
static void
axes_fit(ch_chart_t *k, ch_box_t *box, int reserve)
{
    ch_box_t in = *box;
    int pass, i;

    for (pass = 0; pass < 3; pass++) {
        float room[4] = {0, 0, 0, 0};       /* left top right bottom */

        axes_span(k, &in);
        for (i = 0; i < k->nax; i++) {
            ch_axis_t *a = &k->ax[i];
            int side = a->pos == 'l' ? 0 : a->pos == 't' ? 1 : a->pos == 'r'
                       ? 2 : 3;

            room[side] = fmaxf(room[side], ch_axis_room(k, a));
        }
        if (!reserve || pass == 2) {
            break;
        }
        in.x = box->x + room[0];
        in.y = box->y + room[1];
        in.w = fmaxf(box->w - room[0] - room[2], 8);
        in.h = fmaxf(box->h - room[1] - room[3], 8);
    }
    for (i = 0; i < k->nax; i++) {
        ch_axis_t *a = &k->ax[i];
        const ch_axis_t *b = a->partner >= 0 ? &k->ax[a->partner] : NULL;

        a->at = b != NULL ? ch_place(b, ch_cross(k, b)) : a->horiz
                ? in.y + in.h : in.x;
    }
    *box = in;
}

/* ---------------------------------------------------------------- legend  */

/** @brief Whether legend entry @p idx is one the file took away. */
static int
taken_out(const ch_chart_t *k, int idx)
{
    int i;

    for (i = 0; i < k->nleg_del; i++) {
        if (k->leg_del[i] == idx) {
            return 1;
        }
    }
    return 0;
}

/** @brief Whether a group's legend keys are lines, not boxes. */
static int
keyed_by_line(const ch_group_t *g, const ch_series_t *s)
{
    if (g == NULL) {
        return 1;                   /* a trendline's */
    }
    return g->kind == CH_LINE || (g->kind == CH_RADAR && !g->filled) ||
           (g->kind == CH_SCATTER && s->size == NULL);
}

/** @brief An entry added, unless the file took entry @p idx away. */
static void
entry(ch_chart_t *k, lg_t *lg, int idx, const lg_entry_t *e)
{
    float size = k->legend_font.size;

    if (lg->n >= LG_MAX || taken_out(k, idx) || e->name == NULL) {
        return;
    }
    lg->e[lg->n] = *e;
    lg->e[lg->n].w = (keyed_by_line(e->t != NULL ? NULL : e->g, e->s)
                      ? size * 2.2f : size * 0.7f) + 4 +
                     ch_width(k, &k->legend_font, e->name);
    lg->n++;
}

/** @brief A trendline's legend entry: its own name, or its kind's and
 *         its series', as "Linear (Sales)". */
static void
trend_entry(ch_chart_t *k, lg_t *lg, int idx, const ch_series_t *s,
            const ch_trend_t *t)
{
    static const char *const kind[] = {"Linear", "Expon.", "Log.", "Poly.",
                                       "Power", "Mov. Avg."};
    char buf[200];
    lg_entry_t e;

    memset(&e, 0, sizeof e);
    e.s = s;
    e.t = t;
    e.i = -1;
    if (t->kind == CH_TR_AVERAGE) {
        snprintf(buf, sizeof buf, "%d per. %s (%s)", t->period,
                 kind[t->kind], s->name != NULL ? s->name : "");
    } else {
        snprintf(buf, sizeof buf, "%s (%s)", kind[t->kind],
                 s->name != NULL ? s->name : "");
    }
    e.name = t->name != NULL ? t->name : of_strdup(&k->mem, buf);
    entry(k, lg, idx, &e);
}

/** @brief The legend's entries: a series each, or for a pie (or a lone
 *         series of varied colours) a category each; then trendlines. */
static void
legend_entries(ch_chart_t *k, lg_t *lg)
{
    char num[32];
    int g, s, i, idx = 0;
    lg_entry_t e;

    memset(&e, 0, sizeof e);
    for (g = 0; g < k->ngrp; g++) {
        const ch_group_t *gr = &k->grp[g];
        int each = gr->vary && (gr->nser == 1 || gr->kind == CH_PIE ||
                                gr->kind == CH_DOUGHNUT);

        e.g = gr;
        for (s = 0; s < (each ? 1 : gr->nser); s++) {
            e.s = &gr->ser[s];
            for (i = 0; i < (each ? e.s->n : 1); i++, idx++) {
                if (each) {
                    snprintf(num, sizeof num, "%d", i + 1);
                } else {
                    snprintf(num, sizeof num, "Series %d", e.s->idx + 1);
                }
                e.i = each ? i : -1;
                e.name = !each ? e.s->name : k->cat != NULL && i < k->nnamed
                         ? k->cat[i] : NULL;
                e.name = e.name != NULL ? e.name : of_strdup(&k->mem, num);
                entry(k, lg, idx, &e);
            }
        }
    }
    for (g = 0; g < k->ngrp; g++) {
        for (s = 0; s < k->grp[g].nser; s++) {
            for (i = 0; i < k->grp[g].ser[s].ntrend; i++, idx++) {
                trend_entry(k, lg, idx, &k->grp[g].ser[s],
                            &k->grp[g].ser[s].trend[i]);
            }
        }
    }
}

/** @brief An entry's key drawn at @p x, its line's middle @p y: a line
 *         with its marker, or a box of its fill. */
static void
key(ch_chart_t *k, const lg_entry_t *e, float x, float y)
{
    float size = k->legend_font.size;
    ch_box_t b = {x, y - size * 0.35f, size * 0.7f, size * 0.7f};
    dml_fill_t f;
    dml_line_t l;
    int mk;

    if (e->t != NULL) {
        ch_trend_line(k, e->s, e->t, &l);
        ch_segment(k, x, y, x + size * 2.2f, y, &l);
        return;
    }
    if (keyed_by_line(e->g, e->s)) {
        ch_series_line(k, e->g, e->s, &l);
        l.width = fminf(l.width, 3);
        if (l.width > 0) {
            ch_segment(k, x, y, x + size * 2.2f, y, &l);
        }
        if ((mk = ch_marker_of(e->g, e->s)) != CH_MK_NONE) {
            ch_marker_look(k, e->g, e->s, &f, &l);
            ch_marker(k, mk, fminf(e->s->mk_size, size * 0.8f),
                      x + size * 1.1f, y, &f, &l);
        }
        return;
    }
    ch_point_fill(k, e->g, e->s, e->i, &f);
    ch_series_line(k, e->g, e->s, &l);
    ch_rect(k, &b, &f, l.width > 0 ? &l : NULL);
}

/** @brief The legend's entries flowed into its box: one under another,
 *         or along rows each centred and wrapped at its width; drawn
 *         when @p draw.  @return the height they take. */
static float
legend_flow(ch_chart_t *k, const lg_t *lg, int draw)
{
    float lh = ch_line_h(&k->legend_font), y = lg->box.y + LG_PAD, row, x;
    int i = 0, j, a;

    while (i < lg->n) {
        row = lg->e[i].w;
        for (j = i + 1; lg->across && j < lg->n &&
             row + LG_SEP + lg->e[j].w <= lg->box.w - 2 * LG_PAD; j++) {
            row += LG_SEP + lg->e[j].w;
        }
        x = lg->across ? lg->box.x + (lg->box.w - row) / 2
                       : lg->box.x + LG_PAD;
        if (draw && y + lh > k->h + 1) {
            break;                  /* past the frame: left out */
        }
        for (a = i; draw && a < j; a++) {
            const lg_entry_t *e = &lg->e[a];
            float kw = keyed_by_line(e->t != NULL ? NULL : e->g, e->s)
                       ? k->legend_font.size * 2.2f
                       : k->legend_font.size * 0.7f;

            key(k, e, x, y + lh / 2);
            ch_text(k, &k->legend_font, e->name, x + kw + 4, y + lh / 2, 0,
                    1, 0);
            x += e->w + LG_SEP;
        }
        y += lh;
        i = j;
    }
    return y - lg->box.y + LG_PAD;
}

/** @brief The legend's box: where the file puts it, else on its side of
 *         @p outer, which gives up the room unless it lies over it. */
static void
legend_place(ch_chart_t *k, lg_t *lg, ch_box_t *outer)
{
    float w = 0, h;
    int i;

    lg->across = k->legend_pos == 't' || k->legend_pos == 'b';
    if (k->has_legend_at) {
        lg->box.x = k->legend_at[0] * k->w;
        lg->box.y = k->legend_at[1] * k->h;
        lg->box.w = k->legend_at[2] * k->w;
        lg->box.h = k->legend_at[3] * k->h;
        lg->across = lg->box.w > lg->box.h * 3;
        return;
    }
    for (i = 0; i < lg->n; i++) {
        w = fmaxf(w, lg->e[i].w);
    }
    lg->box.w = lg->across ? outer->w : fminf(w + 2 * LG_PAD, outer->w / 2);
    lg->box.x = lg->across || k->legend_pos == 'l' ? outer->x
                : outer->x + outer->w - lg->box.w;
    lg->box.y = outer->y;
    h = legend_flow(k, lg, 0);
    lg->box.h = h;
    lg->box.y = k->legend_pos == 'b' ? outer->y + outer->h - h
                : lg->across ? outer->y : outer->y + (outer->h - h) / 2;
    if (k->legend_over) {
        return;
    }
    if (lg->across) {
        outer->y += k->legend_pos == 't' ? h + CT_GAP : 0;
        outer->h -= h + CT_GAP;
    } else {
        outer->x += k->legend_pos == 'l' ? lg->box.w + CT_GAP : 0;
        outer->w -= lg->box.w + CT_GAP;
    }
}

/* ------------------------------------------------------------------ chart  */

/** @brief Where the title goes, its middle's top at @p at: where the
 *         file puts it, else the top of @p outer, which gives up the
 *         room. */
static void
title_place(ch_chart_t *k, ch_box_t *outer, float *at)
{
    float w, h;

    ch_block_size(k, &k->title_font, k->title, &w, &h);
    at[0] = k->has_title_at ? k->title_at[0] * k->w + w / 2 : k->w / 2;
    at[1] = k->has_title_at ? k->title_at[1] * k->h : outer->y;
    outer->y += h + CT_GAP;
    outer->h -= h + CT_GAP;
}

/** @brief The plot's box: the file's, else @p outer; less its axes'
 *         room unless the file's box is the plot's inside already. */
static void
plot_place(ch_chart_t *k, const ch_box_t *outer, ch_box_t *plot)
{
    if (k->has_plot_at) {
        plot->x = k->plot_at[0] * k->w;
        plot->y = k->plot_at[1] * k->h;
        plot->w = k->plot_at[2] * k->w;
        plot->h = k->plot_at[3] * k->h;
    } else {
        *plot = *outer;
    }
    plot->w = fmaxf(plot->w, 8);
    plot->h = fmaxf(plot->h, 8);
    axes_fit(k, plot, !k->has_plot_at || !k->plot_inner);
}

/** @brief A box filled and outlined as a part of the chart asks, when
 *         it asks for either. */
static void
ground(ch_chart_t *k, const ch_box_t *b, int fill, const dml_fill_t *f,
       int line, const dml_line_t *l)
{
    if ((fill && f->kind != DML_NOFILL) || (line && l->width > 0)) {
        ch_rect(k, b, fill ? f : NULL, line && l->width > 0 ? l : NULL);
    }
}

/** @brief The chart laid out and drawn. */
static void
draw(ch_chart_t *k)
{
    ch_box_t all = {0, 0, k->w, k->h}, outer, plot;
    float title[2] = {0, 0};
    lg_t *lg = k->legend ? of_alloc(&k->mem, sizeof *lg) : NULL;

    outer.x = outer.y = CT_PAD;
    outer.w = fmaxf(k->w - 2 * CT_PAD, 8);
    outer.h = fmaxf(k->h - 2 * CT_PAD, 8);
    ground(k, &all, k->has_bg, &k->bg, k->has_border, &k->border);
    if (k->title != NULL) {
        title_place(k, &outer, title);
    }
    if (lg != NULL) {
        legend_entries(k, lg);
        legend_place(k, lg, &outer);
    }
    plot_place(k, &outer, &plot);
    ground(k, &plot, k->has_plot_bg, &k->plot_bg, k->has_plot_border,
           &k->plot_border);
    ch_grid(k, &plot);
    ch_plot(k, &plot);
    ch_axes(k, &plot);
    if (lg != NULL && lg->n > 0) {
        (void)legend_flow(k, lg, 1);
    }
    if (k->title != NULL) {
        ch_text_block(k, &k->title_font, k->title, title[0], title[1], 1, 0);
    }
}

int
dml_chart(dml_ctx_t *c, const tiku_xml_node_t *gd, const dml_mat_t *m,
          float w, float h)
{
    const of_rel_t *rel = of_pkg_rel(c->pkg, c->part, tiku_xml_attr(
                                     tiku_xml_child(gd, "c:chart"), "r:id"));
    tiku_xml_t *x = rel != NULL && !rel->external
                    ? of_pkg_xml(c->pkg, rel->target) : NULL;
    const char *part = c->part;
    ch_chart_t *k;
    int n;

    if (x == NULL || !(w > 0) || !(h > 0)) {
        tiku_xml_free(x);
        return 0;
    }
    if ((k = calloc(1, sizeof *k)) == NULL) {
        tiku_xml_free(x);
        return -1;
    }
    k->c = c;
    k->m = m;
    k->w = w;
    k->h = h;
    k->base.size = c->text_pt;
    c->part = rel->target;          /* its pictures are its own */
    n = ch_read_dml(k, tiku_xml_root(x));
    n = n > 0 && axes_begin(k) == 0 ? 0 : n;
    if (n > 0) {
        draw(k);
    }
    n = n > 0 && k->mem.bad ? -1 : n;
    c->part = part;
    of_arena_free(&k->mem);
    free(k);
    tiku_xml_free(x);
    return n < 0 ? -1 : n > 0 ? 1 : 0;
}
