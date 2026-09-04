/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_rename.c - renaming in place
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/** @brief Begin inline rename on the active pose (F2). */
void
browser_begin_rename(app_t *a)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_active(a->view);

    if (p == NULL || a->fs == NULL) {
        return;
    }
    /* A namespace node has no name of its own to change; refuse up front
     * rather than letting the device say no after the fact. */
    if (p->model.kind == TIKU_KIND_DEVICE_NODE ||
        p->model.kind == TIKU_KIND_VIRTUAL_DIR) {
        snprintf(a->status, sizeof a->status,
                 "'%s' is part of the device namespace and cannot be renamed",
                 p->model.name);
        a->dirty = 1;
        return;
    }
    a->editing = 1;
    a->edit_row = browser_pose_index(a, p);
    tiku_field_start(&a->ed, p->model.name);
    /* A character the store will refuse must never reach the field. */
    tiku_field_deny(&a->ed, tiku_fs_name_deny());
    snprintf(a->edit_path, sizeof a->edit_path, "%s", p->model.path);
    a->status[0] = '\0';
    a->dirty = 1;
}

/** @brief Begin the rename the slow-second-click timer just armed. */
void
browser_begin_rename_row(app_t *a, int row)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, row);

    if (p == NULL) {
        return;
    }
    /* The engine armed it on a row; the editor works on the ACTIVE pose, so
     * the two are made to agree before it opens. */
    tiku_trk_poseview_click(a->view, row, 0u);
    browser_begin_rename(a);
}

/** @brief Commit or abandon the inline edit. */
void
browser_end_rename(app_t *a, int commit)
{
    char err[200];

    if (!a->editing) {
        return;
    }
    {   /* MA-050: a name that was not changed is not a rename.  Sent
         * anyway, the store finds the file already there and reports a
         * collision with itself -- an error where the rule wants silence. */
        const char *slash = strrchr(a->edit_path, '/');
        const char *was = (slash != NULL) ? slash + 1 : a->edit_path;

        if (commit && strcmp(was, a->ed.text) == 0) {
            commit = 0;
        }
    }
    if (commit && a->ed.len > 0 &&
        tiku_fs_rename(a->fs, a->edit_path, a->ed.text, err,
                           sizeof err) != 0) {
        /* A rejected name keeps the EDITOR, not just the reason: the text
         * stays, selected whole, so the user retypes immediately instead
         * of starting the rename over (NeXT UIG ch7: a rejected entry is
         * selected and highlighted for retyping). */
        snprintf(a->status, sizeof a->status, "%s", err);
        a->ed.active = 1;               /* Return had already closed it */
        a->ed.sel_a = 0;
        a->ed.sel_b = a->ed.len;
        a->ed.caret = a->ed.len;
        a->dirty = 1;
        return;
    }
    if (commit && a->ed.len > 0) {
        char renamed[TIKU_PATH_MAX];
        char parent[TIKU_PATH_MAX];
        char *slash;
        int row;

        a->status[0] = '\0';
        browser_after_op(a);
        /* The rename re-sorted the row; the application moved it, so the
         * application keeps it visible (NeXT UIG ch3). */
        snprintf(parent, sizeof parent, "%s", a->edit_path);
        slash = strrchr(parent, '/');
        if (slash != NULL) {
            *slash = '\0';
            snprintf(renamed, sizeof renamed, "%s/%s", parent, a->ed.text);
            row = browser_index_of_path(a, renamed);
            if (row >= 0) {
                tiku_trk_poseview_click(a->view, row, 0u);
                tiku_trk_poseview_scroll_into_view(a->view, row,
                                                   browser_visible_rows(a));
            }
        }
    }
    a->editing = 0;
    tiku_field_stop(&a->ed);
    a->dirty = 1;
}
