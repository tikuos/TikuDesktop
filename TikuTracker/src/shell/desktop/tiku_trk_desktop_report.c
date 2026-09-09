/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_report.c - a report on demand.
 *
 * SIGUSR2 asks; the loop writes, on its next turn, what is safe to read
 * from the loop and nowhere else: the mounts and their ports, the
 * channels, the sessions and what they are, the applications hosted in
 * this process, a run in progress and where it is, a look in progress
 * and where it is -- and then the trail.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

#include "tiku_report.h"
#include "tiku_trail.h"

/** @brief A run's state as the trail names it. */
static const char *
run_state_name(int state)
{
    static const char *const names[] = {
        "SENDING", "RUNNING", "ASKING", "ENDED", "STOPPED", "FAILED"
    };

    return (state >= 0 && state < 6) ? names[state] : "?";
}

/** @brief What the desk holds, printed for the report. */
static void
snapshot(FILE *f, void *ctx)
{
    desktop_t *app = ctx;
    int i, n = 0;

    fprintf(f, "mounts:\n");
    if (app->backend != NULL) {
        for (i = 0; i < tiku_mount_slots(app->backend); i++) {
            const char *prefix = tiku_mount_prefix_at(app->backend, i);
            tiku_backend_t *b = tiku_mount_at(app->backend, i);
            char port[128] = "";

            if (prefix == NULL || b == NULL) {
                continue;
            }
                        (void)tiku_backend_tiku_port(b, port, sizeof port);
            fprintf(f, "  %s%s%s\n", prefix,
                    (port[0] != '\0') ? " on " : "", port);
            n++;
        }
    }
    if (n == 0) {
        fprintf(f, "  none\n");
    }

    /* Channels have slots of their own, not the mounts': a window
     * session on a board's marked channel, counted with what it lost. */
    fprintf(f, "channels:\n");
    for (i = 0, n = 0; i < TIKU_MOUNT_MAX; i++) {
        if (app->channels[i] != NULL) {
            fprintf(f, "  fd %d, %d lost\n",
                    tiku_channel_fd(app->channels[i]),
                    tiku_channel_losses(app->channels[i]));
            n++;
        }
    }
    if (n == 0) {
        fprintf(f, "  none\n");
    }

    fprintf(f, "sessions:\n");
    for (i = 0, n = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        const tiku_remote_session_t *s = &app->remote.session[i];
        int w, windows = 0;

        if (!s->used) {
            continue;
        }
        for (w = 0; w < TIKU_REMOTE_WINDOWS; w++) {
            windows += (s->win[w].window != NULL);
        }
        fprintf(f, "  %s, %d window%s\n",
                (s->name[0] != '\0') ? s->name : "(unnamed)",
                windows, (windows == 1) ? "" : "s");
        n++;
    }
    if (n == 0) {
        fprintf(f, "  none\n");
    }

    fprintf(f, "hosted here:\n");
    for (i = 0, n = 0; i < TIKU_TRK_EMBED_MAX; i++) {
        if (app->embeds[i].running && app->embeds[i].descriptor != NULL) {
            fprintf(f, "  %s\n", app->embeds[i].descriptor->name);
            n++;
        }
    }
    if (n == 0) {
        fprintf(f, "  none\n");
    }

    if (app->board_run != NULL) {
        fprintf(f, "a run in progress: %s on %s, %s%s\n",
                app->board_run_saving ? "a save" : "a program",
                app->board_run_name, run_state_name(app->board_run_state),
                app->board_run_asked ? ", at an INPUT" : "");
    } else {
        fprintf(f, "no run in progress\n");
    }
    if (app->look.going) {
        fprintf(f, "a look in progress: port %d of %d, at %s\n",
                app->look.at + 1, app->look.count,
                (app->look.at < app->look.count)
                    ? app->look.port[app->look.at] : "the end");
    } else {
        fprintf(f, "no look in progress\n");
    }
}

/** @brief Write a report now: the snapshot and the trail.  0 when written. */
int
desktop_report_write(desktop_t *app, const char *why, char *out,
                     size_t max)
{
    int rc = tiku_report_write(why, snapshot, app, out, max);

    tiku_trail("report %s: %s", (rc == 0) ? "written" : "refused",
               (rc == 0) ? out : why);
    return rc;
}

/** @brief Write one if SIGUSR2 asked since the last turn. */
void
desktop_report_poll(desktop_t *app)
{
    char path[512];

    if (tiku_report_asked()) {
        (void)desktop_report_write(app, "SIGUSR2", path, sizeof path);
    }
}
