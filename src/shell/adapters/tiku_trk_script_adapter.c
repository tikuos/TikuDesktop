/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script_adapter.c - script observations of desktop presentation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "tiku_trk_script_adapter.h"
#include "tiku_trk_task.h"

#define COUNT_HEIGHT 16

void
tiku_trk_script_adapter_init(tiku_trk_script_adapter_t *adapter,
    tiku_workspace_t *workspace, tiku_surface_t *surface,
    tiku_trk_folder_lifecycle_t *folders,
    tiku_trk_desktop_view_t *desktop_view, tiku_trk_deskbar_t *deskbar,
    tiku_trk_alert_t *alert, tiku_trk_find_window_t *find_window,
    tiku_trk_panel_window_t *panel_window,
    tiku_trk_folder_edit_t *folder_edit, tiku_menu_t **popup,
    tiku_menu_t **popup_sub, tiku_rect_t *popup_frame,
    tiku_rect_t *popup_sub_frame, char *status, char *count_text,
    int row_h)
{
    *adapter = (tiku_trk_script_adapter_t){
        .workspace = workspace,
        .surface = surface,
        .folders = folders,
        .desktop_view = desktop_view,
        .deskbar = deskbar,
        .alert = alert,
        .find_window = find_window,
        .panel_window = panel_window,
        .folder_edit = folder_edit,
        .popup = popup,
        .popup_sub = popup_sub,
        .popup_frame = popup_frame,
        .popup_sub_frame = popup_sub_frame,
        .status = status,
        .count_text = count_text,
        .row_h = row_h
    };
}

int
tiku_trk_script_adapter_tab_point(void *context, const char *title, int *x,
                                  int *y)
{
    tiku_trk_script_adapter_t *adapter = context;
    int i;

    if (adapter == NULL || title == NULL || x == NULL || y == NULL) {
        return 0;
    }
    for (i = 0; i < tiku_workspace_count(adapter->workspace); i++) {
        tiku_window_t *w = tiku_workspace_at(adapter->workspace,
                                                       i);

        if (w == NULL || !w->open) {
            continue;
        }
        if (strstr(w->title, title) == NULL) {
            /* Titles are the folder's NAME now (CW-016); a script that
             * still addresses a window by path finds it by the leaf. */
            const char *leaf = strrchr(title, '/');

            if (leaf == NULL || leaf[1] == '\0' ||
                strstr(w->title, leaf + 1) == NULL) {
                continue;
            }
        }
        /* Inside its tab: the strip is TAB_H deep from the frame's top
         * edge, and starts four pixels in.  Thirty across is inside the
         * narrowest tab the window layer will draw. */
        *x = w->frame.x + 30;
        *y = w->frame.y + tiku_window_tab_h() / 2;
        return 1;
    }
    return 0;
}

int
tiku_trk_script_adapter_row_point(void *context, int row, int *x, int *y)
{
    tiku_trk_script_adapter_t *adapter = context;
    tiku_window_t *focused;
    tiku_trk_folder_window_t *folder;
    tiku_rect_t content;
    int relative;
    int visible;

    if (adapter == NULL || x == NULL || y == NULL || row < 0) {
        return 0;
    }
    focused = tiku_workspace_focused(adapter->workspace);
    folder = tiku_trk_folder_lifecycle_find(adapter->folders, focused);
    if (folder == NULL || folder->view == NULL ||
        tiku_trk_poseview_mode(folder->view) != TIKU_TRK_MODE_LIST) {
        return 0;
    }
    content = tiku_trk_folder_window_content(folder);
    relative = row - tiku_trk_poseview_scroll(folder->view);
    visible = (content.h - COUNT_HEIGHT - 4) / adapter->row_h;
    if (relative < 0 || relative >= visible ||
        row >= tiku_trk_poseview_count(folder->view)) {
        return 0;
    }
    *x = content.x + 20;
    *y = content.y + 2 + relative * adapter->row_h + adapter->row_h / 2;
    return 1;
}

