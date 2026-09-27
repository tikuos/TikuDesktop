/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_raster.c - ink on pixels: filling a path, stroking one, and
 * the masks that clip them.
 *
 * A path arrives already flattened into straight pieces in device
 * space.  Filling walks scanlines a few times per pixel row, sorts the
 * crossings, and lays coverage between the ones the winding rule keeps,
 * exactly at the ends; stroking turns each piece and joint into a small
 * polygon and fills the lot nonzero, which is how their union is drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SUB        4                /* scanlines sampled per pixel row  */
#define POINTS_MAX 4000000          /* a path this long is not one      */
#define FLAT_MAX   64               /* pieces one curve becomes at most */
#define JOIN_MIN   1.2              /* device width below which joints
                                       and caps are not worth drawing   */

/* ------------------------------------------------------------ matrices  */

pdf_mat_t
pdf_mat(double a, double b, double c, double d, double e, double f)
{
    pdf_mat_t m = {a, b, c, d, e, f};

    return m;
}

pdf_mat_t
pdf_mat_mul(pdf_mat_t m, pdf_mat_t n)
{
    pdf_mat_t r;

    r.a = m.a * n.a + m.b * n.c;
    r.b = m.a * n.b + m.b * n.d;
    r.c = m.c * n.a + m.d * n.c;
    r.d = m.c * n.b + m.d * n.d;
    r.e = m.e * n.a + m.f * n.c + n.e;
    r.f = m.e * n.b + m.f * n.d + n.f;
    return r;
}

void
pdf_mat_apply(const pdf_mat_t *m, double x, double y, double *ox, double *oy)
{
    *ox = m->a * x + m->c * y + m->e;
    *oy = m->b * x + m->d * y + m->f;
}

double
pdf_mat_scale(const pdf_mat_t *m)
{
    double det = fabs(m->a * m->d - m->b * m->c);

    return det > 0 ? sqrt(det) : hypot(m->a, m->b);
}

/* ---------------------------------------------------------------- paths  */

void
pdf_path_init(pdf_path_t *p)
{
    memset(p, 0, sizeof *p);
}

void
pdf_path_clear(pdf_path_t *p)
{
    p->n = 0;
    p->nsub = 0;
    p->failed = 0;
}

void
pdf_path_free(pdf_path_t *p)
{
    free(p->pt);
    free(p->start);
    free(p->closed);
    pdf_path_init(p);
}

/** @brief Room for one more point.  @return 0 when there is none. */
static int
room(pdf_path_t *p)
{
    if (p->n + 1 > p->cap) {
        int cap = p->cap ? p->cap * 2 : 256;
        float *pt;

        if (cap > POINTS_MAX) {
            p->failed = 1;
            return 0;
        }
        pt = realloc(p->pt, (size_t)cap * 2 * sizeof *pt);
        if (pt == NULL) {
            p->failed = 1;
            return 0;
        }
        p->pt = pt;
        p->cap = cap;
    }
    return 1;
}

/** @brief A coordinate the rasteriser can hold. */
static float
sane(double v)
{
    if (!(v > -1e6)) {
        return (v == v) ? -1e6f : 0.0f;
    }
    if (!(v < 1e6)) {
        return 1e6f;
    }
    return (float)v;
}

/** @brief One more point on the current subpath, room allowing. */
static void
add_point(pdf_path_t *p, double x, double y)
{
    if (!room(p)) {
        return;
    }
    p->pt[p->n * 2] = sane(x);
    p->pt[p->n * 2 + 1] = sane(y);
    p->n++;
    p->cx = sane(x);
    p->cy = sane(y);
}

void
pdf_path_moveto(pdf_path_t *p, double x, double y)
{
    if (p->nsub + 1 > p->subcap) {
        int cap = p->subcap ? p->subcap * 2 : 16;
        int *s = realloc(p->start, (size_t)cap * sizeof *s);
        int *c = s ? realloc(p->closed, (size_t)cap * sizeof *c) : NULL;

        if (s == NULL || c == NULL) {
            free(s == p->start ? NULL : s);
            p->failed = 1;
            return;
        }
        p->start = s;
        p->closed = c;
        p->subcap = cap;
    }
    p->start[p->nsub] = p->n;
    p->closed[p->nsub] = 0;
    p->nsub++;
    add_point(p, x, y);
    p->sx = p->cx;
    p->sy = p->cy;
}

