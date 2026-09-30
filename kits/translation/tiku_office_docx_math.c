/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_docx_math.c - equations written out along a line.
 *
 * Office Math (ECMA-376 part 1, 22.1) is a tree: fractions, scripts,
 * roots, delimiters, n-ary operators.  Drawing it as Word does needs its
 * own typesetter; reading it needs only the tree written as a person
 * would type it: scripts raised and lowered, a fraction over a slash with
 * its parts bracketed when they need it, a root before its radicand.
 * Letters are slanted, as maths sets variables; the rest stands upright.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_docx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THIN "\xe2\x80\x89"         /* U+2009 THIN SPACE                 */

/** @brief An equation being written: where to, at what script level. */
typedef struct {
    dx_t                *x;
    const dx_math_out_t *out;
    signed char          vert;      /* 1 raised, -1 lowered, 0 on the line */
    int                  depth;     /* elements within elements          */
} mw_t;

static int node(mw_t *m, const tiku_xml_node_t *e);

/** @brief Whether UTF-8 at @p s starts a letter: Latin or Greek. */
static int
letter(const unsigned char *s)
{
    return (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
           ((*s == 0xce || *s == 0xcf) && (s[1] & 0xc0) == 0x80);
}

/** @brief @p n bytes of text in Cambria Math at the script level, letters
 *         slanted when @p slant, bold when @p bold. */
static int
emit(mw_t *m, const char *s, size_t n, int slant, int bold)
{
    size_t i = 0;

    while (i < n) {
        int lt = letter((const unsigned char *)s + i);
        size_t j = i;
        dx_rpr_t own;
        const of_style_t *st;

        while (j < n && letter((const unsigned char *)s + j) == lt) {
            j += (unsigned char)s[j] >= 0xc0 ? 2 : 1;
            while (j < n && ((unsigned char)s[j] & 0xc0) == 0x80) {
                j++;
            }
        }
        memset(&own, 0, sizeof own);
        snprintf(own.font, sizeof own.font, "Cambria Math");
        own.italic = (unsigned char)(slant && lt);
        own.bold = (unsigned char)bold;
        own.vert = m->vert;
        own.set = RP_FONT | RP_ITALIC | RP_BOLD | RP_VERT;
        st = dx_look(m->x, m->out->pstyle, NULL, &own);
        if (st == NULL || m->out->text(m->out->ctx, s + i, j - i, st) != 0)
            return -1;
        i = j;
    }
    return 0;
}

/** @brief Characters the writing itself adds: brackets, slashes. */
static int
lit(mw_t *m, const char *s)
{
    return emit(m, s, strlen(s), 0, 0);
}

/** @brief An m:r: its text, slanted or not as its m:sty says. */
static int
math_run(mw_t *m, const tiku_xml_node_t *r)
{
    const char *sty = tiku_xml_attr(tiku_xml_path(r, "m:rPr/m:sty"),
                                    "m:val");
    char text[1024];
    size_t n = tiku_xml_text(tiku_xml_child(r, "m:t"), text, sizeof text);
    int plain = sty != NULL && (sty[0] == 'p' || strcmp(sty, "b") == 0);

    if (tiku_xml_child(r, "m:t") == NULL) {
        n = tiku_xml_text(tiku_xml_child(r, "w:t"), text, sizeof text);
        plain = 1;                  /* ordinary text within the maths */
    }
    return emit(m, text, n < sizeof text ? n : strlen(text), !plain,
                sty != NULL && sty[0] == 'b');
}

/** @brief Every element under @p e, in order; properties passed over. */
static int
kids(mw_t *m, const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *c;

    for (c = tiku_xml_child(e, NULL); c != NULL; c = tiku_xml_next(c, NULL)) {
        size_t k = strlen(c->name);

        if (k > 2 && strcmp(c->name + k - 2, "Pr") == 0) {
            continue;               /* m:rPr, m:fPr, m:ctrlPr ... */
        }
        if (node(m, c) != 0) {
            return -1;
        }
    }
    return 0;
}

/** @brief Whether argument @p e needs brackets written around it: more
 *         than one piece, or an operator in its text. */
static int
complex_arg(const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *c;
    char text[64];
    int n = 0;

    for (c = tiku_xml_child(e, NULL); c != NULL; c = tiku_xml_next(c, NULL)) {
        size_t k = strlen(c->name);

        n += !(k > 2 && strcmp(c->name + k - 2, "Pr") == 0);
    }
    (void)tiku_xml_text(e, text, sizeof text);
    return n > 1 || strpbrk(text, "+-=<> ") != NULL ||
           strstr(text, "\xc2\xb1") != NULL || strstr(text, "\xe2\x88\x92");
}

/** @brief Argument @p name of @p e, bracketed when @p wrap and it needs
 *         it. */
static int
arg(mw_t *m, const tiku_xml_node_t *e, const char *name, int wrap)
{
    const tiku_xml_node_t *a = tiku_xml_child(e, name);

    if (a == NULL) {
        return 0;
    }
    wrap = wrap && complex_arg(a);
    return (wrap && lit(m, "(") != 0) || kids(m, a) != 0 ||
           (wrap && lit(m, ")") != 0) ? -1 : 0;
}

/** @brief Argument @p name of @p e raised (@p vert 1) or lowered (-1). */
static int
script(mw_t *m, const tiku_xml_node_t *e, const char *name, int vert)
{
    signed char was = m->vert;
    char text[8];
    int err;

    if (tiku_xml_text(tiku_xml_child(e, name), text, sizeof text) == 0) {
        return 0;                   /* an empty script: nothing to raise */
    }
    m->vert = (signed char)vert;
    err = arg(m, e, name, 0);
    m->vert = was;
    return err;
}

/** @brief A delimiter's character from @p pr's child @p name, or @p dflt;
 *         "" when the file says there is none. */
static const char *
delim(const tiku_xml_node_t *pr, const char *name, const char *dflt)
{
    const tiku_xml_node_t *c = tiku_xml_child(pr, name);
    const char *v = tiku_xml_attr(c, "m:val");

    return c == NULL ? dflt : v != NULL ? v : "";
}

/** @brief An m:d: its arguments between their brackets, separated. */
static int
delimited(mw_t *m, const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *pr = tiku_xml_child(e, "m:dPr"), *c;
    const char *sep = delim(pr, "m:sepChr", "|");
    int first = 1;

    if (lit(m, delim(pr, "m:begChr", "(")) != 0) {
        return -1;
    }
    for (c = tiku_xml_child(e, "m:e"); c; c = tiku_xml_next(c, "m:e")) {
        if ((!first && lit(m, sep) != 0) || kids(m, c) != 0) {
            return -1;
        }
        first = 0;
    }
    return lit(m, delim(pr, "m:endChr", ")"));
}

/** @brief An m:nary: its operator, its limits as scripts, its operand. */
static int
nary(mw_t *m, const tiku_xml_node_t *e)
{
    const char *op = delim(tiku_xml_child(e, "m:naryPr"), "m:chr",
                           "\xe2\x88\xab");

    return lit(m, op) != 0 || script(m, e, "m:sub", -1) != 0 ||
           script(m, e, "m:sup", 1) != 0 || arg(m, e, "m:e", 0) != 0
           ? -1 : 0;
}

/** @brief Each child named @p row of @p e written, @p sep between. */
static int
rows(mw_t *m, const tiku_xml_node_t *e, const char *row, int breaks,
     const char *sep)
{
    const tiku_xml_node_t *c;

    for (c = tiku_xml_child(e, row); c != NULL; c = tiku_xml_next(c, row)) {
        if (c != tiku_xml_child(e, row) &&
            (breaks ? m->out->brk(m->out->ctx) : lit(m, sep)) != 0) {
            return -1;
        }
        if (kids(m, c) != 0) {
            return -1;
        }
    }
    return 0;
}

/** @brief The structures that are an argument and something written
 *         around it, as a line writes them. */
static int
shaped(mw_t *m, const tiku_xml_node_t *e, const char *n)
{
    if (strcmp(n, "m:f") == 0) {
        return arg(m, e, "m:num", 1) != 0 || lit(m, "/") != 0 ||
               arg(m, e, "m:den", 1) != 0 ? -1 : 0;
    }
    if (strcmp(n, "m:rad") == 0) {
        return script(m, e, "m:deg", 1) != 0 ||
               lit(m, "\xe2\x88\x9a") != 0 || arg(m, e, "m:e", 1) != 0
               ? -1 : 0;
    }
    if (strcmp(n, "m:sPre") == 0) {
        return script(m, e, "m:sub", -1) != 0 || script(m, e, "m:sup", 1)
               != 0 || arg(m, e, "m:e", 0) != 0 ? -1 : 0;
    }
    if (strcmp(n, "m:func") == 0) {
        return arg(m, e, "m:fName", 0) != 0 || lit(m, THIN) != 0 ||
               arg(m, e, "m:e", 0) != 0 ? -1 : 0;
    }
    return arg(m, e, "m:e", 0) != 0 ||
           script(m, e, "m:sub", -1) != 0 || script(m, e, "m:sup", 1) != 0
           || script(m, e, "m:lim", strcmp(n, "m:limUpp") == 0 ? 1 : -1)
           != 0 ? -1 : 0;
}

/** @brief Whether @p n is a structure written with something around its
 *         arguments, rather than its children one after another. */
static int
structure(const char *n)
{
    static const char *const names[] = {"m:f", "m:rad", "m:sPre", "m:func",
        "m:sSup", "m:sSub", "m:sSubSup", "m:limLow", "m:limUpp", "m:acc",
        "m:bar", "m:groupChr", "m:box", "m:borderBox", "m:phant"};
    size_t i;

    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(n, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief One element of an equation, whatever it is. */
static int
node(mw_t *m, const tiku_xml_node_t *e)
{
    const char *n = e->name;
    int err;

    if (n == NULL || m->depth > 64) {
        return 0;
    }
    m->depth++;
    if (strcmp(n, "m:r") == 0 || strcmp(n, "w:r") == 0) {
        err = math_run(m, e);
    } else if (strcmp(n, "m:d") == 0) {
        err = delimited(m, e);
    } else if (strcmp(n, "m:nary") == 0) {
        err = nary(m, e);
    } else if (strcmp(n, "m:eqArr") == 0) {
        err = rows(m, e, "m:e", 1, NULL);
    } else if (strcmp(n, "m:m") == 0) {
        err = lit(m, "[") != 0 || rows(m, e, "m:mr", 0, "; ") != 0 ||
              lit(m, "]") != 0 ? -1 : 0;
    } else if (strcmp(n, "m:mr") == 0) {
        err = rows(m, e, "m:e", 0, " ");
    } else if (strcmp(n, "m:oMathPara") == 0) {
        err = rows(m, e, "m:oMath", 1, NULL);
    } else if (structure(n)) {
        err = shaped(m, e, n);
    } else {
        err = kids(m, e);           /* m:oMath, m:e, m:num, m:sup ... */
    }
    m->depth--;
    return err;
}

int
dx_math(dx_t *x, const tiku_xml_node_t *e, const dx_math_out_t *out)
{
    mw_t m;

    memset(&m, 0, sizeof m);
    m.x = x;
    m.out = out;
    return node(&m, e);
}
