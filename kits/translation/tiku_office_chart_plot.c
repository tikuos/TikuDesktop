/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart_plot.c - a chart's series drawn: bars standing or
 * lying, clustered or stacked, from where their axis crosses; lines
 * through their points, straight or smoothed, with their markers; areas
 * down to their base or the series under them; pies and doughnuts from
 * their first slice's angle; scatters and bubbles.  Each point's label
 * is noted as it is drawn and set once all are, over everything.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PL_LINE_W  2.25f            /* a line series' width, unsaid      */
#define PL_MK_W    0.75f            /* a marker's outline                */
#define PL_GAP     3.0f             /* between a point and its label     */

/** @brief A label noted while its point was drawn. */
typedef struct {
    const ch_series_t *s;           /* whose                             */
    int      i;                     /* which point                       */
    float    x, y;                  /* where                             */
    int      align, valign;         /* as ch_text takes them             */
    double   pct;                   /* a slice's share, or NAN           */
} pl_label_t;

/** @brief The series being drawn: the group, its axes, its stacks. */
typedef struct {
    ch_chart_t       *k;
    const ch_group_t *g;
    const ch_axis_t  *cat, *val;    /* category (or x) axis, value axis  */
    double            base;         /* where bars and areas grow from    */
    double           *up, *down;    /* each category's stacks so far     */
    double           *total;        /* and its whole, stacked to 100 %   */
    float            *pt;           /* a series' points: x, y, drawn     */
    int              *run;          /* one unbroken run of them          */
    pl_label_t       *lbl;          /* labels noted                      */
    int               nlbl, cap;
} pl_t;

/** @brief Points of a pie's slice, or a doughnut's. */
typedef struct {
    float  cx, cy;                  /* the centre                        */
    float  r0, r1;                  /* inner and outer radius            */
    double a0, a1;                  /* from, to: clockwise from twelve   */
} pl_arc_t;

/* ----------------------------------------------------------------- looks  */

void
ch_point_fill(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
              int i, dml_fill_t *f)
{
    int each = g->vary && i >= 0 && (g->nser == 1 || g->kind == CH_PIE ||
                                     g->kind == CH_DOUGHNUT);

    if (s->pt_fill != NULL && i >= 0 && i < s->n && (s->pt_has[i] & 1)) {
        *f = s->pt_fill[i];
    } else if (s->has_fill) {
        *f = s->fill;
    } else {
        memset(f, 0, sizeof *f);
        f->kind = DML_SOLID;
        f->color = ch_palette(k, each ? i : s->idx);
    }
}

void
ch_series_line(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
               dml_line_t *l)
{
    int traced = g->kind == CH_LINE || (g->kind == CH_RADAR && !g->filled) ||
                 (g->kind == CH_SCATTER && s->size == NULL);

    memset(l, 0, sizeof *l);
    if (s->has_line) {
        *l = s->line;
    }
    if (traced) {
        l->width = !s->has_line ? (g->kind == CH_SCATTER && !g->lines ? 0
                                                                 : PL_LINE_W)
                 : l->width < 0 ? PL_LINE_W : l->width;
        l->join = s->has_line ? l->join : 1;
        l->cap = s->has_line ? l->cap : 1;
    } else {
        l->width = !s->has_line ? 0 : l->width < 0 ? 0.75f : l->width;
    }
    if (l->color == 0) {
        l->color = traced ? ch_palette(k, s->idx) : 0xff000000u;
    }
}

int
ch_marker_of(const ch_group_t *g, const ch_series_t *s)
{
    static const int turn[] = {CH_MK_DIAMOND, CH_MK_SQUARE, CH_MK_TRIANGLE,
                               CH_MK_X, CH_MK_CIRCLE, CH_MK_PLUS};

    if ((g->kind != CH_LINE && g->kind != CH_SCATTER &&
         g->kind != CH_RADAR) || s->size != NULL) {
        return CH_MK_NONE;
    }
    if (s->marker >= 0) {
        return s->marker;
    }
    return g->markers ? turn[s->idx % 6] : CH_MK_NONE;
}

void
ch_marker_look(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
               dml_fill_t *f, dml_line_t *l)
{
    dml_line_t own;
    uint32_t c;

    ch_series_line(k, g, s, &own);
    c = own.width > 0 ? own.color : ch_palette(k, s->idx);
    if (s->has_mk_fill) {
        *f = s->mk_fill;
    } else {
        memset(f, 0, sizeof *f);
        f->kind = DML_SOLID;
        f->color = c;
    }
    memset(l, 0, sizeof *l);
    l->width = PL_MK_W;
    l->color = c;
    if (s->has_mk_line) {
        *l = s->mk_line;
        l->width = l->width < 0 ? PL_MK_W : l->width;
        l->color = l->color != 0 ? l->color : c;
    }
}

