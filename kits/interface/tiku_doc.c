/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_doc.c - reading a help page, and fitting it to a width.
 *
 * Reading cooks the page into one text arena and three tables; laying
 * out walks the blocks for one width and fills two more.  Words are
 * measured one at a time in their own run's face, which is what lets a
 * paragraph mix plain, bold and code, and what keeps a word from being
 * dropped in a box too narrow to hold it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_doc.h"

#include <string.h>

/* The tables are indexed by int16_t, so a cap past that would wrap. */
_Static_assert(TIKU_DOC_BLOCKS_MAX  < 32767, "blocks index is int16_t");
_Static_assert(TIKU_DOC_RUNS_MAX    < 32767, "runs index is int16_t");
_Static_assert(TIKU_DOC_LINES_MAX   < 32767, "lines index is int16_t");
_Static_assert(TIKU_DOC_PIECES_MAX  < 32767, "pieces index is int16_t");
_Static_assert(TIKU_DOC_LINKS_MAX   < 32767, "links index is int16_t");
_Static_assert(TIKU_DOC_BLOCKS_MAX  > 1, "a page needs room for a notice");
_Static_assert(TIKU_DOC_TEXT_MAX    <= 32767, "a run's length is int16_t");

/* One line's worth of scratch, the size the text helpers already use. */
#define DOC_WORD_MAX 256

/* The bits that mean a table filled.  FACE_FLAT and NARROW say how the
 * page was fitted, not that anything was lost, so neither is a cut. */
#define DOC_FULL_CAPS (TIKU_DOC_FULL_TEXT | TIKU_DOC_FULL_BLOCKS | \
                       TIKU_DOC_FULL_RUNS | TIKU_DOC_FULL_LINES | \
                       TIKU_DOC_FULL_PIECES | TIKU_DOC_FULL_LINKS | \
                       TIKU_DOC_FULL_ANCHORS | TIKU_DOC_FULL_IMAGES | \
                       TIKU_DOC_FULL_DEPTH)

/*---------------------------------------------------------------------------*/
/* THE TEXT ARENA                                                            */
/*---------------------------------------------------------------------------*/

/** @brief How many bytes this codepoint takes, valid or not. */
static int utf8_len(const char *s)
{
    unsigned char c = (unsigned char)*s;
    int n = c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc2 ? 2 : 1;
    for (int i = 1; i < n; i++) {
        if (((unsigned char)s[i] & 0xc0) != 0x80) return 1;
    }
    return n;
}

/** @brief Room for @p n more bytes and a terminator. */
static int arena_room(tiku_doc_t *d, int n)
{
    if (d->ntext + n + 1 <= TIKU_DOC_TEXT_MAX) return 1;
    d->full |= TIKU_DOC_FULL_TEXT;
    return 0;
}

static void arena_byte(tiku_doc_t *d, char c)
{
    if (arena_room(d, 1)) d->text[d->ntext++] = c;
}

/** @brief Copy @p n bytes in and terminate them.  @return where, or NONE. */
static int arena_add(tiku_doc_t *d, const char *s, int n)
{
    int at = d->ntext;

    if (!arena_room(d, n)) return TIKU_DOC_NONE;
    memcpy(d->text + at, s, (size_t)n);
    d->ntext += n;
    d->text[d->ntext++] = 0;
    return at;
}

/*---------------------------------------------------------------------------*/
/* ENTITIES                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Write @p cp as UTF-8 into @p out.  @return bytes written. */
static int utf8_put(unsigned cp, char *out)
{
    if (cp < 0x80u) { out[0] = (char)cp; return 1; }
    if (cp < 0x800u) {
        out[0] = (char)(0xc0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 63u));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xe0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 63u));
        out[2] = (char)(0x80u | (cp & 63u));
        return 3;
    }
    out[0] = (char)(0xf0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 63u));
    out[2] = (char)(0x80u | ((cp >> 6) & 63u));
    out[3] = (char)(0x80u | (cp & 63u));
    return 4;
}

/**
 * @brief One entity, starting just past the '&'.
 *
 * An entity this dialect does not know keeps its '&' and reads as
 * itself, which is what a stray ampersand in a page should do.
 * &nbsp; becomes an ordinary space: U+00A0 is two bytes, and a board
 * charges a space advance for each of them, so the two ends would lay
 * the same page out differently.
 *
 * @return bytes written into @p out; @p after lands past the entity.
 */
static int entity(const char *p, char *out, const char **after)
{
    const char *semi = p;
    unsigned cp = 0;
    int n;

    while (*semi && *semi != ';' && semi - p < 10) semi++;
    if (*semi != ';') { *after = p; out[0] = '&'; return 1; }
    n = (int)(semi - p);
    if (p[0] == '#') {
        const char *at = p + 1;
        int hex = (*at == 'x' || *at == 'X');

        if (hex) at++;
        if (at == semi) { *after = p; out[0] = '&'; return 1; }
        for (; at < semi; at++) {
            int v = *at >= '0' && *at <= '9' ? *at - '0' :
                    hex && (*at | 32) >= 'a' && (*at | 32) <= 'f'
                        ? (*at | 32) - 'a' + 10 : -1;
            if (v < 0) { *after = p; out[0] = '&'; return 1; }
            cp = cp * (hex ? 16u : 10u) + (unsigned)v;
            if (cp > 0x10ffffu) { *after = p; out[0] = '&'; return 1; }
        }
        if (cp >= 0xd800u && cp <= 0xdfffu) { *after = p; out[0] = '&'; return 1; }
    } else if (n == 3 && !memcmp(p, "amp", 3)) {
        cp = '&';
    } else if (n == 2 && !memcmp(p, "lt", 2)) {
        cp = '<';
    } else if (n == 2 && !memcmp(p, "gt", 2)) {
        cp = '>';
    } else if (n == 4 && !memcmp(p, "quot", 4)) {
        cp = '"';
    } else if (n == 4 && !memcmp(p, "apos", 4)) {
        cp = '\'';
    } else if (n == 4 && !memcmp(p, "nbsp", 4)) {
        cp = ' ';
    } else {
        *after = p;
        out[0] = '&';
        return 1;
    }
    *after = semi + 1;
    return utf8_put(cp, out);
}

/*---------------------------------------------------------------------------*/
/* READING                                                                   */
/*---------------------------------------------------------------------------*/

enum {
    T_UNKNOWN = 0, T_P, T_H1, T_H2, T_H3, T_UL, T_OL, T_LI, T_PRE,
    T_QUOTE, T_HR, T_IMG, T_A, T_B, T_I, T_CODE, T_BR, T_SKIP
};