void
pdf_path_lineto(pdf_path_t *p, double x, double y)
{
    if (p->nsub == 0) {
        pdf_path_moveto(p, x, y);
        return;
    }
    add_point(p, x, y);
}

void
pdf_path_curveto(pdf_path_t *p, double x1, double y1, double x2, double y2,
                 double x3, double y3)
{
    double x0 = p->cx, y0 = p->cy, len;
    int steps, i;

    if (p->nsub == 0) {
        pdf_path_moveto(p, x0, y0);
    }
    len = hypot(x1 - x0, y1 - y0) + hypot(x2 - x1, y2 - y1) +
          hypot(x3 - x2, y3 - y2);
    steps = (int)(sqrt(len * 2.0) + 1.0);
    if (steps < 3) {
        steps = 3;
    }
    if (steps > FLAT_MAX) {
        steps = FLAT_MAX;
    }
    for (i = 1; i <= steps; i++) {
        double t = (double)i / steps, u = 1.0 - t;
        double x = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 +
                   t * t * t * x3;
        double y = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 +
                   t * t * t * y3;

        add_point(p, x, y);
    }
}

void
pdf_path_close(pdf_path_t *p)
{
    if (p->nsub > 0) {
        p->closed[p->nsub - 1] = 1;
        p->cx = p->sx;
        p->cy = p->sy;
    }
}

