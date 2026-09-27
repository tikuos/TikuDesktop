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
 * CharStrings parse, the encoding, and the charstring machine on one
 * glyph whose outline is known: a square, 400 units on a side.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_type1.h"

static int fails;

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

/** @brief A charstring with four lead bytes, ciphered; @return length. */
static size_t
charstring(unsigned char *out, const unsigned char *ops, size_t n)
{
    memset(out, 0, 4);
    memcpy(out + 4, ops, n);
    encrypt(out, n + 4, 4330);
    return n + 4;
}

/* A square: 0 600 hsbw, 100 100 rmoveto, 400 hlineto, 400 vlineto,
 * -400 hlineto, closepath, endchar.  Numbers 108..1131 are two bytes. */
static const unsigned char square_ops[] = {
    139, 248, 236, 13, 239, 239, 21, 248, 36, 6, 248, 36, 7, 252, 36, 6, 9, 14
};
static const unsigned char notdef_ops[] = { 139, 248, 236, 13, 14 };
static const unsigned char subr_ops[] = { 11 };

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
    unsigned char body[1024], cs[64];
    size_t n = 0, k;

    memset(body, 0, 4);
    n = 4;                                  /* eexec's four lead bytes */
    n += (size_t)sprintf((char *)body + n,
                         "dup /Private 8 dict dup begin\n/lenIV 4 def\n"
                         "/Subrs 1 array\n");
    k = charstring(cs, subr_ops, sizeof subr_ops);
    n += (size_t)sprintf((char *)body + n, "dup 0 %zu RD ", k);
    memcpy(body + n, cs, k);
    n += k;
    n += (size_t)sprintf((char *)body + n, " NP\nND\nend\n"
                                            "/CharStrings 2 dict dup begin\n");
    k = charstring(cs, square_ops, sizeof square_ops);
    n += (size_t)sprintf((char *)body + n, "/square %zu RD ", k);
    memcpy(body + n, cs, k);
    n += k;
    k = charstring(cs, notdef_ops, sizeof notdef_ops);
    n += (size_t)sprintf((char *)body + n, " ND\n/.notdef %zu RD ", k);
    memcpy(body + n, cs, k);
    n += k;
    n += (size_t)sprintf((char *)body + n, " ND\nend\nmark currentfile "
                                            "closefile\n");
    encrypt(body, n, 55665);
    if (sizeof head - 1 + n > max) {
        return 0;
    }
    memcpy(out, head, sizeof head - 1);
    memcpy(out + sizeof head - 1, body, n);
    return sizeof head - 1 + n;
}

int
main(void)
{
    unsigned char font[2048];
    size_t n = make_font(font, sizeof font);
    tiku_t1_t *t1 = tiku_t1_open(font, n);
    tiku_path_t *path = tiku_path_new();
    tiku_hint_t hint;
    float adv = 0;
    int x0 = 0, y0 = 0, w = 0, h = 0, drawn;

    printf("Type 1: a made font read back\n");
    check(n > 0 && t1 != NULL, "T1-001",
          "a font with a binary eexec part opens");
    check(t1 != NULL && tiku_t1_upem(t1) == 1000.0f, "T1-002",
          "its FontMatrix gives 1000 units to the em");
    check(t1 != NULL && tiku_t1_has(t1, "square") && tiku_t1_has(t1, ".notdef")
          && !tiku_t1_has(t1, "circle"), "T1-003",
          "both charstrings were found, and no other");
    check(t1 != NULL && tiku_t1_encoded(t1, 97) != NULL &&
          strcmp(tiku_t1_encoded(t1, 97), "square") == 0 &&
          tiku_t1_encoded(t1, 98) == NULL, "T1-004",
          "the encoding puts the square at 'a' and nothing at 'b'");
    memset(&hint, 0, sizeof hint);
    hint.scale = 1.0f;
    drawn = t1 != NULL && path != NULL &&
            tiku_t1_outline(t1, "square", &hint, path, &adv);
    check(drawn, "T1-005", "the square's charstring runs");
    if (drawn) {
        tiku_path_bounds(path, &x0, &y0, &w, &h);
    }
    check(drawn && adv == 600.0f, "T1-006", "hsbw gave the advance");
    /* The bounds hold a pixel of margin each side of 100..500. */
    check(drawn && x0 == 99 && w == 402 && h == 404 && y0 == -502, "T1-007",
          "the outline is the 400-unit square, y downwards");
    check(t1 == NULL || !tiku_t1_outline(t1, "circle", &hint, path, NULL),
          "T1-008", "a glyph the font lacks is refused");
    check(tiku_t1_open((const unsigned char *)"%!PS not a font", 15) == NULL,
          "T1-009", "text without an eexec part is not a font");
    tiku_path_free(path);
    tiku_t1_close(t1);
    printf("%s\n", fails ? "FAIL" : "PASS: Type 1 fonts read back");
    return fails ? 1 : 0;
}
