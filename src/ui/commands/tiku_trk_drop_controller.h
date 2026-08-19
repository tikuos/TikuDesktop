/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drop_controller.h - folder and Deskbar drop execution.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DROP_CONTROLLER_H_
#define TIKU_TRK_DROP_CONTROLLER_H_

#include <stddef.h>

#include "tiku_trk_alert.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_folder_window.h"
#include "tiku_trk_fs.h"
#include "tiku_trk_color.h"

typedef void (*tiku_trk_drop_color_fn)(void *context,
                                       const tiku_trk_color_t *color);

struct tiku_trk_apps;
struct tiku_trk_app;
typedef int (*tiku_trk_drop_launch_fn)(void *context,
    const struct tiku_trk_app *application, const char *const *paths,
    int count);

/** @brief Dependencies and retained decision state for a pointer drop. */
typedef struct {
    tiku_trk_deskbar_t *deskbar;
    tiku_trk_backend_t *backend;
    tiku_trk_fs_t *fs;
    tiku_trk_desktop_view_t *desktop_view;
    tiku_trk_cwmenu_t *menus;
    tiku_trk_alert_t *alert;
    tiku_trk_drag_t *drag;
    tiku_trk_folder_window_t **drop_folder;
    int *drop_row;
    tiku_trk_drag_t pending;
    char destination[TIKU_TRK_PATH_MAX];
    tiku_trk_menu_t **popup;
    tiku_trk_menu_t **popup_sub;
    tiku_desk_rect_t *popup_frame;
    char *progress_destination;
    size_t progress_destination_size;
    char *status;
    size_t status_size;
    int *dirty;
    tiku_trk_drop_color_fn set_color;
    void *color_context;
    /* A drop landing on an application pose is a LAUNCH, not a move
     * (PVS-044): the registry says which poses are applications, and the
     * shell supplies the one act that runs one. */
    const struct tiku_trk_apps *apps;
    tiku_trk_drop_launch_fn launch;
    void *launch_context;
    int screen_w;
    int screen_h;
} tiku_trk_drop_controller_t;

void tiku_trk_drop_controller_init(tiku_trk_drop_controller_t *controller,
    tiku_trk_deskbar_t *deskbar, tiku_trk_backend_t *backend,
    tiku_trk_fs_t *fs, tiku_trk_desktop_view_t *desktop_view,
    tiku_trk_cwmenu_t *menus, tiku_trk_alert_t *alert,
    tiku_trk_drag_t *drag, tiku_trk_folder_window_t **drop_folder,
    int *drop_row, tiku_trk_menu_t **popup, tiku_trk_menu_t **popup_sub,
    tiku_desk_rect_t *popup_frame, char *progress_destination,
    size_t progress_destination_size, char *status, size_t status_size,
    int *dirty, int screen_w, int screen_h,
    tiku_trk_drop_color_fn set_color, void *color_context);
void tiku_trk_drop_controller_execute(tiku_trk_drop_controller_t *controller,
                                      int x, int y, unsigned modifiers,
                                      unsigned button);

/** @brief Teach the controller which poses are applications (PVS-044). */
void tiku_trk_drop_controller_set_launcher(
    tiku_trk_drop_controller_t *controller, const struct tiku_trk_apps *apps,
    tiku_trk_drop_launch_fn launch, void *launch_context);

#endif /* TIKU_TRK_DROP_CONTROLLER_H_ */