void
ch_marker(ch_chart_t *k, int kind, float size, float x, float y,
          const dml_fill_t *f, const dml_line_t *l)
{
    float r = size / 2;
    ch_path_t p;

    ch_path(&p, x - r, y - r, kind != CH_MK_X && kind != CH_MK_PLUS, 1);
    if (kind == CH_MK_SQUARE || kind == CH_MK_DASH) {
        float h = kind == CH_MK_DASH ? r / 3 : r;

        ch_move(&p, x - r, y - h);
        ch_line(&p, x + r, y - h);
        ch_line(&p, x + r, y + h);
        ch_line(&p, x - r, y + h);
    } else if (kind == CH_MK_DIAMOND || kind == CH_MK_TRIANGLE) {
        ch_move(&p, x, y - r);
        ch_line(&p, x + r, kind == CH_MK_DIAMOND ? y : y + r);
        ch_line(&p, kind == CH_MK_DIAMOND ? x : x - r, y + r);
        ch_line(&p, x - r, kind == CH_MK_DIAMOND ? y : y + r);
    } else if (kind == CH_MK_X || kind == CH_MK_PLUS) {
        float d = kind == CH_MK_X ? r : 0, e = kind == CH_MK_X ? r : 0;

        ch_move(&p, x - d, y - r);
        ch_line(&p, x + d, y + r);
        ch_move(&p, x - r, y + e);
        ch_line(&p, x + r, y - e);
    } else if (kind != CH_MK_NONE) {
        ch_move(&p, x, y - r);
        ch_arc(&p, x, y, r, 0, 2 * M_PI);
    }
    if (kind != CH_MK_X && kind != CH_MK_PLUS) {
        ch_close(&p);
    }
    ch_paint(k, &p, kind == CH_MK_X || kind == CH_MK_PLUS ? NULL : f, l,
             size, size);
}

/** @brief The outline point @p i of series @p s is drawn with: its own,
 *         else its series'. */
static void
point_line(pl_t *q, const ch_series_t *s, int i, dml_line_t *l)
{
    ch_series_line(q->k, q->g, s, l);
    if (s->pt_fill != NULL && i >= 0 && i < s->n && (s->pt_has[i] & 2)) {
        *l = s->pt_line[i];
        l->width = l->width < 0 ? 0.75f : l->width;
        l->color = l->color != 0 ? l->color : 0xff000000u;
    }
}

/* ---------------------------------------------------------------- labels  */

const ch_label_t *
ch_own_label(const ch_series_t *s, int i)
{
    int k;

    for (k = 0; k < s->nown; k++) {
        if (s->own[k].idx == i) {
            return &s->own[k];
        }
    }
    return NULL;
}

int
ch_labels_of(const ch_series_t *s, int i)
{
    const ch_label_t *l = ch_own_label(s, i);

    return l != NULL ? l->labels : s->labels;
}

/** @brief Where point @p i of @p s puts its label: CH_AT_*, or -1 where
 *         its kind puts it. */
static int
at_of(const ch_series_t *s, int i)
{
    const ch_label_t *l = ch_own_label(s, i);

    return l != NULL && l->at >= 0 ? l->at : s->lbl_at;
}

/* ---------------------------------------------------------------- points  */

/** @brief Point @p i of series @p s: NAN where it has none, unless the
 *         chart counts missing points as zero. */
static double
value(const pl_t *q, const ch_series_t *s, int i)
{
    double v = i < s->n ? s->v[i] : NAN;

    return isnan(v) && q->k->blanks == 1 ? 0 : v;
}

/** @brief A label for point @p i of @p s noted at @p x, @p y. */
static void
note(pl_t *q, const ch_series_t *s, int i, float x, float y, int align,
     int valign, double pct)
{
    pl_label_t *l;

    if (q->nlbl >= q->cap) {
        return;
    }
    l = &q->lbl[q->nlbl++];
    l->s = s;
    l->i = i;
    l->x = x;
    l->y = y;
    l->align = align;
    l->valign = valign;
    l->pct = pct;
}

/** @brief The group's stacks emptied, and for one stacked to a hundred
 *         percent each category's whole found. */
static void
stacks_begin(pl_t *q)
{
    int i, s;

    for (i = 0; i < q->k->ncat; i++) {
        q->up[i] = q->down[i] = q->total[i] = 0;
        for (s = 0; s < q->g->nser; s++) {
            double v = value(q, &q->g->ser[s], i);

            q->total[i] += isnan(v) ? 0 : fabs(v);
        }
    }
}

