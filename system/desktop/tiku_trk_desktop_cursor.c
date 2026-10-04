/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_cursor.c - the pointer's shape: what the desktop
 * offers under it, and what a window's content asks for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/**
 * @brief The pointer says what is under it: the front window's corner
 *        grip offers the diagonal resize, a window's content the shape it
 *        asked for, anything else the arrow.  Only while nothing is being
 *        tracked -- a click on another window is spent activating it, but
 *        its content still says what a press there would reach.  Kept
 *        with no host as well, where a scripted run reads it.
 */
void
desktop_pointer_says(desktop_t *app, int x, int y)
{
    tiku_window_t *under = NULL;
    int want = TIKU_CURSOR_ARROW, hit;

    if (app->resize_track != NULL) {
        /* A corner in hand keeps the arrow whatever is under the
         * pointer: a window that will not shrink past its least size
         * leaves the hand inside it, still resizing. */
        want = TIKU_CURSOR_RESIZE_DIAG;
    } else if (!tiku_trk_drag_active(&app->drag) && app->popup == NULL) {
        hit = tiku_workspace_hit(app->desk, x, y, &under);
        if (hit == TIKU_HIT_RESIZE && under != NULL &&
            under == tiku_workspace_focused(app->desk) && under->resizable) {
            want = TIKU_CURSOR_RESIZE_DIAG;
        } else if (hit == TIKU_HIT_CONTENT && under != NULL) {
            want = under->cursor;
        }
    }
    if (want != app->cursor_kind) {
        app->cursor_kind = want;
        if (app->host != NULL) {
            tiku_host_set_cursor(app->host, want);
        }
    }
}

/** @brief Session @p session's TIKU_MSG_CURSOR: the shape one of its
 *         windows' content asks for, shown at once when the pointer is
 *         over it. */
void
desktop_cursor(desktop_t *app, int session, const tiku_msg_t *msg)
{
    tiku_remote_session_t *s;
    int32_t id = 0, shape = 0;
    int k;

    if (session < 0 || session >= TIKU_REMOTE_SESSIONS ||
        !app->remote.session[session].used ||
        !tiku_msg_find_int32(msg, "window", 0, &id) ||
        !tiku_msg_find_int32(msg, "shape", 0, &shape) ||
        shape < TIKU_CURSOR_ARROW || shape > TIKU_CURSOR_HAND) {
        return;
    }
    s = &app->remote.session[session];
    for (k = 0; k < TIKU_REMOTE_WINDOWS; k++) {
        if (s->win[k].window != NULL && s->win[k].win_id == (uint32_t)id) {
            s->win[k].window->cursor = shape;
            desktop_pointer_says(app, app->ptr_x, app->ptr_y);
            return;
        }
    }
}
