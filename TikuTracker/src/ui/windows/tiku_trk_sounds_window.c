/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_sounds_window.c - the Sounds preferences window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_sounds_window.h"

#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_strings.h"
#include "tiku_ui.h"

#include <stdio.h>
#include <string.h>

#define PAD 16
#define BUTTON_H 24
#define TEST_W 64

/** @brief One row of the table: the label's face plus room around it. */
static int
row_h(void)
{
    return tiku_font_plain()->height + 10;
}

/** @brief Where the first row begins, under the heading. */
static int
list_y(void)
{
    return 10 + tiku_font_bold()->height + 8;
}

/** @brief Where the volume stepper sits, under the last row. */
static int
volume_y(void)
{
    return list_y() + TIKU_SOUND_EVENTS * row_h() + 12;
}

/** @brief Where the Defaults and Revert buttons sit. */
static int
button_y(void)
{
    return volume_y() + BUTTON_H + 16;
}

/** @brief The frame's height: the content, its margin, and the tab. */
static int
window_height(void)
{
    return button_y() + BUTTON_H + PAD + tiku_window_tab_h() +
           TIKU_WINDOW_BORDER;
}

/** @brief The Test button of row @p i. */
static tiku_rect_t
test_rect(tiku_rect_t content, int i)
{
    tiku_rect_t r;

    r.w = tiku_ui_button_w(tiku_str("button.test"), TEST_W);
    r.h = row_h() - 4;
    r.x = content.x + content.w - PAD - r.w;
    r.y = content.y + list_y() + i * row_h() + 2;
    return r;
}

/** @brief The checkbox of row @p i, label included. */
static tiku_rect_t
check_rect(tiku_rect_t content, int i)
{
    tiku_rect_t r;

    r.x = content.x + PAD;
    r.y = content.y + list_y() + i * row_h();
    r.w = content.w - 2 * PAD - test_rect(content, i).w - 8;
    r.h = row_h();
    return r;
}

/** @brief The volume's minus button. */
static tiku_rect_t
minus_rect(tiku_rect_t content)
{
    tiku_rect_t r;
    int label_w = tiku_text_width(tiku_font_bold(),
                                  tiku_str("prefs.sounds.volume"));

    r.x = content.x + PAD + label_w + 12;
    r.y = content.y + volume_y();
    r.w = BUTTON_H;
    r.h = BUTTON_H;
    return r;
}

/** @brief The volume's plus button, past the number. */
static tiku_rect_t
plus_rect(tiku_rect_t content)
{
    tiku_rect_t r = minus_rect(content);

    r.x += r.w + 4 + 48 + 4;
    return r;
}

/** @brief The Defaults button. */
static tiku_rect_t
defaults_rect(tiku_rect_t content)
{
    tiku_rect_t r;

    r.w = tiku_ui_button_w(tiku_str("button.defaults"), 112);
    r.h = BUTTON_H;
    r.x = content.x + PAD;
    r.y = content.y + button_y();
    return r;
}

/** @brief The Revert button, beside Defaults. */
static tiku_rect_t
revert_rect(tiku_rect_t content)
{
    tiku_rect_t r = defaults_rect(content);

    r.x += r.w + 12;
    r.w = tiku_ui_button_w(tiku_str("button.revert"), 100);
    return r;
}

/** @brief Whether (@p x, @p y) lies in @p r. */
static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/** @brief Whether anything differs from what the window opened on. */
static int
revertable(const tiku_trk_sounds_window_t *controller)
{
    int i;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        if (tiku_trk_soundprefs_enabled(controller->prefs, i) !=
            controller->opened[i]) {
            return 1;
        }
    }
    return tiku_trk_soundprefs_volume(controller->prefs) !=
           controller->opened_volume;
}

/** @brief Put every setting back to what the window opened on. */
static void
revert(tiku_trk_sounds_window_t *controller)
{
    int i;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        tiku_trk_soundprefs_set_enabled(controller->prefs, i,
                                        controller->opened[i]);
    }
    tiku_trk_prefs_set(controller->prefs, "sound.volume",
                       controller->opened_volume);
}