/** @brief Whether the group stacks its series. */
static int
stacking(const pl_t *q)
{
    return q->g->grouping == CH_STACKED || q->g->grouping == CH_PERCENT;
}

/** @brief Bar @p i of series @p s: from @p from to @p to on the value
 *         axis, on its stack.  @return 0 for a point with no value. */
static int
bar_span(pl_t *q, const ch_series_t *s, int i, double *from, double *to)
{
    double v = value(q, s, i), *run;

    if (isnan(v) || i >= q->k->ncat) {
        return 0;
    }
    if (!stacking(q)) {
        *from = q->base;
        *to = v;
        return 1;
    }
    v = q->g->grouping == CH_PERCENT ? (q->total[i] > 0 ? v / q->total[i]
                                                        : 0) : v;
    run = v >= 0 ? &q->up[i] : &q->down[i];
    *from = *run;
    *to = *run += v;
    return 1;
}

/** @brief Line or area point @p i of @p s as its stack puts it, what is
 *         under it into @p under.  NAN for a point with no value. */
static double
piled(pl_t *q, const ch_series_t *s, int i, double *under)
{
    double v = value(q, s, i);

    *under = q->base;
    if (!stacking(q) || i >= q->k->ncat) {
        return v;
    }
    *under = q->up[i];
    if (isnan(v)) {
        return v;
    }
    v = q->g->grouping == CH_PERCENT ? (q->total[i] > 0 ? v / q->total[i]
                                                        : 0) : v;
    return q->up[i] += v;
}

/** @brief @p part added to label @p out, after the series' separator
 *         when something is there before it. */
static void
add(const ch_series_t *s, const char *part, char *out, size_t max)
{
    size_t n = strlen(out);

    if (part == NULL || part[0] == '\0') {
        return;
    }
    snprintf(out + n, max - n, "%s%s", n > 0 ? s->sep : "", part);
}

/** @brief Label @p l's words: its own, or its series' name, its
 *         category, its value and its share, as it asks. */
static void
label_text(const pl_t *q, const pl_label_t *l, char *out, size_t max)
{
    const ch_series_t *s = l->s;
    const ch_label_t *own = ch_own_label(s, l->i);
    int shows = ch_labels_of(s, l->i);
    char part[160];

    out[0] = '\0';
    if (own != NULL && own->text != NULL) {
        snprintf(out, max, "%s", own->text);
        return;
    }
    if (shows & CH_LBL_SER) {
        add(s, s->name, out, max);
    }
    if (shows & CH_LBL_CAT) {
        if (s->x != NULL) {
            ch_number(s->x[l->i], "General", part, sizeof part);
        } else if (q->k->cat != NULL && l->i < q->k->nnamed) {
            snprintf(part, sizeof part, "%s", q->k->cat[l->i]);
        } else {
            snprintf(part, sizeof part, "%d", l->i + 1);
        }
        add(s, part, out, max);
    }
    if (shows & CH_LBL_VAL) {
        ch_number(s->v[l->i], s->fmt, part, sizeof part);
        add(s, part, out, max);
    }
    if ((shows & CH_LBL_PCT) && !isnan(l->pct)) {
        ch_number(l->pct, "0%", part, sizeof part);
        add(s, part, out, max);
    }
}

/* ------------------------------------------------------------------ bars  */

/** @brief Bar @p i's label: past its end, inside it, at its middle or at
 *         its base, @p base and @p end where its value begins and ends. */
static void
bar_label(pl_t *q, const ch_series_t *s, int i, const ch_box_t *b,
          float base, float end)
{
    int at = at_of(s, i) >= 0 ? at_of(s, i) : stacking(q) ? CH_AT_CENTRE
                                                      : CH_AT_OUT;
    float d = end >= base ? 1 : -1, mid, p, ext;

    mid = q->g->across ? b->y + b->h / 2 : b->x + b->w / 2;
    p = at == CH_AT_OUT ? end + d * PL_GAP : at == CH_AT_IN
        ? end - d * PL_GAP : at == CH_AT_BASE ? base + d * PL_GAP
        : (base + end) / 2;
    ext = at == CH_AT_OUT || at == CH_AT_BASE ? d : at == CH_AT_IN ? -d : 0;
    if (q->g->across) {
        note(q, s, i, p, mid, ext > 0 ? 0 : ext < 0 ? 2 : 1, 1, NAN);
    } else {
        note(q, s, i, mid, p, 1, ext > 0 ? 0 : ext < 0 ? 2 : 1, NAN);
    }
}

/** @brief Bar @p i of @p s, its near edge @p lead from the category's
 *         middle and @p bw across (both signed along the axis). */
