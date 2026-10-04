/* Motion deadlines, clipping, HiDPI and final-state cleanup.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "tiku_motion.h"

/** @brief Start, progress, retirement and the ease, against the clock. */
static void deadlines(void)
{
    tiku_motion_t m = {0};
    assert(tiku_motion_progress(&m, 1000) == 1.0f);
    tiku_motion_start(&m, 1000, 100);
    assert(m.active && tiku_motion_progress(&m, 1000) == 0.0f);
    assert(tiku_motion_progress(&m, 1050) == 0.5f);
    assert(tiku_motion_progress(&m, 1099) < 1.0f && m.active);
    assert(tiku_motion_progress(&m, 1100) == 1.0f && !m.active);
    tiku_motion_start(&m, 1000, 100);
    assert(tiku_motion_progress(&m, 0) == 1.0f && !m.active);
    assert(tiku_motion_progress(&m, 1050) == 1.0f); /* never resumes */
    tiku_motion_start(&m, 1000, 100);
    assert(tiku_motion_progress(&m, 999) == 1.0f && !m.active);
    tiku_motion_start(&m, 0, 100);
    assert(!m.active);
    tiku_motion_start(&m, 1000, 0);
    assert(!m.active);
    tiku_motion_start(&m, 1000, 100);
    tiku_motion_start(&m, 1050, 200);
    assert(tiku_motion_progress(&m, 1150) == 0.5f);
    float previous = -1.0f;
    for (int i = 0; i <= 100; i++) {
        float value = tiku_motion_ease_out(i/100.0f);
        assert(value >= previous && value >= 0.0f && value <= 1.0f);
        previous = value;
    }
}

/** @brief The outline and the fade at @p scale: where they paint and
 *         that a partial repaint matches a whole one. */
static void pixels(int scale)
{
    tiku_rgb_t bg = TIKU_RGB(100, 120, 140);
    tiku_surface_t *s = tiku_surface_new(100, 100, bg);
    tiku_surface_t *full = tiku_surface_new(100, 100, bg);
    assert(s && full);
    assert(!tiku_surface_rescale(s, scale, bg));
    assert(!tiku_surface_rescale(full, scale, bg));
    tiku_rect_t icon = {30, 30, 32, 32};
    tiku_rect_t reach = tiku_motion_outline_bounds(icon);
    for (int i = 0; i <= 10; i++) {
        tiku_fill(s, (tiku_rect_t){0, 0, 100, 100}, bg);
        tiku_motion_outline(s, icon, i/10.0f);
        int changed = 0;
        for (int y = 0; y < 100; y++) for (int x = 0; x < 100; x++) {
            if (tiku_peek(s, x, y) != bg) {
                changed++;
                assert(x >= reach.x && x < reach.x+reach.w);
                assert(y >= reach.y && y < reach.y+reach.h);
                assert(x < icon.x || x >= icon.x+icon.w ||
                       y < icon.y || y >= icon.y+icon.h);
            }
        }
        assert(i == 10 ? changed == 0 : changed > 0);
    }
    /* Capturing a popup at a screen edge must keep the clipped origin;
     * repainting a small part must match the same part of a full frame. */
    tiku_rect_t popup = {-4, -6, 70, 65}, clip = {10, 12, 30, 25}, at = {0};
    for (int i = 0; i <= 10; i++) {
        float progress = i/10.0f;
        tiku_clip_damage(s, NULL);
        tiku_fill(s, (tiku_rect_t){0, 0, 100, 100}, bg);
        tiku_fill(full, (tiku_rect_t){0, 0, 100, 100}, bg);
        tiku_surface_t *under =
            tiku_motion_fade_begin(full, popup, progress, &at);
        assert((under != NULL) == (i < 10));
        tiku_fill(full, popup, TIKU_C_PANEL);
        tiku_motion_fade_end(full, under, at, progress);
        tiku_clip_damage(s, &clip);
        under = tiku_motion_fade_begin(s, popup, progress, &at);
        if (under)
            assert(at.x == clip.x && at.y == clip.y && under->w == clip.w);
        tiku_fill(s, popup, TIKU_C_PANEL);
        tiku_motion_fade_end(s, under, at, progress);
        for (int y = 0; y < 100; y++) for (int x = 0; x < 100; x++) {
            int inside = x >= clip.x && x < clip.x+clip.w &&
                         y >= clip.y && y < clip.y+clip.h;
            assert(tiku_peek(s, x, y) ==
                   (inside ? tiku_peek(full, x, y) : bg));
        }
    }
    tiku_surface_free(s);
    tiku_surface_free(full);
}

int main(void)
{
    deadlines();
    for (int theme = 0; theme < 3; theme++) {
        tiku_theme_set(theme == 1 ? tiku_theme_dark() :
                       theme == 2 ? tiku_theme_mono() : NULL);
        pixels(1); pixels(2);
    }
    tiku_theme_set(NULL);
    puts("PASS: motion timing, cancellation, bounds, fades and HiDPI clipping");
    return 0;
}
