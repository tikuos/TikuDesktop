/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_render.c - a page drawn: the content stream's operators, the
 * graphics state they move, and the ink each leaves.
 *
 * Paths are built in device space and filled or stroked through the
 * clip (ISO 32000 8.5); colour comes through the colour spaces; a form
 * is entered with its own matrix and box; text is placed as the text
 * extractor places it, and each glyph handed to the font layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GS_MAX      48              /* q nesting                        */
#define OPERANDS    32
#define FORM_DEPTH  12
#define OPS_MAX     4000000L        /* operators one page may run       */
#define CS_KEEP     64              /* colour spaces alive per page     */
#define FONTS_MAX   64
#define DASH_MAX    16
#define SHADES_KEEP 128             /* shadings loaded per page         */
#define TILES_KEEP  8               /* pattern cells drawn per page     */
#define TILE_MAX    512             /* a cell's pixels, each way        */
#define NEST_MAX    2               /* patterns inside patterns' cells  */

/** @brief A clip shared by the states that inherited it. */
typedef struct { pdf_mask_t m; int refs; } clip_t;

/** @brief The graphics state (ISO 32000 8.4), the parts drawn with. */
typedef struct {
    pdf_mat_t ctm;
    pdf_cs_t *fcs, *scs;            /* fill and stroke colour spaces    */
    double    fv[8], sv[8];         /* their current colour numbers     */
    int       nfv, nsv;
    uint32_t  fill, stroke;         /* as painted                       */
    int       fpattern, spattern;   /* the colour space is Pattern      */
    pdf_obj_t *fpat, *spat;         /* the pattern named, or NULL       */
    double    falpha, salpha;
    double    lw, miter, dash[DASH_MAX], phase;
    int       cap, join, ndash;
    clip_t   *clip;
    /* text */
    pdf_obj_t   *fontdict;
    pdf_tfont_t *tfont;
    pdf_gfont_t *gfont;
    double       size, tc, tw, th, tl, rise;
    int          tmode;
} gs_t;

typedef struct {
    pdf_obj_t   *dict;
    pdf_tfont_t *font;
    pdf_gfont_t *glyphs;
} fontref_t;

/** @brief A tiling pattern's cell drawn at the device's resolution: its
 *         colours and coverage, and how it repeats in pattern space. */
typedef struct {
    pdf_obj_t     *pat;
    pdf_mat_t      to;              /* pattern space to the device       */
    int            w, h, coloured;
    double         x0, y0, xstep, ystep, sx, sy;   /* sx, sy: px a unit */
    uint32_t      *rgb;
    unsigned char *a;
} tile_t;

typedef struct {
    tiku_pdf_t *doc;
    pdf_pix_t   pix;
    gs_t        gs[GS_MAX];
    int         sp;
    pdf_path_t  path, tmp;
    double      ux, uy, usx, usy;   /* the current point in user space  */
    int         pending_clip;       /* after this paint: 1 nonzero, 2 eo */
    pdf_mat_t   tm, tlm;
    pdf_cs_t   *cs_keep[CS_KEEP];
    int         ncs;
    fontref_t   fonts[FONTS_MAX];
    int         nfont;
    long        ops;
    int         failed;
    int         t3depth;            /* Type 3 glyphs drawing glyphs      */
    pdf_obj_t  *res;                /* the running stream's resources    */
    pdf_mat_t   pbase;              /* its default space: patterns' base */
    int         nest, stencil;      /* in a cell; an uncoloured one      */
    struct { pdf_obj_t *dict; pdf_shade_t *s; } shades[SHADES_KEEP];
    int         nshade;
    tile_t     *tiles[TILES_KEEP];
    int         ntile;
} rs_t;

static void run(rs_t *r, const unsigned char *data, long n, pdf_obj_t *res,
                int depth);
static void paint(rs_t *r, const pdf_mask_t *m, int stroke);
static rs_t *rs_new(tiku_pdf_t *doc, int w, int h, uint32_t bg,
                    pdf_mat_t base);
static uint32_t *rs_finish(rs_t *r);
static void end_path(rs_t *r);
static void moveto(rs_t *r, double x, double y);
static void lineto(rs_t *r, double x, double y);
static void draw_image(rs_t *r, pdf_obj_t *dict, const unsigned char *raw,
                       long rawn, pdf_obj_t *res);

/* --------------------------------------------------------------- clips  */

/** @brief One more holder of @p c (which may be NULL). */
static clip_t *
clip_ref(clip_t *c)
{
    if (c != NULL) {
        c->refs++;
    }
    return c;
}

/** @brief One holder fewer; the last frees it. */
static void
clip_unref(clip_t *c)
{
    if (c != NULL && --c->refs == 0) {
        pdf_mask_free(&c->m);
        free(c);
    }
}

/** @brief The state's clip narrowed to @p mask, which it now owns. */
static void
clip_with(rs_t *r, pdf_mask_t mask)
{
    gs_t *g = &r->gs[r->sp];
    clip_t *c = calloc(1, sizeof *c);

    if (c == NULL) {
        pdf_mask_free(&mask);
        return;
    }
    if (g->clip != NULL) {
        pdf_mask_intersect(&mask, &g->clip->m);
    }
    c->m = mask;
    c->refs = 1;
    clip_unref(g->clip);
    g->clip = c;
}

/* --------------------------------------------------------------- state  */

/** @brief The current graphics state. */
static gs_t *
top(rs_t *r)
{
    return &r->gs[r->sp];
}

/** @brief q: a copy of the state, sharing its clip. */
static void
push(rs_t *r)
{
    if (r->sp + 1 >= GS_MAX) {
        return;
    }
    r->gs[r->sp + 1] = r->gs[r->sp];
    r->sp++;
    clip_ref(top(r)->clip);
}

/** @brief Q: back to the state before the last q. */
static void
pop(rs_t *r)
{
    if (r->sp > 0) {
        clip_unref(top(r)->clip);
        r->sp--;
    }
}

/** @brief Keep @p cs alive for the page; NULL when there is no room. */
static pdf_cs_t *
keep_cs(rs_t *r, pdf_cs_t *cs)
{
    if (cs == NULL) {
        return NULL;
    }
    if (r->ncs == CS_KEEP) {
        pdf_cs_free(r->cs_keep[0]);
        memmove(r->cs_keep, r->cs_keep + 1, (CS_KEEP - 1) * sizeof cs);
        r->ncs--;
    }
    r->cs_keep[r->ncs++] = cs;
    return cs;
}

/** @brief Set the fill or stroke colour from its space and numbers. */
static void
set_colour(gs_t *g, int stroke)
{
    if (stroke) {
        g->stroke = pdf_cs_colour(g->scs, g->sv, g->nsv);
    } else {
        g->fill = pdf_cs_colour(g->fcs, g->fv, g->nfv);
    }
}

