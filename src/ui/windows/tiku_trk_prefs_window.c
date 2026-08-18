/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_prefs_window.c - preferences window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_desk_ui.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_prefs_window.h"

/** @brief Draw the preferences model owned by this controller. */
static void
draw(tiku_desk_window_t *window, tiku_desk_surface_t *surface,
     tiku_desk_rect_t content, void *context)
{
    tiku_trk_prefs_window_t *controller = context;
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    int index[32], count, i, x, y, width, height;

    (void)window;
    tiku_desk_fill(surface, content, TIKU_DESK_C_PANEL);
    for (i = 0; i < TIKU_TRK_PAGES; i++) {
        tiku_desk_rect_t rect;
        int selected = (i == (int)controller->page);

        tiku_trk_prefs_page_rect(i, &x, &y, &width, &height);
        rect.x = content.x + x;
        rect.y = content.y + y;
        rect.w = width;
        rect.h = height;
        tiku_desk_fill(surface, rect, selected ? TIKU_DESK_C_SELECT
                                               : TIKU_DESK_C_PANEL);
        {
            const char *name =
                tiku_trk_prefs_page_name((tiku_trk_page_t)i);
            unsigned ink = selected ? TIKU_DESK_C_SELTEXT
                                    : TIKU_DESK_C_TEXT;

            tiku_desk_text(surface, font, rect.x + 6, rect.y + 15, name,
                           ink);
            if (tiku_trk_prefs_revertable(&controller->model,
                                          (tiku_trk_page_t)i)) {
                /* Bold from one face: the same string one pixel over,
                 * the trick the label halo already uses (TS-060). */
                tiku_desk_text(surface, font, rect.x + 7, rect.y + 15,
                               name, ink);
            }
        }
    }
    count = tiku_trk_prefs_page_controls(&controller->model,
                                          controller->page, index, 32);
    for (i = 0; i < count; i++) {
        tiku_trk_pref_t *pref = &controller->model.pref[index[i]];
        tiku_desk_rect_t box;

        tiku_trk_prefs_control_rect(i, &x, &y, &width, &height);
        box.x = content.x + x;
        box.y = content.y + y + 4;
        box.w = 12;
        box.h = 12;
        if (pref->type == TIKU_TRK_PREF_BOOL) {
            tiku_desk_fill(surface, box, TIKU_DESK_C_DOC);
            tiku_desk_frame(surface, box, TIKU_DESK_C_TEXT);
            if (pref->value) {
                tiku_desk_text(surface, font, box.x + 2, box.y + 11, "x",
                               TIKU_DESK_C_TEXT);
            }
        } else if (pref->type == TIKU_TRK_PREF_COLOUR) {
            /* The swatch IS the control: it shows the colour and a click
             * walks to the next one. */
            tiku_desk_fill(surface, box,
                           (tiku_desk_rgb_t)pref->value & 0xffffffu);
            tiku_desk_frame(surface, box, TIKU_DESK_C_TEXT);
        }
        tiku_desk_text(surface, font, content.x + x + 18,
                       content.y + y + 14, pref->name, TIKU_DESK_C_TEXT);
    }
    for (i = 0; i < 2; i++) {
        tiku_desk_rect_t button;
        int enabled = i ? tiku_trk_prefs_any_revertable(&controller->model)
                        : tiku_trk_prefs_any_defaultable(&controller->model);

        tiku_trk_prefs_button_rect(i, &x, &y, &width, &height);
        button.x = content.x + x;
        button.y = content.y + y;
        button.w = width;
        button.h = height;
        tiku_desk_ui_button(surface, button, i ? "Revert" : "Defaults",
                            enabled ? 0u : TIKU_DESK_S_DISABLED);
    }
}