/** @brief The element @p name is, or T_UNKNOWN. */
static int tag_of(const char *name, int n)
{
    static const struct { const char *name; int n, tag; } k[] = {
        {"p", 1, T_P}, {"h1", 2, T_H1}, {"h2", 2, T_H2}, {"h3", 2, T_H3},
        {"ul", 2, T_UL}, {"ol", 2, T_OL}, {"li", 2, T_LI},
        {"pre", 3, T_PRE}, {"blockquote", 10, T_QUOTE}, {"hr", 2, T_HR},
        {"img", 3, T_IMG}, {"a", 1, T_A}, {"b", 1, T_B}, {"strong", 6, T_B},
        {"i", 1, T_I}, {"em", 2, T_I}, {"code", 4, T_CODE}, {"br", 2, T_BR},
        {"script", 6, T_SKIP}, {"style", 5, T_SKIP}, {"head", 4, T_SKIP},
        {"title", 5, T_SKIP}
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (k[i].n == n && !memcmp(k[i].name, name, (size_t)n)) return k[i].tag;
    }
    return T_UNKNOWN;
}

/** @brief Everything the reader carries while it walks the page. */
typedef struct {
    tiku_doc_t *d;
    int face, ink, link;        /* the style a run would take now       */
    int flags;                  /* TIKU_DOC_RUN_* for the next run      */
    int block;                  /* the block being filled, or NONE      */
    int run;                    /* the run being filled, or NONE        */
    int pending;                /* a space is owed before the next word */
    int quote;                  /* inside a <blockquote>                */
    int depth;                  /* how many lists are open              */
    int ordered[TIKU_DOC_DEPTH_MAX];
    int number[TIKU_DOC_DEPTH_MAX];
    struct { int tag, face, ink, link; } stack[TIKU_DOC_DEPTH_MAX];
    int nstack;
} read_t;

static void run_close(read_t *r)
{
    tiku_doc_t *d = r->d;

    if (r->run == TIKU_DOC_NONE) return;
    d->run[r->run].len = (int16_t)(d->ntext - d->run[r->run].off);
    if (d->run[r->run].len == 0) {
        /* Nothing was put in it, so it is not a run. */
        d->nrun--;
        d->ntext = d->run[r->run].off;
    } else {
        arena_byte(d, 0);
    }
    r->run = TIKU_DOC_NONE;
}

/** @brief Start a run in the style now in force.  @return 1 when there is one. */
static int run_open(read_t *r)
{
    tiku_doc_t *d = r->d;

    if (r->run != TIKU_DOC_NONE) return 1;
    if (r->block == TIKU_DOC_NONE) return 0;
    if (d->nrun == TIKU_DOC_RUNS_MAX) { d->full |= TIKU_DOC_FULL_RUNS; return 0; }
    if (!arena_room(d, 1)) return 0;
    tiku_doc_run_t *run = &d->run[d->nrun];
    memset(run, 0, sizeof *run);
    run->off = d->ntext;
    run->face = (uint8_t)r->face;
    run->ink = (uint8_t)r->ink;
    run->link = (int16_t)r->link;
    run->flags = (uint8_t)r->flags;
    r->run = d->nrun++;
    return 1;
}

/** @brief A change of face, ink or link ends the run it changed. */
static void style_changed(read_t *r)
{
    tiku_doc_t *d = r->d;

    if (r->run == TIKU_DOC_NONE) return;
    if (d->run[r->run].face == (uint8_t)r->face &&
        d->run[r->run].ink == (uint8_t)r->ink &&
        d->run[r->run].link == (int16_t)r->link) return;
    run_close(r);
}

static void block_end(read_t *r)
{
    tiku_doc_t *d = r->d;

    run_close(r);
    if (r->block == TIKU_DOC_NONE) return;
    tiku_doc_block_t *b = &d->block[r->block];
    b->nrun = (int16_t)(d->nrun - b->run0);
    if (!b->nrun && b->kind != TIKU_DOC_BLOCK_RULE &&
        b->kind != TIKU_DOC_BLOCK_IMAGE) {
        d->nblock--;            /* a block with nothing in it is nothing */
    }
    r->block = TIKU_DOC_NONE;
    r->pending = 0;
}

/** @brief Start a block.  @return 1 when there was room for one. */
static int block_begin(read_t *r, int kind, int depth, int index)
{
    tiku_doc_t *d = r->d;

    block_end(r);
    /* The last slot is kept for the notice, so a page that fills the
     * table can still say that it did. */
    if (d->nblock >= TIKU_DOC_BLOCKS_MAX - 1) {
        d->full |= TIKU_DOC_FULL_BLOCKS;
        return 0;
    }
    tiku_doc_block_t *b = &d->block[d->nblock];
    memset(b, 0, sizeof *b);
    b->kind = (uint8_t)kind;
    b->depth = (uint8_t)depth;
    b->index = (int16_t)index;
    b->image = TIKU_DOC_NONE;
    b->run0 = (int16_t)d->nrun;
    r->block = d->nblock++;
    r->pending = 0;
    return 1;
}

/** @brief Put one word's worth of bytes in, opening a run if need be. */
static void text_add(read_t *r, const char *s, int n)
{
    tiku_doc_t *d = r->d;

    if (n <= 0) return;
    if (r->block == TIKU_DOC_NONE && !block_begin(r, r->quote
            ? TIKU_DOC_BLOCK_QUOTE : TIKU_DOC_BLOCK_P, 0, 0)) return;
    if (!run_open(r)) return;
    if (r->pending) {
        tiku_doc_run_t *run = &d->run[r->run];

        r->pending = 0;
        if (d->ntext > run->off) {
            arena_byte(d, ' ');
        } else if (r->run > d->block[r->block].run0 &&
                   (run - 1)->link == TIKU_DOC_NONE && arena_room(d, 1)) {
            /* The space goes to the words before it rather than to the
             * ones about to start, unless those words are a link: what
             * is underlined is the link and not the gap beside it. */
            tiku_doc_run_t *prev = run - 1;

            d->text[prev->off + prev->len] = ' ';
            prev->len++;
            d->text[d->ntext++] = 0;
            run->off = d->ntext;
        } else if (r->run > d->block[r->block].run0) {
            arena_byte(d, ' ');
        }
    }
    if (!arena_room(d, n)) return;
    memcpy(d->text + d->ntext, s, (size_t)n);
    d->ntext += n;
}

