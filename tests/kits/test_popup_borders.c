/* Menus and modal alerts keep a visible border, scaled and clipped too.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "tiku_ui.h"
#include "tiku_alert.h"
#include "tiku_dl.h"

/** @brief The mean of @p c's three channels. */
static unsigned luminance(tiku_rgb_t c)
{
    return (((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255)) / 3;
}

/** @brief Both rings of @p r are one contrasting shade, nothing outside. */
static void border(tiku_surface_t *s, tiku_rect_t r)
{
    tiku_rgb_t edge = tiku_peek(s, r.x, r.y);
    assert(abs((int)luminance(edge) - (int)luminance(TIKU_C_PANEL)) >= 100);
    for (int inset = 0; inset < 2; inset++) {
        tiku_rect_t b = tiku_inset(r, inset);
        for (int x = b.x; x < b.x + b.w; x++) {
            assert(tiku_peek(s, x, b.y) == edge);
            assert(tiku_peek(s, x, b.y + b.h - 1) == edge);
        }
        for (int y = b.y; y < b.y + b.h; y++) {
            assert(tiku_peek(s, b.x, y) == edge);
            assert(tiku_peek(s, b.x + b.w - 1, y) == edge);
        }
    }
    assert(tiku_peek(s, r.x - 1, r.y) == TIKU_C_PANEL);
    assert(tiku_peek(s, r.x + r.w, r.y) == TIKU_C_PANEL);
}

/** @brief Menus, the alert, a clipped border and its replay at @p scale. */
static void check(int scale)
{
    tiku_surface_t *s = tiku_surface_new(500, 300, TIKU_C_PANEL);
    assert(s);
    if (scale > 1) assert(!tiku_surface_rescale(s, scale, TIKU_C_PANEL));
    const char *items[] = {"Applications", "-", "Shut down"};
    tiku_rect_t menu = {12, 10, 176,
                        3 * (tiku_font_plain()->height + 6) + 4};
    for (int hot = -1; hot < 3; hot++) {
        tiku_ui_menu(s, menu, items, 3, hot);
        border(s, menu); /* a hot row or a separator leaves the border */
    }
    tiku_alert_t alert;
    tiku_alert_open(&alert, TIKU_ALERT_WARN, 7,
                    "Shut down the desktop? Close the running applications.",
                    "Cancel|Shut down");
    tiku_rect_t panel = {30, 120, 420, 150};
    tiku_alert_draw(&alert, s, panel);
    border(s, panel);
    int bx, by, bw, bh;
    tiku_alert_button_rect(&alert, 0, panel.w, panel.h, &bx, &by, &bw, &bh);
    assert(tiku_alert_button_at(&alert, panel.w, panel.h,
                                bx + bw / 2, by + bh / 2) == 0);
    /* The border keeps to the caller's clip. */
    tiku_fill(s, menu, TIKU_C_PANEL);
    tiku_rect_t clip = {menu.x, menu.y, menu.w / 2, menu.h};
    tiku_clip_set(s, clip);
    tiku_ui_popup_border(s, menu);
    tiku_clip_reset(s);
    assert(tiku_peek(s, menu.x, menu.y) != TIKU_C_PANEL);
    assert(tiku_peek(s, menu.x + menu.w - 1, menu.y) == TIKU_C_PANEL);
    if (scale == 1) {
        /* Both rings record as frames a display list already carries. */
        tiku_surface_t *replay = tiku_surface_new(500, 300, TIKU_C_PANEL);
        tiku_dl_t *dl = tiku_dl_new(); assert(dl && replay);
        s->record = dl;
        tiku_ui_popup_border(s, menu);
        s->record = NULL;
        assert(!tiku_dl_misses(dl));
        assert(tiku_dl_play(dl, replay));
        border(replay, menu);
        tiku_surface_free(replay);
        tiku_dl_free(dl);
    }
    tiku_surface_free(s);
}

int main(void)
{
    const tiku_theme_t *themes[] = {NULL, tiku_theme_dark(), tiku_theme_mono()};
    for (int theme = 0; theme < 3; theme++) {
        tiku_theme_set(themes[theme]);
        check(1); check(2);
    }
    tiku_theme_set(NULL);
    puts("PASS: menu/alert outlines, contrast, scaling, clipping, hit "
         "targets and display-list replay");
    return 0;
}