static void
bar(pl_t *q, const ch_series_t *s, int i, float lead, float bw)
{
    double from, to;
    float c, a, b;
    dml_fill_t f;
    dml_line_t l;
    ch_box_t box;

    if (!bar_span(q, s, i, &from, &to)) {
        return;
    }
    c = ch_place(q->cat, i) + lead;
    a = ch_place(q->val, from);
    b = ch_place(q->val, to);
    box.x = q->g->across ? fminf(a, b) : fminf(c, c + bw);
    box.y = q->g->across ? fminf(c, c + bw) : fminf(a, b);
    box.w = q->g->across ? fabsf(b - a) : fabsf(bw);
    box.h = q->g->across ? fabsf(bw) : fabsf(b - a);
    ch_point_fill(q->k, q->g, s, i, &f);
    point_line(q, s, i, &l);
    if (box.w > 0.01f && box.h > 0.01f) {
        ch_rect(q->k, &box, &f, l.width > 0 ? &l : NULL);
    }
    if (ch_labels_of(s, i) != 0) {
        bar_label(q, s, i, &box, a, b);
    }
}

/** @brief The group's bars: each category's band shared among its series
 *         side by side, or one stack, the gap and overlap the file's. */
static void
bars(pl_t *q)
{
    const ch_group_t *g = q->g;
    int m = stacking(q) ? 1 : g->nser, si, i;
    double band = fabs(q->cat->to - q->cat->from) / (q->cat->hi - q->cat->lo);
    double ov = g->overlap / 100, bw = band / (m - (m - 1) * ov + g->gap /
                                               100);
    float dir = ch_place(q->cat, 1) >= ch_place(q->cat, 0) ? 1 : -1;

    for (si = 0; si < g->nser; si++) {
        const ch_series_t *s = &g->ser[si];
        double lead = -(m * bw - (m - 1) * ov * bw) / 2 +
                      (m > 1 ? si : 0) * bw * (1 - ov);

        for (i = 0; i < s->n; i++) {
            bar(q, s, i, (float)(dir * lead), (float)(dir * bw));
        }
    }
}

/* ----------------------------------------------------------------- lines  */

/** @brief One unbroken run of @p m points (indices @p r into @p pt) added
 *         to @p p: straight, or smoothed through its neighbours. */
static void
run_path(ch_path_t *p, const float *pt, const int *r, int m, int smooth)
{
    int a;

    for (a = 0; a < m; a++) {
        const float *c = pt + 3 * r[a], *b, *z, *d;

        if (a == 0 || !smooth || m < 3) {
            (a == 0 ? ch_move : ch_line)(p, c[0], c[1]);
            continue;
        }
        b = pt + 3 * r[a - 1];
        z = pt + 3 * r[a > 1 ? a - 2 : a - 1];
        d = pt + 3 * r[a + 1 < m ? a + 1 : a];
        ch_curve(p, b[0] + (c[0] - z[0]) / 6, b[1] + (c[1] - z[1]) / 6,
                 c[0] - (d[0] - b[0]) / 6, c[1] - (d[1] - b[1]) / 6, c[0],
                 c[1]);
    }
}

/** @brief A series' line through its @p n points, broken where one is
 *         missing unless the chart spans them. */
static void
trace(pl_t *q, const ch_series_t *s, int n, const dml_line_t *l)
{
    ch_path_t p;
    int i, j, m;

    ch_path(&p, 0, 0, 0, 1);
    for (i = 0; i < n; i = j) {
        m = 0;
        for (j = i; j < n; j++) {
            if (q->pt[3 * j + 2] != 0) {
                q->run[m++] = j;
            } else if (m > 0 && q->k->blanks != 2) {
                break;
            }
        }
        run_path(&p, q->pt, q->run, m, s->smooth);
    }
    ch_paint(q->k, &p, NULL, l, 0, 0);
}

/** @brief Point @p i's label beside it, clear of its marker @p mk. */
static void
point_label(pl_t *q, const ch_series_t *s, int i, float mk)
{
    int at = at_of(s, i) >= 0 ? at_of(s, i) : CH_AT_RIGHT;
    float x = q->pt[3 * i], y = q->pt[3 * i + 1], r = mk / 2 + PL_GAP;

    if (at == CH_AT_LEFT) {
        note(q, s, i, x - r, y, 2, 1, NAN);
    } else if (at == CH_AT_ABOVE || at == CH_AT_OUT) {
        note(q, s, i, x, y - r, 1, 2, NAN);
    } else if (at == CH_AT_BELOW) {
        note(q, s, i, x, y + r, 1, 0, NAN);
    } else if (at == CH_AT_CENTRE || at == CH_AT_IN) {
        note(q, s, i, x, y, 1, 1, NAN);
    } else {
        note(q, s, i, x + r, y, 0, 1, NAN);
    }
}

