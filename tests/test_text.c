/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_text.c - drawing a name that is not English.
 *
 * Names are UTF-8.  A file manager that walked bytes drew "cafe" for a
 * file called "cafe" with an acute on the e, and two blanks where the
 * letter was: the width was even right, which is how it went unnoticed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_desk_font.h"
#include "tiku_desk_gfx.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief How many pixels of @p s are not the paper it started as. */
static int
inked(const tiku_desk_surface_t *s)
{
    /* A surface nobody rescaled reports scale 0, and every drawing path
     * reads that as 1; counting has to agree with them. */
    int sc = (s->scale > 1) ? s->scale : 1;
    long n = (long)s->w * s->h * sc * sc;
    int count = 0;
    long i;

    for (i = 0; i < n; i++) {
        if (s->px[i] != TIKU_DESK_RGB(255, 255, 255)) {
            count++;
        }
    }
    return count;
}

/** @brief Ink left by @p text on a fresh surface at @p scale. */
static int
ink_of(const char *text, int scale)
{
    tiku_desk_surface_t *s = tiku_desk_surface_new(60, 30,
                                                   TIKU_DESK_RGB(255, 255,
                                                                 255));
    int count;

    if (s == NULL) {
        return -1;
    }
    if (scale > 1) {
        (void)tiku_desk_surface_rescale(s, scale,
                                        TIKU_DESK_RGB(255, 255, 255));
    }
    s->clip.x = 0;
    s->clip.y = 0;
    s->clip.w = s->w;
    s->clip.h = s->h;
    tiku_desk_text(s, tiku_desk_font_plain(), 2, 20, text,
                   TIKU_DESK_RGB(0, 0, 0));
    count = inked(s);
    tiku_desk_surface_free(s);
    return count;
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

/**
 * @brief The share of a drawing's ink that is neither ink nor paper.
 *
 * A soft horizontal edge spends its coverage in the middle of the scale;
 * a crisp one commits.  This is what grid-fitting is FOR, and it can be
 * measured without any reference renderer to compare against.
 */
static double
ambiguous(const char *text)
{
    tiku_desk_surface_t *s = tiku_desk_surface_new(400, 40,
                                                   TIKU_DESK_RGB(255, 255,
                                                                 255));
    int gray = 0, inked = 0;
    long i, n;

    if (s == NULL) {
        return -1.0;
    }
    tiku_desk_text(s, tiku_desk_font_plain(), 4, 28, text,
                   TIKU_DESK_RGB(0, 0, 0));
    n = (long)s->w * s->h;
    for (i = 0; i < n; i++) {
        unsigned v = 255u - (s->px[i] & 0xffu);

        if (v > 8u) {
            inked++;
            if (v >= 48u && v <= 208u) {
                gray++;
            }
        }
    }
    tiku_desk_surface_free(s);
    return inked ? (double)gray / (double)inked : -1.0;
}

int
main(void)
{
    const tiku_desk_font_t *f;
    int space, plain, accented;

    printf("drawing a name that is not English (DEV-011)\n");

    (void)tiku_desk_font_set_family(0);
    (void)tiku_desk_font_set_size(12);
    f = tiku_desk_font_plain();

    space = tiku_desk_text_width(f, " ");
    plain = tiku_desk_text_width(f, "caf") ;
    accented = tiku_desk_text_width(f, "caf\xc3\xa9");
    check(accented - plain > space, "DEV-011",
          "an accented letter takes a letter's room, not a blank's");
    check(tiku_desk_text_width(f, "caf\xc3\xa9") ==
          tiku_desk_text_width(f, "cafe"), "DEV-011",
          "and the same room as the letter it is an accent on");
    check(tiku_desk_text_width(f, "\xc3\xa9\xc3\xa9") ==
          2 * (accented - plain), "DEV-011",
          "two of them take two letters' room: the bytes are not counted");

    /* What is NOT baked must still be harmless. */
    check(tiku_desk_text_width(f, "a\xe2\x80\xa6") ==
          tiku_desk_text_width(f, "a "), "DEV-011",
          "a letter the face does not carry takes a space's room");
    check(tiku_desk_text_width(f, "\xc3" "A") >
          tiku_desk_text_width(f, "A") &&
          tiku_desk_text_width(f, "\xf0\x9f" "A") >
          tiku_desk_text_width(f, "A"), "DEV-011",
          "a sequence cut short takes what follows it with it, not off it");

    /* The ink itself, which is what went wrong before. */
    check(ink_of("caf\xc3\xa9", 1) > ink_of("caf", 1), "DEV-011",
          "the letter is actually drawn, not merely counted");
    check(ink_of("\xc3\xa9", 1) > 0, "DEV-011",
          "on its own as well as in a word");

    /*
     * On a scaled screen the 2x face carries ASCII only, so an accented
     * letter has to come from the 1x face replicated.  A face-wide
     * choice would have dropped it to that face's space glyph -- ink
     * everywhere except on the letters a person outside England types.
     */
    check(ink_of("caf\xc3\xa9", 2) > ink_of("caf", 2), "DEV-011",
          "and on a doubled screen too, where the 2x face has no accents");
    /*
     * Replicated ink comes in 2x2 blocks, so it is EXACTLY four times the
     * 1x ink; a real 2x face rasterises the letter again and lands on a
     * different number.  That is the difference the eye sees as coarse.
     */
    check(ink_of("\xc3\xa9", 2) == 4 * ink_of("\xc3\xa9", 1), "DEV-011",
          "an accent IS replicated: the 2x faces carry ASCII only");

    /* Every family carries them: choosing Serif is not choosing English. */
    {
        int family;

        for (family = 0; family < tiku_desk_font_family_count(); family++) {
            char what[64];

            (void)tiku_desk_font_set_family(family);
            snprintf(what, sizeof what,
                     "%s draws them too, at every size it offers",
                     tiku_desk_font_family_name(family));
            check(ink_of("\xc3\xa9", 1) > 0 &&
                  tiku_desk_text_width(tiku_desk_font_plain(),
                                       "\xc3\xa9") > space,
                  "DEV-011", what);
            snprintf(what, sizeof what,
                     "%s is drawn at real 2x detail, not blown up",
                     tiku_desk_font_family_name(family));
            check(ink_of("Tracker", 2) != 4 * ink_of("Tracker", 1),
                  "DEV-011", what);
        }
        (void)tiku_desk_font_set_family(0);
    }

    /*
     * A face read from a file, rather than baked.  What it can draw is
     * what the FILE has -- which is the whole point of the folder.
     */
    {
        static const char *const cjk[] = {
            "/usr/share/fonts/google-droid-sans-fonts/DroidSansFallbackFull.ttf",
            "/usr/share/fonts/google-droid-sans-fonts/DroidSansJapanese.ttf"
        };
        static const char *const sans[] = {
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
        };
        const char *wide = pick(cjk, 2);
        const char *plain_file = pick(sans, 2);

        if (wide != NULL && tiku_desk_font_set_files(wide, NULL)) {
            (void)tiku_desk_font_set_size(16);
            check(ink_of("\xe4\xb8\x80", 1) > 0 &&
                  ink_of("\xe3\x81\x82", 1) > 0, "DEV-011",
                  "a dropped face draws the letters IT has, Chinese included");
            check(tiku_desk_text_width(tiku_desk_font_plain(),
                                       "\xe4\xb8\x80") > space * 2,
                  "DEV-011",
                  "and gives them the room the file says, not a blank's");
            /*
             * This face has no Latin at all.  Every menu in the desktop
             * is Latin, so a face dropped in for one script must not
             * blank every other one.
             */
            check(ink_of("Tracker", 1) > 0 && ink_of("A", 1) > 0, "DEV-011",
                  "while a letter it has NOT got falls back to a baked face");
        }
        if (plain_file != NULL) {
            double fitted, loose;

            (void)tiku_desk_font_use_baked();
            (void)setenv("TIKU_DESK_HINT", "0", 1);
            (void)tiku_desk_font_set_files(plain_file, NULL);
            (void)tiku_desk_font_set_size(12);
            loose = ambiguous("Handgloves 0123456789 AHOxen");
            (void)tiku_desk_font_use_baked();
            (void)unsetenv("TIKU_DESK_HINT");
            (void)tiku_desk_font_set_files(plain_file, NULL);
            (void)tiku_desk_font_set_size(12);
            fitted = ambiguous("Handgloves 0123456789 AHOxen");
            check(fitted > 0.0 && loose > 0.0 && fitted < loose, "DEV-011",
                  "and the vertical fit commits its edges: less of the ink "
                  "is left at neither end of the scale");
        }
        /*
         * Six faces is all the collection holds, and the interface asks
         * for more than six: a pair at each size, and the smaller faces
         * chrome asks for by size alone.  Making room for one must not
         * take out the face the SAME call already handed back, which was
         * a zeroed face and a crash on the next letter drawn.
         */
        if (plain_file != NULL) {
            const char *bold_file =
                "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf";
            struct stat st;

            tiku_desk_font_use_baked();
            if (stat(bold_file, &st) != 0) {
                bold_file = NULL;
            }
            if (tiku_desk_font_set_files(plain_file, bold_file)) {
                (void)tiku_desk_font_set_size(12);
                (void)tiku_desk_font_at(10);
                (void)tiku_desk_font_at(14);
                (void)tiku_desk_font_at(16);
                (void)tiku_desk_font_set_size(14);
                (void)tiku_desk_font_set_size(16);
                check(tiku_desk_text_width(tiku_desk_font_plain(),
                                           "Handgloves") > 0 &&
                      ink_of("Handgloves", 1) > 0, "DEV-011",
                      "and a seventh face does not take out the pair in "
                      "force to make room for itself");
            }
        }
        /*
         * A space is a glyph the file HAS, with an advance of its own and
         * nothing to draw.  Read that as "the file has not got it" and
         * the width comes from the baked face instead -- so every space
         * in a dropped font would be the wrong width, which is a thing
         * only a monospaced face says out loud.
         */
        {
            static const char *const mono[] = {
                "/usr/share/fonts/source-foundry-hack-fonts/Hack-Regular.ttf",
                "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf"
            };
            const char *grid = pick(mono, 2);

            tiku_desk_font_use_baked();
            if (grid != NULL && tiku_desk_font_set_files(grid, NULL)) {
                const tiku_desk_font_t *f2;

                (void)tiku_desk_font_set_size(12);
                f2 = tiku_desk_font_plain();
                check(tiku_desk_text_width(f2, " ") ==
                      tiku_desk_text_width(f2, "M") &&
                      tiku_desk_text_width(f2, " ") > 0, "DEV-011",
                      "a space in a dropped face is the FILE's space: on a "
                      "monospaced one it is exactly as wide as an M");
            }
        }
        tiku_desk_font_use_baked();
        (void)tiku_desk_font_set_family(0);
        (void)tiku_desk_font_set_size(12);
    }

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nnames outside ASCII hold\n");
    return 0;
}
