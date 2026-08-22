/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_find_window.c - Find window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tiku_trk_find_window.h"
#include "tiku_iconpaint.h"

#define FIND_WIDTH 560

static void
mark_dirty(tiku_trk_find_window_t *controller)
{
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

static void
set_status(tiku_trk_find_window_t *controller, const char *text)
{
    if (controller->status != NULL && controller->status_size > 0u) {
        snprintf(controller->status, controller->status_size, "%s",
                 text != NULL ? text : "");
    }
}

static void
draw(tiku_window_t *window, tiku_surface_t *surface,
     tiku_rect_t content, void *context)
{
    tiku_trk_find_window_t *controller = context;

    tiku_rect_t ir;

    controller->model.has_keyboard =
        (window != NULL &&
         window == tiku_workspace_focused(controller->workspace));
    tiku_trk_find_draw(&controller->model, surface, content);
    /* The draggable query icon (Q-034): picked up, it carries the search
     * to whatever folder it is dropped on. */
    ir = tiku_trk_find_icon_rect(&controller->model, content);
    if (ir.w > 0) {
        (void)tiku_icon_paint(surface, "query", ir.x, ir.y, 32);
    }
}

int
tiku_trk_find_window_icon_at(tiku_trk_find_window_t *controller,
                             int x, int y)
{
    tiku_rect_t ir;

    if (controller == NULL || controller->window == NULL) {
        return 0;
    }
    ir = tiku_trk_find_icon_rect(&controller->model,
        tiku_window_content(controller->window));
    return ir.w > 0 && x >= ir.x && x < ir.x + ir.w &&
           y >= ir.y && y < ir.y + ir.h;
}

static void
sync_window(tiku_trk_find_window_t *controller)
{
    const char *note;

    if (controller->window == NULL) {
        return;
    }
    snprintf(controller->window->title, sizeof controller->window->title,
        "%s", tiku_trk_find_editing_template(&controller->model)
                    ? "Edit Query template" : "Find");
    controller->window->frame.h = tiku_trk_find_height(&controller->model) +
        tiku_window_tab_h() + 2 * TIKU_WINDOW_BORDER;
    note = tiku_trk_find_note(&controller->model);
    if (note[0] != '\0') {
        set_status(controller, note);
        tiku_trk_find_clear_note(&controller->model);
    }
    mark_dirty(controller);
}

static void
save(tiku_trk_find_window_t *controller)
{
    if (tiku_trk_find_save_current(&controller->model,
                                   (int64_t)time(NULL)) != 0) {
        /* Nothing attached to save INTO: the question becomes WHERE, and
         * that question is the save panel's (AW-052).  The old fallback
         * -- a silent store record under an invented name -- remains only
         * for a shell that wired no panel. */
        if (controller->save_as != NULL) {
            controller->save_as(controller->save_as_context);
            return;
        }
        {
        tiku_query_t query;
        char name[TIKU_NAME_MAX];

        tiku_trk_find_default_name(&controller->model, (long)time(NULL),
                                   name, sizeof name);
        tiku_trk_find_build(&controller->model, &query);
        if (tiku_query_save(&query, name, controller->store) == 0) {
            char message[96];

            snprintf(message, sizeof message, "saved as \"%.60s\"", name);
            set_status(controller, message);
        } else {
            set_status(controller, "could not save the query");
        }
        }
    } else {
        set_status(controller, tiku_trk_find_note(&controller->model));
    }
}

static void
primary(tiku_trk_find_window_t *controller)
{
    int done;

    if (tiku_trk_find_primary_is_save(&controller->model)) {
        done = tiku_trk_find_save_current(&controller->model,
                                           (int64_t)time(NULL)) == 0;
        set_status(controller, done
            ? tiku_trk_find_note(&controller->model)
            : "that template cannot be saved");
    } else {
        done = controller->search != NULL &&
            controller->search(controller->search_context,
                               &controller->model);
    }
    if (done) {
        tiku_trk_find_window_close(controller);
    }
}

static int
event(tiku_window_t *window, const tiku_event_t *input,
      void *context)
{
    tiku_trk_find_window_t *controller = context;
    tiku_trk_find_hit_t hit;

    if (input->type != TIKU_EVENT_POINTER_DOWN) {
        return 0;
    }
    hit = tiku_trk_find_click(&controller->model,
        tiku_window_content(window), input->x, input->y);
    if (hit == TIKU_TRK_FIND_HIT_SEARCH) {
        primary(controller);
    } else if (hit == TIKU_TRK_FIND_HIT_SAVE) {
        save(controller);
    }
    if (tiku_trk_find_take_folder_request(&controller->model) &&
        controller->select_folder != NULL) {
        controller->select_folder(controller->save_as_context);
    }
    sync_window(controller);
    return 1;
}

void
tiku_trk_find_window_init(tiku_trk_find_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty, char *status, size_t status_size,
    tiku_trk_find_window_search_fn search, void *search_context)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    controller->status = status;
    controller->status_size = status_size;
    controller->search = search;
    controller->search_context = search_context;
    tiku_trk_find_init(&controller->model);
}