unsigned
tiku_trk_script_adapter_pixel_at(void *context, int x, int y)
{
    tiku_trk_script_adapter_t *adapter = context;

    if (adapter == NULL || adapter->surface == NULL ||
        adapter->surface->px == NULL || x < 0 || y < 0 ||
        x >= adapter->surface->w || y >= adapter->surface->h) {
        return 0u;
    }
    return (unsigned)tiku_peek(adapter->surface, x, y) & 0xffffffu;
}

int
tiku_trk_script_adapter_pose_at(void *context, const char *name,
                                int *x, int *y)
{
    tiku_trk_script_adapter_t *adapter = context;
    tiku_window_t *focused;
    tiku_trk_folder_window_t *folder;
    int i;
    int count;

    if (adapter == NULL || name == NULL) {
        return 0;
    }
    focused = tiku_workspace_focused(adapter->workspace);
    folder = tiku_trk_folder_lifecycle_find(adapter->folders, focused);
    for (;;) {
        if (folder != NULL && folder->view != NULL) {
            count = tiku_trk_poseview_count(folder->view);
            for (i = 0; i < count; i++) {
                tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);
                int width;
                int height;

                if (pose == NULL || strcmp(pose->model.name, name) != 0) {
                    continue;
                }
                tiku_trk_poseview_pose_rect(folder->view, pose, x, y,
                                            &width, &height);
                return 1;
            }
        }
        if (folder == tiku_trk_desktop_view_folder(adapter->desktop_view)) {
            return 0;
        }
        folder = tiku_trk_desktop_view_folder(adapter->desktop_view);
    }
}

/** @brief Like pose_at, but the DESKTOP only: what click-desk aims at. */
static int
tiku_trk_script_adapter_pose_desk_at(void *context, const char *name,
                                     int *x, int *y)
{
    tiku_trk_script_adapter_t *adapter = context;
    tiku_trk_folder_window_t *folder;
    int i, count, width, height;

    if (adapter == NULL || name == NULL) {
        return 0;
    }
    folder = tiku_trk_desktop_view_folder(adapter->desktop_view);
    if (folder == NULL || folder->view == NULL) {
        return 0;
    }
    count = tiku_trk_poseview_count(folder->view);
    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);

        /* By the name it SHOWS as well as the name it has: a synthetic
         * pose like Disks has no file name to be asked for. */
        if (pose != NULL &&
            (strcmp(pose->model.name, name) == 0 ||
             strcmp(tiku_model_display_name(&pose->model), name) == 0)) {
            tiku_trk_poseview_pose_rect(folder->view, pose, x, y,
                                        &width, &height);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_script_adapter_rows_drawn(void *context, int reset)
{
    tiku_trk_script_adapter_t *adapter = context;
    tiku_trk_folder_window_t *folder;
    int n;

    if (adapter == NULL) {
        return 0;
    }
    folder = tiku_trk_desktop_view_folder(adapter->desktop_view);
    if (folder == NULL || folder->render == NULL) {
        return 0;
    }
    n = folder->render->rows_drawn;
    if (reset) {
        folder->render->rows_drawn = 0;
    }
    return n;
}

int
tiku_trk_script_adapter_cell_point(void *context, int row, const char *title,
                                   int *x, int *y)
{
    tiku_trk_script_adapter_t *adapter = context;
    tiku_window_t *focused;
    tiku_trk_folder_window_t *folder;
    const tiku_trk_column_t *cols = NULL;
    tiku_rect_t content;
    int n, i, hs;

    if (adapter == NULL || title == NULL || x == NULL || y == NULL) {
        return 0;
    }
    /* The row answers where it sits; the column answers where it starts.
     * A test that worked either out by arithmetic would be encoding the
     * chrome all over again. */
    if (!tiku_trk_script_adapter_row_point(context, row, x, y)) {
        return 0;
    }
    focused = tiku_workspace_focused(adapter->workspace);
    folder = tiku_trk_folder_lifecycle_find(adapter->folders, focused);
    if (folder == NULL || folder->view == NULL) {
        return 0;
    }
    content = tiku_trk_folder_window_content(folder);
    hs = tiku_trk_poseview_hscroll(folder->view);
    n = tiku_trk_poseview_columns(folder->view, &cols);
    for (i = 0; i < n; i++) {
        if (cols[i].title == NULL || strstr(cols[i].title, title) == NULL) {
            continue;
        }
        *x = content.x + 2 + cols[i].offset - hs + cols[i].width / 2;
        return 1;
    }
    return 0;
}

int
tiku_trk_script_adapter_pose_screen(void *context, const char *name,
                                    int *x, int *y)
{
    tiku_trk_script_adapter_t *adapter = context;
    tiku_window_t *focused;
    tiku_trk_folder_window_t *folder;

    if (adapter == NULL || name == NULL || x == NULL || y == NULL) {
        return 0;
    }
    focused = tiku_workspace_focused(adapter->workspace);
    folder = tiku_trk_folder_lifecycle_find(adapter->folders, focused);
    for (;;) {
        int i, count = (folder != NULL && folder->view != NULL)
                           ? tiku_trk_poseview_count(folder->view) : 0;

        for (i = 0; i < count; i++) {
            tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);
            tiku_rect_t content;
            int ox = 0, oy = 0;

            if (pose == NULL || strcmp(pose->model.name, name) != 0) {
                continue;
            }
            /* The icon's own place, not the union with its name: a landing
             * is where the ICON went, and the union moves with the length
             * of the file name.  Converted the way a press is converted on
             * the way in, so a test can name a drop point instead of the
             * chrome. */
            tiku_trk_poseview_pose_location(folder->view, pose, x, y);
            content = tiku_trk_folder_window_content(folder);
            tiku_trk_poseview_origin(folder->view, &ox, &oy);
            *x += content.x + 2 - ox;
            *y += content.y + 2 - oy;
            return 1;
        }
        /* The desktop has no window to focus, and its icons are poses like
         * any other -- asking about one must not answer "nowhere". */
        if (folder == tiku_trk_desktop_view_folder(adapter->desktop_view)) {
            return 0;
        }
        folder = tiku_trk_desktop_view_folder(adapter->desktop_view);
    }
}

