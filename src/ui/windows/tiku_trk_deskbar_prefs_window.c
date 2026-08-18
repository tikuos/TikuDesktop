/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_prefs_window.c - Deskbar preferences controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_deskbar_prefs_window.h"

enum {
    ROW_SORT = 0, ROW_TRACKER, ROW_EXPANDER, ROW_EXPAND_NEW,
    ROW_HIDE_NAMES, ROW_ICON, ROW_FONT, ROW_DOCS, ROW_FOLDERS, ROW_APPS,
    ROW_ALWAYS_TOP, ROW_AUTO_RAISE, ROW_AUTO_HIDE, ROW_COUNT
};

static const char *const labels[ROW_COUNT] = {
    "Sort applications by name", "Tracker always first",
    "Show application expander", "Expand new applications",
    "Hide application names", "Icon size", "Font size", "Recent documents",
    "Recent folders", "Recent applications", "Always on top",
    "Auto-raise", "Auto-hide"
};

static int
inside(tiku_desk_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void
button(tiku_desk_surface_t *surface, tiku_desk_rect_t rect,
       const char *label, int enabled)
{
    const tiku_desk_font_t *font = tiku_desk_font_plain();

    tiku_desk_fill(surface, rect, TIKU_DESK_C_PANEL);
    tiku_desk_bevel(surface, rect,
        tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_2),
        tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
    (void)tiku_desk_text_centered(surface, font, rect, label,
        enabled ? TIKU_DESK_C_TEXT
                : tiku_desk_tint(TIKU_DESK_C_PANEL, 1.45f));
}

static int
row_enabled(const tiku_trk_deskbar_prefs_window_t *controller, int row)
{
    if (row == ROW_EXPANDER || row == ROW_EXPAND_NEW) {
        return tiku_trk_deskbar_prefs_expanders_enabled(&controller->model,
                   tiku_trk_deskbar_dock(controller->deskbar));
    }
    if (row == ROW_AUTO_RAISE) {
        return tiku_trk_deskbar_prefs_auto_raise_enabled(&controller->model);
    }
    return 1;
}

static int *
row_value(tiku_trk_deskbar_settings_t *s, int row)
{
    switch (row) {
    case ROW_SORT:        return &s->sort_apps;
    case ROW_TRACKER:     return &s->tracker_first;
    case ROW_EXPANDER:    return &s->show_expander;
    case ROW_EXPAND_NEW:  return &s->expand_new_apps;
    case ROW_HIDE_NAMES:  return &s->hide_app_names;
    case ROW_ALWAYS_TOP:  return &s->always_on_top;
    case ROW_AUTO_RAISE:  return &s->auto_raise;
    case ROW_AUTO_HIDE:   return &s->auto_hide;
    default:              return NULL;
    }
}

static void
apply(tiku_trk_deskbar_prefs_window_t *controller)
{
    tiku_trk_deskbar_settings_normalize(&controller->model.value);
    tiku_trk_deskbar_apply_settings(controller->deskbar,
                                    &controller->model.value);
    (void)tiku_trk_deskbar_prefs_save(&controller->model,
                                      controller->store);
    (void)tiku_trk_store_flush(controller->store);
    if (controller->dirty != NULL) { *controller->dirty = 1; }
}

static void
draw(tiku_desk_window_t *window, tiku_desk_surface_t *surface,
     tiku_desk_rect_t content, void *context)
{
    tiku_trk_deskbar_prefs_window_t *controller = context;
    tiku_trk_deskbar_settings_t *s = &controller->model.value;
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    int row;

    (void)window;
    tiku_desk_fill(surface, content, TIKU_DESK_C_PANEL);
    for (row = 0; row < ROW_COUNT; row++) {
        int y = content.y + 12 + row * 29;
        int enabled = row_enabled(controller, row);
        int *value = row_value(s, row);
        char number[24];

        if (value != NULL) {
            tiku_desk_rect_t box = { content.x + 18, y, 13, 13 };

            tiku_desk_fill(surface, box, TIKU_DESK_C_DOC);
            tiku_desk_frame(surface, box, TIKU_DESK_C_TEXT);
            if (*value) {
                tiku_desk_text(surface, font, box.x + 2, box.y + 11, "x",
                               TIKU_DESK_C_TEXT);
            }
        }
        tiku_desk_text(surface, font, content.x + 40, y + 12, labels[row],
            enabled ? TIKU_DESK_C_TEXT
                    : tiku_desk_tint(TIKU_DESK_C_PANEL, 1.45f));
        if (row == ROW_ICON) {
            snprintf(number, sizeof number, "-   %d px   +", s->icon_size);
        } else if (row == ROW_FONT) {
            snprintf(number, sizeof number, "-   %d px   +", s->font_size);
        } else if (row >= ROW_DOCS && row <= ROW_APPS) {
            int which = row - ROW_DOCS;
            tiku_desk_rect_t box = { content.x + 18, y, 13, 13 };

            tiku_desk_fill(surface, box, TIKU_DESK_C_DOC);
            tiku_desk_frame(surface, box, TIKU_DESK_C_TEXT);
            if (s->recent_enabled[which]) {
                tiku_desk_text(surface, font, box.x + 2, box.y + 11, "x",
                               TIKU_DESK_C_TEXT);
            }
            snprintf(number, sizeof number, "-   %d   +",
                     s->recent_count[which]);
        } else {
            number[0] = '\0';
        }
        if (number[0] != '\0') {
            tiku_desk_text(surface, font, content.x + 330, y + 12, number,
                           TIKU_DESK_C_TEXT);
        }
    }
    {
        tiku_desk_rect_t monitor = { content.x + 292, content.y + 392,
                                     116, 82 };
        tiku_trk_db_dock_t dock = tiku_trk_deskbar_dock(controller->deskbar);
        char current[32];

        tiku_desk_fill(surface, monitor, TIKU_DESK_C_DOC);
        tiku_desk_frame(surface, monitor, TIKU_DESK_C_TEXT);
        snprintf(current, sizeof current, "Dock: %d", (int)dock);
        tiku_desk_text(surface, font, monitor.x + 29, monitor.y + 45,
                       current, TIKU_DESK_C_TEXT);
        tiku_desk_text(surface, font, content.x + 18, content.y + 432,
                       "Dock location", TIKU_DESK_C_TEXT);
    }
    button(surface, (tiku_desk_rect_t){ content.x + 18, content.y + 488,
           112, 26 }, "Defaults",
           tiku_trk_deskbar_prefs_defaultable(&controller->model));
    button(surface, (tiku_desk_rect_t){ content.x + 140, content.y + 488,
           100, 26 }, "Revert",
           tiku_trk_deskbar_prefs_revertable(&controller->model));
    button(surface, (tiku_desk_rect_t){ content.x + 278, content.y + 488,
           142, 26 }, "Edit in Tracker...", 1);
}

static int
event(tiku_desk_window_t *window, const tiku_desk_event_t *event,
      void *context)
{
    tiku_trk_deskbar_prefs_window_t *controller = context;
    tiku_desk_rect_t content = tiku_desk_window_content(window);
    int x = event->x - content.x, y = event->y - content.y;
    int row = (y - 12) / 29;

    if (event->type != TIKU_DESK_EVENT_POINTER_DOWN ||
        !inside(content, event->x, event->y)) {
        return 0;
    }
    if (row >= 0 && row < ROW_COUNT && y >= 12 && y < 389) {
        tiku_trk_deskbar_settings_t *s = &controller->model.value;
        int *value = row_value(s, row);

        if (!row_enabled(controller, row)) { return 1; }
        if (row == ROW_ICON) {
            s->icon_size += (x >= 360) ? 8 : -8;
        } else if (row == ROW_FONT) {
            s->font_size += (x >= 360) ? 2 : -2;
        } else if (row >= ROW_DOCS && row <= ROW_APPS) {
            int which = row - ROW_DOCS;

            if (x < 320) {
                s->recent_enabled[which] = !s->recent_enabled[which];
            } else {
                s->recent_count[which] += (x >= 380) ? 1 : -1;
            }
        } else if (value != NULL) {
            *value = !*value;
        }
        apply(controller);
        return 1;
    }
    if (inside((tiku_desk_rect_t){ 292, 392, 116, 82 }, x, y)) {
        tiku_trk_db_dock_t dock = tiku_trk_deskbar_prefs_dock_at(
            x - 292, y - 392, 116, 82,
            tiku_trk_deskbar_dock(controller->deskbar));

        tiku_trk_deskbar_set_dock(controller->deskbar, dock);
        if (controller->dirty != NULL) { *controller->dirty = 1; }
        return 1;
    }
    if (inside((tiku_desk_rect_t){ 18, 488, 112, 26 }, x, y) &&
        tiku_trk_deskbar_prefs_defaultable(&controller->model)) {
        tiku_trk_deskbar_prefs_defaults(&controller->model);
        apply(controller);
        return 1;
    }
    if (inside((tiku_desk_rect_t){ 140, 488, 100, 26 }, x, y) &&
        tiku_trk_deskbar_prefs_revertable(&controller->model)) {
        tiku_trk_deskbar_prefs_revert(&controller->model);
        apply(controller);
        return 1;
    }
    if (inside((tiku_desk_rect_t){ 278, 488, 142, 26 }, x, y)) {
        if (controller->open_path != NULL && controller->menu_dir[0] != '\0') {
            controller->open_path(controller->open_context,
                                  controller->menu_dir);
        }
        return 1;
    }
    return 0;
}

static tiku_desk_rect_t
saved_frame(tiku_trk_deskbar_prefs_window_t *controller)
{
    int32_t position[2];
    int found;

    found = tiku_trk_state_read(controller->store, TIKU_TRK_KEY_SESSION,
            TIKU_TRK_ATTR_DESKBAR_PREFS_FRAME, position, sizeof position) ==
            (int)sizeof position;
    return tiku_trk_deskbar_prefs_frame(found,
        found ? position[0] : 0, found ? position[1] : 0,
        controller->screen_w, controller->screen_h,
        TIKU_TRK_DB_PREFS_W, TIKU_TRK_DB_PREFS_H);
}

void
tiku_trk_deskbar_prefs_window_init(
    tiku_trk_deskbar_prefs_window_t *controller,
    tiku_desk_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    tiku_trk_store_t *store, int screen_w, int screen_h, const char *menu_dir,
    int *dirty, tiku_trk_deskbar_prefs_open_fn open_path, void *open_context)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->deskbar = deskbar;
    controller->store = store;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    controller->open_path = open_path;
    controller->open_context = open_context;
    snprintf(controller->menu_dir, sizeof controller->menu_dir, "%s",
             menu_dir != NULL ? menu_dir : "");
    tiku_trk_deskbar_prefs_init(&controller->model);
    (void)tiku_trk_deskbar_prefs_load(&controller->model, store);
    tiku_trk_deskbar_apply_settings(deskbar, &controller->model.value);
}

