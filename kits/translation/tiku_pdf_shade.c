/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_shade.c - smooth shadings: a colour for every point, from a
 * function over the plane, along an axis, or between two circles.
 *
 * ISO 32000 8.7.4.5: types 1 to 3 are drawn exactly, per pixel, the
 * axial and radial ones through a table of colours along their axis.
 * The meshes (types 4 to 7) are in none of the documents read here and
 * are drawn as one tone, so what they fill keeps its place.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SHADE_LUT  1024             /* colours along an axis or a radius */

struct pdf_shade {
    int         type;
    pdf_cs_t   *cs;
    pdf_func_t *fn;                 /* NULL: the colour is the Background */
    double      coords[6];          /* axis ends, or circle centres, radii */
    double      domain[4];          /* t0 t1, or type 1's x0 x1 y0 y1     */
    int         extend[2];
    pdf_mat_t   matrix;             /* type 1: its domain into the space  */
    double      bbox[4];
    int         has_bbox, has_bg;
    uint32_t    bg;
    uint32_t    flat;               /* a mesh's one tone                  */
    uint32_t    lut[SHADE_LUT];     /* types 2 and 3: colour along t     */
};

/** @brief Up to @p max numbers of array @p a into @p v; @return how many. */
static int
numbers(tiku_pdf_t *doc, pdf_obj_t *a, double *v, int max)
{
    int i, n = 0;

    if (a == NULL || a->kind != PDF_ARRAY) {
        return 0;
    }
    for (i = 0; i < a->u.a.n && n < max; i++) {
        v[n++] = pdf_number(pdf_resolve(doc, a->u.a.item[i]), 0);
    }
    return n;
}

/** @brief The colour the function gives @p in, through the space. */
static uint32_t
colour_of(const pdf_shade_t *s, const double *in, int nin)
{
    double out[PDF_FUNC_MAX_OUT];
    int n = pdf_func_eval(s->fn, in, nin, out);

    if (n < 1) {
        return 0xff808080u;
    }
    return pdf_cs_colour(s->cs, out, n);
}

/** @brief The table of colours along t, for the axial and radial kinds. */
static void
fill_lut(pdf_shade_t *s)
{
    int i;

    for (i = 0; i < SHADE_LUT; i++) {
        double t = s->domain[0] + (s->domain[1] - s->domain[0]) * i /
                                  (SHADE_LUT - 1);

        s->lut[i] = s->fn != NULL ? colour_of(s, &t, 1) : 0xff808080u;
    }
}

pdf_shade_t *
pdf_shade_load(tiku_pdf_t *doc, pdf_obj_t *dict, pdf_obj_t *res)
{
    pdf_shade_t *s;
    double v[6];
    pdf_obj_t *e;

    if (dict == NULL || (dict->kind != PDF_DICT && dict->kind != PDF_STREAM)) {
        return NULL;
    }
    s = calloc(1, sizeof *s);
    if (s == NULL) {
        return NULL;
    }
    s->type = (int)pdf_number(pdf_get(doc, dict, "ShadingType"), 0);
    s->cs = pdf_cs_load(doc, pdf_get(doc, dict, "ColorSpace"), res);
    s->fn = pdf_func_load(doc, pdf_get(doc, dict, "Function"));
    s->domain[1] = s->domain[3] = 1;
    (void)numbers(doc, pdf_get(doc, dict, "Domain"), s->domain, 4);
    (void)numbers(doc, pdf_get(doc, dict, "Coords"), s->coords, 6);
    e = pdf_get(doc, dict, "Extend");
    if (e != NULL && e->kind == PDF_ARRAY && e->u.a.n == 2) {
        pdf_obj_t *a = pdf_resolve(doc, e->u.a.item[0]);
        pdf_obj_t *b = pdf_resolve(doc, e->u.a.item[1]);

        s->extend[0] = a != NULL && a->kind == PDF_BOOL && a->u.b;
        s->extend[1] = b != NULL && b->kind == PDF_BOOL && b->u.b;
    }
    s->matrix = pdf_mat(1, 0, 0, 1, 0, 0);
    if (numbers(doc, pdf_get(doc, dict, "Matrix"), v, 6) == 6) {
        s->matrix = pdf_mat(v[0], v[1], v[2], v[3], v[4], v[5]);
    }
    s->has_bbox = numbers(doc, pdf_get(doc, dict, "BBox"), s->bbox, 4) == 4;
    if (s->cs != NULL && numbers(doc, pdf_get(doc, dict, "Background"), v,
                                 4) == s->cs->n) {
        s->bg = pdf_cs_colour(s->cs, v, s->cs->n);
        s->has_bg = 1;
    }
    if (s->type == 2 || s->type == 3) {
        fill_lut(s);
    }
    /* A mesh: its Background, else its function half way, else grey. */
    s->flat = 0xffc8c8c8u;
    if (s->has_bg) {
        s->flat = s->bg;
    } else if (s->fn != NULL && s->cs != NULL) {
        double mid = (s->domain[0] + s->domain[1]) / 2;

        s->flat = colour_of(s, &mid, 1);
    }
    return s;
}