static void
append_menu_labels(tiku_trk_script_adapter_t *adapter, tiku_menu_t *menu)
{
    int i;

    if (menu == NULL) {
        return;
    }
    for (i = 0; i < menu->count; i++) {
        size_t used = strlen(adapter->menu_labels);

        snprintf(adapter->menu_labels + used,
                 sizeof adapter->menu_labels - used, "%s|",
                 menu->item[i].label);
    }
}

static void
selected_state(tiku_trk_folder_window_t *folder,
               tiku_trk_script_state_t *state, const char *find_type)
{
    int i;
    int count;

    if (folder == NULL || folder->view == NULL) {
        return;
    }
    count = tiku_trk_poseview_count(folder->view);
    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);

        if (pose != NULL) {
            state->kinds |= 2;
            if (!tiku_model_type_is(pose->model.type, find_type)) {
                state->kinds |= 1;
            }
        }
        if (pose != NULL && pose->selected) {
            if (state->sel_name == NULL) {
                state->sel_name = pose->model.name;
            }
            state->selected++;
        }
    }
}

static void
desktop_selection(tiku_trk_script_adapter_t *adapter,
                  tiku_trk_script_state_t *state)
{
    tiku_trk_folder_window_t *desktop =
        tiku_trk_desktop_view_folder(adapter->desktop_view);
    int i;
    int count;

    if (desktop == NULL || desktop->view == NULL) {
        return;
    }
    count = tiku_trk_poseview_count(desktop->view);
    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(desktop->view, i);

        if (pose != NULL && pose->selected) {
            if (state->desk_sel_name == NULL) {
                state->desk_sel_name = pose->model.name;
            }
            state->desk_selected++;
        }
    }
}

