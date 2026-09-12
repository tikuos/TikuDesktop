/* Board close/handoff completion, shared with the worker-order regression. */
typedef struct {
    tiku_backend_t *dev;
    desktop_t *app;
    char name[64];
    int released;
} close_board_t;

/** @brief On a worker, behind whatever still holds the board: close it. */
static void
close_run(void *ctx)
{
    close_board_t *c = ctx;
    tiku_backend_close(c->dev);
}

/** @brief Only a physically closed port may be handed to another app. */
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
 * @brief Take the mount at @p prefix down now, and close its backend
 *        once nothing is in the middle of asking it.
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
    /* Closed by a job with the board's key, so it runs after whatever
     * is still in the middle of asking the board; the loop never waits
     * for that, and the wire is never closed under a hand. */
    memset(&job, 0, sizeof job);
    c->dev = dev; c->app = app; c->released = released;
    {
        /* A prefix with no slash is not a mount path, and NULL + 1 is not
         * an address: take the whole thing rather than walk off it. */
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

/** @brief Drop a mount without granting ownership to a console or runner. */
int desktop_board_unmount(desktop_t *app, const char *prefix)
{ return board_unmount(app, prefix, 0); }

/** @brief Drop a mount and acknowledge release when its port is closed. */
int desktop_board_unmount_released(desktop_t *app, const char *prefix)
{ return board_unmount(app, prefix, 1); }
