/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_geom.h - a rectangle and a colour, and nothing that draws.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_GEOM_H_
#define TIKU_GEOM_H_

#include <stdint.h>

typedef uint32_t tiku_rgb_t;
#define TIKU_RGB(r, g, b) \
    (((tiku_rgb_t)(r) << 16) | ((tiku_rgb_t)(g) << 8) | \
     (tiku_rgb_t)(b))

typedef struct {
    int x, y, w, h;
} tiku_rect_t;

#endif /* TIKU_GEOM_H_ */
