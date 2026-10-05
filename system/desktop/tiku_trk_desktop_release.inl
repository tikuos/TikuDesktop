/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_release.inl - taking a board's mount down: the mount goes
 * at once, and its backend is closed on a worker after any job still asking
 * the board.  Included by tiku_desktop_device_jobs.c, which defines
 * board_key(), and by tests/system/test_console_handoff.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* One board's close, posted under the board's job key. */
typedef struct {
    tiku_backend_t *dev;
    desktop_t *app;
    char name[64];
    int released;               /* tell everyone once the port is closed */
} close_board_t;

/** @brief On a worker, after every job still asking the board: close it. */
static void
close_run(void *ctx)
{
    close_board_t *c = ctx;
    tiku_backend_close(c->dev);
}

/**
 * @brief On the loop, once the port is closed: for a released board, tell
 *        every application and every session TIKU_APP_BOARD_RELEASED, since
 *        only a closed port may be handed to another program.
 */
static void
close_done(void *ctx)
{
    close_board_t *c = ctx;
    if (!c->released) return;
    for (int i = 0; i < TIKU_TRK_EMBED_MAX; i++) {
        struct desktop_embed *slot = &c->app->embeds[i];
        if (slot->running && slot->descriptor->board_said)
            slot->descriptor->board_said(slot->state,
                TIKU_APP_BOARD_RELEASED, c->name);
    }
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++)
        desktop_board_tell(c->app, i, TIKU_APP_BOARD_RELEASED, c->name);
}

/**
 * @brief Take the mount at @p prefix down now, cancel the board's jobs not
 *        yet started, and close its backend once no job is asking it.
 * @param released whether to tell everyone the board was released once its
 *                 port is closed
 * @return 0 when unmounted; -1 when nothing is mounted at @p prefix or no
 *         memory is left.
 */
static int
board_unmount(desktop_t *app, const char *prefix, int released)
{
    tiku_backend_t *dev;
    tiku_work_job_t job;
    char key[TIKU_WORK_KEY];
    close_board_t *c = calloc(1, sizeof *c);

    if (!c) return -1;
    board_key(key, sizeof key, prefix);
    (void)tiku_work_cancel(key);
    dev = tiku_mount_detach(app->backend, prefix);
    if (dev == NULL) {
        free(c);
        return -1;
    }
    tiku_trail("unmount %s", prefix);
    /* Closed by a job with the board's key, so it runs after whatever is
     * still asking the board: the loop never waits for that, and the wire
     * is never closed while a job is using it. */
    memset(&job, 0, sizeof job);
    c->dev = dev; c->app = app; c->released = released;
    {
        /* With no slash in the prefix, strrchr() returns NULL and the
         * whole prefix is the name. */
        const char *leaf = strrchr(prefix, '/');

        snprintf(c->name, sizeof c->name, "%s", leaf ? leaf + 1 : prefix);
    }
    job.run = close_run;
    job.done = close_done; job.free = free;
    job.ctx = c;
    snprintf(job.key, sizeof job.key, "%s", key);
    if (tiku_work_post(&job) != 0) {
        tiku_backend_close(dev);
        close_done(c); free(c);
    }
    return 0;
}

/**
 * @brief Take the mount at @p prefix down and close its backend, without
 *        telling anyone the board was released.
 * @return 0 when unmounted; -1 when nothing is mounted there or no memory
 *         is left.
 */
int desktop_board_unmount(desktop_t *app, const char *prefix)
{ return board_unmount(app, prefix, 0); }

/**
 * @brief Take the mount at @p prefix down and close its backend; once the
 *        port is closed, tell everyone the board was released.
 * @return 0 when unmounted; -1 when nothing is mounted there or no memory
 *         is left.
 */
int desktop_board_unmount_released(desktop_t *app, const char *prefix)
{ return board_unmount(app, prefix, 1); }