/** @brief A series' line, then its markers and labels, over its @p n
 *         points in q->pt. */
static void
series_points(pl_t *q, const ch_series_t *s, int n)
{
    int mk = ch_marker_of(q->g, s), i;
    dml_fill_t f;
    dml_line_t l;

    ch_series_line(q->k, q->g, s, &l);
    if (l.width > 0) {
        trace(q, s, n, &l);
    }
    ch_marker_look(q->k, q->g, s, &f, &l);
    for (i = 0; i < n; i++) {
        if (q->pt[3 * i + 2] == 0) {
            continue;
        }
        if (mk != CH_MK_NONE) {
            ch_marker(q->k, mk, s->mk_size, q->pt[3 * i], q->pt[3 * i + 1],
                      &f, &l);
        }
        if (ch_labels_of(s, i) != 0) {
            point_label(q, s, i, mk != CH_MK_NONE ? s->mk_size : 0);
        }
    }
}

/** @brief A line group's high-low lines: each category's lowest point to
 *         its highest. */
static void
hilo(pl_t *q)
{
    int i, s;

    for (i = 0; q->g->hilo && i < q->k->ncat; i++) {
        double lo = INFINITY, hi = -INFINITY;
        float x = ch_place(q->cat, i);

        for (s = 0; s < q->g->nser; s++) {
            double v = value(q, &q->g->ser[s], i);

            lo = isnan(v) || v >= lo ? lo : v;
            hi = isnan(v) || v <= hi ? hi : v;
        }
        if (hi > lo) {
            ch_segment(q->k, x, ch_place(q->val, lo), x, ch_place(q->val, hi),
                       &q->g->hilo_line);
        }
    }
}

/** @brief A line group's up-down bars: from its first series to its last,
 *         light going up, dark going down. */
static void
updown(pl_t *q)
{
    const ch_group_t *g = q->g;
    double band = fabs(q->cat->to - q->cat->from) / (q->cat->hi - q->cat->lo);
    float w = (float)(band / (1 + g->updown_gap / 100));
    ch_box_t b;
    int i;

    for (i = 0; g->updown && g->nser > 1 && i < q->k->ncat; i++) {
        double a = value(q, &g->ser[0], i), z = value(q, &g->ser[g->nser - 1],
                                                     i);
        float ya, yz;

        if (isnan(a) || isnan(z)) {
            continue;
        }
        ya = ch_place(q->val, a);
        yz = ch_place(q->val, z);
        b.x = ch_place(q->cat, i) - w / 2;
        b.w = w;
        b.y = fminf(ya, yz);
        b.h = fabsf(yz - ya);
        ch_rect(q->k, &b, z >= a ? &g->up : &g->down, &g->updown_line);
    }
}

/** @brief The group's lines: up-down bars and high-low lines under them,
 *         each series' points piled on those before when it stacks. */
static void
lines(pl_t *q)
{
    int si, i;

    updown(q);
    hilo(q);
    for (si = 0; si < q->g->nser; si++) {
        const ch_series_t *s = &q->g->ser[si];

        for (i = 0; i < s->n; i++) {
            double under, v = piled(q, s, i, &under);

            q->pt[3 * i] = ch_place(q->cat, i);
            q->pt[3 * i + 1] = isnan(v) ? 0 : ch_place(q->val, v);
            q->pt[3 * i + 2] = !isnan(v);
        }
        series_points(q, s, s->n);
    }
}

/* ----------------------------------------------------------------- areas  */

/** @brief The group's areas, in order: each down to its base, or to the
 *         series under it when they stack; a missing point counts as
 *         nothing. */
static void
areas(pl_t *q, const ch_box_t *p)
{
    int si, i;

    for (si = 0; si < q->g->nser; si++) {
        const ch_series_t *s = &q->g->ser[si];
        ch_path_t path;
        dml_fill_t f;
        dml_line_t l;

        for (i = 0; i < s->n; i++) {
            double under, v = piled(q, s, i, &under);

            q->pt[3 * i] = ch_place(q->cat, i);
            q->pt[3 * i + 1] = ch_place(q->val, isnan(v) ? under : v);
            q->pt[3 * i + 2] = ch_place(q->val, under);
            if (ch_labels_of(s, i) != 0 && !isnan(v)) {
                note(q, s, i, q->pt[3 * i], (q->pt[3 * i + 1] +
                     q->pt[3 * i + 2]) / 2, 1, 1, NAN);
            }
        }
        ch_path(&path, p->x, p->y, 1, 1);
        for (i = 0; i < s->n; i++) {
            (i == 0 ? ch_move : ch_line)(&path, q->pt[3 * i],
                                         q->pt[3 * i + 1]);
        }
        for (i = s->n - 1; i >= 0; i--) {
            ch_line(&path, q->pt[3 * i], q->pt[3 * i + 2]);
        }
        ch_close(&path);
        ch_point_fill(q->k, q->g, s, -1, &f);
        ch_series_line(q->k, q->g, s, &l);
        ch_paint(q->k, &path, &f, l.width > 0 ? &l : NULL, p->w, p->h);
    }
}

