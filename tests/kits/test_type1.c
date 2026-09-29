/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_type1.c - a Type 1 font made here, byte by byte, and read back.
 *
 * The font is written the way a foundry would: clear text head, then
 * the private part run through eexec, each charstring through its own
 * cipher first.  Reading it back proves both ciphers, the Subrs and
 * CharStrings parse, the encoding, and the charstring machine on glyphs
 * whose outlines are known: a square; two contours kept in subroutines
 * with hints replaced between them, as LaTeX's fonts draw; a flex; and
 * an accent placed by seac.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_type1.h"

/* A charstring program is numbers and these, tagged far past any number
 * a program here holds; the 12-prefixed ones escaped. */
#define OP(o)  (0x40000000 + (o))
#define ESC(o) (0x40000100 + (o))
enum {
    HSTEM = OP(1), HLINETO = OP(6), VLINETO = OP(7), CLOSEPATH = OP(9),
    CALLSUBR = OP(10), RETURN = OP(11), HSBW = OP(13), ENDCHAR = OP(14),
    RMOVETO = OP(21), SEAC = ESC(6), DIV = ESC(12), CALLOTHERSUBR = ESC(16),
    POP = ESC(17), SETCURRENTPOINT = ESC(33)
};

typedef struct {
    const char *name;               /* the glyph's; unused for a Subr */
    const int  *prog;
    size_t      n;
} t1_src_t;

#define SRC(name, prog) { name, prog, sizeof prog / sizeof prog[0] }

/* Subrs 0-4 as every font has them: flex's end, start and points, then
 * hint replacement, whose othersubr answers 3 -- a Subr that returns. */
static const int subr0[] = { 3, 0, CALLOTHERSUBR, POP, POP, SETCURRENTPOINT,
                             RETURN };
static const int subr1[] = { 0, 1, CALLOTHERSUBR, RETURN };
static const int subr2[] = { 0, 2, CALLOTHERSUBR, RETURN };
static const int subr3[] = { RETURN };
static const int subr4[] = { 1, 3, CALLOTHERSUBR, POP, CALLSUBR, RETURN };
/* Two contours and a set of hints, kept apart from the glyphs. */
static const int subr5[] = { 400, HLINETO, 400, VLINETO, -400, HLINETO,
                             CLOSEPATH, RETURN };
static const int subr6[] = { 100, HLINETO, 100, VLINETO, -100, HLINETO,
                             CLOSEPATH, RETURN };
static const int subr7[] = { 100, 400, HSTEM, RETURN };

/* The square: 400 units on a side, 100 in from the origin. */
static const int square[] = { 0, 600, HSBW, 100, 100, RMOVETO, 400, HLINETO,
                              400, VLINETO, -400, HLINETO, CLOSEPATH,
                              ENDCHAR };
static const int notdef[] = { 0, 600, HSBW, ENDCHAR };
/* Libertine's "i": hints replaced, a contour, again, and another. */
static const int pair[] = { 0, 600, HSBW, 7, 4, CALLSUBR, 100, 100, RMOVETO,
                            5, CALLSUBR, 7, 4, CALLSUBR, 0, 100, RMOVETO,
                            6, CALLSUBR, ENDCHAR };
/* The square's foot bent 40 units down by a flex: seven points. */
static const int flex[] = { 0, 600, HSBW, 100, 100, RMOVETO, 1, CALLSUBR,
                            200, 0, RMOVETO, 2, CALLSUBR,
                            -150, -40, RMOVETO, 2, CALLSUBR,
                            100, 0, RMOVETO, 2, CALLSUBR,
                            50, 0, RMOVETO, 2, CALLSUBR,
                            50, 0, RMOVETO, 2, CALLSUBR,
                            100, 0, RMOVETO, 2, CALLSUBR,
                            50, 40, RMOVETO, 2, CALLSUBR,
                            50, 500, 100, 0, CALLSUBR,
                            400, VLINETO, -400, HLINETO, CLOSEPATH, ENDCHAR };
/* An accent 100 wide behind a sidebearing of 20 and a width of 200. */
static const int acute[] = { 20, 200, HSBW, 0, 600, RMOVETO, 100, HLINETO,
                             100, VLINETO, -100, HLINETO, CLOSEPATH,
                             ENDCHAR };
/* "a" with its acute set 480 along: asb adx ady bchar achar seac. */
static const int aacute[] = { 0, 600, HSBW, 20, 480, 0, 97, 194, SEAC };
/* A Subr numbered 3e9 by two divs: past int, where a cast is undefined. */
static const int huge[] = { 0, 600, HSBW, 30000, 1, 100000, DIV, DIV,
                            CALLSUBR, 100, 100, RMOVETO, 5, CALLSUBR,
                            ENDCHAR };