/** @brief Colour space @p cs for fill or stroke, with its initial colour. */
static void
set_space(rs_t *r, pdf_cs_t *cs, int stroke)
{
    gs_t *g = top(r);
    int i, n = cs != NULL ? cs->n : 1;

    if (stroke) {
        g->scs = cs;
        g->nsv = n;
        g->spattern = cs != NULL && cs->kind == CS_PATTERN;
        g->spat = NULL;
        for (i = 0; i < n && i < 8; i++) g->sv[i] = cs ? cs->initial[i] : 0;
    } else {
        g->fcs = cs;
        g->nfv = n;
        g->fpattern = cs != NULL && cs->kind == CS_PATTERN;
        g->fpat = NULL;
        for (i = 0; i < n && i < 8; i++) g->fv[i] = cs ? cs->initial[i] : 0;
    }
    set_colour(g, stroke);
}

/** @brief Operand @p i of @p n as a number, or @p dflt. */
static double
num(pdf_obj_t **ops, int n, int i, double dflt)
{
    return i < n ? pdf_number(ops[i], dflt) : dflt;
}

/* ---------------------------------------------------------------- paths  */

/** @brief The current point moved to user (x, y), noted both ways. */
static void
moveto(rs_t *r, double x, double y)
{
    double dx, dy;

    pdf_mat_apply(&top(r)->ctm, x, y, &dx, &dy);
    pdf_path_moveto(&r->path, dx, dy);
    r->ux = r->usx = x;
    r->uy = r->usy = y;
}

/** @brief l: a line in user space, kept as the current point. */
static void
lineto(rs_t *r, double x, double y)
{
    double dx, dy;

    pdf_mat_apply(&top(r)->ctm, x, y, &dx, &dy);
    pdf_path_lineto(&r->path, dx, dy);
    r->ux = x;
    r->uy = y;
}

/** @brief c: a cubic in user space, through the CTM. */
static void
curveto(rs_t *r, double x1, double y1, double x2, double y2, double x3,
        double y3)
{
    double a, b, c, d, e, f;

    pdf_mat_apply(&top(r)->ctm, x1, y1, &a, &b);
    pdf_mat_apply(&top(r)->ctm, x2, y2, &c, &d);
    pdf_mat_apply(&top(r)->ctm, x3, y3, &e, &f);
    pdf_path_curveto(&r->path, a, b, c, d, e, f);
    r->ux = x3;
    r->uy = y3;
}

/** @brief Fill the current path through the clip with the fill colour. */
static void
fill_path(rs_t *r, int evenodd)
{
    pdf_mask_t m;
    int x0, y0, w, h;

    if (!pdf_path_bounds(&r->path, r->pix.w, r->pix.h, &x0, &y0, &w, &h)) {
        return;
    }
    m = pdf_path_fill(&r->path, evenodd, x0, y0, w, h);
    paint(r, &m, 0);
    pdf_mask_free(&m);
}

/** @brief Stroke the current path with the line state and stroke colour. */
/** @brief The dash pattern in device units; @return how many entries,
 *        0 for a solid line (an all-zero pattern is solid too). */
static int
device_dash(const gs_t *g, double scale, double *dash)
{
    int i, all_zero = 1;

    for (i = 0; i < g->ndash; i++) {
        dash[i] = g->dash[i] * scale;
        all_zero &= dash[i] <= 0;
    }
    return all_zero ? 0 : g->ndash;
}

/** @brief Stroke the current path in the line state, through the clip. */
static void
stroke_path(rs_t *r)
{
    gs_t *g = top(r);
    double scale = pdf_mat_scale(&g->ctm), dash[DASH_MAX];
    pdf_mask_t m;
    int x0, y0, w, h, ndash = device_dash(g, scale, dash);

    pdf_path_stroke(&r->path, g->lw * scale, g->cap, g->join, g->miter,
                    ndash ? dash : NULL, ndash, g->phase * scale, &r->tmp);
    if (!pdf_path_bounds(&r->tmp, r->pix.w, r->pix.h, &x0, &y0, &w, &h)) {
        return;
    }
    m = pdf_path_fill(&r->tmp, 0, x0, y0, w, h);
    paint(r, &m, 1);
    pdf_mask_free(&m);
}

/** @brief A painting operator is over: apply any clip asked for, forget
 *        the path. */
static void
end_path(rs_t *r)
{
    if (r->pending_clip) {
        int x0, y0, w, h;
        pdf_mask_t m;

        if (pdf_path_bounds(&r->path, r->pix.w, r->pix.h, &x0, &y0, &w, &h)) {
            m = pdf_path_fill(&r->path, r->pending_clip == 2, x0, y0, w, h);
        } else {
            m = pdf_path_fill(&r->path, 0, 0, 0, 0, 0);
            m.w = m.h = 0;
            m.a = calloc(1, 1);         /* a clip of nothing */
        }
        clip_with(r, m);
        r->pending_clip = 0;
    }
    pdf_path_clear(&r->path);
}

/** @brief Fill and stroke: f F f* B B* b b* S s n. */
static void
op_paint(rs_t *r, const char *o)
{
    if (o[0] == 's' || o[0] == 'b') {
        pdf_path_close(&r->path);
    }
    if (o[0] == 'f' || o[0] == 'F' || o[0] == 'B' || o[0] == 'b') {
        fill_path(r, o[1] == '*');
    }
    if (o[0] == 'S' || o[0] == 's' || o[0] == 'B' || o[0] == 'b') {
        stroke_path(r);
    }
    end_path(r);
}

/* ------------------------------------------------------------- colours  */

/** @brief cs CS sc scn SC SCN g G rg RG k K. */
static void
op_colour(rs_t *r, const char *o, pdf_obj_t **ops, int n, pdf_obj_t *res)
{
    gs_t *g = top(r);
    int stroke = o[0] >= 'A' && o[0] <= 'Z', i;
    const char *dev = NULL;

    if (!strcmp(o, "cs") || !strcmp(o, "CS")) {
        if (n >= 1 && ops[0]->kind == PDF_NAME) {
            set_space(r, keep_cs(r, pdf_cs_load(r->doc, ops[0], res)), stroke);
        }
        return;
    }
    if (!strcmp(o, "g") || !strcmp(o, "G")) dev = "DeviceGray";
    if (!strcmp(o, "rg") || !strcmp(o, "RG")) dev = "DeviceRGB";
    if (!strcmp(o, "k") || !strcmp(o, "K")) dev = "DeviceCMYK";
    if (dev != NULL) {
        pdf_obj_t name;
        pdf_cs_t *cs;

        memset(&name, 0, sizeof name);
        name.kind = PDF_NAME;
        name.u.s.p = (unsigned char *)dev;
        name.u.s.n = strlen(dev);
        cs = keep_cs(r, pdf_cs_load(r->doc, &name, NULL));
        set_space(r, cs, stroke);
    }
    /* sc, scn, or the numbers of g/rg/k: the colour's parts. */
    {
        double *v = stroke ? g->sv : g->fv;
        int *nv = stroke ? &g->nsv : &g->nfv, k = 0;

        for (i = 0; i < n && k < 8; i++) {
            if (ops[i]->kind == PDF_INT || ops[i]->kind == PDF_REAL) {
                v[k++] = pdf_number(ops[i], 0);
            }
        }
        if (k > 0) {
            *nv = k;
        }
        if (n >= 1 && ops[n - 1]->kind == PDF_NAME) {
            pdf_obj_t *pat = pdf_get(r->doc, pdf_get(r->doc, res, "Pattern"),
                                     (const char *)ops[n - 1]->u.s.p);

            if (stroke) { g->spattern = 1; g->spat = pat; }
            else { g->fpattern = 1; g->fpat = pat; }
        }
    }
    set_colour(g, stroke);
}