void
pdf_shade_free(pdf_shade_t *s)
{
    if (s != NULL) {
        pdf_cs_free(s->cs);
        pdf_func_free(s->fn);
        free(s);
    }
}

/* -------------------------------------------------------------- colour  */

/** @brief The table's colour at @p s along the axis, 0 to 1. */
static uint32_t
along(const pdf_shade_t *s, double t)
{
    int i = (int)(t * (SHADE_LUT - 1) + 0.5);

    return s->lut[i < 0 ? 0 : i >= SHADE_LUT ? SHADE_LUT - 1 : i];
}

/** @brief Type 2: where @p u, @p v falls along the axis, if between its
 *         ends or past one the shading extends. */
static int
axial_at(const pdf_shade_t *s, double u, double v, uint32_t *c)
{
    double dx = s->coords[2] - s->coords[0], dy = s->coords[3] - s->coords[1];
    double d2 = dx * dx + dy * dy;
    double t = d2 > 0 ? ((u - s->coords[0]) * dx + (v - s->coords[1]) * dy) /
                        d2 : 0;

    if (t < 0) {
        if (!s->extend[0]) return 0;
        t = 0;
    } else if (t > 1) {
        if (!s->extend[1]) return 0;
        t = 1;
    }
    *c = along(s, t);
    return 1;
}

/** @brief Where along the circles of type 3's @p c the ones through @p u,
 *         @p v are: the larger first.  @return how many, 0 to 2. */
static int
radial_roots(const double *c, double u, double v, double *root)
{
    double cx = c[3] - c[0], cy = c[4] - c[1], dr = c[5] - c[2];
    double px = u - c[0], py = v - c[1];
    double a = cx * cx + cy * cy - dr * dr;
    double b = px * cx + py * cy + c[2] * dr;
    double k = px * px + py * py - c[2] * c[2], disc;

    if (fabs(a) < 1e-9) {
        if (fabs(b) < 1e-12) return 0;
        root[0] = k / (2 * b);
        return 1;
    }
    disc = b * b - a * k;
    if (disc < 0) return 0;
    root[0] = (b + sqrt(disc)) / a;
    root[1] = (b - sqrt(disc)) / a;
    if (root[1] > root[0]) {
        double t = root[0];

        root[0] = root[1];
        root[1] = t;
    }
    return 2;
}

/** @brief Type 3: the circle, of those between the two and past the ones
 *         extended, that passes through @p u, @p v; the larger when two do. */
static int
radial_at(const pdf_shade_t *s, double u, double v, uint32_t *c)
{
    double root[2], r0 = s->coords[2], dr = s->coords[5] - s->coords[2];
    int n = radial_roots(s->coords, u, v, root), i;

    for (i = 0; i < n; i++) {
        double t = root[i];

        if (r0 + t * dr < 0) continue;
        if (t > 1 && !s->extend[1]) continue;
        if (t < 0 && !s->extend[0]) continue;
        *c = along(s, t < 0 ? 0 : t > 1 ? 1 : t);
        return 1;
    }
    return 0;
}

