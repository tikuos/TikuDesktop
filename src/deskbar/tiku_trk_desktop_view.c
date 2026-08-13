/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_view.c - the Desktop pose-view controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_trk_desktop_view.h"
#include "tiku_trk_deskicons.h"
#include "tiku_trk_volume.h"

static void
draw(tiku_desk_surface_t *surface, void *context)
{
    tiku_trk_desktop_view_t *controller = context;

    if (controller != NULL && controller->folder.view != NULL) {
        tiku_trk_folder_window_draw_icons(&controller->folder, surface,
            tiku_trk_folder_window_content(&controller->folder));
    }
}

tiku_trk_folder_window_t *
tiku_trk_desktop_view_folder(tiku_trk_desktop_view_t *controller)
{
    return controller != NULL ? &controller->folder : NULL;
}

int
tiku_trk_desktop_view_shows_disks(
    const tiku_trk_desktop_view_t *controller)
{
    return controller != NULL ? controller->disks_shown : 0;
}

tiku_trk_rect_t
tiku_trk_desktop_view_area(const tiku_trk_desktop_view_t *controller)
{
    tiku_desk_rect_t frame;
    tiku_trk_rect_t area;

    if (controller == NULL || controller->deskbar == NULL) {
        return (tiku_trk_rect_t){ 0, 0, 0, 0 };
    }
    frame = tiku_trk_deskbar_frame(controller->deskbar);
    area = (tiku_trk_rect_t){ 0, 0, controller->screen_w,
                              controller->screen_h };
    if (frame.w <= 0 || frame.h <= 0) {
        return area;
    }
    if (frame.w < frame.h) {
        if (frame.x > controller->screen_w / 2) {
            area.w = frame.x - TIKU_TRK_DESK_BAR_MARGIN;
        } else {
            area.x = frame.x + frame.w + TIKU_TRK_DESK_BAR_MARGIN;
            area.w = controller->screen_w - area.x;
        }
    } else if (frame.y > controller->screen_h / 2) {
        area.h = frame.y - TIKU_TRK_DESK_BAR_MARGIN;
    } else {
        area.y = frame.y + frame.h + TIKU_TRK_DESK_BAR_MARGIN;
        area.h = controller->screen_h - area.y;
    }
    return area;
}

static void
add_pose(tiku_trk_desktop_view_t *controller, const char *path)
{
    tiku_trk_model_t model;

    if (path == NULL || path[0] == '\0' ||
        controller->backend->ops->stat(controller->backend, path,
                                       &model) != 0) {
        return;
    }
    /* The view's arrival path chooses the same free slot as a watched
     * Desktop entry, so synthetic Disks and Trash poses obey one layout. */
    (void)tiku_trk_poseview_adopt(controller->folder.view, &model);
}

int
tiku_trk_desktop_view_ensure_visible(tiku_trk_desktop_view_t *controller)
{
    tiku_trk_rect_t screen;
    tiku_trk_rect_t area;
    tiku_desk_rect_t frame;
    tiku_trk_rect_t bar;
    int i, count, moved = 0;

    if (controller == NULL || controller->folder.view == NULL) {
        return 0;
    }
    screen = (tiku_trk_rect_t){ 0, 0, controller->screen_w,
                                controller->screen_h };
    area = tiku_trk_desktop_view_area(controller);
    frame = tiku_trk_deskbar_frame(controller->deskbar);
    bar = (tiku_trk_rect_t){ frame.x, frame.y, frame.w, frame.h };
    count = tiku_trk_poseview_count(controller->folder.view);
    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(controller->folder.view,
                                                     i);
        tiku_trk_rect_t rect;
        int x, y, w, h;

        if (pose == NULL || !pose->placed) {
            continue;
        }
        tiku_trk_poseview_pose_rect(controller->folder.view, pose,
                                    &x, &y, &w, &h);
        rect = (tiku_trk_rect_t){ x, y, w, h };
        if (tiku_trk_deskicons_valid(rect, screen, bar, NULL, 0)) {
            continue;
        }
        if (tiku_trk_deskicons_pull_back(&rect, area)) {
            int location_x, location_y;

            /* A pose location is the art anchor, while its rectangle also
             * contains the label; preserve that offset when pulling it in. */
            tiku_trk_poseview_pose_location(controller->folder.view, pose,
                                             &location_x, &location_y);
            tiku_trk_poseview_move_pose(controller->folder.view, pose,
                location_x + rect.x - x, location_y + rect.y - y, 0);
            moved++;
        }
    }
    return moved;
}

