/* Incremental software-frame presentation; SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_PRESENT_H_
#define TIKU_PRESENT_H_
#include "tiku_gfx.h"
#define TIKU_PRESENT_RECTS 64
/** @brief Update native RGB backing pixels; return changed native rectangles.
 * First use/exposure requires force. Destination stride is in pixels. Invalid
 * arguments return -1; no changes return 0. Overflow coalesces, never drops damage.
 */
int tiku_present_update(tiku_rgb_t *dst, int w, int h, int stride,
    const tiku_surface_t *src, int force, tiku_rect_t *rects, int cap);
#endif
