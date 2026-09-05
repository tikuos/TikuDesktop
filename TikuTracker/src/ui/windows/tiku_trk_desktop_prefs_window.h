/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_prefs_window.h - Desktop preferences controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKTOP_PREFS_WINDOW_H_
#define TIKU_TRK_DESKTOP_PREFS_WINDOW_H_

#include "tiku_window.h"
#include "tiku_trk_deskprefs.h"
#include "tiku_trk_desktop_view.h"

#define TIKU_TRK_DESK_PREFS_W 430

typedef struct {
    tiku_trk_deskprefs_t          model;
    tiku_trk_deskprefs_pictures_t pictures;
    tiku_workspace_t        *workspace;
    tiku_window_t           *window;
    tiku_trk_desktop_view_t      *desktop_view;
    tiku_store_t             *store;
    int                           screen_w;
    int                           screen_h;
    int                          *dirty;
    char                          node[TIKU_PATH_MAX];
    char                          pictures_dir[TIKU_PATH_MAX];
} tiku_trk_desktop_prefs_window_t;

/**
 * @brief Bind the controller to the Desktop view, store and workspace and
 *        find the Desktop's own folder.  Opens no window.
 */
void tiku_trk_desktop_prefs_window_init(
    tiku_trk_desktop_prefs_window_t *controller,
    tiku_workspace_t *workspace, tiku_trk_desktop_view_t *desktop_view,
    tiku_store_t *store, int screen_w, int screen_h, int *dirty);
/** @brief Close the window if open; nothing else is held. */
void tiku_trk_desktop_prefs_window_destroy(
    tiku_trk_desktop_prefs_window_t *controller);
/**
 * @brief Open the window centred on the screen, reading the Desktop's
 *        record and pictures afresh; an open window is activated instead.
 */
void tiku_trk_desktop_prefs_window_open(
    tiku_trk_desktop_prefs_window_t *controller);
/** @brief Close the window and ask for a redraw. */
void tiku_trk_desktop_prefs_window_close(
    tiku_trk_desktop_prefs_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_desktop_prefs_window_handle(
    tiku_trk_desktop_prefs_window_t *controller);

#endif /* TIKU_TRK_DESKTOP_PREFS_WINDOW_H_ */