/** @brief gs: the parameters an ExtGState carries that are drawn with. */
static void
op_gs(rs_t *r, pdf_obj_t *name, pdf_obj_t *res)
{
    gs_t *g = top(r);
    pdf_obj_t *ext = pdf_get(r->doc, pdf_get(r->doc, res, "ExtGState"),
                             (const char *)name->u.s.p);
    pdf_obj_t *v;

    if (ext == NULL) {
        return;
    }
    v = pdf_get(r->doc, ext, "CA");
    if (v != NULL) g->salpha = pdf_number(v, 1);
    v = pdf_get(r->doc, ext, "ca");
    if (v != NULL) g->falpha = pdf_number(v, 1);
    v = pdf_get(r->doc, ext, "LW");
    if (v != NULL) g->lw = pdf_number(v, 1);
    v = pdf_get(r->doc, ext, "Font");
    if (v != NULL && v->kind == PDF_ARRAY && v->u.a.n == 2) {
        pdf_obj_t *fd = pdf_resolve(r->doc, v->u.a.item[0]);

        if (fd != NULL && fd->kind == PDF_DICT) {
            g->fontdict = fd;
            g->tfont = NULL;
            g->gfont = NULL;
            g->size = pdf_number(pdf_resolve(r->doc, v->u.a.item[1]), g->size);
        }
    }
}

/* ------------------------------------------------------------ patterns  */

/** @brief Shading @p dict, loaded once a page and kept for it. */
static pdf_shade_t *
shade_for(rs_t *r, pdf_obj_t *dict)
{
    int i;

    if (dict == NULL) {
        return NULL;
    }
    for (i = 0; i < r->nshade; i++) {
        if (r->shades[i].dict == dict) {
            return r->shades[i].s;
        }
    }
    if (r->nshade == SHADES_KEEP) {
        return NULL;
    }
    r->shades[r->nshade].dict = dict;
    r->shades[r->nshade].s = pdf_shade_load(r->doc, dict, r->res);
    return r->shades[r->nshade++].s;
}

/** @brief Pattern @p pat's space to the device: its Matrix, then the
 *         default space of the stream it is painted in. */
static pdf_mat_t
pattern_space(rs_t *r, pdf_obj_t *pat)
{
    pdf_obj_t *m = pdf_get(r->doc, pat, "Matrix");
    pdf_mat_t pm = pdf_mat(1, 0, 0, 1, 0, 0);
    double v[6];
    int i;

    if (m != NULL && m->kind == PDF_ARRAY && m->u.a.n == 6) {
        for (i = 0; i < 6; i++) {
            v[i] = pdf_number(pdf_resolve(r->doc, m->u.a.item[i]),
                              i == 0 || i == 3);
        }
        pm = pdf_mat(v[0], v[1], v[2], v[3], v[4], v[5]);
    }
    return pdf_mat_mul(pm, r->pbase);
}

/** @brief Give a drawn cell back. */
static void
tile_free(tile_t *t)
{
    if (t != NULL) {
        free(t->rgb);
        free(t->a);
        free(t);
    }
}

/** @brief The cell's content drawn on a ground of @p bg by a renderer of
 *         its own, clipped to the pattern's BBox @p bb.
 *  @return its pixels, or NULL. */
static uint32_t *
tile_draw(rs_t *r, pdf_obj_t *pat, const tile_t *t, const double *bb,
          uint32_t bg)
{
    pdf_mat_t ctm = pdf_mat(t->sx, 0, 0, t->sy, -t->x0 * t->sx,
                            -t->y0 * t->sy);
    pdf_obj_t *res = pdf_get(r->doc, pat, "Resources");
    unsigned char *data = NULL;
    long n = 0;
    rs_t *sub;

    if (pdf_stream_data(r->doc, pat, &data, &n, NULL) != 0) {
        return NULL;
    }
    sub = rs_new(r->doc, t->w, t->h, bg, ctm);
    if (sub == NULL) {
        free(data);
        return NULL;
    }
    sub->nest = r->nest + 1;
    sub->stencil = !t->coloured;
    sub->ops = r->ops;
    moveto(sub, bb[0], bb[1]);
    lineto(sub, bb[2], bb[1]);
    lineto(sub, bb[2], bb[3]);
    lineto(sub, bb[0], bb[3]);
    pdf_path_close(&sub->path);
    sub->pending_clip = 1;
    end_path(sub);
    run(sub, data, n, res != NULL ? res : r->res, 0);
    free(data);
    r->ops = sub->ops;
    return rs_finish(sub);
}

/**
 * @brief Tiling pattern @p pat's cell at the resolution @p to gives it:
 *        drawn on white and on black, the two giving each pixel's
 *        coverage and its colour without a painter that knows alpha.
 */
