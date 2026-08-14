/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drop_controller.c - folder and Deskbar drop execution.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_trk_drop_controller.h"
#include "tiku_trk_drop_protocol.h"
#include "tiku_trk_model.h"

void
tiku_trk_drop_controller_init(tiku_trk_drop_controller_t *controller,
    tiku_trk_deskbar_t *deskbar, tiku_trk_backend_t *backend,
    tiku_trk_fs_t *fs, tiku_trk_desktop_view_t *desktop_view,
    tiku_trk_cwmenu_t *menus, tiku_trk_alert_t *alert,
    tiku_trk_drag_t *drag, tiku_trk_folder_window_t **drop_folder,
    int *drop_row, tiku_trk_menu_t **popup, tiku_trk_menu_t **popup_sub,
    tiku_desk_rect_t *popup_frame, char *progress_destination,
    size_t progress_destination_size, char *status, size_t status_size,
    int *dirty, int screen_w, int screen_h)
{
    *controller = (tiku_trk_drop_controller_t){
        .deskbar = deskbar,
        .backend = backend,
        .fs = fs,
        .desktop_view = desktop_view,
        .menus = menus,
        .alert = alert,
        .drag = drag,
        .drop_folder = drop_folder,
        .drop_row = drop_row,
        .popup = popup,
        .popup_sub = popup_sub,
        .popup_frame = popup_frame,
        .progress_destination = progress_destination,
        .progress_destination_size = progress_destination_size,
        .status = status,
        .status_size = status_size,
        .dirty = dirty,
        .screen_w = screen_w,
        .screen_h = screen_h
    };
}

static int
drop_paths(tiku_trk_drop_controller_t *controller, const char **paths)
{
    int i;

    for (i = 0; i < controller->drag->count; i++) {
        paths[i] = controller->drag->path[i];
    }
    return controller->drag->count;
}

static int
drop_on_deskbar(tiku_trk_drop_controller_t *controller, int x, int y,
                unsigned modifiers)
{
    const char *paths[TIKU_TRK_DRAG_MAX];
    char destination[TIKU_TRK_PATH_MAX];
    tiku_trk_db_drop_t drop;
    int team = -1;
    int count;
    int i;

    drop = tiku_trk_deskbar_drop_at(controller->deskbar, x, y, &team);
    if (drop == TIKU_TRK_DB_DROP_NONE &&
        tiku_trk_deskbar_menu_drop_at(controller->deskbar, x, y,
            destination, sizeof destination) == TIKU_TRK_DB_DROP_MENU_DIR) {
        count = drop_paths(controller, paths);
        if (count > 0) {
            (void)tiku_trk_fs_link(controller->fs, paths, count,
                                   destination, 0);
            snprintf(controller->status, controller->status_size,
                     "linking %d item%s into %.40s", count,
                     count == 1 ? "" : "s", destination);
        }
        return 1;
    }
    if (drop == TIKU_TRK_DB_DROP_NONE) {
        return 0;
    }
    count = drop_paths(controller, paths);
    if (count == 0) {
        return 1;
    }
    if (drop == TIKU_TRK_DB_DROP_LEAF) {
        const char *home = getenv("HOME");

        if (home != NULL && snprintf(destination, sizeof destination,
                "%s/.config/deskbar", home) > 0) {
            (void)mkdir(destination, 0755);
            (void)tiku_trk_fs_link(controller->fs, paths, count,
                                   destination, 0);
            snprintf(controller->status, controller->status_size,
                     "linking %d item%s into the Deskbar", count,
                     count == 1 ? "" : "s");
        }
        return 1;
    }
    if (drop == TIKU_TRK_DB_DROP_TEAM && team >= 0) {
        const char *types[TIKU_TRK_DRAG_TYPES];
        int type_count = tiku_trk_drag_types(controller->drag);

        for (i = 0; i < type_count; i++) {
            types[i] = controller->drag->type[i];
        }
        if (!tiku_trk_deskbar_team_accepts(controller->deskbar, team,
                types, type_count,
                (modifiers & TIKU_DESK_MOD_CMD) != 0u)) {
            snprintf(controller->status, controller->status_size,
                     "%.40s cannot open %s",
                     tiku_trk_deskbar_team_name(controller->deskbar, team),
                     count == 1 ? "that" : "those");
            return 1;
        }
        snprintf(controller->status, controller->status_size,
                 "handing %d item%s to %.40s", count,
                 count == 1 ? "" : "s",
                 tiku_trk_deskbar_team_name(controller->deskbar, team));
        return 1;
    }
    count = 0;
    for (i = 0; i < controller->drag->count; i++) {
        tiku_trk_model_t model;

        if (controller->backend->ops->stat(controller->backend,
                controller->drag->path[i], &model) == 0 &&
            tiku_trk_deskbar_launchable(&model)) {
            count++;
        }
    }
    snprintf(controller->status, controller->status_size,
             "launching %d item%s", count, count == 1 ? "" : "s");
    return 1;
}

