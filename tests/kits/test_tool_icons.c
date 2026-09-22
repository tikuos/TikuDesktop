/* The Device Tools wear vector icons of their own, legible at menu and
 * HiDPI sizes.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "tiku_icons_tiku.h"
#include "tiku_icons.h"
#include "tiku_iconpaint.h"

static int row_y = 12;          /* where the strip's current row sits */

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
        assert(tiku_icon_paint(s, name, *x, row_y, n));
        *x += n + 10;
    }
    *x += 20;
}
static int green(unsigned r, unsigned g, unsigned b) { return g > 180 && r < 150 && b < 170; }
static int orange(unsigned r, unsigned g, unsigned b) { return r > 200 && g > 100 && g < 180 && b < 100; }
static int blue(unsigned r, unsigned g, unsigned b) { (void)g; return b > 150 && r < 120; }
static int leaf(unsigned r, unsigned g, unsigned b) { return g > 120 && r < 80 && b < 120; }
static int slate(unsigned r, unsigned g, unsigned b) { return r > 55 && r < 90 && g > 60 && g < 95 && b > 70 && b < 105; }
static int carmine(unsigned r, unsigned g, unsigned b) { return r > 150 && g < 90 && b < 110; }

int main(void)
{
    assert(tiku_icons_tiku_register() > 0);
    tiku_surface_t *s = tiku_surface_new(1200, 170, TIKU_RGB(216,216,216));
    assert(s);
    int x = 8;
    check(s, "tool_bench", &x, green);        /* the tick */
    check(s, "tool_console", &x, green);      /* the live line */
    check(s, "tool_hex", &x, orange);         /* the marked bytes */
    check(s, "tool_monitor", &x, green);      /* the trace */
    check(s, "tool_inspector", &x, blue);     /* the tree's nodes */
    /* The Devices Preferences sections: each mark keeps its own colour
     * at every size, so the source list reads by icon as well as word. */
    x = 8;
    row_y = 92;
    check(s, "section_identity", &x, blue);   /* the face dot */
    check(s, "section_processor", &x, blue);  /* the die */
    check(s, "section_memory", &x, leaf);     /* the board */
    check(s, "section_storage", &x, slate);   /* the platters */
    check(s, "section_power", &x, orange);    /* the bolt */
    check(s, "section_radio", &x, blue);      /* the signal */
    check(s, "section_security", &x, carmine); /* the padlock's body */
    check(s, "section_boot", &x, leaf);       /* the ring */
    assert(!tiku_surface_png(s, "/tmp/tiku-tool-icons.png"));
    tiku_surface_free(s);
    puts("PASS: Bench, Console, Hex, Monitor and Inspector marks at 14-64 px,"
         " and the eight section marks");
    return 0;
}
