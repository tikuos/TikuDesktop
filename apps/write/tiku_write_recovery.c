/*
 * TikuDesktop applications.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_write_recovery.c - private checkpoint directories and explicit recovery.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_write_private.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

/** @brief Create or validate a real directory owned by the current user. */
static int
directory(const char *path, int private)
{
    struct stat st;

    if (mkdir(path, 0700) && errno != EEXIST) return -1;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid())
        return -1;
    return private && (st.st_mode & 0077) ? -1 : 0;
}

/** @brief Select the newest checkpoint not held by a running editor. */
static void
find_checkpoint(write_app_t *app, const char *base)
{
    DIR *dir = opendir(base);
    struct dirent *entry;
    time_t newest = 0;
    unsigned visited = 0;

    if (!dir) return;
    while ((entry = readdir(dir)) && visited++ < 4096) {
        char session[WRITE_PATH], lock[WRITE_PATH], path[WRITE_PATH];
        struct stat st;
        int fd;

        if (strncmp(entry->d_name, "session-", 8)) continue;
        if (snprintf(session, sizeof session, "%s/%s", base,
            entry->d_name) >= (int)sizeof session) continue;
        if (lstat(session, &st) || !S_ISDIR(st.st_mode) ||
            st.st_uid != getuid() || (st.st_mode & 0077)) continue;
        if (snprintf(lock, sizeof lock, "%s/lock", session) >=
            (int)sizeof lock || snprintf(path, sizeof path, "%s/recovery.tkrd",
            session) >= (int)sizeof path) continue;
        fd = open(lock, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0) continue;
        if (!flock(fd, LOCK_EX | LOCK_NB) && !lstat(path, &st) &&
            S_ISREG(st.st_mode) && st.st_uid == getuid() &&
            st.st_size > 0 && st.st_size <= WRITE_FILE_MAX &&
            st.st_mtime >= newest) {
            newest = st.st_mtime;
            strcpy(app->offered, path);
        }
        close(fd);
    }
    closedir(dir);
}

void
write_recovery_init(write_app_t *app)
{
    const char *home = getenv("HOME");
    char base[WRITE_PATH], lock[WRITE_PATH];

    app->recovery_lock = -1;
    if (!home || !*home || snprintf(base, sizeof base, "%s/.local", home) >=
        (int)sizeof base || directory(base, 0)) goto fail;
    if (snprintf(base, sizeof base, "%s/.local/share", home) >=
        (int)sizeof base || directory(base, 0)) goto fail;
    if (snprintf(base, sizeof base, "%s/.local/share/TikuWrite", home) >=
        (int)sizeof base || directory(base, 1)) goto fail;
    find_checkpoint(app, base);
    if (snprintf(app->recovery_dir, sizeof app->recovery_dir,
        "%s/session-XXXXXX", base) >= (int)sizeof app->recovery_dir)
        goto fail;
    if (!mkdtemp(app->recovery_dir)) goto fail;
    if (snprintf(lock, sizeof lock, "%s/lock", app->recovery_dir) >=
        (int)sizeof lock) goto fail;
    app->recovery_lock = open(lock,
        O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (app->recovery_lock < 0 ||
        flock(app->recovery_lock, LOCK_EX | LOCK_NB)) goto fail;
    if (snprintf(app->recovery, sizeof app->recovery, "%s/recovery.tkrd",
        app->recovery_dir) >= (int)sizeof app->recovery) goto fail;
    return;
fail:
    app->recovery[0] = 0;
    snprintf(app->status, sizeof app->status,
             "Recovery storage is unavailable. Save your work manually.");
}

void
write_recovery_stop(write_app_t *app, int discard)
{
    char lock[WRITE_PATH];

    if (discard && *app->recovery) unlink(app->recovery);
    if (app->recovery_lock >= 0) {
        if (discard && snprintf(lock, sizeof lock, "%s/lock",
            app->recovery_dir) < (int)sizeof lock) {
            unlink(lock);
            rmdir(app->recovery_dir);
        }
        close(app->recovery_lock);
        app->recovery_lock = -1;
    }
}

void
write_recovery_tick(write_app_t *app)
{
    uint64_t generation = tiku_richdoc_generation(app->doc);

    if (app->job || !*app->recovery || !write_dirty(app) ||
        app->recovered_generation == generation ||
        app->now - app->edited_at < 2000000 ||
        app->now - app->attempted_at < 5000000) return;
    app->attempted_at = app->now;
    if (write_job_start(app, W_JOB_RECOVERY, app->recovery, NULL, 0)) {
        snprintf(app->status, sizeof app->status,
                 "Recovery checkpoint failed. Save your work manually.");
        app->repaint = 1;
    }
}
