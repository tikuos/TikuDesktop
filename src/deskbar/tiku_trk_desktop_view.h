/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_view.h - the Desktop pose-view controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKTOP_VIEW_H_
#define TIKU_TRK_DESKTOP_VIEW_H_

#include <stdint.h>

#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskicons.h"
#include "tiku_trk_folder_window.h"
#include "tiku_fs.h"
#include "tiku_trk_prefs.h"
#include "tiku_vdir.h"

/** @brief Desktop view state, including its union source and watcher. */
typedef struct {
    tiku_trk_folder_window_t folder;
    tiku_vdir_t          sources;
    tiku_vdir_watch_t    watch;
    tiku_backend_t      *backend;
    tiku_fs_t           *fs;
    tiku_trk_prefs_t        *prefs;
    tiku_trk_deskbar_t      *deskbar;
    int                      screen_w;
    int                      screen_h;
    int                      disks_shown;
    /* Pixels at the top the poses must keep clear (the global menu
     * bar's strip). */
    int                      top_inset;
} tiku_trk_desktop_view_t;

int tiku_trk_desktop_view_init(tiku_trk_desktop_view_t *controller,
    tiku_backend_t *backend, tiku_fs_t *fs,
    tiku_trk_prefs_t *prefs, tiku_trk_deskbar_t *deskbar,
    tiku_trk_folder_render_t *render, int screen_w, int screen_h);
void tiku_trk_desktop_view_destroy(tiku_trk_desktop_view_t *controller);

tiku_trk_folder_window_t *tiku_trk_desktop_view_folder(
    tiku_trk_desktop_view_t *controller);
int tiku_trk_desktop_view_shows_disks(
    const tiku_trk_desktop_view_t *controller);

tiku_trk_rect_t tiku_trk_desktop_view_area(
    const tiku_trk_desktop_view_t *controller);
int tiku_trk_desktop_view_resize(tiku_trk_desktop_view_t *controller,
    int screen_w, int screen_h);
int tiku_trk_desktop_view_ensure_visible(
    tiku_trk_desktop_view_t *controller);
void tiku_trk_desktop_view_rebuild(tiku_trk_desktop_view_t *controller);
/** @brief Keep @p inset pixels at the top clear of poses. */
void tiku_trk_desktop_view_set_top_inset(tiku_trk_desktop_view_t *controller,
                                         int inset);

/** @brief Re-stat the desk Trash pose's fullness (AW-029). */
int tiku_trk_desktop_view_trash_check(tiku_trk_desktop_view_t *controller);

int tiku_trk_desktop_view_poll(tiku_trk_desktop_view_t *controller,
    int64_t now, const tiku_model_change_t *changes, int change_count,
    int complete);

#endif /* TIKU_TRK_DESKTOP_VIEW_H_ */
