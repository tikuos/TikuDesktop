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
#include "tiku_sound.h"
#include "tiku_thumb.h"
#include "tiku_volume.h"
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

#include "tiku_trk_desktop_release.inl"

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
    case DESKTOP_ASK_LOOK:
    case DESKTOP_ASK_BOOT:
        /* A scan LISTS, it does not mount: a board that answered is
         * remembered as a candidate under its processor name, and the
         * port is let go until the Devices menu connects it. */
        if (p->dev != NULL) {
            const tiku_identity_t *id =
                tiku_backend_tiku_identity(p->dev);
            char name[64];

            /* The PROCESSOR names it, as the mount does: that is what a
             * person choosing between two boards chooses by. */
            if (id != NULL && id->mcu[0] != '\0') {
                snprintf(name, sizeof name, "%s", id->mcu);
            } else if (id != NULL && id->name[0] != '\0') {
                snprintf(name, sizeof name, "%s", id->name);
            } else {
                device_mount_name(p->dev, app->backend, name,
                                  sizeof name);
            }
            device_candidate_record(app, p->port, name);
            tiku_backend_close(p->dev);
        } else if (p->ch != NULL) {
            tiku_channel_close(p->ch);
        }
        break;
    default:                    /* DESKTOP_ASK_CONNECT: the one that mounts */
        if (p->dev != NULL) {
            (void)mount_landed(app, p->dev, p->port,
                               p->at[0] ? p->at : NULL);
        } else {
            tiku_trail("  %s: nothing answered", p->port);
            if (p->ch != NULL) {
                (void)channel_keep(app, p->ch);
            }
        }
        break;
    }
    if (app->look.going) {
        app->look.landed++;
        if (--app->look.pending <= 0 && p->kind == DESKTOP_ASK_LOOK) {
            desktop_board_look_done(app);
        }
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

static int sound_opener(void *ctx);

/** @brief From here, every folder on a board is listed by a worker. */
void
desktop_wire_install(desktop_t *app)
{
    tiku_volumes_t vs;
    int n;

    tiku_trk_poseview_set_lister(desktop_lister, app);
    tiku_sound_set_opener(sound_opener, app);
    /* The first scan is the boot's, on the loop, so the cache is never
     * empty; every later one is a worker's. */
    n = tiku_volumes_scan_now(&vs);
    tiku_volumes_cache_set(&vs, n);
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

typedef struct {
    desktop_t    *app;
    char          path[TIKU_PATH_MAX];
    int           w, h;
    tiku_image_t  thumb;
    int           rc;
} thumb_t;

/** @brief On a worker: decode the picture and shrink it. */
static void
thumb_run(void *ctx)
{
    thumb_t *t = ctx;

    t->rc = tiku_thumb_make(t->path, &t->w, &t->h, &t->thumb);
}

/** @brief On the loop: the writes, the count, the next one free to go. */
static void
thumb_done(void *ctx)
{
    thumb_t *t = ctx;

    if (t->rc != 0) {
        memset(&t->thumb, 0, sizeof t->thumb);
    }
    tiku_thumb_landed(&t->app->thumbs, t->app->store, t->path, t->w, t->h,
                      &t->thumb);
    t->app->thumb_job = 0;
    t->app->dirty = 1;
}

/**
 * @brief Decode the next queued picture on a worker, one at a time.
 * @return 1 when one was sent.
 */
int
desktop_wire_thumb(desktop_t *app)
{
    tiku_work_job_t job;
    thumb_t *t;

    if (app->thumb_job) {
        return 0;
    }
    t = calloc(1, sizeof *t);
    if (t == NULL) {
        return 0;
    }
    if (!tiku_thumb_take(&app->thumbs, t->path, sizeof t->path)) {
        free(t);
        return 0;
    }
    t->app = app;
    memset(&job, 0, sizeof job);
    job.run = thumb_run;
    job.done = thumb_done;
    job.free = free;
    job.ctx = t;
    snprintf(job.key, sizeof job.key, "thumb");
    if (tiku_work_post(&job) != 0) {
        tiku_thumb_landed(&app->thumbs, app->store, t->path, 0, 0, NULL);
        free(t);
        return 0;
    }
    app->thumb_job = 1;
    return 1;
}

typedef struct {
    desktop_t      *app;
    tiku_volumes_t  vs;
    int             count;
} volscan_t;

/** @brief On a worker: every mount asked how much room it has. */
static void
volscan_run(void *ctx)
{
    volscan_t *v = ctx;

    v->count = tiku_volumes_scan_now(&v->vs);
}

/** @brief On the loop: what was found is what every scan answers now. */
static void
volscan_done(void *ctx)
{
    volscan_t *v = ctx;

    if (v->count >= 0) {
        tiku_volumes_cache_set(&v->vs, v->count);
    }
    v->app->volumes_job = 0;
}

/**
 * @brief Scan the volumes on a worker, one scan out at a time; a mount
 *        that never answers hangs the worker, and the loop reads the
 *        last scan.
 */
void
desktop_wire_volumes(desktop_t *app)
{
    tiku_work_job_t job;
    volscan_t *v;

    if (app->volumes_job) {
        return;
    }
    v = calloc(1, sizeof *v);
    if (v == NULL) {
        return;
    }
    v->app = app;
    memset(&job, 0, sizeof job);
    job.run = volscan_run;
    job.done = volscan_done;
    job.free = free;
    job.ctx = v;
    snprintf(job.key, sizeof job.key, "volumes");
    if (tiku_work_post(&job) == 0) {
        app->volumes_job = 1;
    } else {
        free(v);
    }
}

typedef struct {
    desktop_t      *app;
    char            mount[TIKU_PATH_MAX];
    char            name[128];
    char            why[200];
    tiku_unmount_t  rc;
} unmount_t;

/** @brief On a worker: umount(2), which flushes what is dirty. */
static void
unmount_run(void *ctx)
{
    unmount_t *u = ctx;

    u->rc = tiku_volume_umount_now(u->mount, u->name, u->why, sizeof u->why);
}

/** @brief On the loop: say how it went. */
static void
unmount_done(void *ctx)
{
    unmount_t *u = ctx;
    desktop_t *app = u->app;

    app->unmount_job = 0;
    if (u->rc == TIKU_UNMOUNT_OK) {
        snprintf(app->status, sizeof app->status, "unmounted \"%.60s\"",
                 u->name);
        tiku_trail("unmounted %s", u->mount);
    } else {
        tiku_sound_play(TIKU_SOUND_BEEP);
        tiku_alert_open(alert_model(app), TIKU_ALERT_STOP,
                        TIKU_TRK_ALERT_TAG_REFUSAL, u->why, "Cancel");
    }
    desktop_wire_volumes(app);
    app->dirty = 1;
}

/**
 * @brief Unmount @p mount on a worker; the status line and the alert
 *        come when it has.  @return 1 when asked.
 */
int
desktop_wire_unmount(desktop_t *app, const char *mount, const char *name)
{
    tiku_work_job_t job;
    unmount_t *u;

    if (app->unmount_job) {
        return 0;
    }
    u = calloc(1, sizeof *u);
    if (u == NULL) {
        return 0;
    }
    u->app = app;
    snprintf(u->mount, sizeof u->mount, "%s", mount);
    snprintf(u->name, sizeof u->name, "%s", name);
    memset(&job, 0, sizeof job);
    job.run = unmount_run;
    job.done = unmount_done;
    job.free = free;
    job.ctx = u;
    snprintf(job.key, sizeof job.key, "volumes");
    if (tiku_work_post(&job) != 0) {
        free(u);
        return 0;
    }
    app->unmount_job = 1;
    snprintf(app->status, sizeof app->status, "unmounting \"%.60s\"...",
             name);
    return 1;
}

/** @brief On a worker: connect to the sound server. */
static void
sound_open_run(void *ctx)
{
    *(int *)ctx = tiku_sound_open_now();
}

/** @brief On the loop: the device is open, or is not. */
static void
sound_open_done(void *ctx)
{
    tiku_sound_opened(*(int *)ctx == 0);
}

/** @brief The sound kit's opener: the connect goes to a worker. */
static int
sound_opener(void *ctx)
{
    tiku_work_job_t job;
    int *rc = calloc(1, sizeof *rc);

    (void)ctx;
    if (rc == NULL) {
        return 0;
    }
    memset(&job, 0, sizeof job);
    job.run = sound_open_run;
    job.done = sound_open_done;
    job.free = free;
    job.ctx = rc;
    snprintf(job.key, sizeof job.key, "sound");
    if (tiku_work_post(&job) != 0) {
        free(rc);
        return 0;
    }
    return 1;
}

/*---------------------------------------------------------------------------*/
/* WHAT A BOARD SAYS ABOUT ITSELF                                            */
/*---------------------------------------------------------------------------*/

/* How long an answer stands before the board is asked again.  The About
 * box reads it while it is open, and a board's own numbers move slowly. */
#define STATS_HOLD_US 4000000

typedef struct {
    desktop_t      *app;
    char            name[64];
    tiku_backend_t *dev;
    char            text[1024];
} sread_t;

/** @brief One node of the board, trimmed, or "" when it has none. */
static void
stat_read(tiku_backend_t *dev, const char *path, char *out, size_t max)
{
    int n = dev->ops->read(dev, path, out, max - 1u);
    char *nl;

    if (n < 0) {
        out[0] = '\0';
        return;
    }
    out[n] = '\0';
    nl = strchr(out, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
}

/* What the board is asked for, in the order the facts are shown. */
enum { SR_MCU, SR_FREQ, SR_SRAM, SR_USED, SR_NVM, SR_NVMFREE, SR_KIND,
       SR_UP, SR_BOOTS, SR_N };

/* The corners of a board's namespace that are there only when the part
 * has the thing: what it HAS, asked by whether it can be seen. */
static const struct { const char *path; const char *say; } sr_has[] = {
    { "/sys/coproc", "coprocessor" },
    { "/sys/npu",    "NPU"         },
    { "/sys/gpu",    "GPU"         },
    { "/sys/flpr",   "FLPR"        },
    { "/sys/crypto", "crypto"      },
    { "/sys/flash",  "NOR flash"   },
    { "/sys/emmc",   "eMMC"        },
};

/** @brief Whether the board has this corner of its namespace. */
static int
have_node(tiku_backend_t *dev, const char *path)
{
    tiku_model_t m;

    return dev->ops->stat(dev, path, &m) == 0;
}

/** @brief A whole node, newlines and all, or "" when it has none. */
static void
node_read(tiku_backend_t *dev, const char *path, char *out, size_t max)
{
    int n = dev->ops->read(dev, path, out, max - 1u);

    out[(n > 0) ? n : 0] = '\0';
}

/**
 * @brief One tier's capacity and use out of /sys/mem/tiers.
 *
 * Matched at the START of a line: "psram" ends in the same four letters
 * as "sram", and a loose search would read one tier's numbers for the
 * other's.
 */
static int
tier_room(const char *tiers, const char *name, unsigned long *total,
          unsigned long *used)
{
    size_t len = strlen(name);
    const char *p = tiers;

    while (p != NULL && *p != '\0') {
        if (strncmp(p, name, len) == 0 && p[len] == ' ') {
            const char *t = strstr(p, "total=");
            const char *u = strstr(p, "used=");

            if (t == NULL || u == NULL) {
                return 0;
            }
            *total = strtoul(t + 6, NULL, 10);
            *used = strtoul(u + 5, NULL, 10);
            return 1;
        }
        p = strchr(p, '\n');
        p = (p != NULL) ? p + 1 : NULL;
    }
    return 0;
}

/**
 * @brief Forward one memory's map, a line at a time, under @p tag.
 *
 * The board says its estates (code, module, tier, store, persist;
 * static, stack, tier, banks) and this end passes them on untouched:
 * how they are drawn is the reader's business.  @return 1 when the
 * board had a map to give.
 */
static int
say_map(sread_t *s, size_t *at, const char *tag, const char *path)
{
    char map[512];
    const char *p;

    node_read(s->dev, path, map, sizeof map);
    if (map[0] == '\0') {
        return 0;
    }
    for (p = map; *p != '\0' && *at < sizeof s->text; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (len > 0u) {
            *at += (size_t)snprintf(s->text + *at, sizeof s->text - *at,
                                    "%s\t%.*s\n", tag, (int)len, p);
        }
        p = (nl != NULL) ? nl + 1 : p + strlen(p);
    }
    return 1;
}

/**
 * @brief Say what the board is and what it holds, as raw facts.
 *
 * The memories come as MAPS -- each estate of each memory with its size
 * and use -- from /sys/mem/sram_map and /sys/mem/nvm_map.  A board too
 * old to carry those answers with its tiers instead, which is less but
 * is at least each tier's own free against its own total.
 */
static void
sread_say(sread_t *s, char raw[][32], const char *tiers)
{
    static const struct { const char *tier; const char *say; } kinds[] = {
        { "sram",   "SRAM"   },
        { "nvm",    NULL     },      /* the device names this one */
        { "hifram", "HIFRAM" },
        { "psram",  "PSRAM"  },
    };
    unsigned long total, used;
    size_t at;
    int i, mapped;

    at = (size_t)snprintf(s->text, sizeof s->text,
             "mcu\t%s\nfreq\t%s\nup\t%s\nboots\t%s\npart\t%s\t%s\t%s\n",
             raw[SR_MCU][0] ? raw[SR_MCU] : "?", raw[SR_FREQ],
             raw[SR_UP], raw[SR_BOOTS], raw[SR_SRAM], raw[SR_NVM],
             raw[SR_KIND][0] ? raw[SR_KIND] : "NVM");
    mapped  = say_map(s, &at, "sram", "/sys/mem/sram_map");
    mapped |= say_map(s, &at, "nvm", "/sys/mem/nvm_map");
    for (i = 0; !mapped && i < (int)(sizeof kinds / sizeof kinds[0]); i++) {
        if (at >= sizeof s->text ||
            !tier_room(tiers, kinds[i].tier, &total, &used)) {
            continue;
        }
        at += (size_t)snprintf(s->text + at, sizeof s->text - at,
                               "mem\t%s\t%lu\t%lu\n",
                               (kinds[i].say != NULL) ? kinds[i].say
                                   : (raw[SR_KIND][0] ? raw[SR_KIND]
                                                      : "NVM"),
                               (total > used) ? total - used : 0UL,
                               total);
    }
    for (i = 0; i < (int)(sizeof sr_has / sizeof sr_has[0]); i++) {
        if (at < sizeof s->text && have_node(s->dev, sr_has[i].path)) {
            at += (size_t)snprintf(s->text + at, sizeof s->text - at,
                                   "has\t%s\n", sr_has[i].say);
        }
    }
}

/** @brief On a worker: ask the board what it is and what it has left. */
static void
sread_run(void *ctx)
{
    static const char *const node[SR_N] = {
        "/sys/device/mcu", "/sys/cpu/freq", "/sys/mem/sram",
        "/sys/mem/used",   "/sys/mem/nvm",  "/sys/mem/nvmfree",
        "/sys/mem/kind",   "/sys/uptime",   "/sys/boot_count"
    };
    sread_t *s = ctx;
    char raw[SR_N][32];
    char tiers[256];
    int i;

    for (i = 0; i < SR_N; i++) {
        stat_read(s->dev, node[i], raw[i], sizeof raw[i]);
    }
    node_read(s->dev, "/sys/mem/tiers", tiers, sizeof tiers);
    sread_say(s, raw, tiers);
}

/** @brief On the loop: keep what it said. */
static void
sread_done(void *ctx)
{
    sread_t *s = ctx;
    int i;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (strcmp(s->app->dev_stats[i].name, s->name) == 0) {
            snprintf(s->app->dev_stats[i].text,
                     sizeof s->app->dev_stats[i].text, "%s", s->text);
            s->app->dev_stats[i].at = tiku_trk_now();
            s->app->dirty = 1;
            return;
        }
    }
}

/** @brief The asking is over. */
static void
sread_free(void *ctx)
{
    sread_t *s = ctx;
    int i;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (strcmp(s->app->dev_stats[i].name, s->name) == 0) {
            s->app->dev_stats[i].asking = 0;
        }
    }
    free(s);
}

/**
 * @brief What @p board last said about itself, into @p out.
 *
 * Answered from what is KEPT, never from the wire: a stale answer sets a
 * worker going and the caller draws what it has until that lands.  So a
 * window showing this is never the reason a frame is late.
 *
 * @return the bytes written.
 */
int
desktop_device_stats(desktop_t *app, const char *board, char *out,
                     size_t max)
{
    int i, at = -1;

    if (app == NULL || board == NULL || board[0] == '\0' || out == NULL ||
        max == 0u) {
        return 0;
    }
    out[0] = '\0';
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (strcmp(app->dev_stats[i].name, board) == 0) {
            at = i;
            break;
        }
        if (at < 0 && app->dev_stats[i].name[0] == '\0') {
            at = i;             /* a free slot, if none names it */
        }
    }
    if (at < 0) {
        return 0;
    }
    if (app->dev_stats[at].name[0] == '\0') {
        snprintf(app->dev_stats[at].name, sizeof app->dev_stats[at].name,
                 "%s", board);
        app->dev_stats[at].text[0] = '\0';
        app->dev_stats[at].at = 0;
    }
    snprintf(out, max, "%s", app->dev_stats[at].text);
    if (!app->dev_stats[at].asking &&
        tiku_trk_now() - app->dev_stats[at].at > STATS_HOLD_US) {
        char prefix[TIKU_PATH_MAX], local[TIKU_PATH_MAX];
        tiku_backend_t *dev;

        snprintf(prefix, sizeof prefix, "/devices/%s/sys", board);
        dev = tiku_backend_serving(app->backend, prefix, local,
                                   sizeof local);
        if (dev != NULL) {
            tiku_work_job_t job;
            sread_t *s = calloc(1, sizeof *s);

            if (s != NULL) {
                s->app = app;
                s->dev = dev;
                snprintf(s->name, sizeof s->name, "%s", board);
                memset(&job, 0, sizeof job);
                job.run = sread_run;
                job.done = sread_done;
                job.free = sread_free;
                job.ctx = s;
                snprintf(job.key, sizeof job.key, "stats:%.40s", board);
                if (tiku_work_post(&job) == 0) {
                    app->dev_stats[at].asking = 1;
                } else {
                    free(s);
                }
            }
        }
    }
    return (int)strlen(out);
}
