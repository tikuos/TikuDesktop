/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_cff.c - a PostScript-outline face built byte by byte.
 *
 * The other tests read the fonts on this machine, the right way to find
 * out whether real ones draw and the wrong way to prove what happens to a
 * bad one: a fuzzer found a crash here in a single byte of an 88 KB file,
 * three bytes in the whole file reaching it, so no bending at random
 * would catch it coming back.  This builds the smallest CFF that gets as
 * far as the bug: a Private DICT whose subroutine index sits two bytes
 * from the end of the table, where its count can be read and its offsets
 * cannot, and a charstring that calls into it.  A reader that lets a
 * half-read index out walks a null pointer here, every time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_ttf.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/* ------------------------------------------------------------ building */

static unsigned char file[4096];
static size_t at;

static void
u8(unsigned v)
{
    if (at < sizeof file) {
        file[at++] = (unsigned char)(v & 0xffu);
    }
}

static void
u16(unsigned v)
{
    u8(v >> 8);
    u8(v);
}

static void
u32(unsigned long v)
{
    u16((unsigned)(v >> 16));
    u16((unsigned)v);
}

/** @brief A DICT operand, in the five-byte form, so sizes are known. */
static void
dict_int(long v)
{
    u8(29u);
    u32((unsigned long)v);
}

#define CFF_AT      256         /* where the CFF table starts in the file */
#define CFF_LEN     52          /* and how long it says it is */

/**
 * @brief Lay out a CFF table whose Subrs index is cut off by two bytes.
 *
 * Offsets count from the table and every length is what the structure
 * above it forces: header 4, an empty Name INDEX 2, a Top DICT INDEX of
 * 5 + 17, two empty INDEXes 4, so the charstrings land at 32 and run 9
 * bytes, the Private DICT at 41, and the subroutine index it names at
 * 49, three bytes from the end of a table 52 long.  Three, not two: at
 * two the index cannot read its offSize and the bug hides.
 */
static void
build_cff(void)
{
    size_t base = at;

    u8(1u); u8(0u); u8(4u); u8(1u);         /* major, minor, hdrSize, offSize */
    u16(0u);                                /* Name INDEX: empty */

    /* Top DICT INDEX: one dictionary, seventeen bytes of it. */
    u16(1u); u8(1u); u8(1u); u8(18u);
    dict_int(32); u8(17u);                  /* CharStrings at 32 */
    dict_int(6); dict_int(41); u8(18u);     /* Private: 6 bytes, at 41 */

    u16(0u);                                /* String INDEX: empty */
    u16(0u);                                /* Global Subrs: empty */

    /* CharStrings INDEX at 32: glyph 0 draws nothing, glyph 1 calls a
     * subroutine that the index cannot hand back. */
    u16(2u); u8(1u); u8(1u); u8(2u); u8(4u);
    u8(0x0Eu);                              /* glyph 0: endchar */
    u8(0x20u); u8(0x0Au);                   /* glyph 1: -107, callsubr */

    /* Private DICT at 41: Subrs eight bytes further on, at 49. */
    dict_int(8); u8(19u);

    u8(0u); u8(0u);                         /* 47, 48: padding */
    u16(5u);                                /* 49: five elements, it says */
    u8(1u);                                 /* 51: one byte per offset --
                                             *     and then the table ends,
                                             *     with no offsets in it */
    while (at - base < CFF_LEN) {
        u8(0u);
    }
}

/**
 * @brief A second CFF, where a subroutine ends the glyph.
 *
 * Type 2's endchar finishes the glyph, so met inside a subroutine it
 * must stop everything after the call; a reader treating it as a return
 * carries on and draws whatever follows.  Glyph 1 calls a subroutine that
 * is nothing but endchar, then tries to draw a triangle, which must not
 * appear.  Header 4, Name 2, Top DICT 5 + 17, two empty INDEXes 4:
 * charstrings at 32 running 19 bytes, Private DICT at 51, Subrs at 57.
 */