static const t1_src_t subrs[] = {
    SRC(NULL, subr0), SRC(NULL, subr1), SRC(NULL, subr2), SRC(NULL, subr3),
    SRC(NULL, subr4), SRC(NULL, subr5), SRC(NULL, subr6), SRC(NULL, subr7)
};
static const t1_src_t glyphs[] = {
    SRC("square", square), SRC(".notdef", notdef), SRC("pair", pair),
    SRC("flex", flex), SRC("a", square), SRC("acute", acute),
    SRC("aacute", aacute), SRC("huge", huge)
};

static int fails;

/** @brief One row of the report, PASS or FAIL; a FAIL is counted. */
static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief The Type 1 cipher, forwards: @p n plain bytes to cipher text. */
static void
encrypt(unsigned char *p, size_t n, unsigned short r)
{
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)(p[i] ^ (r >> 8));

        p[i] = c;
        r = (unsigned short)((c + r) * 52845u + 22719u);
    }
}

/** @brief @p n numbers and operators as charstring bytes; @return how
 *         many.  Numbers are the one- and two-byte forms to +-1131, and
 *         the five-byte one past that, below the operators. */
static size_t
assemble(unsigned char *out, const int *prog, size_t n)
{
    size_t i, k = 0;

    for (i = 0; i < n; i++) {
        int v = prog[i];

        if (v >= ESC(0)) {
            out[k++] = 12;
            out[k++] = (unsigned char)(v - ESC(0));
        } else if (v >= OP(0)) {
            out[k++] = (unsigned char)(v - OP(0));
        } else if (v > 1131 || v < -1131) {
            out[k++] = 255;
            out[k++] = (unsigned char)((unsigned)v >> 24);
            out[k++] = (unsigned char)((unsigned)v >> 16);
            out[k++] = (unsigned char)((unsigned)v >> 8);
            out[k++] = (unsigned char)v;
        } else if (v >= -107 && v <= 107) {
            out[k++] = (unsigned char)(v + 139);
        } else if (v > 0) {
            out[k++] = (unsigned char)(247 + (v - 108) / 256);
            out[k++] = (unsigned char)((v - 108) % 256);
        } else {
            out[k++] = (unsigned char)(251 + (-v - 108) / 256);
            out[k++] = (unsigned char)((-v - 108) % 256);
        }
    }
    return k;
}

/** @brief A program as a charstring: four lead bytes, ciphered;
 *         @return its length. */
static size_t
charstring(unsigned char *out, const t1_src_t *src)
{
    size_t k;

    memset(out, 0, 4);
    k = assemble(out + 4, src->prog, src->n);
    encrypt(out, k + 4, 4330);
    return k + 4;
}

/** @brief @p src as "<head> <bytes> <tail>" onto @p body at @p n;
 *         @return the new length. */
static size_t
put(unsigned char *body, size_t n, const char *head, const t1_src_t *src,
    const char *tail)
{
    unsigned char cs[256];
    size_t k = charstring(cs, src);

    n += (size_t)sprintf((char *)body + n, "%s %zu RD ", head, k);
    memcpy(body + n, cs, k);
    n += k;
    return n + (size_t)sprintf((char *)body + n, "%s", tail);
}

/** @brief The font as its bytes, PFA style with a binary eexec part. */
static size_t
make_font(unsigned char *out, size_t max)
{
    static const char head[] =
        "%!PS-AdobeFont-1.0: Test\n"
        "/FontName /Test def\n"
        "/FontMatrix [0.001 0 0 0.001 0 0] readonly def\n"
        "/Encoding 256 array\n"
        "0 1 255 {1 index exch /.notdef put} for\n"
        "dup 97 /square put\n"
        "readonly def\n"
        "currentdict end\n"
        "currentfile eexec\n";
    static unsigned char body[8192];
    char name[80];
    size_t n = 4, i;                        /* eexec's four lead bytes */

    memset(body, 0, 4);
    n += (size_t)sprintf((char *)body + n, "dup /Private 8 dict dup begin\n"
                         "/lenIV 4 def\n/Subrs %zu array\n",
                         sizeof subrs / sizeof subrs[0]);
    for (i = 0; i < sizeof subrs / sizeof subrs[0]; i++) {
        snprintf(name, sizeof name, "dup %zu", i);
        n = put(body, n, name, &subrs[i], " NP\n");
    }
    n += (size_t)sprintf((char *)body + n, "ND\nend\n/CharStrings %zu dict "
                         "dup begin\n", sizeof glyphs / sizeof glyphs[0]);
    for (i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++) {
        snprintf(name, sizeof name, "/%s", glyphs[i].name);
        n = put(body, n, name, &glyphs[i], " ND\n");
    }
    n += (size_t)sprintf((char *)body + n, "end\nmark currentfile "
                                            "closefile\n");
    encrypt(body, n, 55665);
    if (sizeof head - 1 + n > max) {
        return 0;
    }
    memcpy(out, head, sizeof head - 1);
    memcpy(out + sizeof head - 1, body, n);
    return sizeof head - 1 + n;
}

