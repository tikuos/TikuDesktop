/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_info_window.c - Get Info window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_iconpaint.h"
#include "tiku_trk_info_window.h"

#define INFO_WIDTH    360
#define INFO_HEIGHT   300
#define INFO_PAD       10
#define INFO_ICON      32
#define INFO_TAB_H     20
#define INFO_ROW_H     18
#define CASCADE_STEP   22

static void
mark_dirty(tiku_trk_info_windows_t *controller)
{
    if (controller->dirty != NULL) {
        *controller->dirty = 1;
    }
}

static void
layout(tiku_desk_rect_t content, tiku_desk_rect_t *icon,
       tiku_desk_rect_t *name, tiku_desk_rect_t *tabs,
       tiku_desk_rect_t *body)
{
    *icon = (tiku_desk_rect_t){ content.x + INFO_PAD,
                                content.y + INFO_PAD,
                                INFO_ICON, INFO_ICON };
    *name = (tiku_desk_rect_t){ icon->x + INFO_ICON + INFO_PAD,
                                icon->y + 6,
                                content.w - (icon->x + INFO_ICON + INFO_PAD
                                             - content.x) - INFO_PAD,
                                20 };
    *tabs = (tiku_desk_rect_t){ content.x + INFO_PAD,
                                icon->y + INFO_ICON + INFO_PAD,
                                content.w - 2 * INFO_PAD, INFO_TAB_H };
    *body = (tiku_desk_rect_t){ tabs->x, tabs->y + INFO_TAB_H, tabs->w,
                                content.y + content.h - INFO_PAD
                                    - (tabs->y + INFO_TAB_H) };
    if (body->h < 20) {
        body->h = 20;
    }
}

static tiku_desk_rect_t
tab_rect(tiku_desk_rect_t tabs, int index)
{
    tiku_desk_rect_t result = tabs;

    result.w = tabs.w / TIKU_TRK_INFO_TABS;
    result.x = tabs.x + index * result.w;
    return result;
}

static tiku_desk_rect_t
permission_rect(tiku_desk_rect_t body, int index)
{
    return (tiku_desk_rect_t){ body.x + 8,
        body.y + 10 + index * (INFO_ROW_H + 4), 120, INFO_ROW_H };
}

