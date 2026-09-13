/* Incremental software-frame presentation; SPDX-License-Identifier: Apache-2.0 */
#include "tiku_present.h"
#include <limits.h>
#include <string.h>

/** @brief Update one native tile, using row copies at matching physical sizes. */
static int tile(tiku_rgb_t *dst, int w, int h, int stride,
                const tiku_surface_t *src, tiku_rect_t r, int force)
{
    int sc = src->scale > 0 ? src->scale : 1;
    int sw = src->w*sc, sh = src->h*sc, changed = force;
    for (int y = r.y; y < r.y+r.h; y++) {
        tiku_rgb_t *out = dst+(size_t)y*stride+r.x;
        if ((w == sw && h == sh) ||
            (sc > 1 && sw >= w && sh >= h && sw-w < sc && sh-h < sc)) {
            const tiku_rgb_t *in = src->px+(size_t)y*sw+r.x;
            if (force || memcmp(out, in, (size_t)r.w*sizeof *out)) {
                memcpy(out, in, (size_t)r.w*sizeof *out); changed = 1;
            }
        } else {
            for (int x = r.x; x < r.x+r.w; x++) {
                /* Native resize rounding follows the old logical scaler. */
                tiku_rgb_t px = src->px[((size_t)y*src->h/h)*sc*sw+
                                         ((size_t)x*src->w/w)*sc];
                if (force || out[x-r.x] != px) {
                    out[x-r.x] = px; changed = 1;
                }
            }
        }
    }
    return changed;
}

/** @brief Expand a nonempty rectangle to contain another. */
static tiku_rect_t united(tiku_rect_t a, tiku_rect_t b)
{
    int right = a.x+a.w > b.x+b.w ? a.x+a.w : b.x+b.w;
    int bottom = a.y+a.h > b.y+b.h ? a.y+a.h : b.y+b.h;
    if (b.x < a.x) a.x = b.x;
    if (b.y < a.y) a.y = b.y;
    a.w = right-a.x; a.h = bottom-a.y; return a;
}

int tiku_present_update(tiku_rgb_t *dst, int w, int h, int stride,
    const tiku_surface_t *src, int force, tiku_rect_t *rects, int cap)
{
    if (!dst || !src || !src->px || !rects || cap < 1 || w < 1 || h < 1 ||
        stride < w || src->w < 1 || src->h < 1 ||
        src->scale < 0 || src->scale > 4 || src->w > INT_MAX/4 ||
        src->h > INT_MAX/4 || w > INT_MAX-64 || h > INT_MAX-32) return -1;
    if (force) {
        rects[0] = (tiku_rect_t){0,0,w,h};
        (void)tile(dst,w,h,stride,src,rects[0],1); return 1;
    }
    int n = 0, overflow = 0;
    tiku_rect_t bounds = {0};
    for (int y = 0; y < h; y += 32) {
        for (int x = 0; x < w; x += 64) {
            tiku_rect_t r = {x,y,w-x < 64 ? w-x : 64,h-y < 32 ? h-y : 32};
            if (!tile(dst,w,h,stride,src,r,0)) continue;
            bounds = n ? united(bounds,r) : r;
            if (n && !overflow && rects[n-1].y == y &&
                rects[n-1].x+rects[n-1].w == x) rects[n-1].w += r.w;
            else if (n < cap) rects[n++] = r;
            else overflow = 1;
        }
    }
    if (overflow) { rects[0] = bounds; return 1; }
    return n;
}
