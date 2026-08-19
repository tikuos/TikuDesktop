/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_lifecycle.h - spatial folder-window lifecycle.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FOLDER_LIFECYCLE_H_
#define TIKU_TRK_FOLDER_LIFECYCLE_H_


#include <stddef.h>

#include "tiku_trk_folder_window.h"

/** @brief Services and storage for spatial folder windows. */
typedef struct {
    tiku_trk_folder_window_t *folders;
    int                       capacity;
    int                      *count;
    int                      *cascade_x;
    int                      *cascade_y;
    tiku_desk_workspace_t    *workspace;
    tiku_trk_backend_t       *backend;
    tiku_trk_store_t         *store;
    tiku_trk_prefs_t         *prefs;
    tiku_trk_folder_render_t *render;
    char                     *status;
    size_t                    status_size;
    int                      *dirty;
    int                       screen_w;
    int                       screen_h;
} tiku_trk_folder_lifecycle_t;

/** @brief Bind folder storage and the services required for its lifetime. */
void tiku_trk_folder_lifecycle_init(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folders, int capacity, int *count,
    int *cascade_x, int *cascade_y, tiku_desk_workspace_t *workspace,
    tiku_trk_backend_t *backend, tiku_trk_store_t *store,
    tiku_trk_prefs_t *prefs, tiku_trk_folder_render_t *render,
    char *status, size_t status_size, int *dirty,
    int screen_w, int screen_h);

/** @brief Open a path or raise the spatial window already showing it. */
tiku_trk_folder_window_t *tiku_trk_folder_lifecycle_open(
    tiku_trk_folder_lifecycle_t *controller, const char *path);

/** @brief Return the folder state owned by a composited window. */
tiku_trk_folder_window_t *tiku_trk_folder_lifecycle_find(
    tiku_trk_folder_lifecycle_t *controller, tiku_desk_window_t *window);

/** @brief Persist and close one folder window. */
void tiku_trk_folder_lifecycle_close(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folder);

/** @brief Save one folder's spatial frame when it remains meaningful. */
void tiku_trk_folder_lifecycle_save_frame(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folder);

/** @brief Adopt a live query run into its own result window. */
tiku_trk_folder_window_t *tiku_trk_folder_lifecycle_open_results(
    tiku_trk_folder_lifecycle_t *controller, const char *title,
    tiku_trk_qrun_t *run);

/** @brief Refresh a live result window without discarding row identity. */
int tiku_trk_folder_lifecycle_refresh_results(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folder);

/** @brief Compose a folder window's title honouring the path preference. */
void tiku_trk_folder_title_for(tiku_trk_prefs_t *prefs,
                               const char *path, char *out, size_t max);

#endif /* TIKU_TRK_FOLDER_LIFECYCLE_H_ */