void
tiku_trk_deskbar_prefs_window_close(
    tiku_trk_deskbar_prefs_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        int32_t position[2] = { controller->window->frame.x,
                                controller->window->frame.y };

        (void)tiku_trk_state_write(controller->store, TIKU_TRK_KEY_SESSION,
            TIKU_TRK_ATTR_DESKBAR_PREFS_FRAME, position, sizeof position);
        (void)tiku_trk_store_flush(controller->store);
        tiku_desk_workspace_close(controller->workspace, controller->window);
    }
    controller->window = NULL;
    if (controller->dirty != NULL) { *controller->dirty = 1; }
}

void
tiku_trk_deskbar_prefs_window_destroy(
    tiku_trk_deskbar_prefs_window_t *controller)
{
    tiku_trk_deskbar_prefs_window_close(controller);
}

void
tiku_trk_deskbar_prefs_window_open(
    tiku_trk_deskbar_prefs_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        tiku_desk_workspace_activate(controller->workspace,
                                     controller->window);
        return;
    }
    tiku_trk_deskbar_prefs_mark_opened(&controller->model);
    controller->window = tiku_desk_workspace_open(controller->workspace,
        "Deskbar preferences", saved_frame(controller), draw, controller,
        NULL);
    if (controller->window != NULL) {
        tiku_desk_window_set_size_controls(controller->window, 0, 0);
        tiku_desk_window_set_handlers(controller->window, event, NULL, NULL);
    }
    if (controller->dirty != NULL) { *controller->dirty = 1; }
}

tiku_desk_window_t *
tiku_trk_deskbar_prefs_window_handle(
    tiku_trk_deskbar_prefs_window_t *controller)
{
    return controller->window;
}

int
tiku_trk_deskbar_prefs_window_key(
    tiku_trk_deskbar_prefs_window_t *controller, const char *key)
{
    tiku_trk_db_dock_t dock;

    if (controller == NULL || controller->window == NULL ||
        !controller->window->open) {
        return 0;
    }
    dock = tiku_trk_deskbar_dock(controller->deskbar);
    if (!tiku_trk_deskbar_prefs_dock_key(key, &dock)) { return 0; }
    tiku_trk_deskbar_set_dock(controller->deskbar, dock);
    if (controller->dirty != NULL) { *controller->dirty = 1; }
    return 1;
}
