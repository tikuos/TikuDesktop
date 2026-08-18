/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_roster.c - Deskbar process and window publication.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_deskbar_roster.h"

int
tiku_trk_deskbar_scan_processes(tiku_trk_deskbar_t *deskbar)
{
    DIR *directory;
    struct dirent *entry;
    long live[512];
    int nlive = 0, changed = 0;

    if (deskbar == NULL) {
        return 0;
    }
    directory = opendir("/proc");
    if (directory == NULL) {
        return 0;
    }
    while ((entry = readdir(directory)) != NULL) {
        char path[TIKU_TRK_PATH_MAX], name[64], argv0[160];
        FILE *file;
        size_t length;
        struct stat process;

        if (entry->d_name[0] < '1' || entry->d_name[0] > '9') {
            continue;
        }
        if (snprintf(path, sizeof path, "/proc/%s", entry->d_name) < 0 ||
            stat(path, &process) != 0 || process.st_uid != getuid()) {
            continue;
        }
        /* An empty command line denotes a kernel thread, which has no
         * image or windows for the user to activate. */
        if (snprintf(path, sizeof path, "/proc/%s/cmdline",
                     entry->d_name) < 0) {
            continue;
        }
        file = fopen(path, "r");
        if (file == NULL) {
            continue;
        }
        length = fread(argv0, 1, sizeof argv0 - 1u, file);
        (void)fclose(file);
        if (length == 0u) {
            continue;
        }
        argv0[length < sizeof argv0 - 1u ? length : sizeof argv0 - 1u] =
            '\0';
        if (snprintf(path, sizeof path, "/proc/%s/comm",
                     entry->d_name) < 0) {
            continue;
        }
        file = fopen(path, "r");
        if (file == NULL) {
            continue;
        }
        if (fgets(name, sizeof name, file) == NULL) {
            (void)fclose(file);
            continue;
        }
        (void)fclose(file);
        length = strlen(name);
        while (length > 0u &&
               (name[length - 1u] == '\n' || name[length - 1u] == ' ')) {
            name[--length] = '\0';
        }
        /* Tracker is seeded by its shell, so discovering it again would
         * publish the same application twice. */
        if (name[0] != '\0' && strcmp(name, "desktop") != 0) {
            long pid = atol(entry->d_name);

            (void)tiku_trk_deskbar_add_team_pid(deskbar, name, pid, argv0,
                                                &changed);
            if (nlive < (int)(sizeof live / sizeof live[0])) {
                live[nlive++] = pid;
            }
        }
    }
    (void)closedir(directory);
    /* The other half of a roster: what QUIT since last time.  An entry
     * whose last instance went disappears (DB-015). */
    if (tiku_trk_deskbar_prune_teams(deskbar, live, nlive)) {
        changed = 1;
    }
    return changed;
}

void
tiku_trk_deskbar_publish_workspace(tiku_trk_deskbar_t *deskbar,
    int team, tiku_desk_workspace_t *workspace)
{
    tiku_trk_dbwin_t windows[TIKU_TRK_DB_WINS_MAX];
    tiku_desk_window_t *focused;
    int i, count, published = 0;

    if (deskbar == NULL || workspace == NULL) {
        return;
    }
    focused = tiku_desk_workspace_focused(workspace);
    count = tiku_desk_workspace_count(workspace);
    for (i = 0; i < count && published < TIKU_TRK_DB_WINS_MAX; i++) {
        tiku_desk_window_t *window = tiku_desk_workspace_at(workspace, i);

        if (window == NULL || !window->open) {
            continue;
        }
        memset(&windows[published], 0, sizeof windows[published]);
        tiku_trk_deskbar_strip_title("Tracker", window->title,
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
