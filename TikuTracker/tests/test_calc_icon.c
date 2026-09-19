/* The Calculator wears a real vector icon, legible at menu and HiDPI sizes.
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
    assert(tiku_icons_find("calculator"));
    tiku_surface_t *s = tiku_surface_new(250, 90, TIKU_RGB(216,216,216));
    assert(s);
    int x = 8;
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        int n = sizes[i], slate = 0, keys = 0, display = 0;
        const uint32_t *pixels = tiku_icons_bitmap("calculator", n);
        assert(pixels && !(pixels[0] >> 24));
        for (int p = 0; p < n*n; p++) {
            unsigned a = pixels[p] >> 24, r = (pixels[p] >> 16) & 255;
            unsigned g = (pixels[p] >> 8) & 255, b = pixels[p] & 255;
            if (a > 240 && r < 100 && g < 110 && b < 120) slate++;
            if (a > 240 && r > 220 && g > 215 && b > 200) keys++;
            if (a > 240 && r > 190 && g > 205 && b > 170 && b < 215) display++;
        }
        /* The body, the keys and the display all survive the size. */
        assert(slate > n && keys >= n / 5 && display >= n / 4);
        assert(tiku_icon_paint(s, "calculator", x, 12, n));
        x += n + 10;
    }
    assert(!tiku_surface_png(s, "/tmp/tiku-calc-icon.png"));
    tiku_surface_free(s);
    puts("PASS: Calculator body, display and keys at 14-64 px");
    return 0;
}