static void
draw(tiku_desk_window_t *window, tiku_desk_surface_t *surface,
     tiku_desk_rect_t content, void *context)
{
    tiku_trk_info_window_context_t *window_context = context;
    tiku_trk_info_windows_t *controller = window_context->owner;
    tiku_trk_info_t *info = &controller->model[window_context->slot];
    const tiku_desk_font_t *font = tiku_desk_font_plain();
    tiku_desk_rect_t icon, name, tabs, body;
    int i;

    (void)window;
    tiku_desk_ui_panel(surface, content);
    layout(content, &icon, &name, &tabs, &body);
    if (controller->drop_hot == window_context->slot) {
        (void)tiku_trk_icon_paint_selected(surface,
            tiku_trk_model_icon_name(&info->model), icon.x, icon.y,
            INFO_ICON);
    } else {
        (void)tiku_trk_icon_paint(surface,
            tiku_trk_model_icon_name(&info->model), icon.x, icon.y,
            INFO_ICON);
    }
    if (controller->renaming == window_context->slot) {
        tiku_desk_ui_textfield_sel(surface, name, controller->name.text,
            controller->name.caret, controller->name.sel_a,
            controller->name.sel_b, TIKU_DESK_S_FOCUS);
    } else {
        tiku_desk_text(surface, font, name.x, name.y + font->height,
                       info->model.name, TIKU_DESK_C_TEXT);
    }
    for (i = 0; i < TIKU_TRK_INFO_TABS; i++) {
        tiku_desk_rect_t rect = tab_rect(tabs, i);

        tiku_desk_ui_button(surface, rect,
                            tiku_trk_info_tab_name((tiku_trk_info_tab_t)i),
                            info->tab == (tiku_trk_info_tab_t)i
                                ? TIKU_DESK_S_DEFAULT : 0u);
    }
    tiku_desk_ui_sunken(surface, body, TIKU_DESK_C_PANEL);
    if (info->tab == TIKU_TRK_INFO_TAB_INFO) {
        for (i = 0; i < TIKU_TRK_INFO_FIELDS; i++) {
            char value[TIKU_TRK_INFO_VAL_MAX];
            const char *label = "";
            int y = body.y + 6 + i * INFO_ROW_H + font->height;

            if (tiku_trk_info_field(info, controller->store, i, &label,
                                    value, sizeof value) ==
                    TIKU_TRK_INFO_F_NONE) {
                break;
            }
            tiku_desk_text(surface, font, body.x + 8, y, label,
                           TIKU_DESK_C_TEXT);
            tiku_desk_text(surface, font, body.x + 96, y, value,
                           TIKU_DESK_C_TEXT);
        }
    } else if (info->tab == TIKU_TRK_INFO_TAB_PERMISSIONS) {
        static const char *const labels[] = { "Read", "Write", "Execute" };

        for (i = 0; i < 3; i++) {
            tiku_desk_ui_checkbox(surface, permission_rect(body, i), labels[i],
                (unsigned)(tiku_trk_info_perm(info, i) ? TIKU_DESK_S_ON : 0u)
                | (unsigned)(info->perm_editable ? 0u
                                                 : TIKU_DESK_S_DISABLED));
        }
        if (!info->perm_editable) {
            tiku_desk_text(surface, font, body.x + 8,
                body.y + 14 + 3 * (INFO_ROW_H + 4) + font->height,
                "This item's permissions are not yours to set.",
                TIKU_DESK_C_TEXT);
        }
    } else {
        for (i = 0; i < info->nattr; i++) {
            int y = body.y + 6 + i * INFO_ROW_H + font->height;

            if (y > body.y + body.h) {
                break;
            }
            tiku_desk_text(surface, font, body.x + 8, y,
                           info->attr[i].name, TIKU_DESK_C_TEXT);
            tiku_desk_text(surface, font, body.x + 150, y,
                           info->attr[i].type, TIKU_DESK_C_TEXT);
            tiku_desk_text(surface, font, body.x + 210, y,
                           info->attr[i].value, TIKU_DESK_C_TEXT);
        }
        if (info->nattr == 0) {
            tiku_desk_text(surface, font, body.x + 8,
                           body.y + 6 + font->height, "No attributes.",
                           TIKU_DESK_C_TEXT);
        }
    }
}

