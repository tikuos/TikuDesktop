/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_theme.c - the three tables, and the one thing derived from them.
 *
 * A theme names ROLES and never layout, which is the whole bargain (the
 * Appearance Manager lesson) -- so what is worth testing about one is
 * that its roles stay distinguishable.  A palette whose selection and
 * whose active tab land on the same value has not got two roles, it has
 * one, and every list in the interface stops saying which row is picked.
 *
 * The third table is asked one more thing: that no role in it has any
 * hue, and that the toolkit can TELL, because pictorial art -- icons,
 * thumbnails -- reads that answer and drains itself.  It is derived from
 * the table rather than declared beside it, so this is really a test that
 * the two cannot come apart.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

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

    t = tiku_theme();
    check(apart(t->panel, t->doc) > 8 && apart(t->doc, t->text) > 100,
          "paper is off the panel, and the ink is well off the paper");
    check(apart(t->select, t->tab) > 24,
          "what is SELECTED and what is ACTIVE stay two different things,"
          " which draining a coloured palette is exactly what loses");
    check(apart(t->select, t->seltext) > 100 &&
          apart(t->tab, t->tab_text) > 60,
          "and each carries ink that can be read on it");
    check(apart(t->backdrop, t->panel) > 24,
          "a window is visible against the desktop behind it");

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

    tiku_theme_set(NULL);
    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe tables keep their roles apart\n");
    return 0;
}
