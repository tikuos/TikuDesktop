/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_theme.c - the three tables, and the one thing derived from them.
 *
 * A theme names roles and never layout, which is the whole bargain, so
 * what is worth testing is that its roles stay distinguishable: a palette
 * whose selection and active tab land on the same value has one role, not
 * two, and every list stops saying which row is picked.  The third table
 * is asked one thing more: that no role in it has any hue, and that the
 * toolkit can tell, because pictorial art reads that answer and drains
 * itself.  It is derived from the table rather than declared beside it,
 * so this is a test that the two cannot come apart.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>

#include "tiku_gfx.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] TS-THM  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief How far apart two colours are, worst channel. */
static int
apart(tiku_rgb_t a, tiku_rgb_t b)
{
    int worst = 0;
    int i;

    for (i = 0; i < 3; i++) {
        int sh = 16 - 8 * i;
        int d = (int)((a >> sh) & 0xFFu) - (int)((b >> sh) & 0xFFu);

        if (d < 0) {
            d = -d;
        }
        if (d > worst) {
            worst = d;
        }
    }
    return worst;
}

int
main(void)
{
    const tiku_theme_t *t;

    printf("the three tables (TS-THM)\n");

    tiku_theme_set(NULL);
    check(!tiku_theme_achromatic(), "the default table has hue in it");
    tiku_theme_set(tiku_theme_dark());
    check(!tiku_theme_achromatic(), "and so has the dark one: dusk, not grey");

    tiku_theme_set(tiku_theme_mono());
    check(tiku_theme_achromatic(),
          "the third has none, and the toolkit can say so without being told");

    /*
     * EVERY table, not whichever one was live when this line was
     * reached.  Until now `t` was read once, right after the mono table
     * was set, so every assertion below was pinned against mono alone
     * and neither the light nor the dark table had any role-distinctness
     * cover at all -- the two tables every pixel test actually runs on.
     */
    {
        const tiku_theme_t *table[3];
        const char *called[3];
        int k;

        tiku_theme_set(NULL);
        table[0] = tiku_theme();
        called[0] = "light";
        table[1] = tiku_theme_dark();
        called[1] = "dusk";
        table[2] = tiku_theme_mono();
        called[2] = "mono";

        for (k = 0; k < 3; k++) {
            char what[160];

            t = table[k];
            snprintf(what, sizeof what,
                     "%s: paper is off the panel, and the ink is well off "
                     "the paper", called[k]);
            check(apart(t->panel, t->doc) > 8 && apart(t->doc, t->text) > 100,
                  what);
            snprintf(what, sizeof what,
                     "%s: what is SELECTED and what is ACTIVE stay two "
                     "different things", called[k]);
            check(apart(t->select, t->tab) > 24, what);
            snprintf(what, sizeof what,
                     "%s: and each carries ink that can be read on it",
                     called[k]);
            check(apart(t->select, t->seltext) > 100 &&
                  apart(t->tab, t->tab_text) > 60, what);
            snprintf(what, sizeof what,
                     "%s: a window is visible against the desktop behind it",
                     called[k]);
            check(apart(t->backdrop, t->panel) > 24, what);

            /*
             * The code inks: four meanings that must be four sights, on
             * every table -- including the one with no hue to spend,
             * where they can only part by luminance.  A palette that
             * collapsed two of them would still look deliberate, and
             * would quietly stop telling a string from a remark.
             */
            snprintf(what, sizeof what,
                     "%s: every code ink is legible on the document ground",
                     called[k]);
            check(apart(t->code_word, t->doc) > 60 &&
                  apart(t->code_num, t->doc) > 60 &&
                  apart(t->code_str, t->doc) > 60 &&
                  apart(t->code_rem, t->doc) > 60, what);
            snprintf(what, sizeof what,
                     "%s: and stands apart from the document's own ink, so "
                     "a reserved word does not read as a variable",
                     called[k]);
            check(apart(t->code_word, t->text) > 24 &&
                  apart(t->code_num, t->text) > 24 &&
                  apart(t->code_str, t->text) > 24 &&
                  apart(t->code_rem, t->text) > 24, what);
            snprintf(what, sizeof what,
                     "%s: and the four are four different shades", called[k]);
            check(apart(t->code_word, t->code_num) > 24 &&
                  apart(t->code_word, t->code_str) > 24 &&
                  apart(t->code_word, t->code_rem) > 24 &&
                  apart(t->code_num, t->code_str) > 24 &&
                  apart(t->code_num, t->code_rem) > 24 &&
                  apart(t->code_str, t->code_rem) > 24, what);
        }
    }

    /* The art asks for a grey and must get the eye's grey, not an
     * average: a saturated red and a saturated blue are not equally
     * bright, and a mean makes them so. */
    check(tiku_grey(TIKU_RGB(255, 255, 255)) == TIKU_RGB(255, 255, 255) &&
          tiku_grey(TIKU_RGB(0, 0, 0)) == TIKU_RGB(0, 0, 0),
          "white and black are already their own greys");
    {
        tiku_rgb_t red = tiku_grey(TIKU_RGB(255, 0, 0));
        tiku_rgb_t green = tiku_grey(TIKU_RGB(0, 255, 0));
        tiku_rgb_t blue = tiku_grey(TIKU_RGB(0, 0, 255));

        check(((red >> 16) & 0xFFu) == ((red >> 8) & 0xFFu) &&
              ((red >> 8) & 0xFFu) == (red & 0xFFu),
              "what comes back has no hue left in it at all");
        check((green >> 16) > (red >> 16) && (red >> 16) > (blue >> 16),
              "and green reads brighter than red, and red than blue, as an"
              " eye has it and an average does not");
    }

    printf("\ndisabled ink recedes on every table\n");
    {
        /*
         * The derivation, not the shade: tiku_tint moves a colour toward
         * white or toward black, and either direction inverts on one of
         * the tables -- under dusk a greyed menu command was BRIGHTER
         * than a pickable one.  The midpoint of ink and ground is half
         * the enabled contrast wherever both came from one theme.
         *
         * Setting the table is the check: calling tiku_theme_dark() and
         * throwing the answer away leaves the run labelled dusk reading
         * the light table, and the claim about dusk -- the whole reason
         * the check exists -- never made.
         */
        int k;

        for (k = 0; k < 3; k++) {
            tiku_rgb_t text, panel, dim, doc, faint;
            long ce, cd;

            tiku_theme_set(k == 0 ? NULL
                         : k == 1 ? tiku_theme_dark() : tiku_theme_mono());
            text = TIKU_C_TEXT;
            panel = TIKU_C_PANEL;
            doc = TIKU_C_DOC;
            dim = tiku_dim(text, panel);
            ce = labs((long)(text & 0xffu) - (long)(panel & 0xffu));
            cd = labs((long)(dim & 0xffu) - (long)(panel & 0xffu));
            check(cd > 0 && cd < ce,
                  k == 0 ? "on the light table the dim ink sits between "
                           "text and panel"
                : k == 1 ? "on the dusk table too, where tinting toward "
                           "white made disabled ADVANCE"
                         : "and on the table with no hue to lose");

            /* The faint ink a line number is written in is that same
             * derivation, on the document rather than the panel -- so it
             * recedes wherever code is read. */
            faint = tiku_ink(TIKU_INK_FAINT);
            ce = labs((long)(text & 0xffu) - (long)(doc & 0xffu));
            cd = labs((long)(faint & 0xffu) - (long)(doc & 0xffu));
            check(faint == tiku_dim(text, doc) && cd > 0 && cd < ce,
                  k == 0 ? "and the faint ink recedes on the page, light"
                : k == 1 ? "and the faint ink recedes on the page, dusk"
                         : "and the faint ink recedes on the page, mono");
            /*
             * Each meaning to ITS role, not merely to some role: every
             * check below this one is about the four shades being four,
             * and a wiring that swapped a string for a remark would
             * satisfy all of them while telling the reader the opposite
             * of the truth.
             */
            check(tiku_ink(TIKU_INK_WORD) == TIKU_C_CODE_WORD &&
                  tiku_ink(TIKU_INK_NUM) == TIKU_C_CODE_NUM &&
                  tiku_ink(TIKU_INK_STR) == TIKU_C_CODE_STR &&
                  tiku_ink(TIKU_INK_REMARK) == TIKU_C_CODE_REM,
                  k == 0 ? "each meaning wears its own role, light"
                : k == 1 ? "each meaning wears its own role, dusk"
                         : "each meaning wears its own role, mono");
            check(tiku_ink(TIKU_INK_PLAIN) == text &&
                  tiku_ink(TIKU_INK_WRONG) == TIKU_C_DANGER,
                  k == 0 ? "plain ink is the document's, and what is wrong "
                           "borrows the stop severity rather than a second "
                           "red of its own"
                : k == 1 ? "on dusk as well" : "and on mono");
        }
    }

    tiku_theme_set(NULL);
    printf("\n%s\n", fails ? "FAILURES" : "the tables keep their roles apart");
    return fails ? 1 : 0;
}
