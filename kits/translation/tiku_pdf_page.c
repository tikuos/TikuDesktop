/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_page.c - the pages, in order, and what each inherits.
 *
 * The page tree (ISO 32000 7.7.3) is walked once when the file opens
 * and its leaves kept in order; a page's resources, box and rotation
 * may sit on any ancestor, so they are looked up the Parent chain.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <stdlib.h>
#include <string.h>

#define TREE_DEPTH 64

/** @brief Add @p page to the document's list. */
static int
page_add(tiku_pdf_t *doc, pdf_obj_t *page)
{
    pdf_obj_t **p;

    if (doc->npage >= PDF_PAGES_MAX) {
        return -1;
    }
    p = realloc(doc->page, (size_t)(doc->npage + 1) * sizeof *p);
    if (p == NULL) {
        return -1;
    }
    doc->page = p;
    p[doc->npage++] = page;
    return 0;
}

/**
 * @brief The leaves under @p node, in order.
 *
 * A node with Kids is a branch whatever it calls itself; anything else
 * that looks like a page is one.  @p seen marks object numbers already
 * walked, since a broken tree can point back up.
 */
static void
walk(tiku_pdf_t *doc, pdf_obj_t *node, unsigned char *seen, int depth)
{
    pdf_obj_t *kids, *dict;
    int i;

    if (node == NULL || depth > TREE_DEPTH || doc->npage >= PDF_PAGES_MAX) {
        return;
    }
    if (node->kind == PDF_REF) {
        long num = node->u.ref.num;

        if (num < 0 || num >= doc->nxref || seen[num]) {
            return;
        }
        seen[num] = 1;
        dict = pdf_resolve(doc, node);
    } else {
        dict = node;
    }
    if (dict == NULL || dict->kind != PDF_DICT) {
        return;
    }
    kids = pdf_get(doc, dict, "Kids");
    if (kids != NULL && kids->kind == PDF_ARRAY) {
        for (i = 0; i < kids->u.a.n; i++) {
            walk(doc, kids->u.a.item[i], seen, depth + 1);
        }
        return;
    }
    if (pdf_is_name(pdf_get(doc, dict, "Type"), "Page") ||
        pdf_dict_raw(dict, "Contents") != NULL ||
        pdf_dict_raw(dict, "MediaBox") != NULL) {
        (void)page_add(doc, dict);
    }
}

int
pdf_pages_build(tiku_pdf_t *doc)
{
    pdf_obj_t *root = pdf_root(doc), *pages;
    unsigned char *seen;

    free(doc->page);
    doc->page = NULL;
    doc->npage = 0;
    if (root == NULL) {
        return -1;
    }
    pages = pdf_dict_raw(root, "Pages");
    if (pages == NULL) {
        return -1;
    }
    seen = calloc((size_t)(doc->nxref > 0 ? doc->nxref : 1), 1);
    if (seen == NULL) {
        return -1;
    }
    walk(doc, pages, seen, 0);
    free(seen);
    return 0;
}

pdf_obj_t *
pdf_page_dict(tiku_pdf_t *doc, int page)
{
    if (doc == NULL || page < 0 || page >= doc->npage) {
        return NULL;
    }
    return doc->page[page];
}

pdf_obj_t *
pdf_page_inherited(tiku_pdf_t *doc, pdf_obj_t *page, const char *key)
{
    int depth;

    for (depth = 0; page != NULL && depth < TREE_DEPTH; depth++) {
        pdf_obj_t *v = pdf_get(doc, page, key);

        if (v != NULL && v->kind != PDF_NULL) {
            return v;
        }
        page = pdf_get(doc, page, "Parent");
    }
    return NULL;
}

