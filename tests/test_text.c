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
#include <string.h>

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

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nnames outside ASCII hold\n");
    return 0;
}