/* ------------------------------------------------------------------ pies  */

/** @brief A slice's label: inside it, or outside for "outEnd", saying
 *         its @p share of the whole when asked. */
static void
slice_label(pl_t *q, const ch_series_t *s, int i, const pl_arc_t *c,
            double share)
{
    double mid = (c->a0 + c->a1) / 2;
    int at = at_of(s, i), out = at == CH_AT_OUT && c->r0 <= 0;
    float r = c->r0 > 0 ? (c->r0 + c->r1) / 2 : out ? c->r1 + PL_GAP
            : at == CH_AT_IN ? c->r1 * 0.8f : at == CH_AT_BASE
            ? c->r1 * 0.35f : c->r1 * 0.62f;

    note(q, s, i, c->cx + r * (float)sin(mid), c->cy - r * (float)cos(mid),
         !out ? 1 : sin(mid) >= 0 ? 0 : 2, !out ? 1 : cos(mid) > 0.7 ? 2
         : cos(mid) < -0.7 ? 0 : 1, share);
}

/** @brief Point @p i of @p s as a slice of @p c, pulled out as it asks. */
static void
slice(pl_t *q, const ch_series_t *s, int i, pl_arc_t *c, double share)
{
    double mid = (c->a0 + c->a1) / 2;
    float off = (s->pt_explode != NULL ? s->pt_explode[i] : s->explode) /
                100 * c->r1;
    float x = c->cx + off * (float)sin(mid), y = c->cy - off * (float)cos(mid);
    dml_fill_t f;
    dml_line_t l;
    ch_path_t p;

    ch_path(&p, x - c->r1, y - c->r1, 1, 1);
    if (c->r0 > 0) {
        ch_move(&p, x + c->r1 * (float)sin(c->a0), y - c->r1 *
                (float)cos(c->a0));
        ch_arc(&p, x, y, c->r1, c->a0, c->a1);
        ch_line(&p, x + c->r0 * (float)sin(c->a1), y - c->r0 *
                (float)cos(c->a1));
        ch_arc(&p, x, y, c->r0, c->a1, c->a0);
    } else if (c->a1 - c->a0 >= 2 * M_PI - 1e-6) {
        ch_move(&p, x, y - c->r1);
        ch_arc(&p, x, y, c->r1, 0, 2 * M_PI);
    } else {
        ch_move(&p, x, y);
        ch_line(&p, x + c->r1 * (float)sin(c->a0), y - c->r1 *
                (float)cos(c->a0));
        ch_arc(&p, x, y, c->r1, c->a0, c->a1);
    }
    ch_close(&p);
    ch_point_fill(q->k, q->g, s, i, &f);
    point_line(q, s, i, &l);
    ch_paint(q->k, &p, &f, l.width > 0 ? &l : NULL, 2 * c->r1, 2 * c->r1);
    if (ch_labels_of(s, i) != 0) {
        c->cx = x;
        c->cy = y;
        slice_label(q, s, i, c, share);
    }
}

/** @brief Series @p s as a ring of slices between @p c's radii, each its
 *         value's share of their whole; a pie's ring has no hole. */
static void
ring(pl_t *q, const ch_series_t *s, const pl_arc_t *c)
{
    double total = 0, a = q->g->first * M_PI / 180;
    pl_arc_t one;
    int i;

    for (i = 0; i < s->n; i++) {
        total += isnan(s->v[i]) ? 0 : fabs(s->v[i]);
    }
    for (i = 0; total > 0 && i < s->n; i++) {
        double share = isnan(s->v[i]) ? 0 : fabs(s->v[i]) / total;

        if (share > 0) {
            one = *c;
            one.a0 = a;
            one.a1 = a + share * 2 * M_PI;
            slice(q, s, i, &one, share);
        }
        a += share * 2 * M_PI;
    }
}

/** @brief How far past a pie's edge its labels set outside it reach,
 *         across into @p far[0] and down into @p far[1]: nothing when
 *         none are. */