static void
build_endchar_cff(void)
{
    size_t base = at;

    u8(1u); u8(0u); u8(4u); u8(1u);
    u16(0u);                                /* Name INDEX: empty */

    u16(1u); u8(1u); u8(1u); u8(18u);       /* Top DICT INDEX */
    dict_int(32); u8(17u);                  /* CharStrings at 32 */
    dict_int(6); dict_int(51); u8(18u);     /* Private: 6 bytes, at 51 */

    u16(0u);                                /* String INDEX: empty */
    u16(0u);                                /* Global Subrs: empty */

    /* CharStrings at 32: 2 + 1 + 3 offsets + 13 of data. */
    u16(2u); u8(1u); u8(1u); u8(2u); u8(14u);
    u8(0x0Eu);                              /* glyph 0: endchar */
    u8(0x20u); u8(0x0Au);                   /* glyph 1: -107, callsubr */
    u8(0xEFu); u8(0x8Bu); u8(0x15u);        /* then 100 0 rmoveto ... */
    u8(0x8Bu); u8(0xEFu); u8(0x05u);        /* ... 0 100 rlineto ... */
    u8(0xEFu); u8(0x8Bu); u8(0x05u);        /* ... 100 0 rlineto ... */
    u8(0x0Eu);                              /* ... endchar */

    /* Private DICT at 51: Subrs six bytes on, at 57. */
    dict_int(6); u8(19u);

    /* Subrs INDEX at 57: one subroutine, and it is an endchar. */
    u16(1u); u8(1u); u8(1u); u8(2u);
    u8(0x0Eu);

    while (at - base < 64u) {
        u8(0u);
    }
}

/** @brief The smallest OpenType file that carries the above. */
/**
 * @brief A third CFF, which says its em is a thousandth of a pixel.
 *
 * The FontMatrix states how big a unit is.  Believed, a thousand units
 * to the pixel puts every coordinate outside the range an int holds,
 * where the conversion has no defined answer and the index lands eight
 * gigabytes below the row.  Header 4, Name 2, Top DICT 5 + 49, two empty
 * INDEXes 4: charstrings at 64 running 17 bytes, Private DICT at 81, its
 * empty Subrs at 87.
 */
static void
build_hugescale_cff(void)
{
    size_t base = at;

    u8(1u); u8(0u); u8(4u); u8(1u);
    u16(0u);                                /* Name INDEX: empty */

    u16(1u); u8(1u); u8(1u); u8(50u);       /* Top DICT INDEX: 49 bytes */
    dict_int(64); u8(17u);                  /* CharStrings at 64 */
    dict_int(6); dict_int(81); u8(18u);     /* Private: 6 bytes, at 81 */
    dict_int(1000); dict_int(0); dict_int(0);
    dict_int(1000); dict_int(0); dict_int(0);
    u8(12u); u8(7u);                        /* FontMatrix: a thousand to one */

    u16(0u);                                /* String INDEX: empty */
    u16(0u);                                /* Global Subrs: empty */

    /* CharStrings at 64: glyph 1 draws a small triangle -- small, until
     * the matrix is believed. */
    u16(2u); u8(1u); u8(1u); u8(2u); u8(12u);
    u8(0x0Eu);                              /* glyph 0: endchar */
    u8(0xEFu); u8(0x8Bu); u8(0x15u);        /* 100 0 rmoveto */
    u8(0x8Bu); u8(0xEFu); u8(0x05u);        /* 0 100 rlineto */
    u8(0xEFu); u8(0x8Bu); u8(0x05u);        /* 100 0 rlineto */
    u8(0x0Eu);                              /* endchar */

    dict_int(6); u8(19u);                   /* Private at 81, Subrs at 87 */
    u16(0u);                                /* an empty Subrs INDEX */

    while (at - base < 96u) {
        u8(0u);
    }
}

static int which_cff;

static void
build_font(void)
{
    size_t i;

    memset(file, 0, sizeof file);
    at = 0;
    /* Offset table: OTTO, six tables. */
    u32(0x4F54544Ful);                      /* 'OTTO' */
    u16(6u); u16(0u); u16(0u); u16(0u);

    /* The directory, in tag order, pointing at fixed places. */
    {
        const char *tag[6] = { "CFF ", "cmap", "head", "hhea", "hmtx",
                               "maxp" };
        unsigned long off[6] = { CFF_AT, 512, 640, 768, 896, 960 };
        unsigned long len[6] = { (which_cff == 0) ? CFF_LEN
                                 : ((which_cff == 1) ? 64u : 96u),
                                 24, 54, 36, 8, 6 };

        for (i = 0; i < 6; i++) {
            u8((unsigned)tag[i][0]); u8((unsigned)tag[i][1]);
            u8((unsigned)tag[i][2]); u8((unsigned)tag[i][3]);
            u32(0ul);
            u32(off[i]);
            u32(len[i]);
        }
    }

    at = CFF_AT;
    if (which_cff == 0) {
        build_cff();
    } else if (which_cff == 1) {
        build_endchar_cff();
    } else {
        build_hugescale_cff();
    }

    /* cmap: one trimmed subtable, mapping 'A' to glyph 1. */
    at = 512;
    u16(0u); u16(1u);
    u16(3u); u16(1u); u32(12ul);
    at = 512 + 12;
    u16(6u); u16(12u); u16(0u);
    u16(65u); u16(1u); u16(1u);

    at = 640;                               /* head */
    u32(0x00010000ul); u32(0ul); u32(0ul); u32(0x5F0F3CF5ul);
    u16(0u); u16(1000u);                    /* +18: unitsPerEm */
    at = 640 + 50;
    u16(0u);                                /* indexToLocFormat */

    at = 768;                               /* hhea */
    u32(0x00010000ul);
    u16(800u); u16(0xFF38u);                /* ascender, descender */
    at = 768 + 34;
    u16(2u);                                /* numberOfHMetrics */

    at = 896;                               /* hmtx: two of them */
    u16(500u); u16(0u); u16(500u); u16(0u);

    at = 960;                               /* maxp */
    u32(0x00010000ul); u16(2u);             /* numGlyphs */

    at = 1024;
}