static tile_t *
tile_make(rs_t *r, pdf_obj_t *pat, const pdf_mat_t *to)
{
    double bb[4] = {0, 0, 1, 1};
    pdf_obj_t *box = pdf_get(r->doc, pat, "BBox");
    uint32_t *white, *black;
    tile_t *t;
    long i;

    if (r->nest >= NEST_MAX || (t = calloc(1, sizeof *t)) == NULL) {
        return NULL;
    }
    for (i = 0; box != NULL && box->kind == PDF_ARRAY && box->u.a.n == 4 &&
                i < 4; i++) {
        bb[i] = pdf_number(pdf_resolve(r->doc, box->u.a.item[i]), 0);
    }
    t->pat = pat;
    t->to = *to;
    t->coloured = pdf_number(pdf_get(r->doc, pat, "PaintType"), 1) != 2;
    t->x0 = fmin(bb[0], bb[2]);
    t->y0 = fmin(bb[1], bb[3]);
    t->xstep = fabs(pdf_number(pdf_get(r->doc, pat, "XStep"), 0));
    t->ystep = fabs(pdf_number(pdf_get(r->doc, pat, "YStep"), 0));
    if (t->xstep < 1e-6) t->xstep = fabs(bb[2] - bb[0]);
    if (t->ystep < 1e-6) t->ystep = fabs(bb[3] - bb[1]);
    t->w = (int)ceil(t->xstep * sqrt(to->a * to->a + to->b * to->b));
    t->h = (int)ceil(t->ystep * sqrt(to->c * to->c + to->d * to->d));
    if (t->xstep < 1e-6 || t->ystep < 1e-6) {
        tile_free(t);
        return NULL;
    }
    t->w = t->w < 1 ? 1 : t->w > TILE_MAX ? TILE_MAX : t->w;
    t->h = t->h < 1 ? 1 : t->h > TILE_MAX ? TILE_MAX : t->h;
    t->sx = t->w / t->xstep;
    t->sy = t->h / t->ystep;
    white = tile_draw(r, pat, t, bb, 0xffffffffu);
    black = white != NULL ? tile_draw(r, pat, t, bb, 0xff000000u) : NULL;
    t->rgb = malloc((size_t)t->w * t->h * sizeof *t->rgb);
    t->a = malloc((size_t)t->w * t->h);
    for (i = 0; black != NULL && t->rgb != NULL && t->a != NULL &&
                i < (long)t->w * t->h; i++) {
        int k, a, c[3];

        /* On white a pixel is c + (1 - a) 255, on black c: a from the
         * difference, the colour from black, unpremultiplied. */
        a = 255 - ((int)((white[i] >> 16) & 255) - (int)((black[i] >> 16) & 255)
                   + (int)((white[i] >> 8) & 255) - (int)((black[i] >> 8) & 255)
                   + (int)(white[i] & 255) - (int)(black[i] & 255)) / 3;
        a = a < 0 ? 0 : a > 255 ? 255 : a;
        for (k = 0; k < 3; k++) {
            int v = (int)((black[i] >> (16 - 8 * k)) & 255);

            c[k] = a > 0 ? v * 255 / a : 0;
            c[k] = c[k] > 255 ? 255 : c[k];
        }
        t->a[i] = (unsigned char)a;
        t->rgb[i] = 0xff000000u | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 |
                    (uint32_t)c[2];
    }
    if (black == NULL || t->rgb == NULL || t->a == NULL) {
        tile_free(t);
        t = NULL;
    }
    free(white);
    free(black);
    return t;
}

/** @brief Pattern @p pat's cell for @p to, drawn once a page. */
static tile_t *
tile_for(rs_t *r, pdf_obj_t *pat, const pdf_mat_t *to)
{
    tile_t *t;
    int i;

    for (i = 0; i < r->ntile; i++) {
        if (r->tiles[i]->pat == pat &&
            memcmp(&r->tiles[i]->to, to, sizeof *to) == 0) {
            return r->tiles[i];
        }
    }
    t = tile_make(r, pat, to);
    if (t == NULL) {
        return NULL;
    }
    if (r->ntile == TILES_KEEP) {
        tile_free(r->tiles[0]);
        memmove(r->tiles, r->tiles + 1, (TILES_KEEP - 1) * sizeof t);
        r->ntile--;
    }
    r->tiles[r->ntile++] = t;
    return t;
}

/** @brief Coverage @p m painted with the cell repeated across it; an
 *         uncoloured cell paints @p colour.  @return 0 when it cannot be. */
static int
paint_tiling(rs_t *r, pdf_obj_t *pat, const pdf_mat_t *to,
             const pdf_mask_t *m, const pdf_mask_t *clip, double alpha,
             uint32_t colour)
{
    tile_t *t = tile_for(r, pat, to);
    double det = to->a * to->d - to->b * to->c;
    int x, y, ca = (int)(alpha * 255 + 0.5);

    if (t == NULL || det == 0) {
        return t != NULL;
    }
    for (y = m->y0 < 0 ? 0 : m->y0; y < m->y0 + m->h && y < r->pix.h; y++) {
        for (x = m->x0 < 0 ? 0 : m->x0; x < m->x0 + m->w && x < r->pix.w;
             x++) {
            double px = x + 0.5 - to->e, py = y + 0.5 - to->f;
            double u = (to->d * px - to->c * py) / det - t->x0;
            double v = (to->a * py - to->b * px) / det - t->y0;
            int a = m->a[(size_t)(y - m->y0) * m->w + (x - m->x0)], i, j;

            if (a == 0) continue;
            if (clip != NULL && clip->a != NULL) {
                int cx = x - clip->x0, cy = y - clip->y0;

                if (cx < 0 || cy < 0 || cx >= clip->w || cy >= clip->h) {
                    continue;
                }
                a = a * clip->a[(size_t)cy * clip->w + cx] / 255;
            }
            u -= floor(u / t->xstep) * t->xstep;
            v -= floor(v / t->ystep) * t->ystep;
            i = (int)(u * t->sx);
            j = (int)(v * t->sy);
            i = i < 0 ? 0 : i >= t->w ? t->w - 1 : i;
            j = j < 0 ? 0 : j >= t->h ? t->h - 1 : j;
            a = a * t->a[(size_t)j * t->w + i] / 255 * ca / 255;
            if (a > 0) {
                pdf_blend(r->pix.px + (size_t)y * r->pix.w + x,
                          t->coloured ? t->rgb[(size_t)j * t->w + i] : colour,
                          a);
            }
        }
    }
    return 1;
}

/** @brief Coverage @p m painted with pattern @p pat: its shading or its
 *         tiles.  @return 0 for a pattern this cannot draw. */
static int
paint_pattern(rs_t *r, pdf_obj_t *pat, const pdf_mask_t *m,
              const pdf_mask_t *clip, double alpha, uint32_t colour)
{
    int type = (int)pdf_number(pdf_get(r->doc, pat, "PatternType"), 0);
    pdf_mat_t to = pattern_space(r, pat);

    if (type == 2) {
        pdf_shade_t *s = shade_for(r, pdf_get(r->doc, pat, "Shading"));

        if (s == NULL) {
            return 0;
        }
        pdf_shade_paint(&r->pix, s, &to, m, clip, alpha, 1);
        return 1;
    }
    if (type == 1 && pat->kind == PDF_STREAM) {
        return paint_tiling(r, pat, &to, m, clip, alpha, colour);
    }
    return 0;
}

/** @brief Coverage @p m in the fill or the stroke colour, which a pattern
 *         may give pixel by pixel; inside an uncoloured cell, black. */
static void
paint(rs_t *r, const pdf_mask_t *m, int stroke)
{
    gs_t *g = top(r);
    const pdf_mask_t *clip = g->clip ? &g->clip->m : NULL;
    double alpha = stroke ? g->salpha : g->falpha;
    uint32_t colour = stroke ? g->stroke : g->fill;
    pdf_obj_t *pat = stroke ? g->spat : g->fpat;

    if (r->stencil) {
        pdf_paint(&r->pix, m, clip, 0xff000000u, 1);
        return;
    }
    if (stroke ? g->spattern : g->fpattern) {
        if (pat != NULL && paint_pattern(r, pat, m, clip, alpha, colour)) {
            return;
        }
        colour = 0xffc0c0c0u;       /* a pattern this cannot draw: a tone */
    }
    if (colour >> 24) {
        pdf_paint(&r->pix, m, clip, colour, alpha);
    }
}

