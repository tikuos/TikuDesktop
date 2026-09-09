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

#include "tiku_board_basic.h"
#include "tiku_fs.h"
#include "tiku_trk_poseview.h"
#include "tiku_channel.h"
#include "tiku_sound.h"
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

typedef struct {
    desktop_t             *app;
    tiku_board_basic_t    *run;
    int                    what;
    int                    session;
    struct desktop_embed  *slot;
    int                    saving;
    char                   name[64];
} over_t;

/** @brief On a worker: leave the interpreter and give the shell back. */
static void
over_run(void *ctx)
{
    over_t *o = ctx;

    tiku_board_basic_free(o->run);
}

/** @brief On the loop: the shell is back; the asker hears how it ended. */
static void
over_done(void *ctx)
{
    over_t *o = ctx;

    tiku_trail("run: shell given back on %s", o->name);
    desktop_board_say(o->app, o->session, o->slot, o->what,
                      o->saving ? o->name : "");
}

/**
 * @brief A held run has ended: give the shell back on a worker, and tell
 *        the asker once it is, so the next run finds the shell free.
 */
void
desktop_wire_run_over(desktop_t *app, tiku_board_basic_t *run, int what,
                      int session, struct desktop_embed *slot, int saving,
                      const char *name)
{
    tiku_work_job_t job;
    over_t *o = calloc(1, sizeof *o);

    if (o == NULL) {
        tiku_board_basic_free(run);
        desktop_board_say(app, session, slot, what, saving ? name : "");
        return;
    }
    o->app = app;
    o->run = run;
    o->what = what;
    o->session = session;
    o->slot = slot;
    o->saving = saving;
    snprintf(o->name, sizeof o->name, "%s", name);
    memset(&job, 0, sizeof job);
    job.run = over_run;
    job.done = over_done;
    job.free = free;
    job.ctx = o;
    snprintf(job.key, sizeof job.key, "board:/devices/%.30s", name);
    if (tiku_work_post(&job) != 0) {
        free(o);
        tiku_board_basic_free(run);
        desktop_board_say(app, session, slot, what, saving ? name : "");
    }
}

/** @brief The key of the board a path under /devices/ belongs to. */
static void
key_of_path(char *out, size_t max, const char *path)
{
    const char *third = NULL;

    if (strncmp(path, "/devices/", 9) == 0) {
        third = strchr(path + 9, '/');
    }
    if (third != NULL) {
        snprintf(out, max, "board:%.*s", (int)(third - path), path);
    } else {
        snprintf(out, max, "board:%.40s", path);
    }
}

typedef struct {
    desktop_t             *app;
    tiku_trk_described_t  *p;
    tiku_window_t         *window;  /* the panel's identity when asked */
    int                    row;
    tiku_backend_t        *dev;
    char                   local[TIKU_PATH_MAX];
    char                   value[256];
    char                   err[160];
    int                    rc;
    /* The read-back: each bound row's place and what it said. */
    int                    nrow;
    char                   place[TIKU_FORM_ROWS][TIKU_PATH_MAX];
    char                   said[TIKU_FORM_ROWS][64];
    int                    known[TIKU_FORM_ROWS];
} pwrite_t;

/** @brief On a worker: the write, then every bound place read back. */
static void
pwrite_run(void *ctx)
{
    pwrite_t *w = ctx;
    int i;

    w->rc = w->dev->ops->write(w->dev, w->local, w->value,
                               strlen(w->value), w->err, sizeof w->err);
    for (i = 0; i < w->nrow; i++) {
        if (w->place[i][0] == '\0') {
            continue;
        }
        w->known[i] = tiku_backend_text(w->dev, w->place[i], w->said[i],
                                        sizeof w->said[i]);
    }
}

/**
 * @brief On the loop: the status line and the rows, if the panel is
 *        still the one that asked.
 */