static int
slot_for_path(const tiku_trk_info_windows_t *controller, const char *path)
{
    int i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        if (controller->model[i].used &&
            strcmp(controller->model[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static void
finish_rename(tiku_trk_info_windows_t *controller, int apply)
{
    int slot = controller->renaming;
    char error[160];

    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX) {
        return;
    }
    controller->renaming = -1;
    if (!apply || controller->name.len == 0 ||
        strcmp(controller->name.text,
               controller->model[slot].model.name) == 0) {
        mark_dirty(controller);
        return;
    }
    if (tiku_trk_fs_rename(controller->fs, controller->model[slot].path,
                           controller->name.text, error, sizeof error) != 0) {
        tiku_trk_alert_open(controller->alert, TIKU_TRK_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_ERROR, error, "Cancel");
    } else {
        char parent[TIKU_TRK_PATH_MAX], destination[TIKU_TRK_PATH_MAX];

        if (tiku_trk_info_location(&controller->model[slot], parent,
                                   sizeof parent, NULL, 0)) {
            snprintf(destination, sizeof destination, "%.*s/%.*s",
                (int)(sizeof destination - sizeof controller->name.text - 2u),
                strcmp(parent, "/") == 0 ? "" : parent,
                (int)(sizeof controller->name.text - 1u),
                controller->name.text);
            (void)tiku_trk_info_moved(&controller->model[slot],
                controller->backend, controller->model[slot].path,
                destination);
            if (controller->window[slot] != NULL) {
                tiku_trk_info_title(&controller->model[slot],
                    controller->window[slot]->title,
                    sizeof controller->window[slot]->title);
            }
        }
    }
    mark_dirty(controller);
}

void
tiku_trk_info_windows_init(tiku_trk_info_windows_t *controller,
    tiku_desk_workspace_t *workspace, tiku_trk_backend_t *backend,
    tiku_trk_store_t *store, tiku_trk_fs_t *fs, tiku_trk_alert_t *alert,
    int *dirty)
{
    int i;

    memset(controller, 0, sizeof *controller);
    controller->drop_hot = -1;
    controller->workspace = workspace;
    controller->backend = backend;
    controller->store = store;
    controller->fs = fs;
    controller->alert = alert;
    controller->dirty = dirty;
    controller->renaming = -1;
    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        controller->context[i].owner = controller;
        controller->context[i].slot = i;
    }
}

int
tiku_trk_info_windows_open(tiku_trk_info_windows_t *controller,
                           const char *path)
{
    tiku_desk_rect_t frame;
    char title[TIKU_DESK_WINDOW_TITLE_MAX];
    int slot = slot_for_path(controller, path);

    if (slot >= 0) {
        if (controller->window[slot] != NULL) {
            tiku_desk_workspace_activate(controller->workspace,
                                          controller->window[slot]);
        }
        return slot;
    }
    for (slot = 0; slot < TIKU_TRK_INFO_WINDOW_MAX; slot++) {
        if (!controller->model[slot].used) {
            break;
        }
    }
    if (slot >= TIKU_TRK_INFO_WINDOW_MAX ||
        tiku_trk_info_open(&controller->model[slot], controller->backend,
                           path) != 0) {
        return -1;
    }
    (void)tiku_trk_info_attrs(&controller->model[slot], controller->store);
    frame = (tiku_desk_rect_t){ 120 + slot * CASCADE_STEP,
                                80 + slot * CASCADE_STEP,
                                INFO_WIDTH, INFO_HEIGHT };
    tiku_trk_info_title(&controller->model[slot], title, sizeof title);
    controller->window[slot] = tiku_desk_workspace_open(controller->workspace,
        title, frame, draw, &controller->context[slot],
        &controller->model[slot]);
    mark_dirty(controller);
    return slot;
}

void
tiku_trk_info_windows_close(tiku_trk_info_windows_t *controller, int slot)
{
    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX) {
        return;
    }
    if (controller->window[slot] != NULL) {
        tiku_desk_workspace_close(controller->workspace,
                                  controller->window[slot]);
        controller->window[slot] = NULL;
    }
    if (controller->renaming == slot) {
        controller->renaming = -1;
    }
    memset(&controller->model[slot], 0, sizeof controller->model[slot]);
    mark_dirty(controller);
}

int
tiku_trk_info_windows_slot(const tiku_trk_info_windows_t *controller,
                           const tiku_desk_window_t *window)
{
    int i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        if (controller->window[i] == window && window != NULL &&
            window->open) {
            return i;
        }
    }
    return -1;
}

tiku_trk_info_t *
tiku_trk_info_windows_model(tiku_trk_info_windows_t *controller, int slot)
{
    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX) {
        return NULL;
    }
    return &controller->model[slot];
}

tiku_desk_window_t *
tiku_trk_info_windows_handle(tiku_trk_info_windows_t *controller, int slot)
{
    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX) {
        return NULL;
    }
    return controller->window[slot];
}

tiku_trk_info_hit_t
tiku_trk_info_windows_hit(tiku_trk_info_windows_t *controller, int slot,
                          int x, int y, int *which)
{
    tiku_desk_rect_t content, icon, name, tabs, body;
    tiku_trk_info_t *info;
    int i;

    if (which != NULL) {
        *which = -1;
    }
    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX ||
        controller->window[slot] == NULL) {
        return TIKU_TRK_INFO_HIT_NONE;
    }
    info = &controller->model[slot];
    content = tiku_desk_window_content(controller->window[slot]);
    layout(content, &icon, &name, &tabs, &body);