/** @brief An attribute of the tag at @p p, copied into the arena. */
static int attr(const char *p, const char *name, tiku_doc_t *d)
{
    size_t n = strlen(name);

    while (*p && *p != '>') {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == '>' || !*p) break;
        const char *key = p;
        while (*p && *p != '=' && *p != '>' && *p != ' ') p++;
        size_t klen = (size_t)(p - key);
        while (*p == ' ') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ') p++;
        char quote = (*p == '"' || *p == '\'') ? *p : 0;
        if (quote) p++;
        const char *val = p;
        while (*p && (quote ? *p != quote : (*p != ' ' && *p != '>'))) p++;
        if (klen == n && !memcmp(key, name, n)) {
            char out[TIKU_DOC_TEXT_MAX > 512 ? 512 : TIKU_DOC_TEXT_MAX];
            int at = 0;
            for (const char *s = val; s < p && at < (int)sizeof out - 5; ) {
                if (*s == '&') {
                    const char *after;
                    at += entity(s + 1, out + at, &after);
                    s = after == s + 1 ? s + 1 : after;
                } else {
                    out[at++] = *s++;
                }
            }
            return arena_add(d, out, at);
        }
        if (quote && *p) p++;
    }
    return TIKU_DOC_NONE;
}

/** @brief Remember a name a link may reach. */
static void anchor_add(read_t *r, const char *p)
{
    tiku_doc_t *d = r->d;
    int id = attr(p, "id", d);

    if (id == TIKU_DOC_NONE || r->block == TIKU_DOC_NONE) return;
    if (d->nanchor == TIKU_DOC_ANCHORS_MAX) {
        d->full |= TIKU_DOC_FULL_ANCHORS;
        return;
    }
    d->anchor[d->nanchor].id = id;
    d->anchor[d->nanchor].block = (int16_t)r->block;
    d->anchor[d->nanchor].spare = 0;
    d->nanchor++;
}

static void marker_add(read_t *r, int ordered, int number)
{
    char text[16];
    int n;

    if (ordered) {
        int v = number, digits = 0, i;
        char tmp[8];

        do { tmp[digits++] = (char)('0' + v % 10); v /= 10; }
        while (v && digits < (int)sizeof tmp);
        for (i = 0; i < digits; i++) text[i] = tmp[digits - 1 - i];
        text[digits] = '.';
        n = digits + 1;
    } else {
        text[0] = '-';
        n = 1;
    }
    int face = r->face, ink = r->ink, link = r->link, flags = r->flags;
    r->face = TIKU_MEASURE_PLAIN;
    r->ink = TIKU_DOC_INK_PLAIN;
    r->link = TIKU_DOC_NONE;
    r->flags = TIKU_DOC_RUN_MARKER;
    run_close(r);
    if (run_open(r)) {
        if (arena_room(r->d, n)) {
            memcpy(r->d->text + r->d->ntext, text, (size_t)n);
            r->d->ntext += n;
        }
    }
    run_close(r);
    r->face = face; r->ink = ink; r->link = link; r->flags = flags;
}

/** @brief Put the element stack's topmost matching entry back. */
static void stack_pop(read_t *r, int tag)
{
    int at = r->nstack - 1;

    while (at >= 0 && r->stack[at].tag != tag) at--;
    if (at < 0) return;
    style_changed(r);
    r->face = r->stack[at].face;
    r->ink = r->stack[at].ink;
    r->link = r->stack[at].link;
    run_close(r);
    for (int i = at; i < r->nstack - 1; i++) r->stack[i] = r->stack[i + 1];
    r->nstack--;
}

static void stack_push(read_t *r, int tag)
{
    if (r->nstack == TIKU_DOC_DEPTH_MAX) {
        r->d->full |= TIKU_DOC_FULL_DEPTH;
        return;
    }
    r->stack[r->nstack].tag = tag;
    r->stack[r->nstack].face = r->face;
    r->stack[r->nstack].ink = r->ink;
    r->stack[r->nstack].link = r->link;
    r->nstack++;
}

static void image_add(read_t *r, const char *p)
{
    tiku_doc_t *d = r->d;
    int src, alt;

    if (!block_begin(r, TIKU_DOC_BLOCK_IMAGE, 0, 0)) return;
    src = attr(p, "src", d);
    alt = attr(p, "alt", d);
    if (d->nimage == TIKU_DOC_IMAGES_MAX) {
        d->full |= TIKU_DOC_FULL_IMAGES;
    } else {
        d->image[d->nimage].src = src;
        d->image[d->nimage].alt = alt;
        d->image[d->nimage].w = 0;
        d->image[d->nimage].h = 0;
        d->image[d->nimage].block = (int16_t)r->block;
        d->image[d->nimage].spare = 0;
        d->block[r->block].image = (int16_t)d->nimage++;
    }
    if (alt != TIKU_DOC_NONE) {
        int ink = r->ink;

        r->ink = TIKU_DOC_INK_FAINT;
        text_add(r, d->text + alt, (int)strlen(d->text + alt));
        r->ink = ink;
    }
    block_end(r);
}