static void
outside(const pl_t *q, const ch_series_t *s, float *far)
{
    char text[512];
    double total = 0;
    float w, h;
    pl_label_t l;
    int i;

    far[0] = far[1] = 0;
    for (i = 0; i < s->n; i++) {
        total += isnan(s->v[i]) ? 0 : fabs(s->v[i]);
    }
    for (i = 0; i < s->n; i++) {
        if (at_of(s, i) != CH_AT_OUT || ch_labels_of(s, i) == 0 ||
            isnan(s->v[i])) {
            continue;
        }
        l.s = s;
        l.i = i;
        l.pct = total > 0 ? fabs(s->v[i]) / total : NAN;
        label_text(q, &l, text, sizeof text);
        ch_block_size(q->k, &s->lbl_font, text, &w, &h);
        far[0] = fmaxf(far[0], w + PL_GAP);
        far[1] = fmaxf(far[1], h + PL_GAP);
    }
}

/** @brief A pie (its first series) or a doughnut (a ring for each, the
 *         first innermost), in the middle of plot box @p p, room kept
 *         round a pie for labels set outside it. */
static void
pies(pl_t *q, const ch_box_t *p)
{
    const ch_group_t *g = q->g;
    int rings = g->kind == CH_DOUGHNUT ? g->nser : 1, si, i;
    float most = 0, r = fminf(p->w, p->h) / 2 * 0.92f, hole, far[2];
    pl_arc_t c;

    if (g->kind == CH_PIE) {
        outside(q, &g->ser[0], far);
        r = fmaxf(fminf(r, fminf(p->w / 2 - far[0], p->h / 2 - far[1])),
                  r / 4);
    }

    for (si = 0; si < rings; si++) {
        const ch_series_t *s = &g->ser[si];

        most = s->explode > most ? s->explode : most;
        for (i = 0; s->pt_explode != NULL && i < s->n; i++) {
            most = s->pt_explode[i] > most ? s->pt_explode[i] : most;
        }
    }
    r /= 1 + most / 100;
    hole = g->kind == CH_DOUGHNUT ? r * g->hole / 100 : 0;
    c.cx = p->x + p->w / 2;
    c.cy = p->y + p->h / 2;
    for (si = 0; r > 0 && si < rings; si++) {
        c.r0 = hole + (r - hole) * si / rings;
        c.r1 = hole + (r - hole) * (si + 1) / rings;
        ring(q, &g->ser[si], &c);
    }
}

/* -------------------------------------------------------------- scatters  */

/** @brief A bubble series: each point a circle, its area its size's
 *         share of the group's largest, @p big across. */
static void
bubbles(pl_t *q, const ch_series_t *s, double most, float big)
{
    dml_fill_t f;
    dml_line_t l;
    ch_path_t p;
    int i;

    for (i = 0; i < s->n; i++) {
        float r = most > 0 && s->size[i] > 0
                  ? big / 2 * (float)sqrt(s->size[i] / most) : 0;
        float x = q->pt[3 * i], y = q->pt[3 * i + 1];

        if (q->pt[3 * i + 2] == 0 || !(r > 0)) {
            continue;
        }
        ch_path(&p, x - r, y - r, 1, 1);
        ch_move(&p, x, y - r);
        ch_arc(&p, x, y, r, 0, 2 * M_PI);
        ch_close(&p);
        ch_point_fill(q->k, q->g, s, i, &f);
        point_line(q, s, i, &l);
        ch_paint(q->k, &p, &f, l.width > 0 ? &l : NULL, 2 * r, 2 * r);
        if (ch_labels_of(s, i) != 0) {
            note(q, s, i, x, y, 1, 1, NAN);
        }
    }
}

/** @brief The group's scatters: points at their x and y, a line through
 *         them and their markers, or bubbles, in plot box @p p. */
static void
scatter(pl_t *q, const ch_box_t *p)
{
    double most = 0;
    int si, i;

    for (si = 0; si < q->g->nser; si++) {
        const ch_series_t *s = &q->g->ser[si];

        for (i = 0; s->size != NULL && i < s->n; i++) {
            most = s->size[i] > most ? s->size[i] : most;
        }
    }
    for (si = 0; si < q->g->nser; si++) {
        const ch_series_t *s = &q->g->ser[si];

        for (i = 0; i < s->n; i++) {
            double v = value(q, s, i), x = s->x != NULL ? s->x[i] : i + 1;

            q->pt[3 * i] = ch_place(q->cat, isnan(x) ? 0 : x);
            q->pt[3 * i + 1] = ch_place(q->val, isnan(v) ? 0 : v);
            q->pt[3 * i + 2] = !isnan(v) && !isnan(x);
        }
        if (s->size != NULL) {
            bubbles(q, s, most, fminf(p->w, p->h) / 4 *
                    q->g->bubble_scale / 100);
        } else {
            series_points(q, s, s->n);
        }
    }
}

/* ------------------------------------------------------------------ plot  */

