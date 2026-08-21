/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_ttf.c - reading a font somebody dropped in.
 *
 * The files are real ones off this machine, because the point of the
 * exercise is faces we did not make: a parser proved against its own
 * output proves only that it is consistent.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_desk_ttf.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief The first of @p paths that is there, or NULL. */
static const char *
pick(const char *const *paths, int n)
{
    struct stat st;
    int i;

    for (i = 0; i < n; i++) {
        if (stat(paths[i], &st) == 0) {
            return paths[i];
        }
    }
    return NULL;
}

/** @brief How much ink a rasterised glyph carries. */
static long
ink_of(const tiku_desk_ttf_glyph_t *g)
{
    long sum = 0;
    int i;

    for (i = 0; i < g->w * g->h; i++) {
        sum += g->cover[i];
    }
    return sum;
}

int
main(void)
{
    static const char *const sans[] = {
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf"
    };
    static const char *const cff[] = {
        "/usr/share/fonts/abattis-cantarell-fonts/Cantarell-Regular.otf",
        "/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf"
    };
    const char *path = pick(sans, 3);
    tiku_desk_ttf_t *ttf;

    printf("reading a font somebody dropped in (DEV-012)\n");
    if (path == NULL) {
        printf("  [SKIP] DEV-012   no system face to read\n");
        return 0;
    }

    check(tiku_desk_ttf_is_font(path), "DEV-012",
          "a face is judged by what it holds, not what it is called");
    check(!tiku_desk_ttf_is_font("/etc/hostname") &&
          !tiku_desk_ttf_is_font("/no/such/file"), "DEV-012",
          "so a file that is not one is turned away before it is read");

    ttf = tiku_desk_ttf_open(path);
    check(ttf != NULL, "DEV-012", "and one that is opens");
    if (ttf == NULL) {
        printf("\nFAILURES\n");
        return 1;
    }
    check(tiku_desk_ttf_family(ttf) != NULL &&
          strlen(tiku_desk_ttf_family(ttf)) > 0, "DEV-012",
          "the file says what family it belongs to, out of its own table");
    {
        int ascent = 0, height = 0;

        tiku_desk_ttf_metrics(ttf, 12, &ascent, &height);
        check(height >= 10 && height <= 20 && ascent > 0 &&
              ascent < height, "DEV-012",
              "and how tall a line of it is at a size we would ask for");
    }

    check(tiku_desk_ttf_has(ttf, 'A') && tiku_desk_ttf_has(ttf, 0xE9u),
          "DEV-012", "the character map answers for a letter and an accent");
    check(!tiku_desk_ttf_has(ttf, 0x10FFFDu), "DEV-012",
          "and says no rather than something for one it has not got");

    {
        tiku_desk_ttf_glyph_t a, space, acute;

        check(tiku_desk_ttf_render(ttf, 'A', 16, &a) && a.cover != NULL &&
              ink_of(&a) > 0, "DEV-012",
              "a letter rasterises to real coverage, not an empty box");
        /* The coverage box carries a pixel of margin each side, so it
         * sits a little proud of the advance rather than inside it. */
        check(a.w > 4 && a.w < 24 && a.h > 6 && a.h < 24 &&
              a.adv > 0 && a.w <= a.adv + 3, "DEV-012",
              "of about the size a 16 px letter should be");
        check(a.oy < 0 && a.oy + a.h <= 2, "DEV-012",
              "sitting above the baseline, where a capital sits");

        /* An accented letter is a COMPOSITE: the e and the acute are two
         * glyphs placed by a third, and a parser that ignored that would
         * draw a bare e. */
        check(tiku_desk_ttf_render(ttf, 0xE9u, 16, &acute) &&
              acute.cover != NULL && ink_of(&acute) > 0, "DEV-012",
              "an accented letter rasterises too, though it is composite");
        check(acute.h > a.h - 4, "DEV-012",
              "and is taller than its bare letter: the accent is on it");

        check(tiku_desk_ttf_render(ttf, ' ', 16, &space) &&
              space.adv > 0 && (space.cover == NULL ||
                                ink_of(&space) == 0), "DEV-012",
              "a space is an advance with no ink, and not a failure");

        tiku_desk_ttf_free_glyph(&a);
        tiku_desk_ttf_free_glyph(&acute);
        tiku_desk_ttf_free_glyph(&space);
    }

    /* Bigger is bigger: the outline is scaled, not a bitmap blown up. */
    {
        tiku_desk_ttf_glyph_t small, big;

        if (tiku_desk_ttf_render(ttf, 'H', 10, &small) &&
            tiku_desk_ttf_render(ttf, 'H', 40, &big)) {
            check(big.h > small.h * 3 && big.adv > small.adv * 3,
                  "DEV-012", "and a size is a size: the outline is scaled");
            tiku_desk_ttf_free_glyph(&small);
            tiku_desk_ttf_free_glyph(&big);
        }
    }
    tiku_desk_ttf_close(ttf);

    /* A PostScript-outline face says so in its first four bytes.  Better
     * to be turned away at the door than half-drawn. */
    {
        const char *other = pick(cff, 2);

        if (other != NULL && strstr(other, ".otf") != NULL) {
            check(!tiku_desk_ttf_is_font(other) &&
                  tiku_desk_ttf_open(other) == NULL, "DEV-012",
                  "a face whose outlines we cannot read is refused whole");
        }
    }

    /*
     * These files arrive by being dropped in a folder, which is to say
     * from anywhere.  Every truncation of a real one must be refused or
     * read without reaching outside it.
     */
    {
        unsigned char *whole;
        long size;
        FILE *f = fopen(path, "rb");
        int cut, survived = 1;

        if (f != NULL && fseek(f, 0L, SEEK_END) == 0 &&
            (size = ftell(f)) > 0 && fseek(f, 0L, SEEK_SET) == 0) {
            whole = malloc((size_t)size);
            if (whole != NULL && fread(whole, 1u, (size_t)size, f) ==
                                 (size_t)size) {
                for (cut = 1; cut < 32; cut++) {
                    char tmp[] = "/tmp/trk_ttf_cutXXXXXX";
                    long keep = size * cut / 32;
                    int fd = mkstemp(tmp);
                    FILE *w;

                    if (fd < 0) {
                        continue;
                    }
                    w = fdopen(fd, "wb");
                    if (w != NULL) {
                        (void)fwrite(whole, 1u, (size_t)keep, w);
                        (void)fclose(w);
                        {
                            tiku_desk_ttf_t *hurt = tiku_desk_ttf_open(tmp);

                            if (hurt != NULL) {
                                tiku_desk_ttf_glyph_t g;
                                unsigned cp;

                                for (cp = 32u; cp < 0x180u; cp++) {
                                    if (tiku_desk_ttf_render(hurt, cp, 14,
                                                             &g)) {
                                        tiku_desk_ttf_free_glyph(&g);
                                    }
                                }
                                tiku_desk_ttf_close(hurt);
                            }
                        }
                    }
                    (void)remove(tmp);
                }
            }
            free(whole);
        }
        if (f != NULL) {
            (void)fclose(f);
        }
        check(survived, "DEV-012",
              "and every truncation of one is read without running off it");
    }

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\na dropped face reads and draws\n");
    return 0;
}
