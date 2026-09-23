/* The programming tools, the notes, the developer calculator and the demos
 * wear vector icons of their own.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "tiku_icons_tiku.h"
#include "tiku_icons.h"
#include "tiku_iconpaint.h"

/** @brief Every size has opaque pixels of the mark's own colour. */
static void check(tiku_surface_t *s, const char *name, int *x, int *y,
                  int (*test)(unsigned r, unsigned g, unsigned b))
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
        assert(hits >= n / 7);
        assert(tiku_icon_paint(s, name, *x, *y, n));
        *x += n + 10;
    }
    *x += 20;
    if (*x > 800) { *x = 8; *y += 90; }
}
static int dark(unsigned r, unsigned g, unsigned b) { return r < 90 && g < 100 && b < 130; }
static int green(unsigned r, unsigned g, unsigned b) { return g > 180 && r < 150 && b < 170; }
static int yellow(unsigned r, unsigned g, unsigned b) { return r > 240 && g > 210 && b < 130; }
static int orange(unsigned r, unsigned g, unsigned b) { return r > 190 && g > 90 && g < 190 && b < 120; }
static int blue(unsigned r, unsigned g, unsigned b) { (void)g; return b > 150 && r < 120; }
static int red(unsigned r, unsigned g, unsigned b) { return r > 180 && g < 90 && b < 90; }
static int paper(unsigned r, unsigned g, unsigned b) { return r > 200 && g > 200 && b > 170; }

int main(void)
{
    /* Every one taken: the set refusing the last few is how four marks
     * once vanished without a word. */
    assert(tiku_icons_tiku_register() == tiku_icons_tiku_count());
    tiku_surface_t *s = tiku_surface_new(1000, 280, TIKU_RGB(216,216,216));
    assert(s);
    int x = 8, y = 12;
    check(s, "app_basic", &x, &y, dark);      /* the line number */
    check(s, "app_c", &x, &y, dark);          /* the brace */
    check(s, "app_devcalc", &x, &y, green);   /* the hex readout */
    check(s, "app_notes", &x, &y, yellow);    /* the note */
    check(s, "app_hello", &x, &y, orange);    /* the exclamation */
    check(s, "app_clock", &x, &y, paper);     /* the face */
    check(s, "app_counter", &x, &y, paper);   /* the digit wheels */
    check(s, "app_sketch", &x, &y, blue);     /* the stroke */
    check(s, "app_sensors", &x, &y, red);     /* the mercury */
    check(s, "app_twowin", &x, &y, orange);   /* the tabs */
    assert(!tiku_surface_png(s, "/tmp/tiku-more-icons.png"));
    tiku_surface_free(s);
    puts("PASS: ten more application marks at 14-64 px");
    return 0;
}
