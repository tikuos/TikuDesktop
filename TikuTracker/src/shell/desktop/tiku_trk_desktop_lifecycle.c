/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_lifecycle.c - the way up and the way down: the boot
 * screen's stages, the closing screen, and the answers to the questions the
 * status overlay asks
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/**
 * @brief Hold the line now up for whatever is still owed it.
 *
 * The dwell is paid at the START of the next stage rather than at the end
 * of this one, so the work a stage names counts toward its own dwell: a
 * piece that takes 300ms is shown for 300ms and topped up by nothing,
 * and one that takes 2ms is topped up to the minimum.  What is being
 * paced is the reading, not the loading.  The last line of all has no
 * next stage to pay it, so the caller settles that one itself.
 */
void
splash_settle(desktop_t *app)
{
    int64_t owed;

    if (!app->splash.live) {
        return;
    }
    owed = tiku_trk_splash_owed(&app->splash, tiku_trk_now());
    if (owed > 0) {
        struct timespec ts;

        ts.tv_sec = (time_t)(owed / 1000000);
        ts.tv_nsec = (long)(owed % 1000000) * 1000L;
        (void)nanosleep(&ts, NULL);
    }
}

/** @brief Name the piece now being built, or now being put away. */
void
splash_stage(desktop_t *app, const char *what)
{
    if (!app->splash.live) {
        return;
    }
    splash_settle(app);
    tiku_trk_splash_say(&app->splash, what, tiku_trk_now());
    tiku_trk_splash_draw(&app->splash, app->surf,
                         (tiku_rect_t){ 0, 0, DESK_W, DESK_H },
                         tiku_trk_now());
    if (app->host != NULL) {
        (void)tiku_host_present(app->host, app->surf);
    }
}

/**
 * @brief Take the desktop down behind the screen it came up behind.
 *
 * The shutdown a PERSON asked for, which is the one worth showing: the
 * black comes back over the desktop, and the pieces name themselves as
 * they are put away.  A run with no splash -- scripted, or driven over
 * the conductor channel -- stops where it always stopped.
 */
void
begin_shutdown(desktop_t *app)
{
    tiku_trk_splash_closing(&app->splash, tiku_trk_now());
    if (tiku_trk_splash_closed(&app->splash, tiku_trk_now())) {
        app->running = 0;
        return;
    }
    app->closing = 1;
    app->dirty = 1;
}

/** @brief Hand a modal filesystem answer back to the operation engine. */
void
status_answer(void *context, const tiku_alert_t *alert)
{
    desktop_t *app = context;

    if (alert->tag == TIKU_TRK_ALERT_TAG_CONFLICT) {
        if (alert->chosen == alert->cancel) {
            tiku_fs_cancel(app->fs);
        } else {
            tiku_fs_resolve(app->fs,
                                tiku_trk_alert_conflict_answer(alert));
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_ERROR) {
        tiku_fs_resolve_error(app->fs,
                                  alert->chosen != alert->cancel);
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_SHUTDOWN) {
        if (alert->chosen != alert->cancel) {
            begin_shutdown(app);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_SAVE_REPLACE) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_panel_window_confirmed(&app->file_panel);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_OPEN_ANYWAY) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_open_with_window_confirmed(&app->open_with_window);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_BROKEN_OPEN) {
        if (alert->chosen != alert->cancel &&
            app->broken_open_path[0] != '\0') {
            {
                int islot = tiku_trk_info_windows_open(&app->info_windows,
                                                app->broken_open_path);

                if (islot >= 0) {
                    info_opens_with_fill(app,
                        &app->info_windows.model[islot]);
                }
            }
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_EMPTY_TRASH) {
        if (alert->chosen != alert->cancel) {
            tiku_trk_folder_file_actions_answer_empty(&app->file_actions);
        }
    } else if (alert->tag == TIKU_TRK_ALERT_TAG_DELETE) {
        /* Cancel, Move to Trash, Delete, in the order the buttons are
         * offered (FS-046). */
        folder_t *fl = folder_of(app, tiku_workspace_focused(app->desk));

        if (fl == NULL) {
            fl = desktop_folder(app);
        }
        {
            /* Cancel, Move to Trash, Delete, in the order the buttons
             * are offered (FS-046); the alert's own default already
             * steps left of Delete, so Return lands on Move to Trash. */
            static const tiku_trk_delete_answer_t by_button[] = {
                TIKU_TRK_DELETE_ANSWER_CANCEL,
                TIKU_TRK_DELETE_ANSWER_TRASH,
                TIKU_TRK_DELETE_ANSWER_DELETE,
            };
            int chosen = (alert->chosen >= 0 && alert->chosen < 3)
                             ? alert->chosen : 0;

            tiku_trk_folder_file_actions_answer_delete(&app->file_actions,
                fl, by_button[chosen]);
        }
    }
}

/** @brief Stop asks the operation engine to cancel at its safe boundary. */
void
status_stop(void *context)
{
    desktop_t *app = context;

    tiku_fs_cancel(app->fs);
}
