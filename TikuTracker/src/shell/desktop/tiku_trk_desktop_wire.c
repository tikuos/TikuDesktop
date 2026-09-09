/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_wire.c - a board's wire, asked from a worker.
 *
 * What a board is asked every few hundred milliseconds -- what changed
 * -- costs the whole timeout when the board has gone quiet, and the
 * loop must not pay it.  So the asking is a job keyed by the board, one
 * in flight per board, and the pump reads the last answer.  A mount
 * that goes is detached at once and closed by a job queued behind
 * whatever is still in the middle of asking it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

#include <unistd.h>

#include "tiku_channel.h"
#include "tiku_trail.h"
#include "tiku_work.h"

typedef struct {
    desktop_t          *app;
    int                 slot;
    unsigned            gen;        /* the mounting this is for */
    tiku_backend_t     *dev;
    tiku_model_change_t ch[32];
    int                 n;
    int                 complete;
} ask_t;

/** @brief The key every job on one board shares. */
static void
board_key(char *out, size_t max, const char *prefix)
{
    snprintf(out, max, "board:%s", prefix);
}

/** @brief On a worker: ask the board what changed. */
static void
ask_run(void *ctx)
{
    ask_t *a = ctx;

    a->complete = 1;
    a->n = a->dev->ops->changes(a->dev, a->ch, 32, &a->complete);
}

/** @brief On the loop: keep the answer, for the mounting it was asked of. */
static void
ask_done(void *ctx)
{
    ask_t *a = ctx;
    desktop_t *app = a->app;
    desktop_wire_t *w = &app->wire[a->slot];

    /* Only for the mounting it was asked for: a slot refilled since
     * holds another board, whose changes these are not. */
    if (tiku_mount_gen_at(app->backend, a->slot) == a->gen) {
        w->n = a->n;
        w->complete = a->complete;
        if (a->n > 0) {
            memcpy(w->ch, a->ch, (size_t)a->n * sizeof a->ch[0]);
        }
        w->answered = 1;
    }
    w->in_flight = 0;
}

/** @brief The asking is over, answered or dropped. */
static void
ask_free(void *ctx)
{
    ask_t *a = ctx;

    a->app->wire[a->slot].in_flight = 0;
    free(a);
}

/** @brief Whether the backend at a slot is a board on a wire. */
static int
is_board(const tiku_backend_t *b)
{
    return b != NULL && b->ops != NULL && b->ops->name != NULL &&
           strcmp(b->ops->name, "tiku") == 0 && b->ops->changes != NULL;
}

/** @brief Post one asking per mounted board that has none in flight. */
void
desktop_wire_ask(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        tiku_backend_t *b = tiku_mount_at(app->backend, i);
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        desktop_wire_t *w = &app->wire[i];
        tiku_work_job_t job;
        ask_t *a;

        /* Last turn's answer has been read by now; a new one may land. */
        w->answered = 0;
        w->n = 0;
        if (!is_board(b) || prefix == NULL || w->in_flight) {
            continue;
        }
        a = calloc(1, sizeof *a);
        if (a == NULL) {
            continue;
        }
        a->app = app;
        a->slot = i;
        a->gen = tiku_mount_gen_at(app->backend, i);
        a->dev = b;
        memset(&job, 0, sizeof job);
        job.run = ask_run;
        job.done = ask_done;
        job.free = ask_free;
        job.ctx = a;
        board_key(job.key, sizeof job.key, prefix);
        if (tiku_work_post(&job) == 0) {
            w->in_flight = 1;
        } else {
            free(a);
        }
    }
}

/**
 * @brief What changed, as the router would say it: the boards' last
 *        answers, the local stores asked now.  -1 when any cannot tell.
 */
int
desktop_wire_changes(desktop_t *app, tiku_model_change_t *out, int max,
                     int *complete)
{
    int i, n = 0, any = 0, k;

    *complete = 1;
    for (i = 0; i < TIKU_MOUNT_MAX && n < max; i++) {
        tiku_backend_t *b = tiku_mount_at(app->backend, i);
        desktop_wire_t *w = &app->wire[i];
        int got, mine = 1;

        if (b == NULL) {
            continue;
        }
        if (is_board(b)) {
            /* A board's answer is the worker's last: nothing yet is
             * nothing changed, and "cannot tell" is still that. */
            if (!w->answered) {
                any = 1;
                continue;
            }
            if (w->n < 0) {
                return -1;
            }
            got = (w->n < max - n) ? w->n : max - n;
            for (k = 0; k < got; k++) {
                out[n + k] = w->ch[k];
                out[n + k].id = tiku_mount_tag_id(i, out[n + k].id);
            }
            mine = w->complete;
        } else {
            /* Local stores answer at once, as they always did. */
            if (b->ops->changes == NULL) {
                return -1;
            }
            got = b->ops->changes(b, out + n, max - n, &mine);
            if (got < 0) {
                return -1;
            }
            for (k = 0; k < got; k++) {
                out[n + k].id = tiku_mount_tag_id(i, out[n + k].id);
            }
        }
        any = 1;
        n += got;
        if (!mine) {
            *complete = 0;
        }
    }
    return any ? n : -1;
}

