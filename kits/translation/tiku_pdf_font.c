/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_font.c - a font as its text is read: which character each
 * code stands for, and how far it advances.
 *
 * A ToUnicode map says outright (ISO 32000 9.10.3); a simple font
 * without one says it through its encoding and the glyph names in it
 * (9.6.6); a composite font's codes are CIDs and the map is the only
 * way in.  Widths come from the font dictionary, per code.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"
#include "tiku_pdf_glyphs.inl"

#include <stdlib.h>
#include <string.h>

#define CMAP_STEPS 200000           /* codes one map may set, in all      */

/** @brief Order two glyph names, for the table search. */
static int
name_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, ((const glyph_name_t *)b)->name);
}

/** @brief A hex digit's value, or -1. */
static int
hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint32_t
pdf_glyph_unicode(const char *name)
{
    char base[64];
    const void *hit;
    size_t n;

    if (name == NULL || *name == '\0') {
        return 0;
    }
    n = strcspn(name, ".");        /* "a.sc" is an a in another shape */
    if (n == 0 || n >= sizeof base) {
        return 0;
    }
    memcpy(base, name, n);
    base[n] = '\0';
    if (n == 1) {
        return (unsigned char)base[0];
    }
    hit = bsearch(base, glyph_names, sizeof glyph_names / sizeof glyph_names[0],
                  sizeof glyph_names[0], name_cmp);
    if (hit != NULL) {
        return ((const glyph_name_t *)hit)->cp;
    }
    if ((n == 7 && strncmp(base, "uni", 3) == 0) ||
        (n >= 5 && n <= 7 && base[0] == 'u')) {
        uint32_t v = 0;
        size_t i = base[1] == 'n' ? 3 : 1;

        for (; i < n; i++) {
            int h = hexval(base[i]);

            if (h < 0) {
                return 0;
            }
            v = v * 16 + (uint32_t)h;
        }
        return v;
    }
    return 0;
}

/** @brief The codes of one of the three named encodings, as characters. */
static void
base_encoding(uint32_t *uni, const char *name)
{
    int c;

    memset(uni, 0, 256 * sizeof *uni);
    for (c = 32; c < 127; c++) {
        uni[c] = (uint32_t)c;
    }
    if (name != NULL && strcmp(name, "WinAnsiEncoding") == 0) {
        for (c = 128; c < 160; c++) {
            uni[c] = winansi_high[c - 128];
        }
        for (c = 160; c < 256; c++) {
            uni[c] = (uint32_t)c;
        }
        uni[160] = ' ';
        uni[173] = '-';
    } else if (name != NULL && strcmp(name, "MacRomanEncoding") == 0) {
        for (c = 128; c < 256; c++) {
            uni[c] = macroman_high[c - 128];
        }
    } else {
        uni[39] = 0x2019;
        uni[96] = 0x2018;
        for (c = 161; c < 252; c++) {
            uni[c] = standard_high[c - 161];
        }
    }
}

/** @brief /Differences: a code, then the names of the codes from it. */
static void
differences(tiku_pdf_t *doc, uint32_t *uni, pdf_obj_t *diff)
{
    long code = 0;
    int i;

    if (diff == NULL || diff->kind != PDF_ARRAY) {
        return;
    }
    for (i = 0; i < diff->u.a.n; i++) {
        pdf_obj_t *o = pdf_resolve(doc, diff->u.a.item[i]);

        if (o == NULL) {
            continue;
        }
        if (o->kind == PDF_INT || o->kind == PDF_REAL) {
            code = (long)pdf_number(o, 0);
        } else if (o->kind == PDF_NAME && code >= 0 && code < 256) {
            uni[code++] = pdf_glyph_unicode((const char *)o->u.s.p);
        }
    }
}

/** @brief A big-endian code from a CMap string of one or two bytes. */
static long
cmap_code(const pdf_obj_t *s, int *bytes)
{
    if (s == NULL || s->kind != PDF_STRING || s->u.s.n < 1 || s->u.s.n > 4) {
        return -1;
    }
    *bytes = (int)s->u.s.n;
    if (s->u.s.n == 1) {
        return s->u.s.p[0];
    }
    if (s->u.s.n == 2) {
        return ((long)s->u.s.p[0] << 8) | s->u.s.p[1];
    }
    return -1;                      /* three and four bytes: not read */
}