/** @brief One tag, already bounded by its angles.  @return past it. */
static const char *tag_do(read_t *r, const char *p, const char **skip_to)
{
    const char *body = p + 1;
    int close = *body == '/';
    const char *name;
    int n, tag;

    if (close) body++;
    name = body;
    while (*body && *body != ' ' && *body != '>' && *body != '/' &&
           *body != '\n' && *body != '\t') body++;
    n = (int)(body - name);
    tag = n ? tag_of(name, n) : T_UNKNOWN;
    const char *end = body;
    while (*end && *end != '>') end++;
    if (*end == '>') end++;

    switch (tag) {
    case T_SKIP:
        if (!close) *skip_to = name;
        break;
    case T_P:
        if (close) block_end(r);
        else (void)block_begin(r, r->quote ? TIKU_DOC_BLOCK_QUOTE
                                           : TIKU_DOC_BLOCK_P, 0, 0);
        break;
    case T_H1: case T_H2: case T_H3: {
        int kind = TIKU_DOC_BLOCK_H1 + (tag - T_H1);

        if (close) {
            block_end(r);
            r->face = TIKU_MEASURE_PLAIN;
            break;
        }
        if (!block_begin(r, kind, 0, 0)) break;
        /* A heading's words are the bold face, which is the face they
         * are measured in as well as drawn in. */
        r->face = TIKU_MEASURE_BOLD;
        anchor_add(r, body);
        break;
    }
    case T_UL: case T_OL:
        block_end(r);
        if (close) {
            if (r->depth) r->depth--;
        } else if (r->depth < TIKU_DOC_DEPTH_MAX) {
            r->ordered[r->depth] = tag == T_OL;
            r->number[r->depth] = 0;
            r->depth++;
        } else {
            r->d->full |= TIKU_DOC_FULL_DEPTH;
        }
        break;
    case T_LI: {
        int at = r->depth ? r->depth - 1 : 0;

        if (close) { block_end(r); break; }
        if (!block_begin(r, TIKU_DOC_BLOCK_ITEM, r->depth ? r->depth : 1,
                         r->ordered[at] ? ++r->number[at] : 0)) break;
        anchor_add(r, body);
        marker_add(r, r->ordered[at], r->number[at]);
        break;
    }
    case T_PRE:
        if (close) {
            r->flags &= ~TIKU_DOC_RUN_PRE;
            r->face = TIKU_MEASURE_PLAIN;
            block_end(r);
        } else if (block_begin(r, TIKU_DOC_BLOCK_PRE, 0, 0)) {
            r->flags |= TIKU_DOC_RUN_PRE;
            r->face = TIKU_MEASURE_MONO;
            anchor_add(r, body);
        }
        break;
    case T_QUOTE:
        block_end(r);
        r->quote = !close;
        break;
    case T_HR:
        if (!close && block_begin(r, TIKU_DOC_BLOCK_RULE, 0, 0)) block_end(r);
        break;
    case T_IMG:
        if (!close) image_add(r, body);
        break;
    case T_A:
        if (close) { stack_pop(r, T_A); break; }
        /* An attribute is written into the same arena as the words, so
         * the run has to be closed before one is read or the address
         * lands inside the sentence. */
        run_close(r);
        stack_push(r, T_A);
        if (r->d->nlink == TIKU_DOC_LINKS_MAX) {
            r->d->full |= TIKU_DOC_FULL_LINKS;
        } else {
            int href = attr(body, "href", r->d);

            r->d->link[r->d->nlink].href = href;
            r->d->link[r->d->nlink].run0 = (int16_t)r->d->nrun;
            r->d->link[r->d->nlink].nrun = 0;
            r->d->link[r->d->nlink].piece0 = TIKU_DOC_NONE;
            r->d->link[r->d->nlink].npiece = 0;
            r->link = r->d->nlink++;
            r->ink = TIKU_DOC_INK_LINK;
        }
        run_close(r);
        break;
    case T_B:
        if (close) { stack_pop(r, T_B); break; }
        style_changed(r);
        stack_push(r, T_B);
        r->face = TIKU_MEASURE_BOLD;
        run_close(r);
        break;
    case T_I:
        if (close) { stack_pop(r, T_I); break; }
        style_changed(r);
        stack_push(r, T_I);
        r->ink = TIKU_DOC_INK_EMPHASIS;
        run_close(r);
        break;
    case T_CODE:
        if (close) { stack_pop(r, T_CODE); break; }
        style_changed(r);
        stack_push(r, T_CODE);
        r->face = TIKU_MEASURE_MONO;
        if (r->ink == TIKU_DOC_INK_PLAIN) r->ink = TIKU_DOC_INK_CODE;
        run_close(r);
        break;
    case T_BR:
        if (!close) {
            if (run_open(r)) r->d->run[r->run].flags |= TIKU_DOC_RUN_BREAK;
            run_close(r);
            r->pending = 0;
        }
        break;
    default:
        break;                  /* an unknown tag keeps its text */
    }
    return end;
}

/** @brief Close a link's run range once its element is done. */
static void links_close(tiku_doc_t *d)
{
    for (int i = 0; i < d->nlink; i++) {
        int last = d->link[i].run0;

        while (last < d->nrun && d->run[last].link == i) last++;
        d->link[i].nrun = (int16_t)(last - d->link[i].run0);
    }
}

void tiku_doc_init(tiku_doc_t *d)
{
    memset(d, 0, sizeof *d);
    d->title = TIKU_DOC_NONE;
    d->laid_width = TIKU_DOC_NONE;
}

void tiku_doc_images(tiku_doc_t *d, tiku_doc_image_fn fn, void *ctx)
{
    d->image_fn = fn;
    d->image_ctx = ctx;
}