/** @brief Save the settings, push them into the kit, and redraw. */
static void
apply(tiku_trk_sounds_window_t *controller)
{
    if (controller->path[0] != '\0') {
        (void)tiku_trk_prefs_save(controller->prefs, controller->path);
    }
    tiku_trk_soundprefs_apply(controller->prefs);
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

/**
 * @brief Sound @p event whatever its row says: a person pressing Test
 *        on a moment that is off is asking what they would be missing.
 */
static void
test(tiku_sound_event_t event)
{
    int on = tiku_sound_enabled(event);

    tiku_sound_set_enabled(event, 1);
    tiku_sound_play(event);
    tiku_sound_set_enabled(event, on);
}

/**
 * @brief Draw the table of moments, the volume stepper and the buttons.
 */
static void
draw(tiku_window_t *window, tiku_surface_t *surface, tiku_rect_t content,
     void *context)
{
    tiku_trk_sounds_window_t *controller = context;
    const tiku_font_t *font = tiku_font_plain();
    const tiku_font_t *bold = tiku_font_bold();
    char number[32], key[48];
    int volume = tiku_trk_soundprefs_volume(controller->prefs);
    int i;

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    tiku_text(surface, bold, content.x + PAD, content.y + bold->ascent + 10,
              tiku_str("prefs.sounds.event"), TIKU_C_TEXT);
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        snprintf(key, sizeof key, "sound.%s", tiku_sound_name(i));
        tiku_ui_checkbox(surface, check_rect(content, i), tiku_str(key),
                         tiku_trk_soundprefs_enabled(controller->prefs, i)
                             ? TIKU_S_ON : 0u);
        tiku_ui_button(surface, test_rect(content, i),
                       tiku_str("button.test"), 0u);
    }
    {
        tiku_rect_t minus = minus_rect(content);
        tiku_rect_t plus = plus_rect(content);
        tiku_rect_t shown = { minus.x + minus.w + 4, minus.y, 48, minus.h };

        tiku_text(surface, bold, content.x + PAD,
                  minus.y + (minus.h - bold->height) / 2 + bold->ascent,
                  tiku_str("prefs.sounds.volume"), TIKU_C_TEXT);
        tiku_ui_button(surface, minus, "-", volume > 0 ? 0u : TIKU_S_DISABLED);
        tiku_ui_button(surface, plus, "+",
                       volume < 100 ? 0u : TIKU_S_DISABLED);
        snprintf(number, sizeof number, "%d %%", volume);
        (void)tiku_text_centered(surface, font, shown, number, TIKU_C_TEXT);
    }
    tiku_ui_button(surface, defaults_rect(content), tiku_str("button.defaults"),
                   tiku_trk_soundprefs_defaultable(controller->prefs)
                       ? 0u : TIKU_S_DISABLED);
    tiku_ui_button(surface, revert_rect(content), tiku_str("button.revert"),
                   revertable(controller) ? 0u : TIKU_S_DISABLED);
}

/**
 * @brief A press toggles a moment, tests one, steps the volume or presses
 *        a button, and what it changed is applied.  @return 1 when taken.
 */
static int
event(tiku_window_t *window, const tiku_event_t *ev, void *context)
{
    tiku_trk_sounds_window_t *controller = context;
    tiku_rect_t content = tiku_window_content(window);
    int i;

    if (ev->type != TIKU_EVENT_POINTER_DOWN ||
        !inside(content, ev->x, ev->y)) {
        return 0;
    }
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        if (inside(check_rect(content, i), ev->x, ev->y)) {
            tiku_trk_soundprefs_set_enabled(
                controller->prefs, i,
                !tiku_trk_soundprefs_enabled(controller->prefs, i));
            apply(controller);
            return 1;
        }
        if (inside(test_rect(content, i), ev->x, ev->y)) {
            test(i);
            return 1;
        }
    }
    if (inside(minus_rect(content), ev->x, ev->y) ||
        inside(plus_rect(content), ev->x, ev->y)) {
        if (tiku_trk_soundprefs_step_volume(
                controller->prefs, inside(plus_rect(content), ev->x, ev->y))) {
            apply(controller);
        }
        return 1;
    }
    if (inside(defaults_rect(content), ev->x, ev->y) &&
        tiku_trk_soundprefs_defaultable(controller->prefs)) {
        tiku_trk_soundprefs_defaults(controller->prefs);
        apply(controller);
        return 1;
    }
    if (inside(revert_rect(content), ev->x, ev->y) && revertable(controller)) {
        revert(controller);
        apply(controller);
        return 1;
    }
    return 0;
}

void
tiku_trk_sounds_window_init(tiku_trk_sounds_window_t *controller,
                            tiku_workspace_t *workspace,
                            tiku_trk_prefs_t *prefs, const char *path,
                            int screen_w, int screen_h, int *dirty)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->prefs = prefs;
    if (path != NULL) {
        snprintf(controller->path, sizeof controller->path, "%s", path);
    }
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
}

void
tiku_trk_sounds_window_close(tiku_trk_sounds_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_close(controller->workspace, controller->window);
    }
    controller->window = NULL;
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

void
tiku_trk_sounds_window_destroy(tiku_trk_sounds_window_t *controller)
{
    tiku_trk_sounds_window_close(controller);
}

void
tiku_trk_sounds_window_open(tiku_trk_sounds_window_t *controller)
{
    tiku_rect_t frame;
    int i;

    if (controller->window != NULL && controller->window->open) {
        tiku_workspace_activate(controller->workspace, controller->window);
        return;
    }
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        controller->opened[i] =
            tiku_trk_soundprefs_enabled(controller->prefs, i);
    }
    controller->opened_volume = tiku_trk_soundprefs_volume(controller->prefs);
    frame.w = TIKU_TRK_SOUNDS_W;
    frame.h = window_height();
    frame.x = (controller->screen_w - frame.w) / 2;
    frame.y = (controller->screen_h - frame.h) / 3;
    controller->window = tiku_workspace_open(controller->workspace,
        tiku_str("title.sounds"), frame, draw, controller, NULL);
    if (controller->window != NULL) {
        tiku_window_set_size_controls(controller->window, 0, 0);
        tiku_window_set_handlers(controller->window, event, NULL, NULL);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

tiku_window_t *
tiku_trk_sounds_window_handle(tiku_trk_sounds_window_t *controller)
{
    return controller->window;
}