static void
fill_state(tiku_trk_script_adapter_t *adapter,
           tiku_trk_script_state_t *state)
{
    tiku_window_t *focused =
        tiku_workspace_focused(adapter->workspace);
    tiku_trk_folder_window_t *folder =
        tiku_trk_folder_lifecycle_find(adapter->folders, focused);
    tiku_trk_find_t *find =
        tiku_trk_find_window_model(adapter->find_window);
    tiku_rect_t bar = tiku_trk_deskbar_frame(adapter->deskbar);
    int i;

    memset(state, 0, sizeof *state);
    state->status = adapter->status;
    state->title = focused != NULL ? focused->title : NULL;
    state->alert = adapter->alert->open ? adapter->alert->text : NULL;
    for (i = 0; i < tiku_workspace_count(adapter->workspace); i++) {
        tiku_window_t *window =
            tiku_workspace_at(adapter->workspace, i);

        if (window != NULL && window->open) {
            state->windows++;
        }
    }
    state->renaming = adapter->folder_edit->renaming;
    state->find_text = find->name_text;
    state->find_type = tiku_trk_find_type_label(find);
    state->bar_x = bar.x;
    state->bar_y = bar.y;
    state->win_x = focused != NULL ? focused->frame.x : -1;
    state->win_y = focused != NULL ? focused->frame.y : -1;
    adapter->menu_labels[0] = '\0';
    append_menu_labels(adapter, *adapter->popup);
    append_menu_labels(adapter, *adapter->popup_sub);
    if (adapter->menu_labels[0] == '\0') {
        /* The Deskbar's leaf menu is a menu too: without this a script
         * could only reach it by pixel, and its rows move whenever what
         * the user has recently opened does. */
        size_t used;

        (void)tiku_trk_deskbar_menu_labels(adapter->deskbar, 0,
            adapter->menu_labels, sizeof adapter->menu_labels);
        used = strlen(adapter->menu_labels);
        (void)tiku_trk_deskbar_menu_labels(adapter->deskbar, 1,
            adapter->menu_labels + used,
            sizeof adapter->menu_labels - used);
    }
    state->menu = adapter->menu_labels;
    state->count = adapter->count_text;
    desktop_selection(adapter, state);
    selected_state(folder, state, find->type);
    if (folder != NULL) {
        const tiku_trk_column_t *columns = NULL;

        state->columns = tiku_trk_poseview_columns(folder->view, &columns);
        state->scroll = tiku_trk_poseview_scroll(folder->view);
        state->rows = tiku_trk_poseview_count(folder->view);
        /*
         * How many of them the window can SHOW, which is the list's
         * height divided by a row's -- so it is the one number a script
         * can ask that moves when the face does.
         */
        state->viewport_rows = tiku_trk_poseview_visible_rows(folder->view);
    } else if (focused != NULL && focused ==
               tiku_trk_panel_window_handle(adapter->panel_window)) {
        state->scroll = tiku_trk_panel_window_scroll(adapter->panel_window);
        state->rows = tiku_trk_panel_window_rows(adapter->panel_window);
    }
    state->pose_at = tiku_trk_script_adapter_pose_at;
    state->pose_desk_at = tiku_trk_script_adapter_pose_desk_at;
    state->pose_screen = tiku_trk_script_adapter_pose_screen;
    state->row_point = tiku_trk_script_adapter_row_point;
    state->cell_point = tiku_trk_script_adapter_cell_point;
    state->rows_drawn = tiku_trk_script_adapter_rows_drawn;
    state->tab_point = tiku_trk_script_adapter_tab_point;
    state->pixel_at = tiku_trk_script_adapter_pixel_at;
    state->redraw_diff = adapter->redraw_diff;
    state->redraw_ctx = adapter->redraw_ctx;
    state->pose_ctx = adapter;
}