int tiku_doc_read(tiku_doc_t *d, const char *html)
{
    tiku_doc_image_fn fn = d->image_fn;
    void *ctx = d->image_ctx;
    read_t r;
    const char *p = html ? html : "";
    const char *skip_to = NULL;

    tiku_doc_init(d);
    d->image_fn = fn;
    d->image_ctx = ctx;
    memset(&r, 0, sizeof r);
    r.d = d;
    r.face = TIKU_MEASURE_PLAIN;
    r.ink = TIKU_DOC_INK_PLAIN;
    r.link = TIKU_DOC_NONE;
    r.block = TIKU_DOC_NONE;
    r.run = TIKU_DOC_NONE;

    while (*p) {
        if (skip_to) {
            /* Everything up to this element's own close tag is gone. */
            if (p[0] == '<' && p[1] == '/') {
                const char *name = p + 2;
                int n = 0;

                while (name[n] && name[n] != '>' && name[n] != ' ') n++;
                if (tag_of(name, n) == T_SKIP) {
                    skip_to = NULL;
                    while (*p && *p != '>') p++;
                    if (*p) p++;
                    continue;
                }
            }
            p++;
            continue;
        }
        if (p[0] == '<' && p[1] == '!') {
            if (p[2] == '-' && p[3] == '-') {
                const char *end = strstr(p + 4, "-->");

                p = end ? end + 3 : p + strlen(p);
            } else {
                while (*p && *p != '>') p++;
                if (*p) p++;
            }
            continue;
        }
        if (p[0] == '<' && (p[1] == '/' || p[1] == '_' ||
                            (p[1] | 32) >= 'a' || (p[1] >= '0' && p[1] <= '9'))) {
            p = tag_do(&r, p, &skip_to);
            continue;
        }
        if (*p == '<') { p++; continue; }
        if ((r.flags & TIKU_DOC_RUN_PRE) == 0 &&
            (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
            /* A closing tag ends its run, and the blank after it is
             * still the space between two words. */
            if (r.block != TIKU_DOC_NONE &&
                (r.run != TIKU_DOC_NONE ||
                 d->nrun > d->block[r.block].run0)) {
                r.pending = 1;
            }
            p++;
            continue;
        }
        if (*p == '&') {
            char out[8];
            const char *after;
            int n = entity(p + 1, out, &after);

            text_add(&r, out, n);
            p = after == p + 1 ? p + 1 : after;
            continue;
        }
        if ((r.flags & TIKU_DOC_RUN_PRE) && *p == '\t') {
            text_add(&r, "    ", 4);
            p++;
            continue;
        }
        {
            int n = utf8_len(p);

            if ((unsigned char)*p < 32 && *p != '\n') { p += n; continue; }
            text_add(&r, p, n);
            p += n;
        }
    }
    block_end(&r);
    links_close(d);
    for (int i = 0; i < d->nblock && d->title == TIKU_DOC_NONE; i++) {
        if (d->block[i].kind == TIKU_DOC_BLOCK_H1 && d->block[i].nrun) {
            d->title = d->run[d->block[i].run0].off;
        }
    }
    return d->full == 0;
}

/*---------------------------------------------------------------------------*/
/* LAYING OUT                                                                */
/*---------------------------------------------------------------------------*/

/** @brief Everything the layout carries down a block. */
typedef struct {
    tiku_doc_t *d;
    int width;
    int y;
    int line;                   /* the line being filled, or NONE       */
    int x;                      /* the pen, from the line's own x       */
    int piece;                  /* the piece being grown, or NONE       */
    int block;
    int xc;                     /* where a continuation line begins     */
} lay_t;

/** @brief How tall a face's line is, and how far down its baseline. */
static int face_h(int face) { return tiku_measure_height(face); }

/** @brief What room the line being filled has, its indent taken off. */
static int line_room(const lay_t *L)
{
    int x = L->line != TIKU_DOC_NONE ? L->d->line[L->line].x : L->xc;
    int room = L->width - x;

    return room > 0 ? room : 1;
}

/** @brief Measure @p n bytes of @p s in @p face, through a terminator. */
static int word_w(int face, const char *s, int n)
{
    char buf[DOC_WORD_MAX];
    int total = 0;

    while (n > 0) {
        int take = n < (int)sizeof buf - 1 ? n : (int)sizeof buf - 1;

        /* Never split a codepoint at the scratch edge. */
        while (take > 1 && ((unsigned char)s[take] & 0xc0) == 0x80) take--;
        memcpy(buf, s, (size_t)take);
        buf[take] = 0;
        total += tiku_measure_width(face, buf);
        s += take;
        n -= take;
    }
    return total;
}

static void line_close(lay_t *L)
{
    tiku_doc_t *d = L->d;

    if (L->line == TIKU_DOC_NONE) return;
    tiku_doc_line_t *ln = &d->line[L->line];
    /* A space left at the end of a line is not drawn and not hit. */
    if (ln->npiece) {
        tiku_doc_piece_t *last = &d->piece[ln->piece0 + ln->npiece - 1];
        int face = d->run[last->run].face;
        int before = last->w;

        while (last->len > 0 && d->text[last->off + last->len - 1] == ' ') {
            last->len--;
            last->w = (int16_t)word_w(face, d->text + last->off, last->len);
        }
        /* The pen goes back with it, since the line's own width is
         * where the pen ended and a space that is not drawn is not
         * part of the line. */
        L->x -= before - last->w;
        if (!last->len) ln->npiece--, d->npiece--;
    }
    if (!ln->h) {
        ln->h = (int16_t)face_h(TIKU_MEASURE_PLAIN);
        ln->ascent = (int16_t)tiku_measure_ascent(TIKU_MEASURE_PLAIN);
    }
    ln->w = (int16_t)L->x;
    L->y += ln->h;
    L->line = TIKU_DOC_NONE;
    L->piece = TIKU_DOC_NONE;
}

/** @brief Begin a line at @p x.  @return 1 when there was room. */
static int line_open(lay_t *L, int x)
{
    tiku_doc_t *d = L->d;

    if (L->line != TIKU_DOC_NONE) return 1;
    if (d->nline == TIKU_DOC_LINES_MAX) { d->full |= TIKU_DOC_FULL_LINES; return 0; }
    tiku_doc_line_t *ln = &d->line[d->nline];
    memset(ln, 0, sizeof *ln);
    ln->y = L->y;
    ln->block = (int16_t)L->block;
    ln->piece0 = (int16_t)d->npiece;
    ln->x = (int16_t)x;
    L->line = d->nline++;
    L->x = 0;
    L->piece = TIKU_DOC_NONE;
    if (d->block[L->block].nline == 0) d->block[L->block].line0 = (int16_t)L->line;
    d->block[L->block].nline++;
    return 1;
}

/** @brief Put @p n bytes on the line at the pen, growing the piece. */
static int piece_add(lay_t *L, int run, int off, int n, int w)
{
    tiku_doc_t *d = L->d;

    if (L->line == TIKU_DOC_NONE) return 0;
    tiku_doc_line_t *ln = &d->line[L->line];
    if (L->piece != TIKU_DOC_NONE && d->piece[L->piece].run == run &&
        d->piece[L->piece].off + d->piece[L->piece].len == off) {
        d->piece[L->piece].len = (int16_t)(d->piece[L->piece].len + n);
        d->piece[L->piece].w = (int16_t)(d->piece[L->piece].w + w);
    } else {
        if (d->npiece == TIKU_DOC_PIECES_MAX) {
            d->full |= TIKU_DOC_FULL_PIECES;
            return 0;
        }
        tiku_doc_piece_t *pc = &d->piece[d->npiece];
        pc->off = off;
        pc->len = (int16_t)n;
        pc->x = (int16_t)L->x;
        pc->w = (int16_t)w;
        pc->run = (int16_t)run;
        L->piece = d->npiece++;
        ln->npiece++;
    }
    L->x += w;
    {
        int face = d->run[run].face;
        int h = face_h(face), a = tiku_measure_ascent(face);

        if (h > ln->h) ln->h = (int16_t)h;
        if (a > ln->ascent) ln->ascent = (int16_t)a;
    }
    return 1;
}

/**
 * @brief Place a word too wide for any line, a codepoint at a time.
 *
 * The wrapping helper in tiku_text_layout renders nothing at all in a
 * box narrower than one glyph, so a word placed through it would be
 * lost; here at least one codepoint goes on every line.
 */
static void place_long(lay_t *L, int run, int off, int n, int face)
{
    tiku_doc_t *d = L->d;
    int at = 0;

    d->full |= TIKU_DOC_NARROW;
    while (at < n) {
        int fit = 0, fitw = 0, i = at, w = 0, room = line_room(L);

        while (i < n) {
            int step = utf8_len(d->text + off + i);
            int add = word_w(face, d->text + off + i, step);

            if (L->x + w + add > room) break;
            w += add;
            i += step;
            fit = i - at;
            fitw = w;
        }
        /* A box with no room for one glyph still takes one: the helper
         * in tiku_text_layout renders nothing at all there, and a page
         * must not lose a word to its own margin. */
        if (!fit) {
            fit = utf8_len(d->text + off + at);
            fitw = word_w(face, d->text + off + at, fit);
        }
        if (!piece_add(L, run, off + at, fit, fitw)) return;
        at += fit;
        if (at < n) {
            line_close(L);
            if (!line_open(L, L->xc)) return;
        }
    }
}

/** @brief Lay one run's words out, breaking lines as they fill. */
static void place_run(lay_t *L, int run)
{
    tiku_doc_t *d = L->d;
    const tiku_doc_run_t *rn = &d->run[run];
    int face = rn->face, off = rn->off, n = rn->len, at = 0;

    while (at < n) {
        int start = at, w, room;

        if ((rn->flags & TIKU_DOC_RUN_PRE) && d->text[off + at] == '\n') {
            line_close(L);
            if (!line_open(L, L->xc)) return;
            at++;
            continue;
        }
        /* A word, and the blank space that follows it on the same line. */
        while (at < n && d->text[off + at] != ' ' &&
               !((rn->flags & TIKU_DOC_RUN_PRE) && d->text[off + at] == '\n')) {
            at += utf8_len(d->text + off + at);
        }
        w = word_w(face, d->text + off + start, at - start);
        room = line_room(L);
        if (L->line != TIKU_DOC_NONE && L->x && L->x + w > room) {
            line_close(L);
        }
        if (L->line == TIKU_DOC_NONE && !line_open(L, L->xc)) return;
        room = line_room(L);
        if (w > room) {
            place_long(L, run, off + start, at - start, face);
        } else if (!piece_add(L, run, off + start, at - start, w)) {
            return;
        }
        /* The spaces after it ride with the word, and are trimmed if the
         * line ends here. */
        {
            int sp = at;

            while (at < n && d->text[off + at] == ' ') at++;
            if (at > sp && !piece_add(L, run, off + sp, at - sp,
                                      word_w(face, d->text + off + sp, at - sp))) {
                return;
            }
        }
    }
    if (rn->flags & TIKU_DOC_RUN_BREAK) line_close(L);
}

/** @brief The space a block wants above it, and how far it is indented. */
static void block_metrics(const tiku_doc_t *d, int i, int *above, int *below,
                          int *indent)
{
    int line = face_h(TIKU_MEASURE_PLAIN);
    const tiku_doc_block_t *b = &d->block[i];

    *above = 0;
    *below = line / 2;
    *indent = 0;
    switch (b->kind) {
    case TIKU_DOC_BLOCK_H1: *above = line; *below = line / 2; break;
    case TIKU_DOC_BLOCK_H2: *above = line; *below = line / 3; break;
    case TIKU_DOC_BLOCK_H3: *above = line / 2; *below = line / 4; break;
    case TIKU_DOC_BLOCK_ITEM:
        *indent = b->depth * (line + 4);
        *below = line / 5;
        break;
    case TIKU_DOC_BLOCK_PRE:
        *above = line / 2; *below = line / 2; *indent = line / 2;
        break;
    case TIKU_DOC_BLOCK_QUOTE:
        *above = line / 3; *below = line / 3; *indent = line;
        break;
    case TIKU_DOC_BLOCK_RULE: *above = line / 2; *below = line / 2; break;
    case TIKU_DOC_BLOCK_NOTICE: *above = line; *below = line / 2; break;
    default: break;
    }
    if (i == 0) *above = 0;
}

/** @brief One block: its space above, its lines, its space below. */
static void lay_block(lay_t *L, int i)
{
    tiku_doc_t *d = L->d;
    tiku_doc_block_t *b = &d->block[i];
    int above, below, indent, hang = 0;

    block_metrics(d, i, &above, &below, &indent);
    L->y += above;
    L->block = i;
    b->y = L->y;
    b->line0 = (int16_t)d->nline;
    b->nline = 0;

    if (b->kind == TIKU_DOC_BLOCK_RULE) {
        if (line_open(L, indent)) {
            d->line[L->line].h = (int16_t)(face_h(TIKU_MEASURE_PLAIN) / 2);
            d->line[L->line].ascent = d->line[L->line].h;
            line_close(L);
        }
        b->h = (int16_t)(L->y + below - b->y);
        L->y += below;
        return;
    }
    if (b->kind == TIKU_DOC_BLOCK_NOTICE) {
        if (line_open(L, indent)) {
            d->line[L->line].h = (int16_t)face_h(TIKU_MEASURE_PLAIN);
            d->line[L->line].ascent =
                (int16_t)tiku_measure_ascent(TIKU_MEASURE_PLAIN);
            line_close(L);
        }
        b->h = (int16_t)(L->y + below - b->y);
        L->y += below;
        return;
    }
    if (b->kind == TIKU_DOC_BLOCK_IMAGE && b->image != TIKU_DOC_NONE) {
        int w = 0, h = 0;

        if (d->image_fn && d->image[b->image].src != TIKU_DOC_NONE &&
            d->image_fn(d->image_ctx, d->text + d->image[b->image].src, &w, &h)
            && w > 0 && h > 0) {
            d->image[b->image].w = (int16_t)w;
            d->image[b->image].h = (int16_t)h;
            if (line_open(L, indent)) {
                d->line[L->line].h = (int16_t)h;
                d->line[L->line].ascent = (int16_t)h;
                line_close(L);
            }
            b->h = (int16_t)(L->y + below - b->y);
            L->y += below;
            return;
        }
        d->image[b->image].w = 0;
        d->image[b->image].h = 0;
        /* No pixels: the alt runs are drawn instead, in a frame. */
    }

    /* A list item hangs its marker: the first line begins at the indent
     * and the rest line up under the words. */
    if (b->kind == TIKU_DOC_BLOCK_ITEM && b->nrun &&
        (d->run[b->run0].flags & TIKU_DOC_RUN_MARKER)) {
        hang = word_w(TIKU_MEASURE_PLAIN, d->text + d->run[b->run0].off,
                      d->run[b->run0].len) + face_h(TIKU_MEASURE_PLAIN) / 3;
    }
    L->xc = indent + hang;
    if (!line_open(L, indent)) { b->h = 0; return; }
    for (int k = 0; k < b->nrun; k++) {
        int run = b->run0 + k;

        if (d->run[run].flags & TIKU_DOC_RUN_MARKER) {
            int w = word_w(TIKU_MEASURE_PLAIN, d->text + d->run[run].off,
                           d->run[run].len);

            (void)piece_add(L, run, d->run[run].off, d->run[run].len, w);
            L->x = hang;
            continue;
        }
        place_run(L, run);
        if (L->line == TIKU_DOC_NONE && k + 1 < b->nrun &&
            !line_open(L, L->xc)) break;
    }
    line_close(L);
    b->h = (int16_t)(L->y + below - b->y);
    L->y += below;
}

int tiku_doc_layout(tiku_doc_t *d, int width)
{
    lay_t L;
    int face_height = face_h(TIKU_MEASURE_PLAIN);

    if (width < 1) width = 1;
    if (d->laid_width == width && d->laid_height == face_height) {
        return d->height;
    }
    d->nline = 0;
    d->npiece = 0;
    /* A notice from the last width goes with that width's tables. */
    if (d->nblock && d->block[d->nblock - 1].kind == TIKU_DOC_BLOCK_NOTICE) {
        d->nblock--;
    }
    d->full &= ~(TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES | TIKU_DOC_NARROW);
    /* A heading wants a size the measuring end cannot answer, so every
     * heading is the bold face at the body size. */
    for (int i = 0; i < d->nblock; i++) {
        if (d->block[i].kind == TIKU_DOC_BLOCK_H1 ||
            d->block[i].kind == TIKU_DOC_BLOCK_H2 ||
            d->block[i].kind == TIKU_DOC_BLOCK_H3) {
            d->full |= TIKU_DOC_FACE_FLAT;
            break;
        }
    }
    for (int i = 0; i < d->nlink; i++) {
        d->link[i].piece0 = TIKU_DOC_NONE;
        d->link[i].npiece = 0;
    }
    memset(&L, 0, sizeof L);
    L.d = d;
    L.width = width;
    L.line = TIKU_DOC_NONE;
    L.piece = TIKU_DOC_NONE;
    for (int i = 0; i < d->nblock; i++) lay_block(&L, i);
    /* What a cap cost takes room of its own, so nothing can pass over it. */
    if ((d->full & DOC_FULL_CAPS) && d->nblock < TIKU_DOC_BLOCKS_MAX) {
        tiku_doc_block_t *b = &d->block[d->nblock];

        memset(b, 0, sizeof *b);
        b->kind = TIKU_DOC_BLOCK_NOTICE;
        b->image = TIKU_DOC_NONE;
        b->run0 = (int16_t)d->nrun;
        d->nblock++;
        lay_block(&L, d->nblock - 1);
    }
    for (int i = 0; i < d->npiece; i++) {
        int link = d->run[d->piece[i].run].link;

        if (link < 0 || link >= d->nlink) continue;
        if (d->link[link].piece0 == TIKU_DOC_NONE) {
            d->link[link].piece0 = (int16_t)i;
            d->link[link].npiece = 1;
        } else if (d->link[link].piece0 + d->link[link].npiece == i) {
            /* Only what lies against it: a page that left an anchor open
             * puts another link's words in the middle, and they are not
             * this one's to draw a ring around. */
            d->link[link].npiece++;
        }
    }
    d->height = L.y;
    d->laid_width = width;
    d->laid_height = face_height;
    return d->height;
}

/*---------------------------------------------------------------------------*/
/* ASKING                                                                    */
/*---------------------------------------------------------------------------*/

int tiku_doc_height(const tiku_doc_t *d) { return d->height; }
int tiku_doc_width(const tiku_doc_t *d) { return d->laid_width; }
int tiku_doc_laid(const tiku_doc_t *d)
{ return d->laid_width != TIKU_DOC_NONE && d->laid_width > 0; }
unsigned tiku_doc_full(const tiku_doc_t *d) { return d->full; }
int tiku_doc_block_count(const tiku_doc_t *d) { return d->nblock; }
int tiku_doc_line_count(const tiku_doc_t *d) { return d->nline; }
int tiku_doc_piece_count(const tiku_doc_t *d) { return d->npiece; }
int tiku_doc_link_count(const tiku_doc_t *d) { return d->nlink; }

const tiku_doc_block_t *tiku_doc_block(const tiku_doc_t *d, int i)
{ return i >= 0 && i < d->nblock ? &d->block[i] : NULL; }
const tiku_doc_run_t *tiku_doc_run(const tiku_doc_t *d, int i)
{ return i >= 0 && i < d->nrun ? &d->run[i] : NULL; }
const tiku_doc_line_t *tiku_doc_line(const tiku_doc_t *d, int i)
{ return i >= 0 && i < d->nline ? &d->line[i] : NULL; }
const tiku_doc_piece_t *tiku_doc_piece(const tiku_doc_t *d, int i)
{ return i >= 0 && i < d->npiece ? &d->piece[i] : NULL; }

const char *tiku_doc_text(const tiku_doc_t *d, int off)
{ return off >= 0 && off < d->ntext ? d->text + off : ""; }

const char *tiku_doc_title(const tiku_doc_t *d)
{ return d->title == TIKU_DOC_NONE ? NULL : d->text + d->title; }

int tiku_doc_full_first(const tiku_doc_t *d, const char **what, int *cap)
{
    static const struct { unsigned bit; const char *what; int cap; } t[] = {
        {TIKU_DOC_FULL_TEXT,    "text",    TIKU_DOC_TEXT_MAX},
        {TIKU_DOC_FULL_BLOCKS,  "blocks",  TIKU_DOC_BLOCKS_MAX - 1},
        {TIKU_DOC_FULL_RUNS,    "runs",    TIKU_DOC_RUNS_MAX},
        {TIKU_DOC_FULL_LINES,   "lines",   TIKU_DOC_LINES_MAX},
        {TIKU_DOC_FULL_PIECES,  "pieces",  TIKU_DOC_PIECES_MAX},
        {TIKU_DOC_FULL_LINKS,   "links",   TIKU_DOC_LINKS_MAX},
        {TIKU_DOC_FULL_ANCHORS, "anchors", TIKU_DOC_ANCHORS_MAX},
        {TIKU_DOC_FULL_IMAGES,  "images",  TIKU_DOC_IMAGES_MAX},
        {TIKU_DOC_FULL_DEPTH,   "nesting", TIKU_DOC_DEPTH_MAX}
    };

    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        if (d->full & t[i].bit) {
            if (what) *what = t[i].what;
            if (cap) *cap = t[i].cap;
            return 1;
        }
    }
    return 0;
}

