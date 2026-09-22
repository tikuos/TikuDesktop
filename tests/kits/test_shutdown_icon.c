/* Shut down wears a real vector icon, legible at menu and HiDPI sizes.
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
    assert(tiku_icons_find("shutdown"));
    tiku_surface_t *s = tiku_surface_new(250, 90, TIKU_RGB(216,216,216));
    assert(s);
    int x = 8;
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        int n = sizes[i], slate = 0, ivory = 0;
        const uint32_t *pixels = tiku_icons_bitmap("shutdown", n);
        assert(pixels && !(pixels[0] >> 24));
        for (int p = 0; p < n*n; p++) {
            unsigned a = pixels[p] >> 24, r = (pixels[p] >> 16) & 255;
            unsigned g = (pixels[p] >> 8) & 255, b = pixels[p] & 255;
            if (a > 240 && r < 100 && g < 110 && b < 120) slate++;
            if (a > 240 && r > 240 && g > 225 && b > 210) ivory++;
        }
        /* The button's face outweighs the sign, and the sign is there. */
        assert(slate > ivory && ivory >= n / 2);
        assert(tiku_icon_paint(s, "shutdown", x, 12, n));
        x += n + 10;
    }
    assert(!tiku_surface_png(s, "/tmp/tiku-shutdown-icon.png"));
    tiku_surface_free(s);
    puts("PASS: Shut down button, rim and power sign at 14-64 px");
    return 0;
}