/** @brief sh: the shading over all the clip leaves, in user space. */
static void
op_sh(rs_t *r, pdf_obj_t *name, pdf_obj_t *res)
{
    gs_t *g = top(r);
    pdf_shade_t *s = shade_for(r, pdf_get(r->doc, pdf_get(r->doc, res,
                                                          "Shading"),
                                          (const char *)name->u.s.p));

    if (s != NULL && !r->stencil) {
        pdf_shade_paint(&r->pix, s, &g->ctm, NULL,
                        g->clip ? &g->clip->m : NULL, g->falpha, 0);
    }
}

/* ---------------------------------------------------------------- text  */

/** @brief Both views of @p dict, the text one and the drawn one, loaded
 *        once per page. */
static const fontref_t *
font_for(rs_t *r, pdf_obj_t *dict)
{
    int i;
    fontref_t *f;

    if (dict == NULL) {
        return NULL;
    }
    for (i = 0; i < r->nfont; i++) {
        if (r->fonts[i].dict == dict) {
            return &r->fonts[i];
        }
    }
    if (r->nfont == FONTS_MAX) {
        return NULL;
    }
    f = &r->fonts[r->nfont++];
    f->dict = dict;
    f->font = pdf_tfont_load(r->doc, dict);
    f->glyphs = pdf_gfont_load(r->doc, dict, f->font);
    return f;
}

/**
 * @brief A Type 3 glyph: its procedure run with the font matrix, its
 *        own resources, and the text's fill as the colour.
 */
static void
glyph_proc(rs_t *r, gs_t *g, pdf_gfont_t *gf, long code, const pdf_mat_t *trm,
           pdf_obj_t *res, int depth)
{
    const char *name = code >= 0 && code < 256 ? gf->names[code] : NULL;
    pdf_obj_t *proc = name != NULL ? pdf_get(r->doc, gf->charprocs, name)
                                   : NULL;
    unsigned char *data = NULL;
    long n = 0;
    pdf_mat_t tm = r->tm, tlm = r->tlm;

    if (proc == NULL || proc->kind != PDF_STREAM || r->t3depth >= 4 ||
        r->sp >= GS_MAX - 2 || depth >= FORM_DEPTH ||
        pdf_stream_data(r->doc, proc, &data, &n, NULL) != 0) {
        return;
    }
    push(r);
    top(r)->ctm = pdf_mat_mul(gf->fontmatrix, *trm);
    top(r)->stroke = g->fill;
    top(r)->salpha = g->falpha;
    r->t3depth++;
    {
        pdf_mat_t pbase = r->pbase;

        r->pbase = top(r)->ctm;
        run(r, data, n, gf->t3res != NULL ? gf->t3res : res, depth + 1);
        r->pbase = pbase;
    }
    r->t3depth--;
    free(data);
    while (r->sp > 0 && top(r) != g) {
        pop(r);
    }
    r->tm = tm;
    r->tlm = tlm;
}

/** @brief One glyph's ink: filled, stroked, or both, as the mode says. */
static void
glyph_ink(rs_t *r, gs_t *g, pdf_gfont_t *gf, long code, const pdf_mat_t *trm)
{
    pdf_mask_t m;
    int mode = g->tmode;

    if (mode == 3 || mode == 7 || !pdf_gfont_mask(gf, code, trm, &m)) {
        return;
    }
    if (mode == 0 || mode == 2 || mode == 4 || mode == 6) {
        paint(r, &m, 0);
    }
    if (mode == 1 || mode == 2 || mode == 5 || mode == 6) {
        paint(r, &m, 1);            /* stroked text: the outline's fill */
    }
    pdf_mask_free(&m);
}

/**
 * @brief Draw one string's glyphs where the text state puts them,
 *        advancing the text matrix by each one's width.
 *
 * The glyph is drawn through the text render matrix (ISO 32000 9.4.4):
 * size and rise, then the text matrix, then the CTM.  Type 3 glyphs are
 * content streams and are run; the rest are outlines.
 */
static void
show(rs_t *r, const unsigned char *s, size_t n, pdf_obj_t *res, int depth)
{
    gs_t *g = top(r);
    pdf_tfont_t *f = g->tfont;
    size_t i, step;

    if (f == NULL && g->fontdict != NULL) {
        const fontref_t *ref = font_for(r, g->fontdict);

        if (ref != NULL) {
            f = g->tfont = ref->font;
            g->gfont = ref->glyphs;
        }
    }
    step = (f != NULL && f->two_byte) ? 2 : 1;
    for (i = 0; i + step <= n; i += step) {
        long code = step == 2 ? ((long)s[i] << 8) | s[i + 1] : s[i];
        double w = f != NULL && f->width[code] >= 0 ? f->width[code]
                   : f != NULL ? f->missing : 0.5;
        double tx;

        if (g->gfont != NULL && g->tmode != 3 && g->tmode != 7) {
            pdf_mat_t trm = pdf_mat_mul(
                pdf_mat(g->size * g->th, 0, 0, g->size, 0, g->rise),
                pdf_mat_mul(r->tm, g->ctm));

            if (g->gfont->kind == GF_T3) {
                glyph_proc(r, g, g->gfont, code, &trm, res, depth);
                g = top(r);
            } else {
                glyph_ink(r, g, g->gfont, code, &trm);
            }
        }
        tx = (w * g->size + g->tc + ((step == 1 && code == 32) ? g->tw : 0)) *
             g->th;
        r->tm = pdf_mat_mul(pdf_mat(1, 0, 0, 1, tx, 0), r->tm);
    }
}

