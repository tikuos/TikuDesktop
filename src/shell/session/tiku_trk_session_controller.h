/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_session_controller.h - desktop session restore and persistence.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SESSION_CONTROLLER_H_
#define TIKU_TRK_SESSION_CONTROLLER_H_

#include "tiku_trk_deskbar.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_folder_lifecycle.h"
#include "tiku_trk_fs.h"
#include "tiku_trk_model.h"
#include "tiku_trk_state.h"

/** @brief Session-owned services shared by startup and shutdown. */
typedef struct {
    tiku_trk_store_t *store;
    tiku_trk_deskbar_t *deskbar;
    tiku_trk_desktop_view_t *desktop_view;
    tiku_trk_folder_lifecycle_t *folders;
} tiku_trk_session_controller_t;

tiku_trk_store_t *tiku_trk_session_open_store(tiku_trk_backend_t *backend);
void tiku_trk_session_controller_init(
    tiku_trk_session_controller_t *controller, tiku_trk_store_t *store,
    tiku_trk_deskbar_t *deskbar, tiku_trk_desktop_view_t *desktop_view,
    tiku_trk_folder_lifecycle_t *folders);
void tiku_trk_session_restore_deskbar(
    tiku_trk_session_controller_t *controller);
void tiku_trk_session_restore_folders(
    tiku_trk_session_controller_t *controller, const char *const *paths,
    int path_count, int safe_mode, int device_backend);
void tiku_trk_session_controller_save(
    tiku_trk_session_controller_t *controller);
void tiku_trk_session_drain_filesystem(tiku_trk_fs_t *filesystem);

#endif /* TIKU_TRK_SESSION_CONTROLLER_H_ */