/** @brief The first two characters of a UTF-16BE destination string. */
static void
cmap_dst(const pdf_obj_t *s, uint32_t *first, uint32_t *second)
{
    uint32_t cp[2] = {0, 0};
    size_t i, k = 0;

    *first = *second = 0;
    if (s == NULL || s->kind != PDF_STRING) {
        return;
    }
    for (i = 0; i + 1 < s->u.s.n && k < 2; i += 2) {
        uint32_t u = ((uint32_t)s->u.s.p[i] << 8) | s->u.s.p[i + 1];

        if (u >= 0xD800 && u < 0xDC00 && i + 3 < s->u.s.n) {
            uint32_t lo = ((uint32_t)s->u.s.p[i + 2] << 8) | s->u.s.p[i + 3];

            u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        cp[k++] = u;
    }
    if (s->u.s.n == 1) {
        cp[0] = s->u.s.p[0];        /* one byte: seen in the wild */
    }
    *first = cp[0];
    *second = cp[1];
}

/** @brief Set code @p c of @p f to @p cp, within the font's range. */
static void
set_uni(pdf_tfont_t *f, long c, uint32_t cp, uint32_t cp2)
{
    if (c >= 0 && c < f->codes) {
        f->uni[c] = cp;
        f->uni2[c] = cp2;
    }
}

/**
 * @brief Read a ToUnicode CMap: bfchar pairs and bfrange runs.
 *
 * The map is PostScript-shaped but its tokens are PDF's, so the object
 * lexer reads it; only the operators between begin/end matter.
 */
static void
tounicode(tiku_pdf_t *doc, pdf_tfont_t *f, pdf_obj_t *stream)
{
    unsigned char *data = NULL;
    long n = 0, steps = 0;
    pdf_cur_t c;
    pdf_obj_t *st[4] = {NULL, NULL, NULL, NULL};
    int mode = 0, sp = 0;

    if (stream == NULL || stream->kind != PDF_STREAM ||
        pdf_stream_data(doc, stream, &data, &n, NULL) != 0) {
        return;
    }
    c.p = data;
    c.n = n;
    c.pos = 0;
    for (;;) {
        pdf_obj_t *o = pdf_parse(&c, 0);
        int bytes;

        if (o == NULL || steps > CMAP_STEPS) {
            pdf_free(o);
            break;
        }
        if (o->kind == PDF_OP) {
            const char *op = (const char *)o->u.s.p;

            if (strcmp(op, "beginbfchar") == 0) mode = 1;
            else if (strcmp(op, "beginbfrange") == 0) mode = 2;
            else if (strncmp(op, "end", 3) == 0) mode = 0;
            pdf_free(o);
            while (sp > 0) {
                pdf_free(st[--sp]);
            }
            continue;
        }
        if (sp == 4) {
            pdf_free(st[0]);
            memmove(st, st + 1, 3 * sizeof st[0]);
            sp = 3;
        }
        st[sp++] = o;
        if (mode == 1 && sp >= 2) {
            long code = cmap_code(st[0], &bytes);
            uint32_t a, b;

            cmap_dst(st[1], &a, &b);
            set_uni(f, code, a, b);
            steps++;
            pdf_free(st[0]);
            pdf_free(st[1]);
            sp = 0;
        } else if (mode == 2 && sp >= 3) {
            long lo = cmap_code(st[0], &bytes), hi = cmap_code(st[1], &bytes);
            long k;

            if (lo >= 0 && hi >= lo && hi - lo < 65536) {
                if (st[2]->kind == PDF_ARRAY) {
                    for (k = 0; k < st[2]->u.a.n && lo + k <= hi; k++) {
                        uint32_t a, b;

                        cmap_dst(st[2]->u.a.item[k], &a, &b);
                        set_uni(f, lo + k, a, b);
                    }
                } else {
                    uint32_t a, b;

                    cmap_dst(st[2], &a, &b);
                    for (k = 0; lo + k <= hi && steps++ < CMAP_STEPS; k++) {
                        set_uni(f, lo + k, a ? a + (uint32_t)k : 0, b);
                    }
                }
            }
            pdf_free(st[0]);
            pdf_free(st[1]);
            pdf_free(st[2]);
            sp = 0;
        }
    }
    free(data);
}

/** @brief Widths of a simple font: /FirstChar on, from /Widths. */
static void
simple_widths(tiku_pdf_t *doc, pdf_tfont_t *f, pdf_obj_t *font, double scale)
{
    pdf_obj_t *w = pdf_get(doc, font, "Widths");
    pdf_obj_t *desc = pdf_get(doc, font, "FontDescriptor");
    long first = (long)pdf_number(pdf_get(doc, font, "FirstChar"), 0);
    int i;

    f->missing = pdf_number(pdf_get(doc, desc, "MissingWidth"), 0) * scale;
    if (w == NULL || w->kind != PDF_ARRAY) {
        f->missing = 0.5;           /* a standard face: its stand-in's */
        if (!f->type3) {
            (void)pdf_standin_widths(doc, font, f->uni, f->width);
        }
        return;
    }
    for (i = 0; i < w->u.a.n; i++) {
        pdf_obj_t *o = pdf_resolve(doc, w->u.a.item[i]);

        if (first + i >= 0 && first + i < 256 && o != NULL &&
            (o->kind == PDF_INT || o->kind == PDF_REAL)) {
            f->width[first + i] = pdf_number(o, 0) * scale;
        }
    }
}

/** @brief Widths of a CID font: /W runs, /DW for the rest. */
static void
cid_widths(tiku_pdf_t *doc, pdf_tfont_t *f, pdf_obj_t *cidfont)
{
    pdf_obj_t *w = pdf_get(doc, cidfont, "W");
    int i = 0;

    f->missing = pdf_number(pdf_get(doc, cidfont, "DW"), 1000) / 1000.0;
    if (w == NULL || w->kind != PDF_ARRAY) {
        return;
    }
    while (i < w->u.a.n) {
        pdf_obj_t *a = pdf_resolve(doc, w->u.a.item[i]), *b, *cw;
        long c, last, k;

        if (a == NULL || (a->kind != PDF_INT && a->kind != PDF_REAL) ||
            i + 1 >= w->u.a.n) {
            break;
        }
        c = (long)pdf_number(a, 0);
        b = pdf_resolve(doc, w->u.a.item[i + 1]);
        if (b != NULL && b->kind == PDF_ARRAY) {
            for (k = 0; k < b->u.a.n; k++) {
                if (c + k >= 0 && c + k < f->codes) {
                    pdf_obj_t *wk = pdf_resolve(doc, b->u.a.item[k]);

                    f->width[c + k] = pdf_number(wk, 1000) / 1000.0;
                }
            }
            i += 2;
            continue;
        }
        if (i + 2 >= w->u.a.n) {
            break;
        }
        last = (long)pdf_number(b, 0);
        cw = pdf_resolve(doc, w->u.a.item[i + 2]);
        for (k = c; k <= last && k < f->codes && k - c < 65536; k++) {
            if (k >= 0) {
                f->width[k] = pdf_number(cw, 1000) / 1000.0;
            }
        }
        i += 3;
    }
}

void
pdf_tfont_free(pdf_tfont_t *f)
{
    if (f == NULL) {
        return;
    }
    free(f->uni);
    free(f->uni2);
    free(f->width);
    free(f);
}

pdf_tfont_t *
pdf_tfont_load(tiku_pdf_t *doc, pdf_obj_t *font)
{
    pdf_tfont_t *f = calloc(1, sizeof *f);
    pdf_obj_t *sub = pdf_get(doc, font, "Subtype"), *enc;
    double scale = 1.0 / 1000.0;
    long i;

    if (f == NULL) {
        return NULL;
    }
    f->two_byte = pdf_is_name(sub, "Type0");
    f->type3 = pdf_is_name(sub, "Type3");
    f->codes = f->two_byte ? 65536 : 256;
    f->uni = calloc((size_t)f->codes, sizeof *f->uni);
    f->uni2 = calloc((size_t)f->codes, sizeof *f->uni2);
    f->width = malloc((size_t)f->codes * sizeof *f->width);
    if (f->uni == NULL || f->uni2 == NULL || f->width == NULL) {
        pdf_tfont_free(f);
        return NULL;
    }
    for (i = 0; i < f->codes; i++) {
        f->width[i] = -1;
    }
    if (f->two_byte) {
        pdf_obj_t *kids = pdf_get(doc, font, "DescendantFonts");
        pdf_obj_t *cid = (kids != NULL && kids->kind == PDF_ARRAY &&
                          kids->u.a.n > 0)
                         ? pdf_resolve(doc, kids->u.a.item[0]) : NULL;

        f->missing = 1.0;
        if (cid != NULL) {
            cid_widths(doc, f, cid);
        }
        tounicode(doc, f, pdf_get(doc, font, "ToUnicode"));
        return f;
    }
    if (f->type3) {
        pdf_obj_t *m = pdf_get(doc, font, "FontMatrix");

        scale = (m != NULL && m->kind == PDF_ARRAY && m->u.a.n == 6)
                ? pdf_number(pdf_resolve(doc, m->u.a.item[0]), 0.001) : 0.001;
    }
    enc = pdf_get(doc, font, "Encoding");
    if (enc != NULL && enc->kind == PDF_NAME) {
        base_encoding(f->uni, (const char *)enc->u.s.p);
    } else if (enc != NULL && enc->kind == PDF_DICT) {
        pdf_obj_t *base = pdf_get(doc, enc, "BaseEncoding");

        base_encoding(f->uni, base != NULL && base->kind == PDF_NAME
                              ? (const char *)base->u.s.p : NULL);
        differences(doc, f->uni, pdf_get(doc, enc, "Differences"));
    } else {
        base_encoding(f->uni, NULL);
    }
    simple_widths(doc, f, font, scale);
    tounicode(doc, f, pdf_get(doc, font, "ToUnicode"));
    return f;
}