void
tiku_trk_desktop_view_rebuild(tiku_trk_desktop_view_t *controller)
{
    tiku_trk_volumes_t volumes;
    tiku_trk_model_t models[64];
    char trash[TIKU_TRK_PATH_MAX];
    int count = 0;

    if (controller == NULL || controller->folder.view == NULL) {
        return;
    }
    if (tiku_trk_volumes_scan(&volumes) >= 0) {
        tiku_trk_deskicons_sources(&volumes, controller->backend,
            controller->folder.path, &controller->sources);
    }
    count = tiku_trk_vdir_list(&controller->sources, controller->backend,
        models, (int)(sizeof models / sizeof models[0]));
    if (count < 0) {
        count = 0;
    }
    (void)tiku_trk_poseview_set_models(controller->folder.view, models,
                                       count);
    (void)tiku_trk_poseview_restore_state(controller->folder.view);
    tiku_trk_poseview_set_mode(controller->folder.view,
        tiku_trk_deskicons_mode(
            tiku_trk_poseview_mode(controller->folder.view)));
    (void)tiku_trk_poseview_place_unplaced(controller->folder.view,
        tiku_trk_desktop_view_area(controller).w);
    controller->disks_shown = tiku_trk_deskicons_show_disks(
        tiku_trk_prefs_get(controller->prefs, "ShowDisksIcon"),
        tiku_trk_prefs_get(controller->prefs, "MountVolumesOntoDesktop"));
    if (controller->disks_shown) {
        add_pose(controller, "/");
    }
    if (tiku_trk_fs_trash_dir(controller->fs, controller->folder.path,
                              trash, sizeof trash) == 0) {
        add_pose(controller, trash);
    }
    (void)tiku_trk_poseview_place_unplaced(controller->folder.view,
        tiku_trk_desktop_view_area(controller).w);
    (void)tiku_trk_desktop_view_ensure_visible(controller);
}

int
tiku_trk_desktop_view_init(tiku_trk_desktop_view_t *controller,
    tiku_trk_backend_t *backend, tiku_trk_fs_t *fs,
    tiku_trk_prefs_t *prefs, tiku_trk_deskbar_t *deskbar,
    tiku_trk_folder_render_t *render, int screen_w, int screen_h)
{
    const char *home;

    if (controller == NULL || backend == NULL || fs == NULL ||
        prefs == NULL || deskbar == NULL || render == NULL) {
        return -1;
    }
    memset(controller, 0, sizeof *controller);
    controller->backend = backend;
    controller->fs = fs;
    controller->prefs = prefs;
    controller->deskbar = deskbar;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    home = getenv("HOME");
    snprintf(controller->folder.path, sizeof controller->folder.path,
             "%s/Desktop", home != NULL && home[0] != '\0' ? home : "");
    controller->folder.view = tiku_trk_poseview_new(
        backend, controller->folder.path);
    if (controller->folder.view == NULL) {
        return -1;
    }
    tiku_trk_folder_window_configure(&controller->folder, render);
    tiku_trk_poseview_set_viewport(controller->folder.view,
                                   screen_w, screen_h);
    tiku_trk_poseview_set_mode(controller->folder.view,
                               TIKU_TRK_MODE_ICON);
    tiku_trk_desktop_view_rebuild(controller);
    tiku_trk_vdir_watch_init(&controller->watch, &controller->sources);
    tiku_desk_workspace_set_backdrop(render->workspace, draw, controller);
    return 0;
}

int
tiku_trk_desktop_view_poll(tiku_trk_desktop_view_t *controller,
    int64_t now, const tiku_trk_change_t *changes, int change_count,
    int complete)
{
    unsigned flags;
    int gone = -1;
    int changed = 0;
    int want_disks;

    if (controller == NULL || controller->folder.view == NULL) {
        return 0;
    }
    flags = tiku_trk_poseview_poll(controller->folder.view, now, changes,
                                   change_count, complete);
    if (flags != TIKU_TRK_NOTIFY_NOTHING) {
        (void)tiku_trk_poseview_place_unplaced(controller->folder.view,
            tiku_trk_desktop_view_area(controller).w);
        (void)tiku_trk_desktop_view_ensure_visible(controller);
        changed++;
    }
    want_disks = tiku_trk_deskicons_show_disks(
        tiku_trk_prefs_get(controller->prefs, "ShowDisksIcon"),
        tiku_trk_prefs_get(controller->prefs, "MountVolumesOntoDesktop"));
    if (want_disks != controller->disks_shown) {
        tiku_trk_desktop_view_rebuild(controller);
        changed++;
    }
    if (tiku_trk_vdir_poll(&controller->sources, &controller->watch,
                           &gone) != TIKU_TRK_VDIR_CH_NONE) {
        tiku_trk_desktop_view_rebuild(controller);
        changed++;
    }
    return changed;
}

void
tiku_trk_desktop_view_destroy(tiku_trk_desktop_view_t *controller)
{
    if (controller == NULL || controller->folder.view == NULL) {
        return;
    }
    (void)tiku_trk_poseview_save_state(controller->folder.view);
    tiku_trk_poseview_free(controller->folder.view);
    controller->folder.view = NULL;
}