/** @brief Apply one pointer action and persist it immediately. */
static int
event(tiku_desk_window_t *window, const tiku_desk_event_t *event,
      void *context)
{
    tiku_trk_prefs_window_t *controller = context;
    tiku_desk_rect_t content;
    tiku_trk_pref_hit_t hit;
    int which = -1;

    if (event->type != TIKU_DESK_EVENT_POINTER_DOWN ||
        (controller->blocked != NULL && *controller->blocked)) {
        return 0;
    }
    content = tiku_desk_window_content(window);
    hit = tiku_trk_prefs_hit(&controller->model, controller->page,
                             event->x - content.x, event->y - content.y,
                             &which);
    if (hit == TIKU_TRK_PREF_HIT_NONE) {
        return 0;
    }
    if (hit == TIKU_TRK_PREF_HIT_PAGE) {
        controller->page = (tiku_trk_page_t)which;
    } else if (hit == TIKU_TRK_PREF_HIT_SETTING) {
        tiku_trk_pref_t *pref = &controller->model.pref[which];

        if (pref->type == TIKU_TRK_PREF_BOOL) {
            pref->value = !pref->value;
        } else if (pref->type == TIKU_TRK_PREF_COLOUR) {
            pref->value = tiku_trk_prefs_colour_next(pref->value);
        }
    } else {
        int page;

        for (page = 0; page < TIKU_TRK_PAGES; page++) {
            if (hit == TIKU_TRK_PREF_HIT_DEFAULTS) {
                tiku_trk_prefs_defaults(&controller->model,
                                        (tiku_trk_page_t)page);
            } else {
                tiku_trk_prefs_revert(&controller->model,
                                      (tiku_trk_page_t)page);
            }
        }
    }
    if (controller->path[0] != '\0') {
        (void)tiku_trk_prefs_save(&controller->model, controller->path);
    }
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
    return 1;
}

void
tiku_trk_prefs_window_init(tiku_trk_prefs_window_t *controller,
    tiku_desk_workspace_t *workspace, int screen_w, int screen_h,
    const char *settings_path, int *dirty, const int *blocked)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    controller->blocked = blocked;
    controller->page = TIKU_TRK_PAGE_DESKTOP;
    tiku_trk_prefs_init(&controller->model);
    if (settings_path != NULL && settings_path[0] != '\0') {
        snprintf(controller->path, sizeof controller->path, "%s",
                 settings_path);
        (void)tiku_trk_prefs_load(&controller->model, controller->path);
    }
}

void
tiku_trk_prefs_window_destroy(tiku_trk_prefs_window_t *controller)
{
    if (controller->window != NULL && controller->window->open) {
        tiku_desk_workspace_close(controller->workspace, controller->window);
    }
    controller->window = NULL;
    tiku_trk_prefs_free(&controller->model);
}

tiku_trk_prefs_t *
tiku_trk_prefs_window_model(tiku_trk_prefs_window_t *controller)
{
    return &controller->model;
}

tiku_desk_window_t *
tiku_trk_prefs_window_handle(tiku_trk_prefs_window_t *controller)
{
    return controller->window;
}

void
tiku_trk_prefs_window_open(tiku_trk_prefs_window_t *controller)
{
    tiku_desk_rect_t frame;

    if (controller->window != NULL && controller->window->open) {
        tiku_desk_workspace_activate(controller->workspace,
                                     controller->window);
        return;
    }
    frame.x = (controller->screen_w - TIKU_TRK_PREF_W) / 2;
    frame.y = (controller->screen_h - TIKU_TRK_PREF_H) / 3;
    frame.w = TIKU_TRK_PREF_W;
    frame.h = TIKU_TRK_PREF_H + 26;
    tiku_trk_prefs_mark_opened(&controller->model);
    controller->window = tiku_desk_workspace_open(controller->workspace,
        "Tracker preferences", frame, draw, controller, NULL);
    if (controller->window != NULL) {
        tiku_desk_window_set_handlers(controller->window, event, NULL, NULL);
    }
}

void
tiku_trk_prefs_window_close(tiku_trk_prefs_window_t *controller)
{
    if (controller == NULL || controller->window == NULL ||
        !controller->window->open) {
        return;
    }
    tiku_desk_workspace_close(controller->workspace, controller->window);
    controller->window = NULL;
}
