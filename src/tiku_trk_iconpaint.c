/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_iconpaint.c - compositing a rendered icon onto a surface.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_iconpaint.h"
#include "tiku_trk_icons.h"

/** @brief Blend one premultiplied source pixel over an opaque destination. */
static tiku_desk_rgb_t
over(uint32_t src, tiku_desk_rgb_t dst)
{
    unsigned a = (src >> 24) & 0xFFu;
    unsigned inv = 255u - a;
    unsigned r, g, b;

    /* Source is already multiplied by its own alpha, so the destination just
     * loses the same fraction: dst * (1 - a) + src. */
    r = ((src >> 16) & 0xFFu) + (((dst >> 16) & 0xFFu) * inv + 127u) / 255u;
    g = ((src >> 8) & 0xFFu)  + (((dst >> 8) & 0xFFu) * inv + 127u) / 255u;
    b = (src & 0xFFu)         + ((dst & 0xFFu) * inv + 127u) / 255u;
    if (r > 255u) { r = 255u; }
    if (g > 255u) { g = 255u; }
    if (b > 255u) { b = 255u; }
    return TIKU_DESK_RGB(r, g, b);
}

static int
paint(tiku_desk_surface_t *s, const char *name, int x, int y, int size,
      float mix, tiku_desk_rgb_t wash)
{
    const uint32_t *bmp;
    int row, col;
    unsigned m;

    if (s == NULL || name == NULL || size <= 0) {
        return 0;
    }
    bmp = tiku_trk_icons_bitmap(name, size);
    if (bmp == NULL) {
        return 0;
    }
    if (mix < 0.0f) { mix = 0.0f; }
    if (mix > 1.0f) { mix = 1.0f; }
    m = (unsigned)(mix * 255.0f + 0.5f);

    for (row = 0; row < size; row++) {
        int py = y + row;

        if (py < s->clip.y || py >= s->clip.y + s->clip.h ||
            py < 0 || py >= s->h) {
            continue;
        }
        for (col = 0; col < size; col++) {
            int px = x + col;
            uint32_t src = bmp[(size_t)row * (size_t)size + (size_t)col];
            tiku_desk_rgb_t *dst;

            if (px < s->clip.x || px >= s->clip.x + s->clip.w ||
                px < 0 || px >= s->w || ((src >> 24) & 0xFFu) == 0u) {
                continue;
            }
            dst = &s->px[(size_t)py * (size_t)s->w + (size_t)px];
            *dst = over(src, *dst);
            if (m != 0u) {
                unsigned r = ((*dst >> 16) & 0xFFu) * (255u - m) +
                             ((wash >> 16) & 0xFFu) * m;
                unsigned g = ((*dst >> 8) & 0xFFu) * (255u - m) +
                             ((wash >> 8) & 0xFFu) * m;
                unsigned b = (*dst & 0xFFu) * (255u - m) + (wash & 0xFFu) * m;
                *dst = TIKU_DESK_RGB(r / 255u, g / 255u, b / 255u);
            }
        }
    }
    return 1;
}

int
tiku_trk_icon_paint(tiku_desk_surface_t *s, const char *name, int x, int y,
                    int size)
{
    return paint(s, name, x, y, size, 0.0f, 0);
}

int
tiku_trk_icon_paint_dim(tiku_desk_surface_t *s, const char *name, int x, int y,
                        int size, float mix, tiku_desk_rgb_t wash)
{
    return paint(s, name, x, y, size, mix, wash);
}

int
tiku_trk_icon_paint_selected(tiku_desk_surface_t *s, const char *name, int x,
                             int y, int size)
{
    /* Light enough that the art is still recognisable -- the icon says WHAT
     * the row is, and a selection that obliterated it would cost more than
     * it conveys. */
    return paint(s, name, x, y, size, 0.35f, TIKU_DESK_C_SELECT);
}

void
tiku_trk_thumb_paint(tiku_desk_surface_t *s, const tiku_trk_image_t *th,
                     int x, int y, int size)
{
    int py, px;

    if (s == NULL || th == NULL || th->px == NULL || size <= 0) {
        return;
    }
    for (py = 0; py < size; py++) {
        long sy = (long)py * th->h / size;

        for (px = 0; px < size; px++) {
            long sx = (long)px * th->w / size;
            uint32_t p = th->px[sy * th->w + sx];

            /* The transparent margin is left alone rather than painted:
             * that is what makes the thumbnail sit ON the row instead of
             * inside a square of its own (IV-034). */
            if ((p >> 24) != 0u) {
                tiku_desk_pixel(s, x + px, y + py,
                                TIKU_DESK_RGB((p >> 16) & 0xffu,
                                              (p >> 8) & 0xffu, p & 0xffu));
            }
        }
    }
}