/** @brief BT ET Td TD Tm T* TL Tc Tw Tz Ts Tf Tr Tj TJ ' ". */
static void
op_text(rs_t *r, const char *o, pdf_obj_t **ops, int n, pdf_obj_t *res,
        int depth)
{
    gs_t *g = top(r);

    if (!strcmp(o, "BT")) {
        r->tm = r->tlm = pdf_mat(1, 0, 0, 1, 0, 0);
    } else if (!strcmp(o, "Tf") && n >= 2) {
        if (ops[0]->kind == PDF_NAME) {
            g->fontdict = pdf_get(r->doc, pdf_get(r->doc, res, "Font"),
                                  (const char *)ops[0]->u.s.p);
            g->tfont = NULL;
            g->gfont = NULL;
        }
        g->size = num(ops, n, 1, 1);
    } else if (!strcmp(o, "Td") && n >= 2) {
        r->tlm = pdf_mat_mul(pdf_mat(1, 0, 0, 1, num(ops, n, 0, 0),
                                     num(ops, n, 1, 0)), r->tlm);
        r->tm = r->tlm;
    } else if (!strcmp(o, "TD") && n >= 2) {
        g->tl = -num(ops, n, 1, 0);
        r->tlm = pdf_mat_mul(pdf_mat(1, 0, 0, 1, num(ops, n, 0, 0),
                                     num(ops, n, 1, 0)), r->tlm);
        r->tm = r->tlm;
    } else if (!strcmp(o, "Tm") && n >= 6) {
        r->tm = r->tlm = pdf_mat(num(ops, n, 0, 1), num(ops, n, 1, 0),
                                 num(ops, n, 2, 0), num(ops, n, 3, 1),
                                 num(ops, n, 4, 0), num(ops, n, 5, 0));
    } else if (!strcmp(o, "T*")) {
        r->tlm = pdf_mat_mul(pdf_mat(1, 0, 0, 1, 0, -g->tl), r->tlm);
        r->tm = r->tlm;
    } else if (!strcmp(o, "TL")) {
        g->tl = num(ops, n, 0, 0);
    } else if (!strcmp(o, "Tc")) {
        g->tc = num(ops, n, 0, 0);
    } else if (!strcmp(o, "Tw")) {
        g->tw = num(ops, n, 0, 0);
    } else if (!strcmp(o, "Tz")) {
        g->th = num(ops, n, 0, 100) / 100.0;
    } else if (!strcmp(o, "Ts")) {
        g->rise = num(ops, n, 0, 0);
    } else if (!strcmp(o, "Tr")) {
        g->tmode = (int)num(ops, n, 0, 0);
    } else if (!strcmp(o, "Tj") && n >= 1 && ops[0]->kind == PDF_STRING) {
        show(r, ops[0]->u.s.p, ops[0]->u.s.n, res, depth);
    } else if ((!strcmp(o, "'") || !strcmp(o, "\"")) && n >= 1) {
        pdf_obj_t *s = ops[n - 1];

        if (o[0] == '"' && n >= 3) {
            g->tw = num(ops, n, 0, g->tw);
            g->tc = num(ops, n, 1, g->tc);
        }
        r->tlm = pdf_mat_mul(pdf_mat(1, 0, 0, 1, 0, -g->tl), r->tlm);
        r->tm = r->tlm;
        if (s->kind == PDF_STRING) {
            show(r, s->u.s.p, s->u.s.n, res, depth);
        }
    } else if (!strcmp(o, "TJ") && n >= 1 && ops[0]->kind == PDF_ARRAY) {
        int i;

        for (i = 0; i < ops[0]->u.a.n; i++) {
            pdf_obj_t *e = ops[0]->u.a.item[i];

            if (e->kind == PDF_STRING) {
                show(r, e->u.s.p, e->u.s.n, res, depth);
            } else if (e->kind == PDF_INT || e->kind == PDF_REAL) {
                double tx = -pdf_number(e, 0) / 1000.0 * g->size * g->th;

                r->tm = pdf_mat_mul(pdf_mat(1, 0, 0, 1, tx, 0), r->tm);
            }
        }
    }
}

/* ---------------------------------------------------------------- forms  */

/** @brief A form XObject: its matrix, its box as a clip, its stream. */
static void
form(rs_t *r, pdf_obj_t *xo, pdf_obj_t *res, int depth)
{
    pdf_obj_t *m = pdf_get(r->doc, xo, "Matrix");
    pdf_obj_t *bbox = pdf_get(r->doc, xo, "BBox");
    pdf_obj_t *fres = pdf_get(r->doc, xo, "Resources");
    unsigned char *data = NULL;
    long n = 0;
    int sp = r->sp;

    if (depth >= FORM_DEPTH) {
        return;
    }
    push(r);
    if (m != NULL && m->kind == PDF_ARRAY && m->u.a.n == 6) {
        double v[6];
        int i;

        for (i = 0; i < 6; i++) {
            v[i] = pdf_number(pdf_resolve(r->doc, m->u.a.item[i]),
                              i == 0 || i == 3);
        }
        top(r)->ctm = pdf_mat_mul(pdf_mat(v[0], v[1], v[2], v[3], v[4], v[5]),
                                  top(r)->ctm);
    }
    if (bbox != NULL && bbox->kind == PDF_ARRAY && bbox->u.a.n == 4) {
        double v[4];
        int i;

        for (i = 0; i < 4; i++) {
            v[i] = pdf_number(pdf_resolve(r->doc, bbox->u.a.item[i]), 0);
        }
        pdf_path_clear(&r->path);
        moveto(r, v[0], v[1]);
        lineto(r, v[2], v[1]);
        lineto(r, v[2], v[3]);
        lineto(r, v[0], v[3]);
        pdf_path_close(&r->path);
        r->pending_clip = 1;
        end_path(r);
    }
    if (pdf_stream_data(r->doc, xo, &data, &n, NULL) == 0) {
        pdf_mat_t pbase = r->pbase;

        r->pbase = top(r)->ctm;     /* the form's space, as it is painted */
        run(r, data, n, fres != NULL ? fres : res, depth + 1);
        r->pbase = pbase;
        free(data);
    }
    while (r->sp > sp) {
        pop(r);
    }
}

/** @brief Do: a form is entered, an image is drawn over the unit square. */
static void
op_do(rs_t *r, pdf_obj_t *name, pdf_obj_t *res, int depth)
{
    pdf_obj_t *xo = pdf_get(r->doc, pdf_get(r->doc, res, "XObject"),
                            (const char *)name->u.s.p);

    if (xo == NULL || xo->kind != PDF_STREAM) {
        return;
    }
    if (pdf_is_name(pdf_get(r->doc, xo, "Subtype"), "Form")) {
        form(r, xo, res, depth);
    } else if (pdf_is_name(pdf_get(r->doc, xo, "Subtype"), "Image")) {
        draw_image(r, xo, NULL, 0, res);
    }
}

/** @brief An image drawn over the unit square, as the state paints. */
static void
draw_image(rs_t *r, pdf_obj_t *dict, const unsigned char *raw, long rawn,
           pdf_obj_t *res)
{
    gs_t *g = top(r);
    pdf_img_t img;

    if (pdf_image_decode(r->doc, dict, raw, rawn, res, &img) == 0) {
        pdf_image_draw(&r->pix, &img, &g->ctm, g->clip ? &g->clip->m : NULL,
                       g->fpattern ? 0xff808080u : g->fill, g->falpha);
        pdf_image_free(&img);
    }
}

/** @brief Where the EI that ends inline data at @p from stands. */
static long
find_ei(const pdf_cur_t *c, long from)
{
    long at = from;

    while (at < c->n) {
        long ei = pdf_find(c->p, c->n, at, "EI");

        if (ei < 0) {
            return c->n;
        }
        if ((ei == 0 || c->p[ei - 1] <= ' ') &&
            (ei + 2 >= c->n || c->p[ei + 2] <= ' ')) {
            return ei;
        }
        at = ei + 2;
    }
    return c->n;
}

