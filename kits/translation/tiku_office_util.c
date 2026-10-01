/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_util.c - what every office reader stands on: memory given
 * back all at once, arrays that grow while built, the document itself,
 * one copy of each look however many runs share it, and numbers written
 * as lists and page footers write them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CHUNK 65536                 /* an arena grows by at least this   */

struct of_chunk {
    of_chunk_t *next;
    size_t      used, cap;
    unsigned char data[];
};

/* ------------------------------------------------------------- memory  */

void *
of_alloc(of_arena_t *a, size_t n)
{
    of_chunk_t *c = a->head;
    void *p;

    n = (n + 15) & ~(size_t)15;
    if (c == NULL || c->used + n > c->cap) {
        size_t cap = n > CHUNK ? n : CHUNK;

        if ((c = malloc(sizeof *c + cap)) == NULL) {
            a->bad = 1;
            return NULL;
        }
        c->next = a->head;
        c->used = 0;
        c->cap = cap;
        a->head = c;
    }
    p = c->data + c->used;
    c->used += n;
    memset(p, 0, n);
    return p;
}

char *
of_strndup(of_arena_t *a, const char *s, size_t n)
{
    char *d = of_alloc(a, n + 1);

    if (d != NULL) {
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return d;
}

char *
of_strdup(of_arena_t *a, const char *s)
{
    return s != NULL ? of_strndup(a, s, strlen(s)) : NULL;
}

void
of_arena_free(of_arena_t *a)
{
    while (a->head != NULL) {
        of_chunk_t *next = a->head->next;

        free(a->head);
        a->head = next;
    }
}

void
of_vec_init(of_vec_t *v, size_t size)
{
    memset(v, 0, sizeof *v);
    v->size = size;
}

void *
of_vec_push(of_vec_t *v)
{
    unsigned char *slot;

    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 16;
        void *g = realloc(v->p, (size_t)cap * v->size);

        if (g == NULL) {
            return NULL;
        }
        v->p = g;
        v->cap = cap;
    }
    slot = (unsigned char *)v->p + (size_t)v->n++ * v->size;
    memset(slot, 0, v->size);
    return slot;
}

void *
of_vec_freeze(of_vec_t *v, of_arena_t *a, int *n)
{
    void *p = NULL;

    *n = v->n;
    if (v->n > 0 && (p = of_alloc(a, (size_t)v->n * v->size)) != NULL) {
        memcpy(p, v->p, (size_t)v->n * v->size);
    }
    if (p == NULL) {
        *n = 0;
    }
    v->n = 0;
    return p;
}

void
of_vec_free(of_vec_t *v)
{
    free(v->p);
    v->p = NULL;
    v->n = v->cap = 0;
}

/* ----------------------------------------------------------- document  */

of_doc_t *
of_doc_new(void)
{
    of_doc_t *d = calloc(1, sizeof *d);

    if (d != NULL) {
        d->tab = 36;
    }
    return d;
}

void
of_doc_free(of_doc_t *d)
{
    if (d == NULL) {
        return;
    }
    if (d->art_free != NULL) {
        d->art_free(d->art_ctx);
    }
    free(d->styles);
    of_arena_free(&d->arena);
    free(d);
}

/** @brief Whether two strings are the same, either maybe NULL. */
static int
same(const char *a, const char *b)
{
    return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0);
}

/** @brief Whether two looks are the same look. */
static int
same_style(const of_style_t *a, const of_style_t *b)
{
    return same(a->font, b->font) && same(a->font_ea, b->font_ea) &&
           a->size == b->size && a->color == b->color && a->back == b->back
           && a->bold == b->bold && a->italic == b->italic &&
           a->caps == b->caps && a->smallcaps == b->smallcaps &&
           a->hidden == b->hidden && a->underline == b->underline &&
           a->strike == b->strike && a->vert == b->vert &&
           a->spacing == b->spacing && a->rise == b->rise &&
           a->scale == b->scale;
}

/** @brief A look's hash, over what tells looks apart most. */
static unsigned
style_hash(const of_style_t *s)
{
    unsigned h = 2166136261u;
    const char *f = s->font ? s->font : "";
    float size = s->size > 0 && s->size < 65536 ? s->size : 0;

    for (; *f; f++) {
        h = (h ^ (unsigned char)*f) * 16777619u;
    }
    h = (h ^ (unsigned)(size * 8)) * 16777619u;
    h = (h ^ s->color) * 16777619u;
    h = (h ^ (unsigned)(s->bold | s->italic << 1 | s->underline << 2 |
                        (unsigned char)s->vert << 6)) * 16777619u;
    return h;
}

