/* The Terminal and the Editor wear vector icons of their own, legible at
 * menu and HiDPI sizes.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "tiku_icons_tiku.h"
#include "tiku_icons.h"
#include "tiku_iconpaint.h"

/** @brief Count opaque pixels that pass @p test at every size and paint them. */
static void check(tiku_surface_t *s, const char *name, int *x,
                  int (*test)(unsigned r, unsigned g, unsigned b), int least)
{
    const int sizes[] = {14, 16, 24, 32, 64};
    assert(tiku_icons_find(name));
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        int n = sizes[i], hits = 0;
        const uint32_t *pixels = tiku_icons_bitmap(name, n);
        assert(pixels && !(pixels[0] >> 24));
        for (int p = 0; p < n*n; p++)
            if ((pixels[p] >> 24) > 240 &&
                test((pixels[p] >> 16) & 255, (pixels[p] >> 8) & 255,
                     pixels[p] & 255)) hits++;
        assert(hits >= n / least);
        assert(tiku_icon_paint(s, name, *x, 12, n));
        *x += n + 10;
    }
}
static int green(unsigned r, unsigned g, unsigned b) { return g > 180 && r < 150 && b < 170; }
static int screen(unsigned r, unsigned g, unsigned b) { return r < 40 && g < 50 && b < 50; }
static int paper(unsigned r, unsigned g, unsigned b) { return r > 240 && g > 235 && b > 210; }
static int yellow(unsigned r, unsigned g, unsigned b) { return r > 220 && g > 160 && b < 130; }

int main(void)
{
    assert(tiku_icons_tiku_register() > 0);
    tiku_surface_t *s = tiku_surface_new(760, 90, TIKU_RGB(216,216,216));
    assert(s);
    int x = 8;
    check(s, "terminal", &x, green, 4);    /* the prompt */
    check(s, "terminal", &x, screen, 1);   /* on its screen */
    x = 8 + 380;
    check(s, "editor", &x, paper, 1);      /* the sheet */
    check(s, "editor", &x, yellow, 4);     /* and the pencil across it */
    assert(!tiku_surface_png(s, "/tmp/tiku-app-icons.png"));
    tiku_surface_free(s);
    puts("PASS: Terminal screen and prompt, Editor sheet and pencil at 14-64 px");
    return 0;
}