/** @brief Glyph @p name outlined at one pixel a unit into @p path;
 *         @return 1 with its bounds in @p b (x0 y0 w h) and advance. */
static int
outline(const tiku_t1_t *t1, tiku_path_t *path, const char *name, int b[4],
        float *adv)
{
    tiku_hint_t hint;

    memset(&hint, 0, sizeof hint);
    hint.scale = 1.0f;
    memset(b, 0, 4 * sizeof b[0]);
    *adv = 0;
    tiku_path_reset(path);
    if (t1 == NULL || path == NULL ||
        !tiku_t1_outline(t1, name, &hint, path, adv)) {
        return 0;
    }
    tiku_path_bounds(path, &b[0], &b[1], &b[2], &b[3]);
    return 1;
}

/** @brief The glyphs LaTeX's fonts are made of: subroutines, flex, seac.
 *  Bounds keep a pixel of margin each side, and y runs downwards. */
static void
machine_tests(const tiku_t1_t *t1, tiku_path_t *path)
{
    int b[4];
    float adv;

    check(outline(t1, path, "pair", b, &adv) && b[0] == 99 && b[2] == 402 &&
          b[1] == -702 && b[3] == 604, "T1-010",
          "a Subr's return goes back to the glyph: both contours drawn");
    check(outline(t1, path, "flex", b, &adv) && b[0] == 99 && b[2] == 402 &&
          b[1] == -502 && b[3] == 444, "T1-011",
          "a flex of seven points is drawn, 40 units under the foot");
    check(outline(t1, path, "aacute", b, &adv) && b[0] == 99 &&
          b[2] == 482 && b[1] == -702 && b[3] == 604, "T1-012",
          "seac sets the accent where it says, past the base");
    check(adv == 600.0f, "T1-013",
          "and the composite keeps its own advance, not the accent's");
    /* Under check-sanitize an unguarded cast of 3e9 stops the suite. */
    check(outline(t1, path, "huge", b, &adv) && b[2] == 0, "T1-014",
          "a Subr number past int is refused, and the glyph ends there");
}

/** @brief The font made, read back and drawn; @return 0 when every row
 *         passes. */
int
main(void)
{
    static unsigned char font[8192];
    size_t n = make_font(font, sizeof font);
    tiku_t1_t *t1 = tiku_t1_open(font, n);
    tiku_path_t *path = tiku_path_new();
    float adv = 0;
    int b[4], drawn;

    printf("Type 1: a made font read back\n");
    check(n > 0 && t1 != NULL, "T1-001",
          "a font with a binary eexec part opens");
    check(t1 != NULL && tiku_t1_upem(t1) == 1000.0f, "T1-002",
          "its FontMatrix gives 1000 units to the em");
    check(t1 != NULL && tiku_t1_has(t1, "square") && tiku_t1_has(t1, ".notdef")
          && !tiku_t1_has(t1, "circle"), "T1-003",
          "the charstrings were found, and no other");
    check(t1 != NULL && tiku_t1_encoded(t1, 97) != NULL &&
          strcmp(tiku_t1_encoded(t1, 97), "square") == 0 &&
          tiku_t1_encoded(t1, 98) == NULL, "T1-004",
          "the encoding puts the square at 'a' and nothing at 'b'");
    drawn = outline(t1, path, "square", b, &adv);
    check(drawn, "T1-005", "the square's charstring runs");
    check(drawn && adv == 600.0f, "T1-006", "hsbw gave the advance");
    /* The bounds hold a pixel of margin each side of 100..500. */
    check(drawn && b[0] == 99 && b[2] == 402 && b[3] == 404 && b[1] == -502,
          "T1-007", "the outline is the 400-unit square, y downwards");
    check(!outline(t1, path, "circle", b, &adv), "T1-008",
          "a glyph the font lacks is refused");
    check(tiku_t1_open((const unsigned char *)"%!PS not a font", 15) == NULL,
          "T1-009", "text without an eexec part is not a font");
    machine_tests(t1, path);
    tiku_path_free(path);
    tiku_t1_close(t1);
    printf("%s\n", fails ? "FAIL" : "PASS: Type 1 fonts read back");
    return fails ? 1 : 0;
}