/** @brief One stream's decoded bytes appended to @p out. */
static int
append(tiku_pdf_t *doc, pdf_obj_t *st, unsigned char **out, long *n)
{
    unsigned char *data = NULL, *p;
    long len = 0;

    if (st == NULL || st->kind != PDF_STREAM ||
        pdf_stream_data(doc, st, &data, &len, NULL) != 0) {
        return 0;                   /* a missing part is skipped */
    }
    if (*n + len + 1 > PDF_STREAM_MAX) {
        free(data);
        return -1;
    }
    p = realloc(*out, (size_t)(*n + len + 2));
    if (p == NULL) {
        free(data);
        return -1;
    }
    memcpy(p + *n, data, (size_t)len);
    p[*n + len] = '\n';
    p[*n + len + 1] = '\0';
    *out = p;
    *n += len + 1;
    free(data);
    return 0;
}

int
pdf_page_content(tiku_pdf_t *doc, pdf_obj_t *page, unsigned char **out,
                 long *n)
{
    pdf_obj_t *c = pdf_get(doc, page, "Contents");
    int i;

    *out = NULL;
    *n = 0;
    if (c == NULL) {
        *out = calloc(1, 1);
        return *out != NULL ? 0 : -1;
    }
    if (c->kind == PDF_ARRAY) {
        for (i = 0; i < c->u.a.n; i++) {
            if (append(doc, pdf_resolve(doc, c->u.a.item[i]), out, n) != 0) {
                free(*out);
                *out = NULL;
                return -1;
            }
        }
    } else if (append(doc, c, out, n) != 0) {
        free(*out);
        *out = NULL;
        return -1;
    }
    if (*out == NULL) {
        *out = calloc(1, 1);
    }
    return *out != NULL ? 0 : -1;
}

/** @brief A rectangle's normalised corners; 0 unless it has four numbers. */
static int
rect(tiku_pdf_t *doc, pdf_obj_t *a, double *x0, double *y0, double *x1,
     double *y1)
{
    double v[4];
    int i;

    if (a == NULL || a->kind != PDF_ARRAY || a->u.a.n != 4) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        pdf_obj_t *o = pdf_resolve(doc, a->u.a.item[i]);

        if (o == NULL || (o->kind != PDF_INT && o->kind != PDF_REAL)) {
            return 0;
        }
        v[i] = pdf_number(o, 0);
    }
    *x0 = v[0] < v[2] ? v[0] : v[2];
    *x1 = v[0] < v[2] ? v[2] : v[0];
    *y0 = v[1] < v[3] ? v[1] : v[3];
    *y1 = v[1] < v[3] ? v[3] : v[1];
    return *x1 - *x0 > 1 && *y1 - *y0 > 1 && *x1 - *x0 < 20000 &&
           *y1 - *y0 < 20000;
}

int
tiku_pdf_page_size(tiku_pdf_t *doc, int page, double *w, double *h,
                   int *rotate)
{
    pdf_obj_t *p = pdf_page_dict(doc, page);
    double x0 = 0, y0 = 0, x1 = 612, y1 = 792, cx0, cy0, cx1, cy1;
    long r;

    if (p == NULL) {
        return -1;
    }
    (void)rect(doc, pdf_page_inherited(doc, p, "MediaBox"), &x0, &y0, &x1,
               &y1);
    if (rect(doc, pdf_page_inherited(doc, p, "CropBox"), &cx0, &cy0, &cx1,
             &cy1)) {
        /* What a viewer shows is the crop, within the media. */
        if (cx0 > x0) x0 = cx0;
        if (cy0 > y0) y0 = cy0;
        if (cx1 < x1) x1 = cx1;
        if (cy1 < y1) y1 = cy1;
    }
    r = (long)pdf_number(pdf_page_inherited(doc, p, "Rotate"), 0);
    r = ((r % 360) + 360) % 360;
    r = (r + 45) / 90 * 90 % 360;
    if (w != NULL) *w = x1 - x0;
    if (h != NULL) *h = y1 - y0;
    if (rotate != NULL) *rotate = (int)r;
    return 0;
}