void
tiku_trk_find_window_set_store(tiku_trk_find_window_t *controller,
                               tiku_store_t *store)
{
    controller->store = store;
    tiku_trk_find_set_store(&controller->model, store);
}

void
tiku_trk_find_window_set_types(tiku_trk_find_window_t *controller,
                               const tiku_trk_types_t *types)
{
    tiku_trk_find_set_types(&controller->model, types);
}

tiku_trk_find_t *
tiku_trk_find_window_model(tiku_trk_find_window_t *controller)
{
    return &controller->model;
}

tiku_window_t *
tiku_trk_find_window_handle(tiku_trk_find_window_t *controller)
{
    return controller->window;
}

void
tiku_trk_find_window_open(tiku_trk_find_window_t *controller)
{
    tiku_rect_t frame;

    if (controller->window != NULL && controller->window->open) {
        sync_window(controller);
        tiku_workspace_activate(controller->workspace,
                                     controller->window);
        mark_dirty(controller);
        return;
    }
    frame.w = FIND_WIDTH;
    frame.h = tiku_trk_find_height(&controller->model) +
        tiku_window_tab_h() + 2 * TIKU_WINDOW_BORDER;
    frame.x = (controller->screen_w - frame.w) / 2;
    frame.y = (controller->screen_h - frame.h) / 2;
    controller->window = tiku_workspace_open(controller->workspace,
        "Find", frame, draw, controller, &controller->model);
    if (controller->window != NULL) {
        tiku_window_set_handlers(controller->window, event, NULL, NULL);
        tiku_window_set_size_controls(controller->window, 0, 0);
    }
    sync_window(controller);
}

void
tiku_trk_find_window_close(tiku_trk_find_window_t *controller)
{
    if (controller->window != NULL) {
        tiku_workspace_close(controller->workspace,
                                  controller->window);
        controller->window = NULL;
        mark_dirty(controller);
    }
}

int
tiku_trk_find_window_key(tiku_trk_find_window_t *controller, unsigned key,
                         char character)
{
    if (controller->window == NULL || !controller->window->open ||
        tiku_workspace_focused(controller->workspace) !=
            controller->window) {
        return 0;
    }
    if (key == TIKU_KEY_RETURN) {
        primary(controller);
    } else if (key == TIKU_KEY_ESCAPE) {
        tiku_trk_find_window_close(controller);
    } else if (key == TIKU_KEY_BACKSPACE) {
        tiku_trk_find_backspace(&controller->model);
    } else if (character != '\0') {
        tiku_trk_find_key(&controller->model, character);
    }
    mark_dirty(controller);
    return 1;
}

int
tiku_trk_find_window_popup_open(
    const tiku_trk_find_window_t *controller)
{
    return controller->window != NULL && controller->window->open &&
        tiku_trk_find_popup_open(&controller->model);
}
