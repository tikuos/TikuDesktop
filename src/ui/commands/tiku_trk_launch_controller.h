/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_launch_controller.h - application launch presentation services.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_LAUNCH_CONTROLLER_H_
#define TIKU_TRK_LAUNCH_CONTROLLER_H_

#include <stddef.h>

#include "tiku_trk_folder_window.h"
#include "tiku_trk_launch.h"
#include "tiku_trk_menu.h"
#include "tiku_trk_state.h"

typedef void (*tiku_trk_launch_open_path_fn)(void *context,
                                             const char *path);

/** @brief Shared application registry and shell launch result sink. */
typedef struct {
    tiku_trk_apps_t              *apps;
    tiku_trk_store_t             *store;
    char                         *status;
    size_t                        status_size;
    tiku_trk_launch_open_path_fn  open_path;
    void                         *open_context;
} tiku_trk_launch_controller_t;

/** @brief Bind the application registry, status sink, and Tracker callback. */
void tiku_trk_launch_controller_init(tiku_trk_launch_controller_t *controller,
    tiku_trk_apps_t *apps, tiku_trk_store_t *store, char *status,
    size_t status_size, tiku_trk_launch_open_path_fn open_path,
    void *open_context);

/** @brief Return the preferred application for one model, if any. */
const tiku_trk_app_t *tiku_trk_launch_controller_handler(
    tiku_trk_launch_controller_t *controller,
    const tiku_trk_model_t *model);
/** @brief Launch paths and publish the semantic result to the status sink. */
int tiku_trk_launch_controller_run(tiku_trk_launch_controller_t *controller,
    const tiku_trk_app_t *application, const char *const *paths, int count);
/** @brief Rebuild an Open-with menu for the folder's current selection. */
void tiku_trk_launch_controller_build_menu(
    tiku_trk_launch_controller_t *controller, tiku_trk_menu_t *menu,
    tiku_trk_folder_window_t *folder);
/** @brief Invoke one entry from the freshly rebuilt Open-with candidates. */
void tiku_trk_launch_controller_pick(
    tiku_trk_launch_controller_t *controller,
    tiku_trk_folder_window_t *folder, int index);

#endif /* TIKU_TRK_LAUNCH_CONTROLLER_H_ */