#define INSIDE(rect) (x >= (rect).x && x < (rect).x + (rect).w && \
                      y >= (rect).y && y < (rect).y + (rect).h)
    if (INSIDE(icon)) { return TIKU_TRK_INFO_HIT_ICON; }
    if (INSIDE(name)) { return TIKU_TRK_INFO_HIT_NAME; }
    for (i = 0; i < TIKU_TRK_INFO_TABS; i++) {
        tiku_desk_rect_t rect = tab_rect(tabs, i);

        if (INSIDE(rect)) {
            if (which != NULL) { *which = i; }
            return TIKU_TRK_INFO_HIT_TAB;
        }
    }
    if (info->tab == TIKU_TRK_INFO_TAB_PERMISSIONS) {
        for (i = 0; i < 3; i++) {
            tiku_desk_rect_t rect = permission_rect(body, i);

            if (INSIDE(rect)) {
                if (which != NULL) { *which = i; }
                return TIKU_TRK_INFO_HIT_PERMISSION;
            }
        }
    }
    if (info->tab == TIKU_TRK_INFO_TAB_INFO && INSIDE(body)) {
        i = (y - body.y - 6) / INFO_ROW_H;
        if (i >= 0 && i < TIKU_TRK_INFO_FIELDS) {
            if (which != NULL) { *which = i; }
            return TIKU_TRK_INFO_HIT_FIELD;
        }
    }
#undef INSIDE
    return TIKU_TRK_INFO_HIT_NONE;
}

void
tiku_trk_info_windows_begin_rename(tiku_trk_info_windows_t *controller,
                                   int slot)
{
    if (slot < 0 || slot >= TIKU_TRK_INFO_WINDOW_MAX ||
        !controller->model[slot].used) {
        return;
    }
    controller->renaming = slot;
    tiku_trk_edit_start(&controller->name,
                        controller->model[slot].model.name);
    tiku_trk_edit_deny(&controller->name, tiku_trk_fs_name_deny());
    mark_dirty(controller);
}

int
tiku_trk_info_windows_key(tiku_trk_info_windows_t *controller,
    int key, unsigned modifiers, char character)
{
    int slot = tiku_trk_info_windows_slot(controller,
        tiku_desk_workspace_focused(controller->workspace));

    if (slot < 0) {
        return 0;
    }
    if (controller->renaming == slot) {
        if (key == TIKU_DESK_KEY_RETURN) {
            finish_rename(controller, 1);
        } else if (key == TIKU_DESK_KEY_ESCAPE) {
            finish_rename(controller, 0);
        } else if (key == TIKU_DESK_KEY_BACKSPACE) {
            (void)tiku_trk_edit_key(&controller->name,
                                    TIKU_TRK_EDIT_BACKSPACE, 0, '\0');
        } else if (character != '\0') {
            (void)tiku_trk_edit_key(&controller->name,
                                    TIKU_TRK_EDIT_CHAR, 0, character);
        }
        mark_dirty(controller);
        return 1;
    }
    if ((modifiers & TIKU_DESK_MOD_CMD) != 0u &&
        (key == 'e' || key == 'E')) {
        tiku_trk_info_windows_begin_rename(controller, slot);
        return 1;
    }
    if ((modifiers & TIKU_DESK_MOD_CMD) != 0u &&
        (key == 'w' || key == 'W')) {
        tiku_trk_info_windows_close(controller, slot);
        return 1;
    }
    return 0;
}

int
tiku_trk_info_windows_calc_step(tiku_trk_info_windows_t *controller)
{
    int i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        if (controller->model[i].used &&
            tiku_trk_info_calc_step(&controller->model[i],
                                    controller->backend)) {
            mark_dirty(controller);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_info_windows_device_gone(tiku_trk_info_windows_t *controller,
                                  uint64_t device)
{
    int changed = 0, i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        if (controller->model[i].used &&
            controller->model[i].model.facts.dev == device) {
            tiku_trk_info_windows_close(controller, i);
            changed++;
        }
    }
    return changed;
}

int
tiku_trk_info_windows_refresh(tiku_trk_info_windows_t *controller,
                              int64_t now)
{
    int changed = 0, i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        tiku_trk_model_t model;

        if (!controller->model[i].used) {
            continue;
        }
        if (tiku_trk_info_refresh(&controller->model[i],
                                  controller->backend, now)) {
            changed++;
        }
        if (controller->backend->ops->stat(controller->backend,
                controller->model[i].path, &model) != 0) {
            if (tiku_trk_info_refind(&controller->model[i],
                                     controller->backend)) {
                tiku_trk_info_title(&controller->model[i],
                    controller->window[i]->title,
                    sizeof controller->window[i]->title);
            } else {
                tiku_trk_info_windows_close(controller, i);
            }
            changed++;
        }
    }
    if (changed) {
        mark_dirty(controller);
    }
    return changed;
}