/** @brief Write the face built above, and hand back the path. */
static const char *
plant(void)
{
    /* Written afresh every time: mkstemp EATS the template, so a static
     * one works once and hands back nothing ever after. */
    static char path[32];
    int fd;
    FILE *f;

    snprintf(path, sizeof path, "/tmp/trk_cff_madeXXXXXX");
    fd = mkstemp(path);

    if (fd < 0) {
        return NULL;
    }
    f = fdopen(fd, "wb");
    if (f == NULL) {
        return NULL;
    }
    (void)fwrite(file, 1u, at, f);
    (void)fclose(f);
    return path;
}

int
main(void)
{
    const char *path;
    tiku_ttf_t *ttf;

    printf("a PostScript-outline face built byte by byte (DEV-013)\n");

    build_font();
    path = plant();
    if (path == NULL) {
        printf("  [FAIL] DEV-013   nowhere to write the face\n");
        return 1;
    }
    check(tiku_ttf_is_font(path), "DEV-013",
          "what we built is taken for a face, as a bad one would be");

    ttf = tiku_ttf_open(path);
    check(ttf != NULL, "DEV-013",
          "and opens: everything it says about itself is well formed");
    if (ttf != NULL) {
        tiku_ttf_glyph_t g;

        /*
         * The one that matters.  Glyph 1's charstring calls subroutine
         * zero of an index whose count says five and whose offsets are
         * off the end of the table.  A reader that keeps the count from
         * a read that FAILED will follow a pointer it never set.
         */
        memset(&g, 0, sizeof g);
        (void)tiku_ttf_render(ttf, 'A', 16, &g);
        tiku_ttf_free_glyph(&g);
        check(1, "DEV-013",
              "a subroutine index cut off two bytes short draws nothing, "
              "rather than following a pointer nobody set");

        (void)tiku_ttf_render(ttf, 'B', 16, &g);
        tiku_ttf_free_glyph(&g);
        check(1, "DEV-013",
              "and a letter the map has not got is simply not drawn");
        tiku_ttf_close(ttf);
    }
    (void)remove(path);

    /* The second one: a subroutine that ends the glyph. */
    which_cff = 1;
    build_font();
    path = plant();
    if (path != NULL) {
        ttf = tiku_ttf_open(path);
        check(ttf != NULL, "DEV-013",
              "a second face, whose subroutine is nothing but an endchar");
        if (ttf != NULL) {
            tiku_ttf_glyph_t g;
            long ink = 0;

            memset(&g, 0, sizeof g);
            if (tiku_ttf_render(ttf, 'A', 40, &g) && g.cover != NULL) {
                int k;

                for (k = 0; k < g.w * g.h; k++) {
                    ink += g.cover[k];
                }
            }
            check(ink == 0, "DEV-013",
                  "ends the GLYPH: the triangle drawn after the call that "
                  "reached it never appears");
            tiku_ttf_free_glyph(&g);
            tiku_ttf_close(ttf);
        }
        (void)remove(path);
    }

    /* The third: a face whose own units are a thousandth of a pixel. */
    which_cff = 2;
    build_font();
    path = plant();
    if (path != NULL) {
        ttf = tiku_ttf_open(path);
        check(ttf != NULL, "DEV-013",
              "a third face, claiming a thousand units to the pixel");
        if (ttf != NULL) {
            tiku_ttf_glyph_t g;

            memset(&g, 0, sizeof g);
            (void)tiku_ttf_render(ttf, 'A', 12, &g);
            check(g.w >= 0 && g.w < 64 && g.h >= 0 && g.h < 64, "DEV-013",
                  "is not believed: a 12 px letter stays the size of one, "
                  "rather than running past what an int can hold");
            tiku_ttf_free_glyph(&g);
            tiku_ttf_close(ttf);
        }
        (void)remove(path);
    }

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe made-up face is read without harm\n");
    return 0;
}