/** @brief The table of looks twice as large.  @return 0, or -1. */
static int
styles_grow(of_doc_t *d)
{
    int cap = d->stylecap ? d->stylecap * 2 : 256, i;
    const of_style_t **t = calloc((size_t)cap, sizeof *t);

    if (t == NULL) {
        return -1;
    }
    for (i = 0; i < d->stylecap; i++) {
        if (d->styles[i] != NULL) {
            unsigned k = style_hash(d->styles[i]) & (unsigned)(cap - 1);

            while (t[k] != NULL) {
                k = (k + 1) & (unsigned)(cap - 1);
            }
            t[k] = d->styles[i];
        }
    }
    free(d->styles);
    d->styles = t;
    d->stylecap = cap;
    return 0;
}

const of_style_t *
of_style(of_doc_t *d, const of_style_t *s)
{
    of_style_t *copy;
    unsigned k;

    if ((d->nstyles + 1) * 2 > d->stylecap && styles_grow(d) != 0) {
        return NULL;
    }
    for (k = style_hash(s) & (unsigned)(d->stylecap - 1); d->styles[k];
         k = (k + 1) & (unsigned)(d->stylecap - 1)) {
        if (same_style(d->styles[k], s)) {
            return d->styles[k];
        }
    }
    if ((copy = of_alloc(&d->arena, sizeof *copy)) == NULL) {
        return NULL;
    }
    *copy = *s;
    copy->font = of_strdup(&d->arena, s->font);
    copy->font_ea = of_strdup(&d->arena, s->font_ea);
    d->styles[k] = copy;
    d->nstyles++;
    return copy;
}

void
of_style_default(of_style_t *s)
{
    memset(s, 0, sizeof *s);
    s->font = "Times New Roman";
    s->size = 12;
    s->scale = 1;
}

void
of_pstyle_default(of_pstyle_t *p)
{
    memset(p, 0, sizeof *p);
    p->align = OF_LEFT;
    p->line_rule = OF_LINE_AUTO;
    p->line = 1;
    p->outline = -1;
    p->widows = 1;
}

/* ------------------------------------------------------------ numbers  */

/** @brief @p n in Roman numerals, capitals when @p upper, into @p out. */
static void
roman(int n, int upper, char *out, size_t max)
{
    static const struct { int v; const char *s; } r[] = {
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"},
        {90, "xc"}, {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"},
        {4, "iv"}, {1, "i"}
    };
    size_t k = 0, i;

    for (i = 0; i < sizeof r / sizeof r[0] && n > 0; i++) {
        while (n >= r[i].v && k + strlen(r[i].s) < max) {
            const char *p;

            for (p = r[i].s; *p; p++) {
                out[k++] = upper ? (char)(*p - 32) : *p;
            }
            n -= r[i].v;
        }
    }
    out[k] = '\0';
}

void
of_number(int n, int fmt, char *out, size_t max)
{
    if (max == 0) {
        return;
    }
    if ((fmt == OF_NUM_UROMAN || fmt == OF_NUM_LROMAN) && n > 0 &&
        n < 4000) {
        roman(n, fmt == OF_NUM_UROMAN, out, max);
    } else if ((fmt == OF_NUM_ULETTER || fmt == OF_NUM_LLETTER) && n > 0) {
        char c = (char)((fmt == OF_NUM_ULETTER ? 'A' : 'a') + (n - 1) % 26);
        int reps = (n - 1) / 26 + 1;
        size_t k;

        for (k = 0; k < (size_t)reps && k + 1 < max && k < 8; k++) {
            out[k] = c;             /* a, b ... z, aa, bb: as Word counts */
        }
        out[k] = '\0';
    } else {
        snprintf(out, max, "%d", n);
    }
}

/* ------------------------------------------------------------ symbols  */

/** @brief Whether @p hay holds @p needle, ASCII case aside. */
static int
has_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);

    for (; hay != NULL && *hay; hay++) {
        if (strncasecmp(hay, needle, n) == 0) {
            return 1;
        }
    }
    return 0;
}