int
pdf_path_bounds(const pdf_path_t *p, int w, int h, int *x0, int *y0,
                int *bw, int *bh)
{
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    int i, ax, ay, bx, by;

    for (i = 0; i < p->n; i++) {
        float x = p->pt[i * 2], y = p->pt[i * 2 + 1];

        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    if (p->n == 0) {
        return 0;
    }
    ax = (int)floorf(minx);
    ay = (int)floorf(miny);
    bx = (int)ceilf(maxx) + 1;
    by = (int)ceilf(maxy) + 1;
    if (ax < 0) ax = 0;
    if (ay < 0) ay = 0;
    if (bx > w) bx = w;
    if (by > h) by = h;
    if (bx <= ax || by <= ay) {
        return 0;
    }
    *x0 = ax;
    *y0 = ay;
    *bw = bx - ax;
    *bh = by - ay;
    return 1;
}

/* -------------------------------------------------------------- filling  */

typedef struct { float x; int dir; } crossing_t;

/** @brief qsort's order for crossings: left to right. */
static int
by_x(const void *a, const void *b)
{
    float d = ((const crossing_t *)a)->x - ((const crossing_t *)b)->x;

    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

/** @brief Lay coverage over [xa, xb) of one sub-scanline into @p acc. */
static void
span(float *acc, int w, float xa, float xb)
{
    int ia, ib, i;

    if (xb <= 0 || xa >= (float)w) {
        return;
    }
    if (xa < 0) xa = 0;
    if (xb > (float)w) xb = (float)w;
    if (xb <= xa) {
        return;
    }
    ia = (int)xa;
    ib = (int)xb;
    if (ia == ib) {
        acc[ia] += xb - xa;
        return;
    }
    acc[ia] += (float)(ia + 1) - xa;
    for (i = ia + 1; i < ib && i < w; i++) {
        acc[i] += 1.0f;
    }
    if (ib < w) {
        acc[ib] += xb - (float)ib;
    }
}

pdf_mask_t
pdf_path_fill(const pdf_path_t *p, int evenodd, int x0, int y0, int w, int h)
{
    pdf_mask_t m = {x0, y0, w, h, NULL};
    crossing_t *cross;
    float *acc;
    int row, s, i, ncross_max = 0;

    if (p->n == 0 || w <= 0 || h <= 0 || p->failed) {
        m.w = m.h = 0;
        return m;
    }
    m.a = calloc((size_t)w * (size_t)h, 1);
    acc = calloc((size_t)w + 1, sizeof *acc);
    for (s = 0; s < p->nsub; s++) {
        int end = (s + 1 < p->nsub) ? p->start[s + 1] : p->n;

        ncross_max += end - p->start[s];
    }
    cross = malloc((size_t)(ncross_max + 1) * sizeof *cross);
    if (m.a == NULL || acc == NULL || cross == NULL) {
        free(m.a);
        free(acc);
        free(cross);
        m.a = NULL;
        m.w = m.h = 0;
        return m;
    }
    for (row = 0; row < h; row++) {
        int touched = 0;

        for (s = 0; s < SUB; s++) {
            float y = (float)(y0 + row) + ((float)s + 0.5f) / SUB;
            int ncross = 0, k, wind = 0;
            int sub;

            for (sub = 0; sub < p->nsub; sub++) {
                int first = p->start[sub];
                int end = (sub + 1 < p->nsub) ? p->start[sub + 1] : p->n;
                int n = end - first;

                for (i = 0; i < n; i++) {
                    const float *a = p->pt + (first + i) * 2;
                    const float *b = p->pt + (first + (i + 1) % n) * 2;
                    float ya = a[1], yb = b[1];
                    int dir = 1;

                    if (i + 1 == n && n < 2) {
                        continue;
                    }
                    if (ya == yb) {
                        continue;
                    }
                    if (ya > yb) {
                        const float *t = a;

                        a = b;
                        b = t;
                        ya = a[1];
                        yb = b[1];
                        dir = -1;
                    }
                    if (y < ya || y >= yb) {
                        continue;
                    }
                    cross[ncross].x = a[0] + (b[0] - a[0]) * (y - ya) /
                                      (yb - ya) - (float)x0;
                    cross[ncross].dir = dir;
                    ncross++;
                }
            }
            if (ncross < 2) {
                continue;
            }
            qsort(cross, (size_t)ncross, sizeof *cross, by_x);
            for (k = 0; k + 1 < ncross; k++) {
                wind += evenodd ? 1 : cross[k].dir;
                if (evenodd ? (wind & 1) : (wind != 0)) {
                    span(acc, w, cross[k].x, cross[k + 1].x);
                    touched = 1;
                }
            }
        }
        if (touched) {
            unsigned char *out = m.a + (size_t)row * (size_t)w;

            for (i = 0; i < w; i++) {
                float v = acc[i] * (255.0f / SUB);

                out[i] = (unsigned char)(v > 255.0f ? 255 : v + 0.5f);
                acc[i] = 0;
            }
        }
    }
    free(acc);
    free(cross);
    return m;
}

/* ------------------------------------------------------------- stroking  */

/** @brief A closed quadrilateral, wound one way, as a subpath of @p out. */
static void
quad(pdf_path_t *out, double ax, double ay, double bx, double by,
     double cx, double cy, double dx, double dy)
{
    pdf_path_moveto(out, ax, ay);
    pdf_path_lineto(out, bx, by);
    pdf_path_lineto(out, cx, cy);
    pdf_path_lineto(out, dx, dy);
    pdf_path_close(out);
}

/** @brief A filled circle of radius @p r at (x, y), as a polygon. */
static void
disc(pdf_path_t *out, double x, double y, double r)
{
    int n = r < 2 ? 8 : r < 6 ? 12 : 24, i;

    for (i = 0; i < n; i++) {
        double t = 2 * M_PI * i / n;

        if (i == 0) {
            pdf_path_moveto(out, x + r * cos(t), y + r * sin(t));
        } else {
            pdf_path_lineto(out, x + r * cos(t), y + r * sin(t));
        }
    }
    pdf_path_close(out);
}

/** @brief One straight piece, its body and a cap at each end asked for. */
static void
piece(pdf_path_t *out, double ax, double ay, double bx, double by,
      double hw, int cap, int cap_a, int cap_b)
{
    double dx = bx - ax, dy = by - ay, len = hypot(dx, dy), nx, ny;

    if (len <= 0) {
        if (cap == 1) {
            disc(out, ax, ay, hw);
        } else if (cap == 2) {
            quad(out, ax - hw, ay - hw, ax + hw, ay - hw, ax + hw, ay + hw,
                 ax - hw, ay + hw);
        }
        return;
    }
    dx /= len;
    dy /= len;
    if (cap == 2) {
        if (cap_a) { ax -= dx * hw; ay -= dy * hw; }
        if (cap_b) { bx += dx * hw; by += dy * hw; }
    }
    nx = -dy * hw;
    ny = dx * hw;
    quad(out, ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny,
         ax - nx, ay - ny);
    if (cap == 1) {
        if (cap_a) disc(out, ax, ay, hw);
        if (cap_b) disc(out, bx, by, hw);
    }
}

/** @brief The joint at @p b between pieces from @p a and to @p c. */
static void
joint(pdf_path_t *out, double ax, double ay, double bx, double by,
      double cx, double cy, double hw, int join, double miter)
{
    double d1x = bx - ax, d1y = by - ay, l1 = hypot(d1x, d1y);
    double d2x = cx - bx, d2y = cy - by, l2 = hypot(d2x, d2y);
    double cosang, theta;

    if (l1 <= 0 || l2 <= 0) {
        return;
    }
    d1x /= l1; d1y /= l1;
    d2x /= l2; d2y /= l2;
    if (join == 1) {
        disc(out, bx, by, hw);
        return;
    }
    cosang = d1x * d2x + d1y * d2y;
    if (cosang > 0.999) {
        return;                     /* straight on: no gap to fill */
    }
    /* The wedge between the two offset edges on the outside of the
     * turn, as a triangle to the corner; a miter adds the tip. */
    {
        double cross = d1x * d2y - d1y * d2x;
        double s = cross > 0 ? -1 : 1;   /* outside is opposite the turn */
        double p1x = bx + s * (-d1y) * hw, p1y = by + s * d1x * hw;
        double p2x = bx + s * (-d2y) * hw, p2y = by + s * d2x * hw;

        theta = acos(cosang < -1 ? -1 : cosang);
        if (join == 0 && miter >= 1 && theta < M_PI &&
            1.0 / sin((M_PI - theta) / 2) <= miter) {
            double mx = (p1x + p2x) / 2 - bx, my = (p1y + p2y) / 2 - by;
            double ml = hypot(mx, my), want = hw / cos(theta / 2);

            if (ml > 0) {
                mx = bx + mx / ml * want;
                my = by + my / ml * want;
                pdf_path_moveto(out, bx, by);
                pdf_path_lineto(out, p1x, p1y);
                pdf_path_lineto(out, mx, my);
                pdf_path_lineto(out, p2x, p2y);
                pdf_path_close(out);
                return;
            }
        }
        pdf_path_moveto(out, bx, by);
        pdf_path_lineto(out, p1x, p1y);
        pdf_path_lineto(out, p2x, p2y);
        pdf_path_close(out);
    }
}

/** @brief One subpath's pieces, cut by the dash pattern if there is one. */
static void
stroke_sub(const pdf_path_t *p, int sub, double hw, int cap, int join,
           double miter, const double *dash, int ndash, double phase,
           pdf_path_t *out)
{
    int first = p->start[sub];
    int end = (sub + 1 < p->nsub) ? p->start[sub + 1] : p->n;
    int n = end - first, closed = p->closed[sub], i, di = 0, on = 1;
    double left = 0, total = 0;
    int pieces = closed ? n : n - 1;

    if (n < 1) {
        return;
    }
    if (n == 1) {
        piece(out, p->pt[first * 2], p->pt[first * 2 + 1],
              p->pt[first * 2], p->pt[first * 2 + 1], hw, cap, 1, 1);
        return;
    }
    for (i = 0; i < ndash; i++) {
        total += dash[i] > 0 ? dash[i] : 0;
    }
    if (ndash > 0 && total > 0) {
        double ph = fmod(phase, total * (ndash & 1 ? 2 : 1));

        left = dash[0];
        while (ph >= left && ph > 0) {
            ph -= left;
            di = (di + 1) % ndash;
            on = !on;
            left = dash[di];
        }
        left -= ph;
    } else {
        ndash = 0;
    }
    for (i = 0; i < pieces; i++) {
        const float *a = p->pt + (first + i) * 2;
        const float *b = p->pt + (first + (i + 1) % n) * 2;
        double ax = a[0], ay = a[1], bx = b[0], by = b[1];
        double len = hypot(bx - ax, by - ay), at = 0;

        if (ndash == 0) {
            piece(out, ax, ay, bx, by, hw, cap, !closed && i == 0,
                  !closed && i == pieces - 1);
        } else {
            while (at < len) {
                double take = left < len - at ? left : len - at;
                double t0 = at / len, t1 = (at + take) / len;

                if (on && take > 0) {
                    piece(out, ax + (bx - ax) * t0, ay + (by - ay) * t0,
                          ax + (bx - ax) * t1, ay + (by - ay) * t1, hw, cap,
                          1, 1);
                }
                at += take;
                left -= take;
                if (left <= 1e-9) {
                    di = (di + 1) % ndash;
                    on = !on;
                    left = dash[di] > 0 ? dash[di] : 0.01;
                }
            }
        }
        if (hw >= JOIN_MIN / 2 && ndash == 0 && (closed || i + 1 < pieces)) {
            const float *c = p->pt + (first + (i + 2) % n) * 2;

            joint(out, ax, ay, bx, by, c[0], c[1], hw, join, miter);
        }
    }
}

void
pdf_path_stroke(const pdf_path_t *p, double width, int cap, int join,
                double miter, const double *dash, int ndash, double phase,
                pdf_path_t *out)
{
    double hw = width / 2;
    int s;

    pdf_path_clear(out);
    if (hw < 0.4) {
        hw = 0.4;                   /* the thinnest line the screen shows */
    }
    for (s = 0; s < p->nsub; s++) {
        stroke_sub(p, s, hw, cap, join, miter, dash, ndash, phase, out);
    }
}

/* ---------------------------------------------------------------- masks  */

void
pdf_mask_free(pdf_mask_t *m)
{
    free(m->a);
    m->a = NULL;
    m->w = m->h = 0;
}

pdf_mask_t
pdf_mask_full(int w, int h)
{
    pdf_mask_t m = {0, 0, w, h, NULL};

    m.a = malloc((size_t)w * (size_t)h);
    if (m.a != NULL) {
        memset(m.a, 255, (size_t)w * (size_t)h);
    } else {
        m.w = m.h = 0;
    }
    return m;
}

void
pdf_mask_intersect(pdf_mask_t *m, const pdf_mask_t *clip)
{
    int x, y;

    if (clip == NULL || clip->a == NULL || m->a == NULL) {
        return;
    }
    for (y = 0; y < m->h; y++) {
        int cy = m->y0 + y - clip->y0;

        for (x = 0; x < m->w; x++) {
            int cx = m->x0 + x - clip->x0;
            unsigned char *a = m->a + (size_t)y * m->w + x;

            if (cy < 0 || cy >= clip->h || cx < 0 || cx >= clip->w) {
                *a = 0;
            } else {
                *a = (unsigned char)((*a * clip->a[(size_t)cy * clip->w + cx]
                                      + 127) / 255);
            }
        }
    }
}

void
pdf_paint(pdf_pix_t *pix, const pdf_mask_t *m, const pdf_mask_t *clip,
          uint32_t colour, double alpha)
{
    int x, y, ca = (int)(alpha * 255 + 0.5);
    unsigned cr = (colour >> 16) & 0xff, cg = (colour >> 8) & 0xff;
    unsigned cb = colour & 0xff;

    if (m->a == NULL || ca <= 0) {
        return;
    }
    if (ca > 255) ca = 255;
    for (y = 0; y < m->h; y++) {
        int py = m->y0 + y;

        if (py < 0 || py >= pix->h) {
            continue;
        }
        for (x = 0; x < m->w; x++) {
            int px = m->x0 + x, a = m->a[(size_t)y * m->w + x];
            uint32_t *d;
            unsigned dr, dg, db;

            if (px < 0 || px >= pix->w || a == 0) {
                continue;
            }
            if (clip != NULL && clip->a != NULL) {
                int cx = px - clip->x0, cy = py - clip->y0;

                if (cx < 0 || cx >= clip->w || cy < 0 || cy >= clip->h) {
                    continue;
                }
                a = (a * clip->a[(size_t)cy * clip->w + cx] + 127) / 255;
            }
            a = (a * ca + 127) / 255;
            if (a == 0) {
                continue;
            }
            d = pix->px + (size_t)py * pix->w + px;
            dr = (*d >> 16) & 0xff;
            dg = (*d >> 8) & 0xff;
            db = *d & 0xff;
            dr = (dr * (255 - (unsigned)a) + cr * (unsigned)a + 127) / 255;
            dg = (dg * (255 - (unsigned)a) + cg * (unsigned)a + 127) / 255;
            db = (db * (255 - (unsigned)a) + cb * (unsigned)a + 127) / 255;
            *d = 0xff000000u | (dr << 16) | (dg << 8) | db;
        }
    }
}