/**
 * @brief BI <dict> ID <bytes> EI: the image drawn and the cursor past it.
 *
 * Unfiltered data has a length the dictionary implies, which is trusted
 * over a search for EI, since the bytes may hold those letters.
 */
static void
inline_image(rs_t *r, pdf_cur_t *c, pdf_obj_t *res)
{
    pdf_obj_t *d = pdf_new(PDF_DICT), *o, *v;
    long start, len = -1, end;

    if (d == NULL) {
        c->pos = c->n;
        return;
    }
    for (;;) {
        o = pdf_parse(c, 0);
        if (o == NULL ||
            (o->kind == PDF_OP && !strcmp((char *)o->u.s.p, "ID"))) {
            pdf_free(o);
            break;
        }
        if (o->kind != PDF_NAME) {
            pdf_free(o);
            continue;
        }
        v = pdf_parse(c, 0);
        if (v == NULL) {
            pdf_free(o);
            break;
        }
        if (v->kind == PDF_OP || pdf_dict_set(d, (char *)o->u.s.p, v) != 0) {
            pdf_free(v);
        }
        pdf_free(o);
    }
    start = c->pos + 1;                     /* the one space after ID */
    if (start > c->n) {
        start = c->n;
    }
    v = pdf_get(r->doc, d, "Filter");
    if (v == NULL) v = pdf_get(r->doc, d, "F");
    if (v == NULL) {
        pdf_obj_t *im = pdf_get(r->doc, d, "ImageMask");
        pdf_obj_t *cso = pdf_get(r->doc, d, "ColorSpace");
        long w = (long)pdf_number(pdf_get(r->doc, d, "Width"), 0);
        long h = (long)pdf_number(pdf_get(r->doc, d, "Height"), 0);
        int bpc = (int)pdf_number(pdf_get(r->doc, d, "BitsPerComponent"), 8);
        int n = 1;

        if (im == NULL) im = pdf_get(r->doc, d, "IM");
        if (cso == NULL) cso = pdf_get(r->doc, d, "CS");
        if (w == 0) w = (long)pdf_number(pdf_get(r->doc, d, "W"), 0);
        if (h == 0) h = (long)pdf_number(pdf_get(r->doc, d, "H"), 0);
        if (bpc == 8 && pdf_get(r->doc, d, "BPC") != NULL) {
            bpc = (int)pdf_number(pdf_get(r->doc, d, "BPC"), 8);
        }
        if (im != NULL && im->kind == PDF_BOOL && im->u.b) {
            bpc = 1;
        } else if (cso != NULL) {
            pdf_cs_t *cs = pdf_cs_load(r->doc, cso, res);

            n = cs != NULL ? cs->n : 1;
            pdf_cs_free(cs);
        }
        len = (w * n * bpc + 7) / 8 * h;
        if (len < 0 || start + len > c->n) {
            len = -1;
        }
    }
    end = len >= 0 ? find_ei(c, start + len) : find_ei(c, start);
    if (len < 0) {
        len = end - start;
        while (len > 0 && c->p[start + len - 1] <= ' ') {
            len--;
        }
    }
    draw_image(r, d, c->p + start, len, res);
    pdf_free(d);
    c->pos = end < c->n ? end + 2 : c->n;
}

/* ------------------------------------------------------------ operators  */

/** @brief Path construction and the graphics state: q Q cm w J j M d
 *        m l c v y h re W W*. */
static int
op_state(rs_t *r, const char *o, pdf_obj_t **ops, int n)
{
    gs_t *g = top(r);

    if (!strcmp(o, "q")) push(r);
    else if (!strcmp(o, "Q")) pop(r);
    else if (!strcmp(o, "cm") && n >= 6) {
        g->ctm = pdf_mat_mul(pdf_mat(num(ops, n, 0, 1), num(ops, n, 1, 0),
                                     num(ops, n, 2, 0), num(ops, n, 3, 1),
                                     num(ops, n, 4, 0), num(ops, n, 5, 0)),
                             g->ctm);
    } else if (!strcmp(o, "w")) g->lw = num(ops, n, 0, 1);
    else if (!strcmp(o, "J")) g->cap = (int)num(ops, n, 0, 0);
    else if (!strcmp(o, "j")) g->join = (int)num(ops, n, 0, 0);
    else if (!strcmp(o, "M")) g->miter = num(ops, n, 0, 10);
    else if (!strcmp(o, "d") && n >= 2 && ops[0]->kind == PDF_ARRAY) {
        int i;

        g->ndash = 0;
        for (i = 0; i < ops[0]->u.a.n && i < DASH_MAX; i++) {
            g->dash[g->ndash++] = pdf_number(ops[0]->u.a.item[i], 0);
        }
        g->phase = num(ops, n, 1, 0);
    } else if (!strcmp(o, "m") && n >= 2) moveto(r, num(ops, n, 0, 0),
                                                 num(ops, n, 1, 0));
    else if (!strcmp(o, "l") && n >= 2) lineto(r, num(ops, n, 0, 0),
                                               num(ops, n, 1, 0));
    else if (!strcmp(o, "c") && n >= 6) {
        curveto(r, num(ops, n, 0, 0), num(ops, n, 1, 0), num(ops, n, 2, 0),
                num(ops, n, 3, 0), num(ops, n, 4, 0), num(ops, n, 5, 0));
    } else if (!strcmp(o, "v") && n >= 4) {
        curveto(r, r->ux, r->uy, num(ops, n, 0, 0), num(ops, n, 1, 0),
                num(ops, n, 2, 0), num(ops, n, 3, 0));
    } else if (!strcmp(o, "y") && n >= 4) {
        curveto(r, num(ops, n, 0, 0), num(ops, n, 1, 0), num(ops, n, 2, 0),
                num(ops, n, 3, 0), num(ops, n, 2, 0), num(ops, n, 3, 0));
    } else if (!strcmp(o, "h")) {
        pdf_path_close(&r->path);
        r->ux = r->usx;
        r->uy = r->usy;
    } else if (!strcmp(o, "re") && n >= 4) {
        double x = num(ops, n, 0, 0), y = num(ops, n, 1, 0);
        double w = num(ops, n, 2, 0), h = num(ops, n, 3, 0);

        moveto(r, x, y);
        lineto(r, x + w, y);
        lineto(r, x + w, y + h);
        lineto(r, x, y + h);
        pdf_path_close(&r->path);
        r->ux = x;
        r->uy = y;
    } else if (!strcmp(o, "W")) r->pending_clip = 1;
    else if (!strcmp(o, "W*")) r->pending_clip = 2;
    else return 0;
    return 1;
}

