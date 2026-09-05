/*
 * TikuTracker -- the file manager for TikuOS.
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

/**
 * @brief Set the controller up over the backend, filesystem, settings and
 *        bar, build the Desktop's poses and take the workspace backdrop.
 * @return 0, or -1 for a missing argument.
 */
int tiku_trk_desktop_view_init(tiku_trk_desktop_view_t *controller,
    tiku_backend_t *backend, tiku_fs_t *fs,
    tiku_trk_prefs_t *prefs, tiku_trk_deskbar_t *deskbar,
    tiku_trk_folder_render_t *render, int screen_w, int screen_h);
/** @brief Save the Desktop's pose state and free its view. */
void tiku_trk_desktop_view_destroy(tiku_trk_desktop_view_t *controller);

/** @brief The folder window behind the Desktop, or NULL. */
tiku_trk_folder_window_t *tiku_trk_desktop_view_folder(
    tiku_trk_desktop_view_t *controller);
/** @brief Whether the Disks pose is on the Desktop. */
int tiku_trk_desktop_view_shows_disks(
    const tiku_trk_desktop_view_t *controller);

/** @brief The area the Desktop's poses may occupy, clear of the bar. */
tiku_trk_rect_t tiku_trk_desktop_view_area(
    const tiku_trk_desktop_view_t *controller);
/**
 * @brief Take a new screen size: poses keep their places on the grid of
 *        the area, and any left off the screen are brought back.
 * @return How many poses moved.
 */
int tiku_trk_desktop_view_resize(tiku_trk_desktop_view_t *controller,
    int screen_w, int screen_h);
/**
 * @brief Bring every pose lying off the screen or under the bar back into
 *        the area.
 * @return How many poses moved.
 */
int tiku_trk_desktop_view_ensure_visible(
    tiku_trk_desktop_view_t *controller);
/**
 * @brief Rebuild the Desktop's poses from the volumes, the Trash and the
 *        Desktop folder, place the unplaced and bring them into view.
 */
void tiku_trk_desktop_view_rebuild(tiku_trk_desktop_view_t *controller);
/**
 * @brief Re-ask whether the Trash is full, across every volume (AW-029).
 *
 * The listing diff only re-stats an entry whose own directory changed, and
 * a file trashed on ANOTHER volume changes nothing under the Desktop.  So
 * on the owner's slow beat the desk Trash pose is asked again -- and the
 * answer is the union over the same trash directories the union window
 * lists (PVN-027): a file trashed on a memory stick fills the desk icon
 * even while the home trash is empty.
 *
 * @return nonzero when the icon flipped and a repaint is owed.
 */
void tiku_trk_desktop_view_set_top_inset(tiku_trk_desktop_view_t *controller,
                                         int inset);

/** @brief Re-stat the desk Trash pose's fullness (AW-029). */
int tiku_trk_desktop_view_trash_check(tiku_trk_desktop_view_t *controller);

/**
 * @brief Poll the Desktop's view and its sources for changes and rebuild
 *        what moved.
 * @return How many things changed.
 */
int tiku_trk_desktop_view_poll(tiku_trk_desktop_view_t *controller,
    int64_t now, const tiku_model_change_t *changes, int change_count,
    int complete);

#endif /* TIKU_TRK_DESKTOP_VIEW_H_ */