static int
arrange_in_source(tiku_trk_drop_controller_t *controller, int x, int y,
                  unsigned modifiers)
{
    tiku_trk_folder_window_t *folder = *controller->drop_folder;
    int dx;
    int dy;
    int i;
    int count;
    int snap;

    if (*controller->drop_row >= 0 || folder->view == NULL ||
        tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST ||
        strcmp(controller->drag->src_dir, folder->path) != 0) {
        return 0;
    }
    dx = x - controller->drag->x0;
    dy = y - controller->drag->y0;
    snap = (modifiers & TIKU_DESK_MOD_CMD) != 0u;
    count = tiku_trk_poseview_count(folder->view);
    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);
        int px;
        int py;

        if (pose == NULL || !pose->selected) {
            continue;
        }
        tiku_trk_poseview_pose_location(folder->view, pose, &px, &py);
        tiku_trk_poseview_move_pose(folder->view, pose, px + dx, py + dy,
                                    snap);
    }
    if (folder == tiku_trk_desktop_view_folder(controller->desktop_view)) {
        (void)tiku_trk_desktop_view_ensure_visible(controller->desktop_view);
    }
    (void)tiku_trk_poseview_save_state(folder->view);
    *controller->dirty = 1;
    return 1;
}

static int
drop_destination(tiku_trk_drop_controller_t *controller, char *destination,
                 size_t destination_size)
{
    tiku_trk_folder_window_t *folder = *controller->drop_folder;

    if (*controller->drop_row >= 0) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view,
                                                     *controller->drop_row);

        if (pose == NULL) {
            return 0;
        }
        snprintf(destination, destination_size, "%s", pose->model.path);
    } else {
        snprintf(destination, destination_size, "%s", folder->path);
    }
    return 1;
}

static int
drop_allowed(tiku_trk_drop_controller_t *controller,
             const char *destination, unsigned drag_modifiers)
{
    tiku_trk_model_t model;
    const char *reason = NULL;
    char refused[200];

    if (controller->drag->foreign_refs) {
        tiku_trk_foreign_offer_t offer;
        tiku_trk_drop_result_t result;
        const char *types = controller->drag->ntype > 0
                                ? controller->drag->type[0] : "text/uri-list";
        (void)tiku_trk_foreign_offer_parse(&offer, "copy move link", types, 0);
        result = tiku_trk_foreign_drop(&offer, destination[0] != '\0', 0,
                                       strcmp(destination, "/") == 0, 0);
        if (!result.accepted) {
            tiku_trk_alert_open(controller->alert, TIKU_TRK_ALERT_WARN,
                                TIKU_TRK_ALERT_TAG_REFUSAL,
                                "This drop target cannot accept those items.",
                                "Cancel");
            return 0;
        }
    }

    if (controller->backend->ops->stat(controller->backend, destination,
                                       &model) != 0 ||
        !tiku_trk_model_is_mutable_container(&model)) {
        reason = "You can't move or copy items to read-only volumes.";
    } else if (tiku_trk_fs_in_trash(controller->fs, destination)) {
        if ((drag_modifiers & TIKU_TRK_DRAG_MOD_LINK) != 0u) {
            reason = "Sorry, you can't create links in the Trash.";
        } else if ((drag_modifiers & TIKU_TRK_DRAG_MOD_COPY) != 0u) {
            reason = "Sorry, you can't copy items to the Trash.";
        }
    }
    if (reason == NULL && tiku_trk_fs_check_move(controller->fs,
            controller->drag->path[0], destination, refused,
            sizeof refused) != TIKU_TRK_REFUSE_NONE) {
        reason = refused;
    }
    if (reason != NULL) {
        tiku_trk_alert_open(controller->alert, TIKU_TRK_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_REFUSAL, reason, "Cancel");
        return 0;
    }
    return 1;
}