tiku_rect_t tiku_doc_piece_rect(const tiku_doc_t *d, int piece)
{
    tiku_rect_t r = {0, 0, 0, 0};

    if (piece < 0 || piece >= d->npiece) return r;
    const tiku_doc_piece_t *pc = &d->piece[piece];
    for (int i = 0; i < d->nline; i++) {
        if (piece < d->line[i].piece0 ||
            piece >= d->line[i].piece0 + d->line[i].npiece) continue;
        r.x = d->line[i].x + pc->x;
        r.y = (int)d->line[i].y;
        r.w = pc->w;
        r.h = d->line[i].h;
        return r;
    }
    return r;
}

tiku_rect_t tiku_doc_block_rect(const tiku_doc_t *d, int block)
{
    tiku_rect_t r = {0, 0, 0, 0};

    if (block < 0 || block >= d->nblock) return r;
    const tiku_doc_block_t *b = &d->block[block];
    r.y = (int)b->y;
    r.h = b->h;
    r.x = d->laid_width;
    for (int i = b->line0; i < b->line0 + b->nline && i < d->nline; i++) {
        int right = d->line[i].x + d->line[i].w;

        if (d->line[i].x < r.x) r.x = d->line[i].x;
        if (right - r.x > r.w) r.w = right - r.x;
    }
    if (!b->nline) { r.x = 0; r.w = 0; }
    return r;
}