static void
pwrite_done(void *ctx)
{
    pwrite_t *w = ctx;
    tiku_trk_described_t *p = w->p;
    int i, asked = 0, answered = 0;

    if (p->window != w->window || p->window == NULL || !p->window->open) {
        return;                 /* closed, or another panel in its place */
    }
    if (w->rc != 0) {
        snprintf(p->status, sizeof p->status, "%s: %s",
                 p->form.row[w->row].label,
                 w->err[0] != '\0' ? w->err : "refused");
        tiku_sound_play(TIKU_SOUND_BEEP);
    } else {
        p->status[0] = '\0';
    }
    for (i = 0; i < w->nrow && i < p->form.nrow; i++) {
        tiku_form_row_t *row = &p->form.row[i];

        if (w->place[i][0] == '\0') {
            continue;
        }
        asked++;
        if (w->known[i]) {
            size_t n;

            answered++;
            snprintf(row->value, sizeof row->value, "%s", w->said[i]);
            n = strlen(row->value);
            while (n > 0u && (row->value[n - 1u] == '\n' ||
                              row->value[n - 1u] == '\r' ||
                              row->value[n - 1u] == ' ')) {
                row->value[--n] = '\0';
            }
            row->known = 1;
        } else {
            row->known = 0;
        }
    }
    panel_refreshed(p, asked, answered);
    w->app->dirty = 1;
}

/**
 * @brief Write row @p row of panel @p p on a worker and read the panel
 *        back.  @return 1 when asked; 0 when nothing serves the place.
 */
int
desktop_wire_panel_write(desktop_t *app, tiku_trk_described_t *p, int row)
{
    tiku_work_job_t job;
    pwrite_t *w;
    char path[TIKU_PATH_MAX], local[TIKU_PATH_MAX];
    const char *bind = bind_path(p, &p->form.row[row], path, sizeof path);
    tiku_backend_t *dev = tiku_backend_serving(app->backend, bind, local,
                                               sizeof local);
    int i;

    if (dev == NULL) {
        return 0;
    }
    w = calloc(1, sizeof *w);
    if (w == NULL) {
        return 0;
    }
    w->app = app;
    w->p = p;
    w->window = p->window;
    w->row = row;
    w->dev = dev;
    snprintf(w->local, sizeof w->local, "%s", local);
    snprintf(w->value, sizeof w->value, "%s", p->form.row[row].value);
    w->nrow = (p->form.nrow < TIKU_FORM_ROWS) ? p->form.nrow
                                              : TIKU_FORM_ROWS;
    for (i = 0; i < w->nrow; i++) {
        const tiku_form_row_t *r = &p->form.row[i];
        char rp[TIKU_PATH_MAX], rl[TIKU_PATH_MAX];

        w->place[i][0] = '\0';
        if (r->bind[0] == '\0' || r->kind == TIKU_FORM_BUTTON) {
            continue;
        }
        /* Read back through the same backend, by its own path, so the
         * worker never walks the mount table. */
        if (tiku_backend_serving(app->backend,
                                 bind_path(p, r, rp, sizeof rp),
                                 rl, sizeof rl) == dev) {
            snprintf(w->place[i], sizeof w->place[i], "%s", rl);
        }
    }
    memset(&job, 0, sizeof job);
    job.run = pwrite_run;
    job.done = pwrite_done;
    job.free = free;
    job.ctx = w;
    key_of_path(job.key, sizeof job.key, bind);
    if (tiku_work_post(&job) != 0) {
        free(w);
        return 0;
    }
    tiku_trail("panel: write %s", bind);
    return 1;
}

typedef struct {
    desktop_t      *app;
    char            path[TIKU_PATH_MAX];
    char            local[TIKU_PATH_MAX];
    tiku_backend_t *dev;
    char            value[64];
    int             n;
} rread_t;

