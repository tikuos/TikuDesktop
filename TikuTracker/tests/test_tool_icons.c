/* The Device Tools wear vector icons of their own, legible at menu and
 * HiDPI sizes.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "tiku_icons_tiku.h"
#include "tiku_icons.h"
#include "tiku_iconpaint.h"

/** @brief Every size has opaque pixels of the mark's own colour. */
static void check(tiku_surface_t *s, const char *name, int *x,
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
        assert(hits >= n / 5);
        assert(tiku_icon_paint(s, name, *x, 12, n));
        *x += n + 10;
    }
    *x += 20;
}
static int green(unsigned r, unsigned g, unsigned b) { return g > 180 && r < 150 && b < 170; }
static int orange(unsigned r, unsigned g, unsigned b) { return r > 200 && g > 100 && g < 180 && b < 100; }
static int blue(unsigned r, unsigned g, unsigned b) { (void)g; return b > 150 && r < 120; }

int main(void)
{
    assert(tiku_icons_tiku_register() > 0);
    tiku_surface_t *s = tiku_surface_new(1000, 90, TIKU_RGB(216,216,216));
    assert(s);
    int x = 8;
    check(s, "tool_bench", &x, green);        /* the tick */
    check(s, "tool_console", &x, green);      /* the live line */
    check(s, "tool_hex", &x, orange);         /* the marked bytes */
    check(s, "tool_monitor", &x, green);      /* the trace */
    check(s, "tool_inspector", &x, blue);     /* the tree's nodes */
    assert(!tiku_surface_png(s, "/tmp/tiku-tool-icons.png"));
    tiku_surface_free(s);
    puts("PASS: Bench, Console, Hex, Monitor and Inspector marks at 14-64 px");
    return 0;
}