/** @brief One operator with its operands, to whichever group owns it. */
static void
op(rs_t *r, const char *o, pdf_obj_t **ops, int n, pdf_obj_t *res,
   pdf_cur_t *c, int depth)
{
    if (op_state(r, o, ops, n)) {
        return;
    }
    if (!strcmp(o, "f") || !strcmp(o, "F") || !strcmp(o, "f*") ||
        !strcmp(o, "B") || !strcmp(o, "B*") || !strcmp(o, "b") ||
        !strcmp(o, "b*") || !strcmp(o, "S") || !strcmp(o, "s") ||
        !strcmp(o, "n")) {
        op_paint(r, o);
    } else if (!strcmp(o, "g") || !strcmp(o, "G") || !strcmp(o, "rg") ||
               !strcmp(o, "RG") || !strcmp(o, "k") || !strcmp(o, "K") ||
               !strcmp(o, "cs") || !strcmp(o, "CS") || !strcmp(o, "sc") ||
               !strcmp(o, "SC") || !strcmp(o, "scn") || !strcmp(o, "SCN")) {
        op_colour(r, o, ops, n, res);
    } else if (!strcmp(o, "gs") && n >= 1 && ops[0]->kind == PDF_NAME) {
        op_gs(r, ops[0], res);
    } else if (!strcmp(o, "Do") && n >= 1 && ops[0]->kind == PDF_NAME) {
        op_do(r, ops[0], res, depth);
    } else if (!strcmp(o, "sh") && n >= 1 && ops[0]->kind == PDF_NAME) {
        op_sh(r, ops[0], res);
    } else if (!strcmp(o, "BI")) {
        inline_image(r, c, res);
    } else if (o[0] == 'T' || o[0] == 'B' || o[0] == 'E' || o[0] == '\'' ||
               o[0] == '"') {
        op_text(r, o, ops, n, res, depth);
    }
}

/** @brief A content stream, operator by operator, within the budget. */
static void
run(rs_t *r, const unsigned char *data, long n, pdf_obj_t *res, int depth)
{
    pdf_cur_t c = { data, n, 0 };
    pdf_obj_t *ops[OPERANDS], *outer = r->res;
    int nops = 0;

    r->res = res;
    for (;;) {
        pdf_obj_t *o = pdf_parse(&c, 0);
        int i;

        if (o == NULL || r->failed || r->ops++ > OPS_MAX) {
            pdf_free(o);
            break;
        }
        if (o->kind != PDF_OP) {
            if (nops == OPERANDS) {
                pdf_free(ops[0]);
                memmove(ops, ops + 1, (size_t)(OPERANDS - 1) * sizeof ops[0]);
                nops--;
            }
            ops[nops++] = o;
            continue;
        }
        op(r, (const char *)o->u.s.p, ops, nops, res, &c, depth);
        pdf_free(o);
        for (i = 0; i < nops; i++) {
            pdf_free(ops[i]);
        }
        nops = 0;
    }
    while (nops > 0) {
        pdf_free(ops[--nops]);
    }
    r->res = outer;
}

/* ------------------------------------------------------------------ API  */

/** @brief A renderer of @p w by @p h pixels on a ground of @p bg, drawing
 *         through @p base, which is also its patterns' default space. */
static rs_t *
rs_new(tiku_pdf_t *doc, int w, int h, uint32_t bg, pdf_mat_t base)
{
    rs_t *r = calloc(1, sizeof *r);
    long i;

    if (r == NULL) {
        return NULL;
    }
    r->doc = doc;
    r->pix.w = w;
    r->pix.h = h;
    r->pix.px = malloc((size_t)w * (size_t)h * sizeof *r->pix.px);
    if (r->pix.px == NULL) {
        free(r);
        return NULL;
    }
    for (i = 0; i < (long)w * h; i++) {
        r->pix.px[i] = bg;
    }
    pdf_path_init(&r->path);
    pdf_path_init(&r->tmp);
    r->gs[0].ctm = base;
    r->gs[0].fill = r->gs[0].stroke = 0xff000000u;
    r->gs[0].falpha = r->gs[0].salpha = 1;
    r->gs[0].lw = 1;
    r->gs[0].miter = 10;
    r->gs[0].th = 1;
    r->gs[0].nfv = r->gs[0].nsv = 1;
    r->tm = r->tlm = pdf_mat(1, 0, 0, 1, 0, 0);
    r->pbase = base;
    return r;
}

/** @brief Everything a renderer held given back but its pixels, which
 *         are returned for the caller to free. */
static uint32_t *
rs_finish(rs_t *r)
{
    uint32_t *px = r->pix.px;
    int i;

    while (r->sp > 0) {
        pop(r);
    }
    clip_unref(r->gs[0].clip);
    for (i = 0; i < r->ncs; i++) {
        pdf_cs_free(r->cs_keep[i]);
    }
    for (i = 0; i < r->nfont; i++) {
        pdf_tfont_free(r->fonts[i].font);
        pdf_gfont_free(r->fonts[i].glyphs);
    }
    for (i = 0; i < r->nshade; i++) {
        pdf_shade_free(r->shades[i].s);
    }
    for (i = 0; i < r->ntile; i++) {
        tile_free(r->tiles[i]);
    }
    pdf_path_free(&r->path);
    pdf_path_free(&r->tmp);
    free(r);
    return px;
}

int
tiku_pdf_render_size(tiku_pdf_t *doc, int page, double scale, int *w, int *h)
{
    pdf_mat_t m;

    if (doc == NULL || scale <= 0) {
        return -1;
    }
    return pdf_page_matrix(doc, page, scale, 0, 0, &m, w, h);
}

int
tiku_pdf_render_rect(tiku_pdf_t *doc, int page, double scale, int x0, int y0,
                     int w, int h, tiku_pdf_pixmap_t *out)
{
    rs_t *r;
    pdf_mat_t base;
    pdf_obj_t *p = pdf_page_dict(doc, page);
    unsigned char *data = NULL;
    long n = 0;
    int pw, ph;

    if (p == NULL || out == NULL || w <= 0 || h <= 0 || scale <= 0 ||
        (long)w * h > 64L * 1024L * 1024L ||
        pdf_page_matrix(doc, page, scale, x0, y0, &base, &pw, &ph) != 0) {
        return -1;
    }
    r = rs_new(doc, w, h, 0xffffffffu, base);
    if (r == NULL) {
        return -1;
    }
    if (pdf_page_content(doc, p, &data, &n) == 0) {
        run(r, data, n, pdf_page_inherited(doc, p, "Resources"), 0);
        free(data);
    }
    out->w = w;
    out->h = h;
    out->px = rs_finish(r);
    return 0;
}

int
tiku_pdf_render(tiku_pdf_t *doc, int page, double scale, tiku_pdf_pixmap_t *out)
{
    int w, h;

    if (tiku_pdf_render_size(doc, page, scale, &w, &h) != 0) {
        return -1;
    }
    return tiku_pdf_render_rect(doc, page, scale, 0, 0, w, h, out);
}

void
tiku_pdf_pixmap_free(tiku_pdf_pixmap_t *p)
{
    if (p != NULL) {
        free(p->px);
        p->px = NULL;
        p->w = p->h = 0;
    }
}
