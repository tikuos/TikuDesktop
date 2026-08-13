/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_panel_window.c - Open and Save panel window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_iconpaint.h"
#include "tiku_trk_panel_window.h"
#include "tiku_trk_task.h"

#define PANEL_BUTTON_W 78
#define PANEL_BUTTON_H 22
#define PANEL_PADDING  10
#define PANEL_FIELD_H  22
#define PANEL_WIDTH    460
#define PANEL_HEIGHT   320
#define PANEL_MINI_ICON 16

typedef enum {
    PANEL_HIT_NONE = 0,
    PANEL_HIT_LIST,
    PANEL_HIT_NAME,
    PANEL_HIT_DEFAULT,
    PANEL_HIT_CANCEL
} panel_hit_t;

static void
mark_dirty(tiku_trk_panel_window_t *controller)
{
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

static void
layout(const tiku_trk_panel_window_t *controller, tiku_desk_rect_t content,
       tiku_desk_rect_t *list, tiku_desk_rect_t *name,
       tiku_desk_rect_t *default_button, tiku_desk_rect_t *cancel)
{
    int bottom = content.y + content.h - PANEL_PADDING - PANEL_BUTTON_H;
    int field = controller->model.mode == TIKU_TRK_PANEL_SAVE
                    ? PANEL_FIELD_H + PANEL_PADDING : 0;

    *list = (tiku_desk_rect_t){ content.x + PANEL_PADDING,
                                content.y + PANEL_PADDING,
                                content.w - 2 * PANEL_PADDING, 0 };
    list->h = bottom - list->y - PANEL_PADDING - field;
    if (list->h < 40) {
        list->h = 40;
    }
    *name = (tiku_desk_rect_t){ list->x,
                                list->y + list->h + PANEL_PADDING,
                                list->w, PANEL_FIELD_H };
    *default_button = (tiku_desk_rect_t){
        content.x + content.w - PANEL_PADDING - PANEL_BUTTON_W,
        bottom, PANEL_BUTTON_W, PANEL_BUTTON_H };
    *cancel = *default_button;
    cancel->x -= PANEL_PADDING + PANEL_BUTTON_W;
}

static int
selection(const tiku_trk_panel_window_t *controller, tiku_trk_model_t *out,
          int max)
{
    int i, count, selected = 0;

    if (controller->view == NULL) {
        return 0;
    }
    count = tiku_trk_poseview_count(controller->view);
    for (i = 0; i < count && selected < max; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(controller->view, i);

        if (pose != NULL && pose->selected) {
            out[selected++] = pose->model;
        }
    }
    return selected;
}

static void
draw(tiku_desk_window_t *window, tiku_desk_surface_t *surface,
     tiku_desk_rect_t content, void *context)
{
    tiku_trk_panel_window_t *controller = context;
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    tiku_desk_rect_t list, name, default_button, cancel;
    tiku_trk_model_t selected[8];
    int selected_count = selection(controller, selected, 8);
    int rows, scroll, total, i;

    (void)window;
    tiku_desk_ui_panel(surface, content);
    layout(controller, content, &list, &name, &default_button, &cancel);
    tiku_desk_ui_sunken(surface, list, TIKU_DESK_C_DOC);
    if (controller->view == NULL) {
        return;
    }
    rows = (list.h - 4) / controller->row_h;
    tiku_trk_poseview_set_visible_rows(controller->view, rows);
    scroll = tiku_trk_poseview_scroll(controller->view);
    total = tiku_trk_poseview_count(controller->view);
    tiku_desk_clip_set(surface, tiku_desk_inset(list, 2));
    for (i = 0; i < rows && scroll + i < total; i++) {
        tiku_trk_pose_t *pose =
            tiku_trk_poseview_at(controller->view, scroll + i);
        tiku_desk_rect_t row;
        int mini = controller->row_h - 4;

        if (pose == NULL) {
            continue;
        }
        row = (tiku_desk_rect_t){ list.x + 2,
                                  list.y + 2 + i * controller->row_h,
                                  list.w - 4, controller->row_h };
        if (mini > PANEL_MINI_ICON) {
            mini = PANEL_MINI_ICON;
        }
        if (pose->selected) {
            tiku_desk_fill(surface, row, TIKU_DESK_C_SELECT);
        }
        (void)tiku_trk_icon_paint(surface,
            tiku_trk_model_icon_name(&pose->model), row.x + 2,
            row.y + (controller->row_h - mini) / 2, mini);
        tiku_desk_text(surface, font, row.x + 2 + mini + 4,
            row.y + (controller->row_h + font->height) / 2 - 2,
            pose->model.name,
            pose->selected ? TIKU_DESK_C_SELTEXT : TIKU_DESK_C_TEXT);
    }
    tiku_desk_clip_reset(surface);
    if (controller->model.mode == TIKU_TRK_PANEL_SAVE) {
        tiku_desk_ui_textfield_sel(surface, name, controller->name.text,
            controller->model.focus == TIKU_TRK_PANEL_FOCUS_NAME
                ? controller->name.caret : -1,
            controller->name.sel_a, controller->name.sel_b,
            controller->model.focus == TIKU_TRK_PANEL_FOCUS_NAME
                ? TIKU_DESK_S_FOCUS : 0u);
    }
    tiku_desk_ui_button(surface, cancel, "Cancel", 0u);
    tiku_desk_ui_button(surface, default_button,
        tiku_trk_panel_button_label(&controller->model, selected,
                                    selected_count),
        tiku_trk_panel_button_enabled(&controller->model, selected,
                                      selected_count)
            ? TIKU_DESK_S_DEFAULT : TIKU_DESK_S_DISABLED);
}

static void
reload(tiku_trk_panel_window_t *controller, const char *select)
{
    int i, count;

    if (controller->view != NULL) {
        tiku_trk_poseview_free(controller->view);
    }
    controller->view = tiku_trk_poseview_new(controller->backend,
                                              controller->model.dir);
    if (controller->view == NULL) {
        mark_dirty(controller);
        return;
    }
    tiku_trk_poseview_set_icons(controller->view, controller->icons);
    tiku_trk_poseview_set_mode(controller->view, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_set_multiple(controller->view,
                                   controller->model.multiple);
    (void)tiku_trk_poseview_reload(controller->view);
    if (select != NULL && select[0] != '\0') {
        count = tiku_trk_poseview_count(controller->view);
        for (i = 0; i < count; i++) {
            tiku_trk_pose_t *pose =
                tiku_trk_poseview_at(controller->view, i);

            if (pose != NULL && strcmp(pose->model.name, select) == 0) {
                tiku_trk_poseview_click(controller->view, i, 0u);
                break;
            }
        }
    }
    if (controller->window != NULL) {
        tiku_trk_panel_title(&controller->model, controller->window->title,
                             sizeof controller->window->title);
    }
    mark_dirty(controller);
}

static void
go(tiku_trk_panel_window_t *controller, const char *directory)
{
    if (directory == NULL || directory[0] == '\0') {
        return;
    }
    snprintf(controller->model.dir, sizeof controller->model.dir, "%s",
             directory);
    reload(controller, NULL);
}

static void
commit(tiku_trk_panel_window_t *controller)
{
    tiku_trk_model_t selected[8];
    char destination[TIKU_TRK_PATH_MAX];
    int count;

    if (controller->target == NULL) {
        return;
    }
    count = selection(controller, selected, 8);
    if (count == 1 && tiku_trk_model_is_container(&selected[0])) {
        snprintf(destination, sizeof destination, "%s", selected[0].path);
    } else {
        snprintf(destination, sizeof destination, "%s",
                 controller->model.dir);
    }
    if (controller->commit != NULL) {
        controller->commit(controller->commit_context, controller->target,
                           controller->command, destination);
    }
    tiku_trk_panel_window_close(controller);
}

static void
activate_default(tiku_trk_panel_window_t *controller)
{
    tiku_trk_model_t selected[8];
    int count = selection(controller, selected, 8);

    if (!tiku_trk_panel_button_enabled(&controller->model, selected, count)) {
        return;
    }
    if (count == 1 && tiku_trk_model_is_container(&selected[0])) {
        go(controller, selected[0].path);
        return;
    }
    commit(controller);
}

static panel_hit_t
hit(tiku_trk_panel_window_t *controller, int x, int y, int *row)
{
    tiku_desk_rect_t content, list, name, default_button, cancel;

    if (row != NULL) {
        *row = -1;
    }
    if (controller->window == NULL || !controller->window->open) {
        return PANEL_HIT_NONE;
    }
    content = tiku_desk_window_content(controller->window);
    layout(controller, content, &list, &name, &default_button, &cancel);
#define INSIDE(rect) (x >= (rect).x && x < (rect).x + (rect).w && \
                      y >= (rect).y && y < (rect).y + (rect).h)
    if (INSIDE(default_button)) { return PANEL_HIT_DEFAULT; }
    if (INSIDE(cancel))         { return PANEL_HIT_CANCEL; }
    if (controller->model.mode == TIKU_TRK_PANEL_SAVE && INSIDE(name)) {
        return PANEL_HIT_NAME;
    }
    if (INSIDE(list)) {
        int index = (y - list.y - 2) / controller->row_h;

        if (row != NULL && controller->view != NULL) {
            index += tiku_trk_poseview_scroll(controller->view);
            *row = index >= 0 &&
                   index < tiku_trk_poseview_count(controller->view)
                       ? index : -1;
        }
        return PANEL_HIT_LIST;
    }
#undef INSIDE
    return PANEL_HIT_NONE;
}

static int
event(tiku_desk_window_t *window, const tiku_desk_event_t *input,
      void *context)
{
    tiku_trk_panel_window_t *controller = context;
    int row = -1;
    int64_t now;

    (void)window;
    if (input->type != TIKU_DESK_EVENT_POINTER_DOWN) {
        return 0;
    }
    switch (hit(controller, input->x, input->y, &row)) {
    case PANEL_HIT_CANCEL:
        tiku_trk_panel_window_close(controller);
        break;
    case PANEL_HIT_DEFAULT:
        activate_default(controller);
        break;
    case PANEL_HIT_NAME:
        controller->model.focus = TIKU_TRK_PANEL_FOCUS_NAME;
        break;
    case PANEL_HIT_LIST:
        controller->model.focus = TIKU_TRK_PANEL_FOCUS_LIST;
        tiku_trk_poseview_click(controller->view, row, 0u);
        now = input->time_us != 0 ? input->time_us : tiku_trk_now();
        if (row >= 0 && now - controller->press_at < 500000 &&
            controller->press_row == row) {
            tiku_trk_pose_t *pose =
                tiku_trk_poseview_at(controller->view, row);

            if (pose != NULL && tiku_trk_panel_activate(&controller->model,
                    &pose->model) == TIKU_TRK_PANEL_ACT_ENTER) {
                go(controller, pose->model.path);
            } else if (pose != NULL) {
                commit(controller);
            }
        }
        controller->press_at = now;
        controller->press_row = row;
        break;
    default:
        return 0;
    }
    mark_dirty(controller);
    return 1;
}

void
tiku_trk_panel_window_init(tiku_trk_panel_window_t *controller,
    tiku_desk_workspace_t *workspace, tiku_trk_backend_t *backend,
    tiku_trk_store_t *store, tiku_trk_nodeicon_t *icons, int row_h,
    int screen_w, int screen_h, int *dirty,
    tiku_trk_panel_window_commit_fn commit_callback, void *commit_context)
{
    memset(controller, 0, sizeof *controller);
    controller->workspace = workspace;
    controller->backend = backend;
    controller->store = store;
    controller->icons = icons;
    controller->row_h = row_h;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    controller->dirty = dirty;
    controller->press_row = -1;
    controller->commit = commit_callback;
    controller->commit_context = commit_context;
}

void
tiku_trk_panel_window_open(tiku_trk_panel_window_t *controller, void *target,
                           int command, const char *initial_directory)
{
    tiku_desk_rect_t frame;
    char title[TIKU_DESK_WINDOW_TITLE_MAX];
    int x = 0, y = 0, width = PANEL_WIDTH, height = PANEL_HEIGHT;

    if (target == NULL) {
        return;
    }
    controller->target = target;
    controller->command = command;
    tiku_trk_panel_open(&controller->model, TIKU_TRK_PANEL_OPEN,
        TIKU_TRK_PANEL_DIR, initial_directory, controller->backend,
        "Tracker");
    controller->model.multiple = 0;
    if (!tiku_trk_panel_restore_frame(controller->store, &x, &y, &width,
                                      &height)) {
        x = (controller->screen_w - width) / 2;
        y = (controller->screen_h - height) / 3;
    }
    frame = (tiku_desk_rect_t){ x, y, width, height };
    tiku_trk_edit_start(&controller->name, "");
    if (controller->window != NULL && controller->window->open) {
        controller->window->frame = frame;
        tiku_desk_workspace_activate(controller->workspace,
                                     controller->window);
    } else {
        tiku_trk_panel_title(&controller->model, title, sizeof title);
        controller->window = tiku_desk_workspace_open(controller->workspace,
            title, frame, draw, controller, &controller->model);
        if (controller->window != NULL) {
            tiku_desk_window_set_handlers(controller->window, event,
                                           NULL, NULL);
        }
    }
    reload(controller, NULL);
}

void
tiku_trk_panel_window_close(tiku_trk_panel_window_t *controller)
{
    if (controller->window != NULL) {
        tiku_trk_panel_save_frame(controller->store,
            controller->window->frame.x, controller->window->frame.y,
            controller->window->frame.w, controller->window->frame.h);
        tiku_desk_workspace_close(controller->workspace,
                                  controller->window);
        controller->window = NULL;
    }
    if (controller->view != NULL) {
        tiku_trk_poseview_free(controller->view);
        controller->view = NULL;
    }
    controller->target = NULL;
    controller->press_at = 0;
    controller->press_row = -1;
    mark_dirty(controller);
}

void
tiku_trk_panel_window_destroy(tiku_trk_panel_window_t *controller)
{
    tiku_trk_panel_window_close(controller);
}

tiku_desk_window_t *
tiku_trk_panel_window_handle(tiku_trk_panel_window_t *controller)
{
    return controller->window;
}

tiku_trk_panel_t *
tiku_trk_panel_window_model(tiku_trk_panel_window_t *controller)
{
    return &controller->model;
}

int
tiku_trk_panel_window_key(tiku_trk_panel_window_t *controller, unsigned key,
                          unsigned modifiers, char character)
{
    tiku_trk_model_t selected[8];
    tiku_trk_panel_key_t action;
    unsigned tracker_mods = 0;
    int selected_count, panel_key = 0;

    if (controller->window == NULL || !controller->window->open ||
        tiku_desk_workspace_focused(controller->workspace) !=
            controller->window) {
        return 0;
    }
    if ((modifiers & TIKU_DESK_MOD_SHIFT) != 0u) {
        tracker_mods |= TIKU_TRK_MOD_SHIFT;
    }
    if ((modifiers & TIKU_DESK_MOD_CMD) != 0u) {
        tracker_mods |= TIKU_TRK_MOD_CMD;
    }
    selected_count = selection(controller, selected, 8);
    if (key == TIKU_DESK_KEY_ESCAPE)      { panel_key = 27; }
    else if (key == TIKU_DESK_KEY_RETURN) { panel_key = '\n'; }
    else if (key == TIKU_DESK_KEY_TAB)    { panel_key = '\t'; }
    else if (key == TIKU_DESK_KEY_UP)     { panel_key = TIKU_TRK_KEY_UP; }
    else if (key == TIKU_DESK_KEY_DOWN)   { panel_key = TIKU_TRK_KEY_DOWN; }
    else if (key >= 'a' && key <= 'z')    { panel_key = (int)key; }
    action = tiku_trk_panel_key(&controller->model, panel_key, tracker_mods,
        controller->model.focus == TIKU_TRK_PANEL_FOCUS_LIST,
        selected_count > 0);
    switch (action) {
    case TIKU_TRK_PANEL_KEY_CANCEL:
        tiku_trk_panel_window_close(controller);
        break;
    case TIKU_TRK_PANEL_KEY_DEFAULT:
        activate_default(controller);
        break;
    case TIKU_TRK_PANEL_KEY_PARENT: {
        char parent[TIKU_TRK_PATH_MAX];

        if (tiku_trk_panel_parent(&controller->model, parent,
                                  sizeof parent)) {
            go(controller, parent);
        }
        break;
    }
    case TIKU_TRK_PANEL_KEY_ENTER:
        if (selected_count == 1 &&
            tiku_trk_model_is_container(&selected[0])) {
            go(controller, selected[0].path);
        }
        break;
    case TIKU_TRK_PANEL_KEY_HOME:
        go(controller, controller->model.home);
        break;
    case TIKU_TRK_PANEL_KEY_DESKTOP:
        go(controller, controller->model.desktop);
        break;
    case TIKU_TRK_PANEL_KEY_FOCUS:
        if (controller->model.mode == TIKU_TRK_PANEL_SAVE) {
            controller->model.focus =
                controller->model.focus == TIKU_TRK_PANEL_FOCUS_LIST
                    ? TIKU_TRK_PANEL_FOCUS_NAME
                    : TIKU_TRK_PANEL_FOCUS_LIST;
        }
        break;
    default:
        if (controller->model.focus == TIKU_TRK_PANEL_FOCUS_LIST &&
            (key == TIKU_DESK_KEY_UP || key == TIKU_DESK_KEY_DOWN)) {
            (void)tiku_trk_poseview_key_nav(controller->view,
                key == TIKU_DESK_KEY_UP ? TIKU_TRK_KEY_UP
                                        : TIKU_TRK_KEY_DOWN,
                tracker_mods);
        } else if (controller->model.mode == TIKU_TRK_PANEL_SAVE &&
                   controller->model.focus == TIKU_TRK_PANEL_FOCUS_NAME) {
            if (key == TIKU_DESK_KEY_BACKSPACE) {
                (void)tiku_trk_edit_key(&controller->name,
                    TIKU_TRK_EDIT_BACKSPACE, tracker_mods, '\0');
            } else if (character != '\0') {
                (void)tiku_trk_edit_key(&controller->name,
                    TIKU_TRK_EDIT_CHAR, tracker_mods, character);
            }
            snprintf(controller->model.name, sizeof controller->model.name,
                     "%.*s", (int)sizeof controller->model.name - 1,
                     controller->name.text);
            controller->model.name_len = controller->name.len;
        }
        break;
    }
    mark_dirty(controller);
    return 1;
}

int
tiku_trk_panel_window_poll(tiku_trk_panel_window_t *controller, int64_t now,
    const tiku_trk_change_t *changes, int count, int complete)
{
    unsigned result;

    if (controller->window == NULL || !controller->window->open ||
        controller->view == NULL) {
        return 0;
    }
    result = tiku_trk_poseview_poll(controller->view, now, changes, count,
                                    complete);
    if ((result & TIKU_TRK_NOTIFY_GONE) != 0u) {
        tiku_trk_panel_dir_gone(&controller->model, controller->backend);
        reload(controller, NULL);
    } else if ((result & TIKU_TRK_NOTIFY_RETITLE) != 0u) {
        (void)tiku_trk_panel_dir_moved(&controller->model,
            controller->model.dir, tiku_trk_poseview_path(controller->view));
    }
    if (result != TIKU_TRK_NOTIFY_NOTHING) {
        mark_dirty(controller);
        return 1;
    }
    return 0;
}

int
tiku_trk_panel_window_device_gone(tiku_trk_panel_window_t *controller,
                                  const char *mount)
{
    size_t length;

    if (controller->window == NULL || !controller->window->open ||
        mount == NULL || mount[0] == '\0') {
        return 0;
    }
    length = strlen(mount);
    if (strncmp(controller->model.dir, mount, length) != 0 ||
        (controller->model.dir[length] != '\0' &&
         controller->model.dir[length] != '/')) {
        return 0;
    }
    tiku_trk_panel_dir_gone(&controller->model, controller->backend);
    reload(controller, NULL);
    return 1;
}

int
tiku_trk_panel_window_scroll(const tiku_trk_panel_window_t *controller)
{
    return controller->view != NULL
        ? tiku_trk_poseview_scroll(controller->view) : 0;
}

int
tiku_trk_panel_window_rows(const tiku_trk_panel_window_t *controller)
{
    return controller->view != NULL
        ? tiku_trk_poseview_count(controller->view) : 0;
}