/** @brief Whether the board in @p slot brought any change this pump. */
int
desktop_wire_changed(desktop_t *app, int slot)
{
    return slot >= 0 && slot < TIKU_MOUNT_MAX &&
           app->wire[slot].answered && app->wire[slot].n > 0;
}

/** @brief On a worker, behind whatever still holds the board: close it. */
static void
close_run(void *ctx)
{
    tiku_backend_close(ctx);
}

/**
 * @brief Take the mount at @p prefix down now, and close its backend
 *        once nothing is in the middle of asking it.
 */
int
desktop_board_unmount(desktop_t *app, const char *prefix)
{
    tiku_backend_t *dev;
    tiku_work_job_t job;
    char key[TIKU_WORK_KEY];

    board_key(key, sizeof key, prefix);
    (void)tiku_work_cancel(key);
    dev = tiku_mount_detach(app->backend, prefix);
    if (dev == NULL) {
        return -1;
    }
    tiku_trail("unmount %s", prefix);
    /* Closed by a job with the board's key, so it runs after whatever
     * is still in the middle of asking the board; the loop never waits
     * for that, and the wire is never closed under a hand. */
    memset(&job, 0, sizeof job);
    job.run = close_run;
    job.ctx = dev;
    snprintf(job.key, sizeof job.key, "%s", key);
    if (tiku_work_post(&job) != 0) {
        tiku_backend_close(dev);
    }
    return 0;
}

#define PROBE_MS 3000           /* a port with no prompt may still frame */

typedef struct {
    desktop_t       *app;
    int              kind;
    char             port[256];
    int              baud;
    int              probe;     /* merely asked, not waited for */
    char             at[TIKU_PATH_MAX];
        tiku_backend_t  *dev;       /* what answered, or NULL */
    tiku_channel_t  *ch;        /* a port that frames but has no shell */
} probe_t;

/** @brief On a worker: open the port, or find it frames without a shell. */
static void
probe_run(void *ctx)
{
        probe_t *p = ctx;

    p->dev = p->probe ? tiku_backend_tiku_open_within(p->port, p->baud,
                                                       800, 0)
                      : tiku_backend_tiku_open(p->port, p->baud);
    if (p->dev == NULL && !p->probe) {
        /* Named on the command line and no prompt: it may still carry
         * windows over frames, which is worth three seconds' asking. */
        p->ch = tiku_channel_open_port(p->port, p->baud);
        if (p->ch != NULL && !tiku_channel_probe(p->ch, PROBE_MS)) {
            tiku_channel_close(p->ch);
            p->ch = NULL;
        }
    }
}

/** @brief On the loop: mount what answered, and count the port answered. */
static void
probe_done(void *ctx)
{
    probe_t *p = ctx;
    desktop_t *app = p->app;

    switch (p->kind) {
    case DESKTOP_ASK_RECLAIM:
        desktop_board_reclaim_landed(app, p->dev);
        return;
    default:
        break;
    }
    if (p->dev != NULL) {
        (void)mount_landed(app, p->dev, p->port, p->at[0] ? p->at : NULL);
    } else {
        tiku_trail("  %s: nothing answered", p->port);
        if (p->ch != NULL) {
            (void)channel_keep(app, p->ch);
        }
    }
    app->look.landed++;
    if (--app->look.pending <= 0 && app->look.going &&
        p->kind == DESKTOP_ASK_LOOK) {
        desktop_board_look_done(app);
    }
}

/** @brief The probe is over. */
static void
probe_free(void *ctx)
{
    free(ctx);
}

/**
 * @brief Ask a port whether a board is on it, on a worker.
 * @return 0 when asked; -1 when the port is already this shell's, or
 *         no worker could take it.
 */
int
desktop_wire_probe(desktop_t *app, int kind, const char *port, int baud,
                   int probe, const char *at)
{
    tiku_work_job_t job;
    probe_t *p;

    if (port_is_mounted(app, port)) {
        return -1;              /* already this shell's; leave it alone */
    }
    p = calloc(1, sizeof *p);
    if (p == NULL) {
        return -1;
    }
    p->app = app;
    p->kind = kind;
    snprintf(p->port, sizeof p->port, "%s", (port != NULL) ? port : "");
    p->baud = baud;
    p->probe = probe;
    snprintf(p->at, sizeof p->at, "%s", (at != NULL) ? at : "");
        memset(&job, 0, sizeof job);
    job.run = probe_run;
    job.done = probe_done;
    job.free = probe_free;
    job.ctx = p;
    /* Keyed per port: every port at once, never two hands on one. */
        snprintf(job.key, sizeof job.key, "port:%.40s", p->port);
    if (tiku_work_post(&job) != 0) {
        free(p);
        return -1;
    }
        tiku_trail("%s %s", probe ? "probe" : "open", p->port);
    return 0;
}

/**
 * @brief Collect until @p pending reaches zero or @p ms pass -- for the
 *        boot, when the loop is not turning yet.
 */
void
desktop_wire_wait(desktop_t *app, int *pending, long ms)
{
    long waited = 0L;

    (void)app;
    while (*pending > 0 && waited < ms) {
        (void)tiku_work_collect();
        usleep(10000);
        waited += 10L;
    }
}