int tiku_doc_line_at(const tiku_doc_t *d, int y)
{
    if (!tiku_doc_laid(d)) return TIKU_DOC_NONE;
    for (int i = 0; i < d->nline; i++) {
        if (y >= d->line[i].y && y < d->line[i].y + d->line[i].h) return i;
    }
    return TIKU_DOC_NONE;
}

tiku_doc_hit_t tiku_doc_hit(const tiku_doc_t *d, int x, int y)
{
    tiku_doc_hit_t h;
    int line;

    h.kind = TIKU_DOC_HIT_NONE;
    h.link = TIKU_DOC_NONE;
    h.block = TIKU_DOC_NONE;
    h.line = TIKU_DOC_NONE;
    h.piece = TIKU_DOC_NONE;
    if (!tiku_doc_laid(d)) return h;
    line = tiku_doc_line_at(d, y);
    if (line == TIKU_DOC_NONE) return h;
    h.line = line;
    h.block = d->line[line].block;
    for (int i = d->line[line].piece0;
         i < d->line[line].piece0 + d->line[line].npiece; i++) {
        int left = d->line[line].x + d->piece[i].x;

        if (x < left || x >= left + d->piece[i].w) continue;
        h.piece = i;
        h.kind = TIKU_DOC_HIT_TEXT;
        if (d->run[d->piece[i].run].link != TIKU_DOC_NONE) {
            h.kind = TIKU_DOC_HIT_LINK;
            h.link = d->run[d->piece[i].run].link;
        }
        return h;
    }
    if (h.block != TIKU_DOC_NONE &&
        d->block[h.block].kind == TIKU_DOC_BLOCK_IMAGE &&
        d->block[h.block].image != TIKU_DOC_NONE) {
        const tiku_doc_image_t *im = &d->image[d->block[h.block].image];

        if (im->w > 0 && x >= d->line[line].x &&
            x < d->line[line].x + im->w) {
            h.kind = TIKU_DOC_HIT_IMAGE;
        }
    }
    return h;
}