static void
reserve_icon_landings(tiku_trk_drop_controller_t *controller,
                      const char **paths, int count, int x, int y)
{
    tiku_trk_folder_window_t *folder = *controller->drop_folder;
    tiku_desk_rect_t content;
    int origin_x = 0;
    int origin_y = 0;
    int i;

    if (*controller->drop_row >= 0 ||
        tiku_trk_poseview_mode(folder->view) == TIKU_TRK_MODE_LIST) {
        return;
    }
    content = tiku_trk_folder_window_content(folder);
    tiku_trk_poseview_origin(folder->view, &origin_x, &origin_y);
    for (i = 0; i < count; i++) {
        const char *name = strrchr(paths[i], '/');
        int landing_x;
        int landing_y;

        tiku_trk_drag_landing(controller->drag, i,
            x - (content.x + 2) + origin_x,
            y - (content.y + 2) + origin_y, &landing_x, &landing_y);
        tiku_trk_poseview_drop_land(folder->view,
            name != NULL ? name + 1 : paths[i], landing_x, landing_y);
    }
}

void
tiku_trk_drop_controller_execute(tiku_trk_drop_controller_t *controller,
                                 int x, int y, unsigned modifiers,
                                 unsigned button)
{
    const char *paths[TIKU_TRK_DRAG_MAX];
    char destination[TIKU_TRK_PATH_MAX];
    unsigned drag_modifiers = 0;
    tiku_trk_drop_t action;
    int same_store;
    int count;

    if (controller == NULL || drop_on_deskbar(controller, x, y, modifiers) ||
        *controller->drop_folder == NULL ||
        arrange_in_source(controller, x, y, modifiers) ||
        !drop_destination(controller, destination, sizeof destination)) {
        return;
    }
    if ((modifiers & TIKU_DESK_MOD_OPTION) != 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_COPY;
    }
    if ((modifiers & TIKU_DESK_MOD_CMD) != 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_LINK;
    }
    if (!drop_allowed(controller, destination, drag_modifiers)) {
        return;
    }
    same_store = tiku_trk_fs_drop_action(controller->fs,
        controller->drag->path[0], destination, 0) == TIKU_TRK_OP_MOVE;
    action = tiku_trk_drag_action(controller->drag, destination,
                                  drag_modifiers, same_store);
    count = drop_paths(controller, paths);
    if (action == TIKU_TRK_DROP_NOTHING || count == 0) {
        return;
    }
    if (button == 3u) {
        tiku_trk_model_t model;
        int destination_read_only = 0;
        int source_read_only = 0;

        if (controller->backend->ops->stat(controller->backend,
                destination, &model) == 0) {
            destination_read_only =
                !tiku_trk_model_is_mutable_container(&model);
        }
        if (controller->backend->ops->stat(controller->backend,
                controller->drag->src_dir, &model) == 0) {
            source_read_only = !tiku_trk_model_is_mutable_container(&model);
        }
        snprintf(controller->destination, sizeof controller->destination,
                 "%s", destination);
        controller->pending = *controller->drag;
        *controller->popup = tiku_trk_cwmenu_drop(controller->menus,
            destination_read_only, source_read_only,
            (modifiers & TIKU_DESK_MOD_SHIFT) ? TIKU_TRK_MOD_SHIFT : 0u);
        *controller->popup_sub = NULL;
        *controller->popup_frame = tiku_trk_menu_place(*controller->popup,
            x, y, (tiku_desk_rect_t){ 0, 0, controller->screen_w,
                                      controller->screen_h });
        return;
    }
    reserve_icon_landings(controller, paths, count, x, y);
    snprintf(controller->progress_destination,
             controller->progress_destination_size, "%s", destination);
    switch (action) {
    case TIKU_TRK_DROP_COPY:
        (void)tiku_trk_fs_copy(controller->fs, paths, count, destination,
                               TIKU_TRK_CONFLICT_ASK);
        snprintf(controller->status, controller->status_size,
                 "copying %d item%s", count, count == 1 ? "" : "s");
        break;
    case TIKU_TRK_DROP_LINK: {
        int relative = (modifiers & TIKU_DESK_MOD_SHIFT) != 0u;

        (void)tiku_trk_fs_link(controller->fs, paths, count, destination,
                               relative);
        snprintf(controller->status, controller->status_size,
                 "%s %d item%s", relative ? "linking (relative)" : "linking",
                 count, count == 1 ? "" : "s");
        break;
    }
    default:
        (void)tiku_trk_fs_move(controller->fs, paths, count, destination,
                               TIKU_TRK_CONFLICT_ASK);
        snprintf(controller->status, controller->status_size,
                 "moving %d item%s", count, count == 1 ? "" : "s");
        break;
    }
}
