/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_prefs_window.h - preferences window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_PREFS_WINDOW_H_
#define TIKU_TRK_PREFS_WINDOW_H_

#include "tiku_desk_window.h"
#include "tiku_trk_model.h"
#include "tiku_trk_prefs.h"

/** @brief Preferences state together with the one window that edits it. */
typedef struct {
    tiku_trk_prefs_t       model;
    tiku_desk_workspace_t *workspace;
    tiku_desk_window_t    *window;
    tiku_trk_page_t        page;
    char                   path[TIKU_TRK_PATH_MAX];
    int                   *dirty;
    const int             *blocked;
    int                    screen_w;
    int                    screen_h;
} tiku_trk_prefs_window_t;

void tiku_trk_prefs_window_init(tiku_trk_prefs_window_t *controller,
    tiku_desk_workspace_t *workspace, int screen_w, int screen_h,
    const char *settings_path, int *dirty, const int *blocked);
void tiku_trk_prefs_window_destroy(tiku_trk_prefs_window_t *controller);

/** @brief The shared preferences model read by the rest of Tracker. */
tiku_trk_prefs_t *tiku_trk_prefs_window_model(
    tiku_trk_prefs_window_t *controller);

/** @brief Current native desktop window, or NULL before the first open. */
tiku_desk_window_t *tiku_trk_prefs_window_handle(
    tiku_trk_prefs_window_t *controller);

/** @brief Open the preferences window or activate its existing instance. */
void tiku_trk_prefs_window_open(tiku_trk_prefs_window_t *controller);

#endif /* TIKU_TRK_PREFS_WINDOW_H_ */
