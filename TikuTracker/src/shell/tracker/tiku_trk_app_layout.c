/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_layout.c - the layout, settled after a change: title, rows,
 * writability
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/** @brief The host window's title: the path, prefixed by the device's id. */
void
browser_retitle(app_t *a)
{
    snprintf(a->title, sizeof a->title, "%s%s%s",
             (a->backend->devid[0] != '\0') ? "device:" : "",
             (a->backend->devid[0] != '\0') ? a->backend->devid : "",
             tiku_trk_poseview_path(a->view));
    if (a->backend->devid[0] == '\0') {
        snprintf(a->title, sizeof a->title, "%s",
                 tiku_trk_poseview_path(a->view));
    }
    tiku_host_set_title(a->host, a->title);
}

/**
 * @brief Place anything the restore did not position.
 *
 * Saved positions cover only what the user arranged; everything else -- new
 * entries, and icons auto-placed last time -- must be laid out once the
 * folder has finished loading, or it is simply invisible (PVL-020).
 */
void
browser_settle_layout(app_t *a)
{
    tiku_rect_t lr;

    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST) {
        return;
    }
    lr = browser_list_rect(a);
    (void)tiku_trk_poseview_place_unplaced(a->view, lr.w - 4);
}

/** @brief Apply the shared ShowNavigator preference when its file changes. */
void
browser_sync_navigator_preference(app_t *a, int force)
{
    struct stat st;
    int present, visible = 1, hide_dotfiles = 0;
    int folders_first = 1, filter = 0;
    int64_t now = tiku_trk_now();
    time_t mtime = 0;
    off_t size = 0;

    if (!force && now - a->prefs_checked < 100000) {
        return;
    }
    a->prefs_checked = now;
    present = a->prefs_path[0] != '\0' && stat(a->prefs_path, &st) == 0;
    if (present) {
        mtime = st.st_mtime;
        size = st.st_size;
    }
    if (!force && present == a->prefs_present &&
        (!present || (mtime == a->prefs_mtime && size == a->prefs_size))) {
        return;
    }
    if (present) {
        tiku_trk_prefs_t prefs;
        int dark;

        tiku_trk_prefs_init(&prefs);
        (void)tiku_trk_prefs_load(&prefs, a->prefs_path);
        visible = tiku_trk_prefs_get(&prefs, "ShowNavigator") != 0;
        hide_dotfiles = tiku_trk_prefs_get(&prefs, "HideDotFiles") != 0;
        folders_first = tiku_trk_prefs_get(&prefs, "SortFolderNamesFirst") != 0;
        filter = tiku_trk_prefs_get(&prefs, "TypeAheadFiltering") != 0;
        dark = tiku_trk_prefs_get(&prefs, "DarkMode");
        if (dark < 0 || dark > 2) {
            dark = 0;
        }
        if (dark != a->theme_mode) {
            /* The table swap, live (settings semantics): the browser
             * repaints whole frames, so a change costs one redraw. */
            a->theme_mode = dark;
            tiku_theme_set(dark == 1 ? tiku_theme_dark()
                         : dark == 2 ? tiku_theme_mono() : NULL);
            a->dirty = 1;
        }
        tiku_trk_prefs_free(&prefs);
    }
    a->prefs_present = present;
    a->prefs_mtime = mtime;
    a->prefs_size = size;
    if (visible != a->show_nav) {
        a->show_nav = visible;
        tiku_trk_poseview_set_visible_rows(a->view, browser_visible_rows(a));
        browser_settle_layout(a);
        a->dirty = 1;
    }
    tiku_trk_poseview_set_hide_dotfiles(a->view, hide_dotfiles);
    tiku_trk_poseview_set_folders_first(a->view, folders_first);
    a->filter_pref = filter;
    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST &&
        filter != tiku_trk_poseview_is_filtering(a->view)) {
        /* Live in both directions: on engages filtering for the next
         * keystroke, off drops the terms and restores the listing
         * (TS-052).  Guarded on the current state so an unrelated
         * settings change cannot wipe a filter mid-use. */
        tiku_trk_poseview_set_filtering(a->view, filter);
        a->dirty = 1;
    }
}

/**
 * @brief Copy the selected poses' paths into @p paths, pointing @p out at
 *        each, at most @p max of them.  @return how many were copied.
 */
int
browser_selection_paths(app_t *a, char paths[][TIKU_PATH_MAX],
                const char *out[], int max)
{
    int i, n = tiku_trk_poseview_count(a->view), k = 0;

    for (i = 0; i < n && k < max; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);
        if (p == NULL || !p->selected) {
            continue;
        }
        snprintf(paths[k], TIKU_PATH_MAX, "%s", p->model.path);
        out[k] = paths[k];
        k++;
    }
    return k;
}

/**
 * @brief Ask the store whether this folder can be written to (PVL-076).
 *
 * Asked of the FOLDER rather than assumed from the backend: a device
 * namespace has writable and read-only regions side by side, and the tint
 * has to follow the one being shown.
 */
void
browser_settle_writability(app_t *a)
{
    tiku_model_t m;

    a->read_only = 0;
    if (a->backend == NULL || a->backend->ops->stat == NULL) {
        return;
    }
    if (a->backend->ops->stat(a->backend, tiku_trk_poseview_path(a->view),
                              &m) == 0) {
        a->read_only = ((m.facts.perm & TIKU_P_WRITE) == 0u);
    }
}

/** @brief Refresh the listing after an operation changed the folder. */
