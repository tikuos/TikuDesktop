/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_roster.c - Deskbar process and window publication.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_sys.h"
#include "tiku_trk_deskbar_roster.h"

/** @brief What one pass of the roster is building, across the callbacks. */
struct roster_pass {
    tiku_trk_deskbar_t *deskbar;
    long                live[512];
    int                 nlive;
    int                 changed;
};

/** @brief One running program, offered by the system seam. */
static void
roster_note(const tiku_sys_process_t *process, void *ctx)
{
    struct roster_pass *pass = ctx;

    /* Tracker is seeded by its shell, so discovering it again would
     * publish the same application twice. */
    if (strcmp(process->name, "desktop") == 0) {
        return;
    }
    (void)tiku_trk_deskbar_add_team_pid(pass->deskbar, process->name,
                                        process->pid, process->command,
                                        &pass->changed);
    if (pass->nlive < (int)(sizeof pass->live / sizeof pass->live[0])) {
        pass->live[pass->nlive++] = process->pid;
    }
}

int
tiku_trk_deskbar_scan_processes(tiku_trk_deskbar_t *deskbar)
{
    struct roster_pass pass;

    if (deskbar == NULL) {
        return 0;
    }
    memset(&pass, 0, sizeof pass);
    pass.deskbar = deskbar;
    if (tiku_sys_processes(roster_note, &pass) < 0) {
        return 0;
    }
    /* The other half of a roster: what QUIT since last time.  An entry
     * whose last instance went disappears (DB-015). */
    if (tiku_trk_deskbar_prune_teams(deskbar, pass.live, pass.nlive)) {
        pass.changed = 1;
    }
    return pass.changed;
}

void
tiku_trk_deskbar_publish_workspace(tiku_trk_deskbar_t *deskbar,
    int team, tiku_workspace_t *workspace)
{
    tiku_trk_dbwin_t windows[TIKU_TRK_DB_WINS_MAX];
    tiku_window_t *focused;
    int i, count, published = 0;

    if (deskbar == NULL || workspace == NULL) {
        return;
    }
    focused = tiku_workspace_focused(workspace);
    count = tiku_workspace_count(workspace);
    for (i = 0; i < count && published < TIKU_TRK_DB_WINS_MAX; i++) {
        tiku_window_t *window = tiku_workspace_at(workspace, i);

        if (window == NULL || !window->open) {
            continue;
        }
        memset(&windows[published], 0, sizeof windows[published]);
        tiku_trk_deskbar_strip_title("TikuTracker", window->title,
            windows[published].title, sizeof windows[published].title);
        windows[published].state = window->minimized
            ? TIKU_TRK_DBW_MINIMIZED : TIKU_TRK_DBW_SHOWN;
        windows[published].current = window == focused;
        windows[published].tag = window->tag;
        published++;
    }
    tiku_trk_deskbar_set_windows(deskbar, team, windows, published,
                                 focused != NULL);
}
