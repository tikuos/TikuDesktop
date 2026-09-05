/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_navigate.c - navigation: opening, back, forward, up, and the
 * location field
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/**
 * @brief The expanding-rectangle animation over a pose being opened.
 *
 * Seven steps, ten milliseconds apart, growing two pixels each time and
 * inverted so no colour has to be chosen or restored (PVS-013, PVL-073).
 * Drawn only for a MOUSE open: a keyboard open has no clicked rectangle to
 * grow out of.
 */
void
browser_open_animation(app_t *a, tiku_rect_t from)
{
    int k;

    for (k = 0; k < 7; k++) {
        tiku_rect_t r = from;

        r.x -= 2 * k;
        r.y -= 2 * k;
        r.w += 4 * k;
        r.h += 4 * k;
        tiku_invert_frame(a->surf, r);
        browser_blit(a);
        usleep(10000);
        tiku_invert_frame(a->surf, r);   /* inversion undoes itself */
    }
    browser_blit(a);
}

/**
 * @brief Show folder @p path in the window: the view's state and columns
 *        are saved, the directory switched and loaded, the layout and
 *        writability settled, and the destination recorded in the
 *        navigator when @p record is set.  @return 1 when the window now
 *        shows @p path (already, or after the switch), 0 when @p path is
 *        empty, not a container, or refused by the store.
 */
int
browser_navigate_to(app_t *a, const char *path, int record)
{
    char dest[TIKU_PATH_MAX];
    tiku_model_t m;

    if (path == NULL || path[0] == '\0' ||
        (a->backend->ops->stat != NULL &&
         (a->backend->ops->stat(a->backend, path, &m) != 0 ||
          !tiku_model_is_container(&m)))) {
        return 0;
    }
    /* Opening a pose passes storage owned by the view.  Switching folders
     * replaces that pose array, so keep the destination independent of it. */
    snprintf(dest, sizeof dest, "%s", path);
    if (strcmp(dest, tiku_trk_poseview_path(a->view)) == 0) {
        return 1;
    }
    tiku_scroll_detach(&a->sb);
    (void)tiku_trk_poseview_save_state(a->view);
    (void)tiku_trk_poseview_save_columns(a->view);
    /* switch_dir returns the new row count, and only a negative value is an
     * error.  A non-empty directory is therefore a successful switch. */
    if (tiku_trk_poseview_switch_dir(a->view, dest) < 0) {
        tiku_scroll_attach(&a->sb);
        return 0;
    }
    (void)tiku_trk_poseview_load_begin(a->view, tiku_trk_now());
    tiku_scroll_attach(&a->sb);
    (void)tiku_trk_poseview_load_step(a->view, tiku_trk_now());
    (void)tiku_trk_poseview_restore_state(a->view);
    (void)tiku_trk_poseview_restore_columns(a->view);
    browser_settle_layout(a);
    browser_settle_writability(a);
    if (record) {
        tiku_trk_navigator_set(&a->nav, dest);
    }
    browser_retitle(a);
    a->dirty = 1;
    return 1;
}

/** @brief Step to the preceding history entry. */
void
browser_navigate_back(app_t *a)
{
    char path[TIKU_PATH_MAX];

    if (tiku_trk_navigator_back(&a->nav, path, sizeof path) &&
        !browser_navigate_to(a, path, 0)) {
        (void)tiku_trk_navigator_forward(&a->nav, path, sizeof path);
    }
}

/** @brief Step to the history entry most recently left by Back. */
void
browser_navigate_forward(app_t *a)
{
    char path[TIKU_PATH_MAX];

    if (tiku_trk_navigator_forward(&a->nav, path, sizeof path) &&
        !browser_navigate_to(a, path, 0)) {
        (void)tiku_trk_navigator_back(&a->nav, path, sizeof path);
    }
}

/** @brief Navigate to the enclosing folder and record that new branch. */
void
browser_navigate_up(app_t *a)
{
    char path[TIKU_PATH_MAX];

    if (tiku_trk_navigator_parent(tiku_trk_poseview_path(a->view), path,
                                  sizeof path)) {
        (void)browser_navigate_to(a, path, 1);
    }
}

/** @brief Give the location field the keyboard with its path selected. */
void
browser_begin_location(app_t *a)
{
    browser_end_rename(a, 1);
    a->nav_editing = 1;
    tiku_field_start(&a->nav_edit,
                        tiku_trk_navigator_current(&a->nav));
    a->dirty = 1;
}

/** @brief Apply or abandon the location field without surfacing bad paths. */
void
browser_end_location(app_t *a, int apply)
{
    char path[TIKU_PATH_MAX];

    if (!a->nav_editing) {
        return;
    }
    snprintf(path, sizeof path, "%s",
             (a->nav_edit.text[0] != '\0') ? a->nav_edit.text : "/");
    tiku_field_stop(&a->nav_edit);
    a->nav_editing = 0;
    if (apply) {
        (void)browser_navigate_to(a, path, 1);
    }
    a->dirty = 1;
}

/** @brief Open the active pose: a container navigates, a value is reported. */
void
browser_open_selection(app_t *a)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_active(a->view);

    if (p == NULL) {
        return;
    }
    if (tiku_model_is_container(&p->model)) {
        (void)browser_navigate_to(a, p->model.path, 1);
    }
    a->dirty = 1;
}
