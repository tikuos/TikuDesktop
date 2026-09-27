/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_fax.c - CCITT fax decoding against libtiff's encoder: Group 4,
 * Group 3 one- and two-dimensional, with and without byte-aligned
 * EOLs; runs past 2560; rows asked for and missing; bits cut short and
 * bits that are no code at all.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_fax.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

#include "../fixtures/fax.inc"

/** @brief Pixels of @p out, rows of @p w bits, that are not the pattern
 *         (black a 0 bit, or a 1 when @p black_is_1), over @p h rows. */
static long
wrong(const unsigned char *out, int wide, int w, int h, int black_is_1)
{
    long bad = 0, rb = (w + 7) / 8;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int bit = out[y * rb + x / 8] >> (7 - x % 8) & 1;

            bad += (bit == black_is_1) != fax_pixel(wide, x, y);
        }
    }
    return bad;
}

/** @brief Decode @p n bytes of a fixture as @p p says; @return how many
 *         pixels are wrong, or -1 when the size is not the rows'. */
static long
decoded(const unsigned char *src, long n, const tiku_fax_t *p, int wide,
        int h)
{
    unsigned char *out = NULL;
    long made = tiku_fax_decode(src, n, p, &out, 1L << 24), bad;

    bad = made == (long)(p->columns + 7) / 8 * h
          ? wrong(out, wide, p->columns, h, p->black_is_1) : -1;
    free(out);
    return bad;
}

int
main(void)
{
    static const struct {
        const unsigned char *bytes;
        long n;
        int k, align, wide;
        const char *what;
    } sets[] = {
        {fax_small_g4, sizeof fax_small_g4, -1, 0, 0, "Group 4"},
        {fax_small_g3_1d, sizeof fax_small_g3_1d, 0, 0, 0, "Group 3, 1-D"},
        {fax_small_g3_2d, sizeof fax_small_g3_2d, 4, 0, 0, "Group 3, 2-D"},
        {fax_small_g3_2d_fill, sizeof fax_small_g3_2d_fill, 4, 1, 0,
         "Group 3, 2-D, EOLs on bytes"},
        {fax_wide_g4, sizeof fax_wide_g4, -1, 0, 1, "Group 4, runs past 2560"},
        {fax_wide_g3_1d, sizeof fax_wide_g3_1d, 0, 0, 1,
         "Group 3 1-D, runs past 2560"},
    };
    unsigned char junk[512], *out = NULL;
    char what[96];
    tiku_fax_t p;
    size_t i;
    long made;

    printf("CCITT fax, as libtiff wrote it\n");
    for (i = 0; i < sizeof sets / sizeof sets[0]; i++) {
        int h = sets[i].wide ? FAX_WIDE_H : FAX_SMALL_H;

        p.k = sets[i].k;
        p.columns = sets[i].wide ? FAX_WIDE_W : FAX_SMALL_W;
        p.rows = h;
        p.byte_align = sets[i].align;
        p.black_is_1 = 0;
        snprintf(what, sizeof what, "%s reads back as drawn", sets[i].what);
        check(decoded(sets[i].bytes, sets[i].n, &p, sets[i].wide, h) == 0,
              "FAX-001", what);
    }
    p.k = -1;
    p.columns = FAX_SMALL_W;
    p.rows = FAX_SMALL_H;
    p.byte_align = 0;
    p.black_is_1 = 1;
    check(decoded(fax_small_g4, sizeof fax_small_g4, &p, 0, FAX_SMALL_H) == 0,
          "FAX-002", "BlackIs1 makes black the 1 bit");
    p.black_is_1 = 0;
    p.rows = 0;
    check(decoded(fax_small_g4, sizeof fax_small_g4, &p, 0, FAX_SMALL_H) == 0,
          "FAX-003", "without Rows, the rows the data holds and no more");
    p.k = 0;
    check(decoded(fax_small_g3_1d, sizeof fax_small_g3_1d, &p, 0,
                  FAX_SMALL_H) == 0,
          "FAX-003", "and so for Group 3, to its end of page");
    p.k = -1;
    p.rows = FAX_SMALL_H + 6;
    made = tiku_fax_decode(fax_small_g4, sizeof fax_small_g4, &p, &out,
                           1L << 24);
    check(made == (FAX_SMALL_W + 7) / 8 * (FAX_SMALL_H + 6) &&
          wrong(out, 0, FAX_SMALL_W, FAX_SMALL_H, 0) == 0 &&
          out[made - 1] == 0xff && out[FAX_SMALL_H * 19] == 0xff,
          "FAX-004", "rows asked for past the data's come out white");
    free(out);
    out = NULL;
    p.rows = FAX_SMALL_H;
    made = tiku_fax_decode(fax_small_g4, 40, &p, &out, 1L << 24);
    check(made == 19 * FAX_SMALL_H && wrong(out, 0, FAX_SMALL_W, 4, 0) == 0,
          "FAX-005", "bits cut short: the rows before the cut, then white");
    free(out);
    out = NULL;
    for (i = 0; i < sizeof junk; i++) {
        junk[i] = (unsigned char)(i * 7919 >> 3);
    }
    made = tiku_fax_decode(junk, sizeof junk, &p, &out, 1L << 24);
    check(made == -1 || made == 19 * FAX_SMALL_H, "FAX-006",
          "bits that are no code read as nothing, or as white rows");
    free(out);
    out = NULL;
    p.columns = 0;
    check(tiku_fax_decode(fax_small_g4, sizeof fax_small_g4, &p, &out,
                          1L << 24) == -1 && out == NULL,
          "FAX-006", "a row of no columns is refused");
    p.columns = FAX_SMALL_W;
    check(tiku_fax_decode(fax_small_g4, sizeof fax_small_g4, &p, &out,
                          100) == -1 && out == NULL,
          "FAX-006", "and so are rows past the room given");
    printf("%s\n", fails ? "FAIL" : "PASS: Group 3 and Group 4 fax");
    return fails ? 1 : 0;
}
