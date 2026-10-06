/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_surface_alloc.c - a failed surface resize, at every display scale,
 * leaves the surface's pixels, geometry, clip and damage as they were.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_allocation, failures;

/** @brief Allocator used by the separately compiled graphics fixture. */
void *tiku_test_surface_malloc(size_t bytes)
{
    return fail_allocation ? NULL : malloc(bytes);
}

/** @brief Print PASS or FAIL for @p condition and count a failure. */
static void check(int condition, const char *message)
{
    printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) {
        failures++;
    }
}

/** @brief Exercise failed and successful resizing at every display scale. */
int main(void)
{
    tiku_surface_t *s = tiku_surface_new(8, 6, 0xff123456);
    if (!s) {
        return 1;
    }
    for (int scale = 1; scale <= 4; scale++) {
        check(!tiku_surface_rescale(s, scale, 0xff123456),
              "rescale succeeds when memory is available");
        tiku_clip_set(s, (tiku_rect_t){1, 1, 4, 3});
        tiku_rect_t damage = {2, 2, 2, 2};
        tiku_clip_damage(s, &damage);
        tiku_surface_t saved = *s;
        fail_allocation = 1;
        check(tiku_surface_rescale(s, scale % 4 + 1, 0xffabcdef) == -1,
              "rescale reports allocation failure");
        check(!memcmp(s, &saved, sizeof saved),
              "failed rescale preserves the entire surface state");
        if (s->scale != saved.scale) {
            s->scale = saved.scale;
        }
        check(tiku_surface_resize(s, 16, 12, 0xffabcdef) == -1,
              "resize reports allocation failure");
        check(!memcmp(s, &saved, sizeof saved),
              "failed resize preserves the entire surface state");
        fail_allocation = 0;
        check(tiku_peek(s, 7, 5) == 0xff123456,
              "allocation failure preserves the last pixel");
        tiku_clip_reset(s);
    }
    check(tiku_surface_rescale(s, 0, 0) == -1 &&
          tiku_surface_rescale(s, 5, 0) == -1,
          "invalid scales are refused");
    tiku_surface_free(s);
    return failures ? 1 : 0;
}