uint32_t
of_symbol(const char *font, uint32_t code)
{
    static const uint16_t greek[26] = {
        0x3b1, 0x3b2, 0x3c7, 0x3b4, 0x3b5, 0x3c6, 0x3b3, 0x3b7, 0x3b9,
        0x3d5, 0x3ba, 0x3bb, 0x3bc, 0x3bd, 0x3bf, 0x3c0, 0x3b8, 0x3c1,
        0x3c3, 0x3c4, 0x3c5, 0x3d6, 0x3c9, 0x3be, 0x3c8, 0x3b6};
    static const struct { uint8_t c; uint16_t u; } sym[] = {
        {0xb7, 0x2022}, {0xa7, 0x2663}, {0xa8, 0x2666}, {0xa9, 0x2665},
        {0xaa, 0x2660}, {0xae, 0x2192}, {0xac, 0x2190}, {0xde, 0x21d2},
        {0xd6, 0x221a}, {0xb0, 0xb0}, {0xb1, 0xb1}, {0xb4, 0xd7},
        {0xb8, 0xf7}, {0xa3, 0x2264}, {0xb3, 0x2265}, {0xb9, 0x2260},
        {0xbb, 0x2248}, {0xa5, 0x221e}, {0xd2, 0xae}, {0xd3, 0xa9},
        {0xd4, 0x2122}, {0xe0, 0x25ca}, {0xb2, 0x2033}, {0xa2, 0x2032}};
    static const struct { uint8_t c; uint16_t u; } wing[] = {
        {0x6c, 0x25cf}, {0x6e, 0x25a0}, {0x6f, 0x25a1}, {0x71, 0x2751},
        {0x75, 0x25c6}, {0x76, 0x2756}, {0xa7, 0x25aa}, {0xa8, 0x25fb},
        {0xd8, 0x27a2}, {0xfc, 0x2713}, {0xfb, 0x2717}, {0xfe, 0x2611},
        {0xa1, 0x25cb}, {0x9f, 0x2022}, {0x77, 0x2b25}, {0xe8, 0x2794},
        {0x4a, 0x263a}, {0x4c, 0x2639}, {0x46, 0x261e}, {0xf0, 0x21e8}};
    uint32_t c = code >= 0xf000 && code <= 0xf0ff ? code - 0xf000 : code;
    size_t i;

    if (font == NULL || c > 0xff) {
        return code;
    }
    if (has_ci(font, "wingdings")) {
        for (i = 0; i < sizeof wing / sizeof wing[0]; i++) {
            if (wing[i].c == c) {
                return wing[i].u;
            }
        }
        return 0x2022;              /* a bullet for the rest */
    }
    if (!has_ci(font, "symbol")) {
        return code;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        return greek[(c | 0x20) - 'a'] - (c < 'a' ? 0x20 : 0);
    }
    for (i = 0; i < sizeof sym / sizeof sym[0]; i++) {
        if (sym[i].c == c) {
            return sym[i].u;
        }
    }
    return c < 0x80 ? c : code;
}

/* ---------------------------------------------------------- choices  */

/** @brief Whether every prefix in @p req (a Requires list, spaces
 *         between) names a namespace read here. */
static int
understood(const char *req)
{
    static const char *const known[] = {"wps", "wpg", "wpc", "wp14", "w14",
                                        "w15", "w16se", "a14", "p14", "p15",
                                        "c14", "c16", "a16", "v"};
    size_t i, n;

    while (req != NULL && *req) {
        req += strspn(req, " ");
        n = strcspn(req, " ");
        for (i = 0; n > 0 && i < sizeof known / sizeof known[0]; i++) {
            if (strlen(known[i]) == n && strncmp(req, known[i], n) == 0) {
                break;
            }
        }
        if (n > 0 && i == sizeof known / sizeof known[0]) {
            return 0;
        }
        req += n;
    }
    return req != NULL;
}

const tiku_xml_node_t *
of_alternate(const tiku_xml_node_t *ac)
{
    const tiku_xml_node_t *c;

    for (c = tiku_xml_child(ac, "mc:Choice"); c != NULL;
         c = tiku_xml_next(c, "mc:Choice")) {
        if (understood(tiku_xml_attr(c, "Requires"))) {
            return c;
        }
    }
    return tiku_xml_child(ac, "mc:Fallback");
}
