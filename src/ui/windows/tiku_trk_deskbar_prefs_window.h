/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_prefs_window.h - Deskbar preferences controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_PREFS_WINDOW_H_
#define TIKU_TRK_DESKBAR_PREFS_WINDOW_H_

#include "tiku_desk_window.h"
#include "tiku_trk_deskbar_prefs.h"

#define TIKU_TRK_DB_PREFS_W 470
#define TIKU_TRK_DB_PREFS_H 580

typedef void (*tiku_trk_deskbar_prefs_open_fn)(void *context,
                                                const char *path);

typedef struct {
    tiku_trk_deskbar_prefs_t model;
    tiku_desk_workspace_t *workspace;
    tiku_desk_window_t *window;
    tiku_trk_deskbar_t *deskbar;
    tiku_trk_store_t *store;
    int screen_w;
    int screen_h;
    int *dirty;
    char menu_dir[TIKU_TRK_PATH_MAX];
    tiku_trk_deskbar_prefs_open_fn open_path;
    void *open_context;
} tiku_trk_deskbar_prefs_window_t;

void tiku_trk_deskbar_prefs_window_init(
    tiku_trk_deskbar_prefs_window_t *controller,
    tiku_desk_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    tiku_trk_store_t *store, int screen_w, int screen_h, const char *menu_dir,
    int *dirty, tiku_trk_deskbar_prefs_open_fn open_path,
    void *open_context);
void tiku_trk_deskbar_prefs_window_destroy(
    tiku_trk_deskbar_prefs_window_t *controller);
void tiku_trk_deskbar_prefs_window_open(
    tiku_trk_deskbar_prefs_window_t *controller);
void tiku_trk_deskbar_prefs_window_close(
    tiku_trk_deskbar_prefs_window_t *controller);
tiku_desk_window_t *tiku_trk_deskbar_prefs_window_handle(
    tiku_trk_deskbar_prefs_window_t *controller);
int tiku_trk_deskbar_prefs_window_key(
    tiku_trk_deskbar_prefs_window_t *controller, const char *key);

#endif /* TIKU_TRK_DESKBAR_PREFS_WINDOW_H_ */