static int
menu_pick(tiku_trk_script_adapter_t *adapter, const char *label,
          tiku_event_t *event)
{
    tiku_trk_find_t *find =
        tiku_trk_find_window_model(adapter->find_window);
    tiku_menu_t *menu = *adapter->popup_sub != NULL
        ? *adapter->popup_sub : *adapter->popup;
    tiku_rect_t frame = *adapter->popup_sub != NULL
        ? *adapter->popup_sub_frame : *adapter->popup_frame;
    int i;

    if (menu == NULL && tiku_trk_find_popup_open(find)) {
        menu = find->popup_sub != NULL ? find->popup_sub : &find->popup;
        frame = find->popup_sub != NULL ? find->popup_sub_frame
                                        : find->popup_frame;
    }
    if (menu == NULL) {
        int x = 0, y = 0;

        if (tiku_trk_deskbar_menu_point(adapter->deskbar, label, &x, &y)) {
            event->type = TIKU_EVENT_POINTER_DOWN;
            event->x = x;
            event->y = y;
            event->button = 1u;
            event->time_us = tiku_trk_now();
            return 1;
        }
        fprintf(stderr, "script: no menu is open for '%s'\n", label);
        return 0;
    }
    {
        /* An exact label outranks a substring: "Move to" must not land on
         * "Move to Trash" merely because the Trash row comes first. */
        int pass;

        for (pass = 0; pass < 2; pass++) {
            for (i = 0; i < menu->count; i++) {
                if (menu->item[i].separator) {
                    continue;
                }
                if (pass == 0
                        ? strcmp(menu->item[i].label, label) != 0
                        : strstr(menu->item[i].label, label) == NULL) {
                    continue;
                }
                event->type = TIKU_EVENT_POINTER_DOWN;
                event->x = frame.x + 8;
                event->y = frame.y + tiku_menu_item_top(menu, i) +
                    tiku_menu_item_height(menu, i) / 2;
                event->button = 1u;
                event->time_us = tiku_trk_now();
                return 1;
            }
        }
    }
    fprintf(stderr, "script: no menu item matching '%s'\n", label);
    return 0;
}

tiku_trk_script_adapter_result_t
tiku_trk_script_adapter_next(tiku_trk_script_adapter_t *adapter,
                             tiku_trk_script_t *script,
                             tiku_event_t *event)
{
    tiku_trk_script_state_t state;
    tiku_trk_script_step_t step;

    fill_state(adapter, &state);
    if (!tiku_trk_script_pump(script, &state, &step)) {
        return TIKU_TRK_SCRIPT_ADAPTER_STOP;
    }
    switch (step.what) {
    case TIKU_TRK_SC_KEY:
        event->type = TIKU_EVENT_KEY_DOWN;
        event->key = step.key;
        event->modifiers = step.modifiers;
        if (step.key >= 0x20u && step.key < 0x7fu) {
            event->text[0] = (char)step.key;
            if ((step.modifiers & TIKU_MOD_SHIFT) != 0u &&
                event->text[0] >= 'a' && event->text[0] <= 'z') {
                event->text[0] = (char)(event->text[0] - 'a' + 'A');
            }
        }
        return TIKU_TRK_SCRIPT_ADAPTER_EVENT;
    case TIKU_TRK_SC_MOVE:
        event->type = TIKU_EVENT_POINTER_MOVE;
        event->x = step.x;
        event->y = step.y;
        event->modifiers = step.modifiers;
        return TIKU_TRK_SCRIPT_ADAPTER_EVENT;
    case TIKU_TRK_SC_PRESS:
    case TIKU_TRK_SC_RELEASE:
        event->type = step.what == TIKU_TRK_SC_PRESS
            ? TIKU_EVENT_POINTER_DOWN : TIKU_EVENT_POINTER_UP;
        event->x = step.x;
        event->y = step.y;
        event->modifiers = step.modifiers;
        event->button = step.button;
        event->time_us = tiku_trk_now();
        return TIKU_TRK_SCRIPT_ADAPTER_EVENT;
    case TIKU_TRK_SC_MENU_PICK:
        return menu_pick(adapter, step.path, event)
            ? TIKU_TRK_SCRIPT_ADAPTER_EVENT : TIKU_TRK_SCRIPT_ADAPTER_IDLE;
    case TIKU_TRK_SC_SHOT:
        snprintf(adapter->shot_path, sizeof adapter->shot_path, "%s",
                 step.path);
        return TIKU_TRK_SCRIPT_ADAPTER_SHOT;
    case TIKU_TRK_SC_QUIT:
        return TIKU_TRK_SCRIPT_ADAPTER_STOP;
    default:
        return TIKU_TRK_SCRIPT_ADAPTER_IDLE;
    }
}