const char *tiku_doc_link_href(const tiku_doc_t *d, int link)
{
    if (link < 0 || link >= d->nlink) return "";
    return d->link[link].href == TIKU_DOC_NONE ? ""
                                               : d->text + d->link[link].href;
}

int tiku_doc_link_pieces(const tiku_doc_t *d, int link, int *first, int *n)
{
    if (link < 0 || link >= d->nlink ||
        d->link[link].piece0 == TIKU_DOC_NONE) return 0;
    if (first) *first = d->link[link].piece0;
    if (n) *n = d->link[link].npiece;
    return 1;
}

int tiku_doc_link_next(const tiku_doc_t *d, int from, int back)
{
    if (!d->nlink) return TIKU_DOC_NONE;
    if (from < 0 || from >= d->nlink) return back ? d->nlink - 1 : 0;
    if (back) return from == 0 ? d->nlink - 1 : from - 1;
    return from + 1 == d->nlink ? 0 : from + 1;
}

int tiku_doc_anchor_y(const tiku_doc_t *d, const char *id)
{
    if (!id || !*id) return TIKU_DOC_NONE;
    for (int i = 0; i < d->nanchor; i++) {
        if (d->anchor[i].id == TIKU_DOC_NONE) continue;
        if (strcmp(d->text + d->anchor[i].id, id)) continue;
        if (d->anchor[i].block < 0 || d->anchor[i].block >= d->nblock) break;
        return (int)d->block[d->anchor[i].block].y;
    }
    return TIKU_DOC_NONE;
}

tiku_doc_place_t tiku_doc_place(const tiku_doc_t *d, int y)
{
    tiku_doc_place_t at;

    at.block = 0;
    at.into = 0;
    for (int i = 0; i < d->nblock; i++) {
        if (d->block[i].y > y) break;
        at.block = i;
        at.into = 0;
        for (int k = d->block[i].line0;
             k < d->block[i].line0 + d->block[i].nline && k < d->nline; k++) {
            if (d->line[k].y <= y) at.into = k - d->block[i].line0;
        }
    }
    return at;
}

int tiku_doc_place_y(const tiku_doc_t *d, tiku_doc_place_t at)
{
    if (at.block < 0 || at.block >= d->nblock) return 0;
    const tiku_doc_block_t *b = &d->block[at.block];
    int line = b->line0 + at.into;

    if (at.into > 0 && line < b->line0 + b->nline && line < d->nline) {
        return (int)d->line[line].y;
    }
    return (int)b->y;
}

int tiku_doc_check(const tiku_doc_t *d)
{
    int last_y = 0;

    for (int i = 0; i < d->npiece; i++) {
        const tiku_doc_piece_t *pc = &d->piece[i];

        if (pc->run < 0 || pc->run >= d->nrun) return 0;
        if (pc->off < d->run[pc->run].off) return 0;
        if (pc->off + pc->len > d->run[pc->run].off + d->run[pc->run].len) return 0;
        if (pc->len < 0 || pc->w < 0) return 0;
    }
    for (int i = 0; i < d->nline; i++) {
        const tiku_doc_line_t *ln = &d->line[i];

        if (ln->piece0 < 0 || ln->npiece < 0) return 0;
        if (ln->piece0 + ln->npiece > d->npiece) return 0;
        if (ln->block < 0 || ln->block >= d->nblock) return 0;
        if (ln->y < last_y) return 0;
        last_y = (int)ln->y;
        if (ln->h <= 0) return 0;
    }
    for (int i = 0; i < d->nblock; i++) {
        const tiku_doc_block_t *b = &d->block[i];

        if (b->run0 < 0 || b->run0 + b->nrun > d->nrun) return 0;
        if (b->nline && b->line0 + b->nline > d->nline) return 0;
    }
    for (int i = 0; i < d->nlink; i++) {
        if (d->link[i].piece0 == TIKU_DOC_NONE) continue;
        if (d->link[i].piece0 + d->link[i].npiece > d->npiece) return 0;
    }
    return 1;
}