/** @brief Type 1: the function over its domain, through its Matrix. */
static int
function_at(const pdf_shade_t *s, double u, double v, uint32_t *c)
{
    const pdf_mat_t *m = &s->matrix;
    double det = m->a * m->d - m->b * m->c, in[2];

    if (det == 0 || s->fn == NULL) return 0;
    u -= m->e;
    v -= m->f;
    in[0] = (m->d * u - m->c * v) / det;
    in[1] = (m->a * v - m->b * u) / det;
    if (in[0] < s->domain[0] || in[0] > s->domain[1] ||
        in[1] < s->domain[2] || in[1] > s->domain[3]) return 0;
    *c = colour_of(s, in, 2);
    return 1;
}

/** @brief The shading's colour at @p u, @p v in its own space; 0 where
 *         it paints nothing. */
static int
shade_at(const pdf_shade_t *s, double u, double v, uint32_t *c)
{
    if (s->has_bbox && (u < fmin(s->bbox[0], s->bbox[2]) ||
                        u > fmax(s->bbox[0], s->bbox[2]) ||
                        v < fmin(s->bbox[1], s->bbox[3]) ||
                        v > fmax(s->bbox[1], s->bbox[3]))) return 0;
    if (s->type == 2) return axial_at(s, u, v, c);
    if (s->type == 3) return radial_at(s, u, v, c);
    if (s->type == 1) return function_at(s, u, v, c);
    *c = s->flat;                   /* a mesh: where its BBox says it is */
    return s->has_bbox;
}

/* ------------------------------------------------------------ painting  */

/** @brief The pixels to visit: @p m's box, else the clip's, else all. */
static void
extent(const pdf_pix_t *pix, const pdf_mask_t *m, const pdf_mask_t *clip,
       int *box)
{
    const pdf_mask_t *by = m != NULL ? m : clip;

    box[0] = 0;
    box[1] = 0;
    box[2] = pix->w;
    box[3] = pix->h;
    if (by != NULL && by->a != NULL) {
        box[0] = by->x0 > 0 ? by->x0 : 0;
        box[1] = by->y0 > 0 ? by->y0 : 0;
        box[2] = by->x0 + by->w < pix->w ? by->x0 + by->w : pix->w;
        box[3] = by->y0 + by->h < pix->h ? by->y0 + by->h : pix->h;
    }
}

/** @brief How much of mask @p m covers pixel @p x, @p y: 255 when it is
 *         NULL, 0 outside it. */
static int
cover(const pdf_mask_t *m, int x, int y)
{
    if (m == NULL) return 255;
    if (m->a == NULL || x < m->x0 || y < m->y0 || x >= m->x0 + m->w ||
        y >= m->y0 + m->h) return 0;
    return m->a[(size_t)(y - m->y0) * m->w + (x - m->x0)];
}

void
pdf_shade_paint(pdf_pix_t *pix, const pdf_shade_t *s, const pdf_mat_t *to,
                const pdf_mask_t *m, const pdf_mask_t *clip, double alpha,
                int background)
{
    double det = to->a * to->d - to->b * to->c;
    int box[4], x, y, ca = (int)(alpha * 255 + 0.5);

    if (s == NULL || det == 0 || ca <= 0 || (m != NULL && m->a == NULL)) {
        return;
    }
    if (ca > 255) ca = 255;
    extent(pix, m, clip, box);
    for (y = box[1]; y < box[3]; y++) {
        for (x = box[0]; x < box[2]; x++) {
            double px = x + 0.5 - to->e, py = y + 0.5 - to->f;
            double u = (to->d * px - to->c * py) / det;
            double v = (to->a * py - to->b * px) / det;
            int a = cover(m, x, y);
            uint32_t colour;

            if (a == 0 || (clip != NULL && (a = a * cover(clip, x, y) /
                                                255) == 0)) continue;
            if (!shade_at(s, u, v, &colour)) {
                /* A pattern's fill shows its Background, or for a mesh
                 * its tone, where the shading itself does not reach. */
                if (!background || (!s->has_bg && s->type < 4)) continue;
                colour = s->has_bg ? s->bg : s->flat;
            }
            a = (a * ca + 127) / 255;
            if (a > 0 && (colour >> 24) != 0) {
                pdf_blend(pix->px + (size_t)y * pix->w + x, colour, a);
            }
        }
    }
}
