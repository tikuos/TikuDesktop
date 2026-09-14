/* Force Quit uses a real vector icon, legible at menu and HiDPI sizes.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "tiku_icons_tiku.h"
#include "tiku_icons.h"
#include "tiku_iconpaint.h"

int main(void)
{
    const int sizes[] = {14, 16, 24, 32, 64};
    assert(tiku_icons_tiku_register() > 0);
    assert(tiku_icons_find("force_quit"));
    tiku_surface_t *s = tiku_surface_new(250, 90, TIKU_RGB(216,216,216));
    assert(s);
    int x = 8;
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        int n = sizes[i], red = 0, ivory = 0;
        const uint32_t *pixels = tiku_icons_bitmap("force_quit", n);
        assert(pixels && !(pixels[0] >> 24));
        for (int p = 0; p < n*n; p++) {
            unsigned a = pixels[p] >> 24, r = (pixels[p] >> 16) & 255;
            unsigned g = (pixels[p] >> 8) & 255, b = pixels[p] & 255;
            if (a > 240 && r > 150 && g < 150 && b < 140) red++;
            if (a > 240 && r > 240 && g > 225 && b > 210) ivory++;
        }
        assert(red > n && ivory > 0);
        assert(tiku_icon_paint(s, "force_quit", x, 12, n));
        x += n + 10;
    }
    assert(!tiku_surface_png(s, "/tmp/tiku-forcequit-icon.png"));
    tiku_surface_free(s);
    puts("PASS: Force Quit outline, red face and cross at 14–64 px");
    return 0;
}