void
ch_clip(const ch_chart_t *k, const ch_box_t *p)
{
    const float x[4] = {p->x, p->x + p->w, p->x + p->w, p->x};
    const float y[4] = {p->y, p->y, p->y + p->h, p->y + p->h};
    const dml_mat_t *m = k->m;
    int i;

    tiku_pdf_writer_save(k->c->w);
    for (i = 0; i < 4; i++) {
        (i == 0 ? tiku_pdf_writer_move : tiku_pdf_writer_line)(
            k->c->w, m->a * x[i] + m->c * y[i] + m->e,
            m->b * x[i] + m->d * y[i] + m->f);
    }
    tiku_pdf_writer_close(k->c->w);
    tiku_pdf_writer_clip(k->c->w, 0);
}

/** @brief The group's series' trendlines, held inside plot box @p p. */
static void
trends(pl_t *q, const ch_box_t *p)
{
    int s;

    for (s = 0; s < q->g->nser; s++) {
        if (q->g->ser[s].ntrend > 0 && q->g->kind != CH_AREA &&
            q->g->kind != CH_PIE && q->g->kind != CH_DOUGHNUT) {
            ch_clip(q->k, p);
            ch_trends(q->k, &q->g->ser[s], q->cat, q->val,
                      q->g->kind == CH_BAR && q->g->across);
            tiku_pdf_writer_restore(q->k->c->w);
        }
    }
}

/** @brief One group drawn in plot box @p p, its labels noted. */
static void
group(pl_t *q, const ch_group_t *g, const ch_box_t *p)
{
    int axes = g->kind != CH_PIE && g->kind != CH_DOUGHNUT;

    if (axes && (g->cat_ax < 0 || g->val_ax < 0)) {
        return;
    }
    if (g->kind == CH_RADAR) {
        ch_radar(q->k, g, p);
        return;
    }
    q->g = g;
    q->cat = axes ? &q->k->ax[g->cat_ax] : NULL;
    q->val = axes ? &q->k->ax[g->val_ax] : NULL;
    q->base = axes ? ch_cross(q->k, q->val) : 0;
    stacks_begin(q);
    if (g->kind == CH_BAR || g->kind == CH_AREA) {
        ch_clip(q->k, p);
        if (g->kind == CH_BAR) {
            bars(q);
        } else {
            areas(q, p);
        }
        tiku_pdf_writer_restore(q->k->c->w);
    } else if (g->kind == CH_LINE) {
        lines(q);
    } else if (g->kind == CH_SCATTER) {
        scatter(q, p);
    } else {
        pies(q, p);
    }
    trends(q, p);
}

/** @brief How many labels the chart's points ask for, at most. */
static int
label_count(const ch_chart_t *k)
{
    int g, s, n = 0;

    for (g = 0; g < k->ngrp; g++) {
        for (s = 0; s < k->grp[g].nser; s++) {
            n += k->grp[g].ser[s].labels != 0 || k->grp[g].ser[s].nown > 0
                 ? k->grp[g].ser[s].n : 0;
        }
    }
    return n;
}

void
ch_plot(ch_chart_t *k, const ch_box_t *p)
{
    static const int order[] = {CH_RADAR, CH_AREA, CH_BAR, CH_PIE,
                                CH_DOUGHNUT, CH_LINE, CH_SCATTER};
    size_t n = (size_t)(k->ncat > 0 ? k->ncat : 1);
    char text[512];
    pl_t q;
    int o, g;

    memset(&q, 0, sizeof q);
    q.k = k;
    q.up = of_alloc(&k->mem, n * sizeof *q.up);
    q.down = of_alloc(&k->mem, n * sizeof *q.down);
    q.total = of_alloc(&k->mem, n * sizeof *q.total);
    q.pt = of_alloc(&k->mem, 3 * n * sizeof *q.pt);
    q.run = of_alloc(&k->mem, n * sizeof *q.run);
    q.cap = label_count(k);
    q.lbl = q.cap > 0 ? of_alloc(&k->mem, (size_t)q.cap * sizeof *q.lbl)
                      : NULL;
    if (k->mem.bad) {
        return;
    }
    for (o = 0; o < (int)(sizeof order / sizeof order[0]); o++) {
        for (g = 0; g < k->ngrp; g++) {
            if (k->grp[g].kind == order[o]) {
                group(&q, &k->grp[g], p);
            }
        }
    }
    for (o = 0; o < q.nlbl; o++) {
        label_text(&q, &q.lbl[o], text, sizeof text);
        ch_text_block(k, &q.lbl[o].s->lbl_font, text, q.lbl[o].x,
                      q.lbl[o].y, q.lbl[o].align, q.lbl[o].valign);
    }
}
