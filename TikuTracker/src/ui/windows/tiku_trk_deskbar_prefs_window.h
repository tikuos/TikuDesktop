/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_prefs_window.h - Deskbar preferences controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_PREFS_WINDOW_H_
#define TIKU_TRK_DESKBAR_PREFS_WINDOW_H_

#include "tiku_window.h"
#include "tiku_trk_deskbar_prefs.h"

#define TIKU_TRK_DB_PREFS_W 470
#define TIKU_TRK_DB_PREFS_H 580

typedef void (*tiku_trk_deskbar_prefs_open_fn)(void *context,
                                                const char *path);

typedef struct {
    tiku_trk_deskbar_prefs_t model;
    tiku_workspace_t *workspace;
    tiku_window_t *window;
    tiku_trk_deskbar_t *deskbar;
    tiku_store_t *store;
    int screen_w;
    int screen_h;
    int *dirty;
    char menu_dir[TIKU_PATH_MAX];
    tiku_trk_deskbar_prefs_open_fn open_path;
    void *open_context;
} tiku_trk_deskbar_prefs_window_t;

/**
 * @brief Bind the controller to its bar, store and workspace, load the
 *        saved settings and apply them to the bar.  Opens no window.
 */
void tiku_trk_deskbar_prefs_window_init(
    tiku_trk_deskbar_prefs_window_t *controller,
    tiku_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    tiku_store_t *store, int screen_w, int screen_h, const char *menu_dir,
    int *dirty, tiku_trk_deskbar_prefs_open_fn open_path,
    void *open_context);
/** @brief Close the window if open; nothing else is held. */
void tiku_trk_deskbar_prefs_window_destroy(
    tiku_trk_deskbar_prefs_window_t *controller);
/**
 * @brief Open the window at its remembered frame, reloading the settings
 *        first; an open window is activated instead.
 */
void tiku_trk_deskbar_prefs_window_open(
    tiku_trk_deskbar_prefs_window_t *controller);
/** @brief Close the window, remembering its position in the session. */
void tiku_trk_deskbar_prefs_window_close(
    tiku_trk_deskbar_prefs_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_deskbar_prefs_window_handle(
    tiku_trk_deskbar_prefs_window_t *controller);
/**
 * @brief A key while the window is open: a dock key moves the bar.
 *        @return 1 when the key was one.
 */
int tiku_trk_deskbar_prefs_window_key(
    tiku_trk_deskbar_prefs_window_t *controller, const char *key);

#endif /* TIKU_TRK_DESKBAR_PREFS_WINDOW_H_ */