/** @brief On a worker: read the node a replicant shows. */
static void
rread_run(void *ctx)
{
    rread_t *r = ctx;

    r->n = r->dev->ops->read(r->dev, r->local, r->value,
                             sizeof r->value - 1u);
    if (r->n >= 0) {
        char *nl;

        r->value[r->n] = '\0';
        nl = strchr(r->value, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
    }
}

/** @brief On the loop: the value shown, or the replicant gone. */
static void
rread_done(void *ctx)
{
    rread_t *r = ctx;

    if (tiku_trk_deskbar_replicant_set(r->app->deskbar, r->path,
                                       (r->n >= 0) ? r->value : NULL)) {
        r->app->dirty = 1;
    }
}

/** @brief The asking is over, answered or dropped. */
static void
rread_free(void *ctx)
{
    rread_t *r = ctx;

    r->app->replicant_reads--;
    free(r);
}

/**
 * @brief Read every Deskbar replicant that shows a board's node, on a
 *        worker; the local ones the Deskbar reads itself, cheaply.
 */
void
desktop_wire_replicants(desktop_t *app)
{
    char paths[16][TIKU_PATH_MAX];
    int n, i;

    if (app->deskbar == NULL || app->replicant_reads > 0) {
        return;                 /* last pump's reads are still out */
    }
    n = tiku_trk_deskbar_replicant_paths(app->deskbar, paths, 16);
    for (i = 0; i < n; i++) {
        tiku_work_job_t job;
        rread_t *r;
        char local[TIKU_PATH_MAX];
        tiku_backend_t *dev;

        if (strncmp(paths[i], "/devices/", 9) != 0) {
            continue;
        }
        dev = tiku_backend_serving(app->backend, paths[i], local,
                                   sizeof local);
        if (dev == NULL) {
            (void)tiku_trk_deskbar_replicant_set(app->deskbar, paths[i],
                                                 NULL);
            continue;
        }
        r = calloc(1, sizeof *r);
        if (r == NULL) {
            continue;
        }
        r->app = app;
        snprintf(r->path, sizeof r->path, "%s", paths[i]);
        snprintf(r->local, sizeof r->local, "%s", local);
        r->dev = dev;
        memset(&job, 0, sizeof job);
        job.run = rread_run;
        job.done = rread_done;
        job.free = rread_free;
        job.ctx = r;
        key_of_path(job.key, sizeof job.key, paths[i]);
        if (tiku_work_post(&job) == 0) {
            app->replicant_reads++;
        } else {
            free(r);
        }
    }
}

typedef struct {
    desktop_t             *app;
    tiku_trk_poseview_t   *view;
    int                    gen;
    int                    kind;
    tiku_backend_t        *dev;
    int                    slot;
    unsigned               mount_gen;
    char                   prefix[TIKU_PATH_MAX];
    char                   local[TIKU_PATH_MAX];
    tiku_model_t          *entries;
    int                    n;
} listing_t;

/** @brief The walk's callback, on a worker: keep the entry, adopted. */
static int
listing_one(const tiku_model_t *m, void *ctx)
{
    listing_t *l = ctx;

    if (l->n >= 4096) {
        return 1;
    }
    l->entries[l->n] = *m;
    tiku_mount_adopt_entry(l->prefix, l->slot, &l->entries[l->n]);
    l->n++;
    return 0;
}

/** @brief On a worker: list the folder on the board. */
static void
listing_run(void *ctx)
{
    listing_t *l = ctx;

    l->n = 0;
    if (l->dev->ops->list(l->dev, l->local, listing_one, l) < 0) {
        l->n = -1;
    }
}

/** @brief On the loop: the entries to the view, if it is still there. */
static void
listing_done(void *ctx)
{
    listing_t *l = ctx;

    if (!tiku_trk_poseview_alive(l->view) ||
        tiku_mount_gen_at(l->app->backend, l->slot) != l->mount_gen) {
        return;
    }
    if (l->kind == TIKU_TRK_LIST_RELIST) {
        unsigned nf = tiku_trk_poseview_relist_landed(l->view, l->gen,
                                                      l->entries, l->n,
                                                      tiku_trk_now());

        if (nf != TIKU_TRK_NOTIFY_NOTHING) {
            l->app->dirty = 1;
        }
    } else {
        (void)tiku_trk_poseview_load_landed(l->view, l->gen, l->entries,
                                            l->n);
        l->app->dirty = 1;
    }
    tiku_trail("list: %s landed, %d entries", l->local, l->n);
}

/** @brief The listing is over. */
static void
listing_free(void *ctx)
{
    listing_t *l = ctx;

    free(l->entries);
    free(l);
}

/**
 * @brief The lister every view asks: a folder on a board is listed by a
 *        worker; anything else lists as it always did.
 */
static int
desktop_lister(void *ctx, tiku_trk_poseview_t *v, int gen, int kind)
{
    desktop_t *app = ctx;
    const char *path = tiku_trk_poseview_path(v);
    tiku_backend_t *dev;
    tiku_work_job_t job;
    listing_t *l;
    char local[TIKU_PATH_MAX];
    int i;

    if (path == NULL || strncmp(path, "/devices/", 9) != 0) {
        return 0;
    }
    dev = tiku_backend_serving(app->backend, path, local, sizeof local);
    if (!is_board(dev)) {
        return 0;               /* the /devices folder itself, or local */
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (tiku_mount_at(app->backend, i) == dev) {
            break;
        }
    }
    if (i == TIKU_MOUNT_MAX) {
        return 0;
    }
    l = calloc(1, sizeof *l);
    if (l == NULL) {
        return 0;
    }
    l->entries = calloc(4096, sizeof *l->entries);
    if (l->entries == NULL) {
        free(l);
        return 0;
    }
    l->app = app;
    l->view = v;
    l->gen = gen;
    l->kind = kind;
    l->dev = dev;
    l->slot = i;
    l->mount_gen = tiku_mount_gen_at(app->backend, i);
    snprintf(l->prefix, sizeof l->prefix, "%s",
             tiku_mount_prefix_at(app->backend, i));
    snprintf(l->local, sizeof l->local, "%s", local);
    memset(&job, 0, sizeof job);
    job.run = listing_run;
    job.done = listing_done;
    job.free = listing_free;
    job.ctx = l;
    key_of_path(job.key, sizeof job.key, path);
    if (tiku_work_post(&job) != 0) {
        listing_free(l);
        return 0;
    }
    tiku_trail("list: %s asked", path);
    return 1;
}

/** @brief From here, every folder on a board is listed by a worker. */
void
desktop_wire_install(desktop_t *app)
{
    tiku_trk_poseview_set_lister(desktop_lister, app);
}

/** @brief On a worker: step the engine until it is done, or asks. */
static void
fs_run(void *ctx)
{
    tiku_fs_t *fs = ((tiku_fs_t **)ctx)[0];

    while (tiku_fs_busy(fs) && tiku_fs_step(fs, 1) > 0) {
    }
}

/** @brief On the loop: the engine has stopped, done or asking. */
static void
fs_done(void *ctx)
{
    desktop_t *app = ((void **)ctx)[1];

    app->fs_job = 0;
    app->dirty = 1;
}

/** @brief The asking is over. */
static void
fs_free_ctx(void *ctx)
{
    free(ctx);
}

/**
 * @brief Run the file engine's queued operation on a worker, one job
 *        for the whole of it; the loop reads progress, answers a
 *        question when the job stops on one, and posts the next.
 */
void
desktop_wire_fs_run(desktop_t *app)
{
    tiku_work_job_t job;
    void **ctx;

    if (app->fs == NULL || app->fs_job) {
        return;
    }
    ctx = calloc(2, sizeof *ctx);
    if (ctx == NULL) {
        return;
    }
    ctx[0] = app->fs;
    ctx[1] = app;
    memset(&job, 0, sizeof job);
    job.run = fs_run;
    job.done = fs_done;
    job.free = fs_free_ctx;
    job.ctx = ctx;
    snprintf(job.key, sizeof job.key, "fs");
    if (tiku_work_post(&job) == 0) {
        app->fs_job = 1;
    } else {
        free(ctx);
    }
}

