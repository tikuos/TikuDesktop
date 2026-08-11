/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_notify.c - deriving change notices, and parking them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_notify.h"

#include <stdio.h>
#include <string.h>

void
tiku_trk_evlist_clear(tiku_trk_evlist_t *l)
{
    if (l != NULL) {
        l->count = 0;
        l->overflowed = 0;
    }
}

/** @brief The directory part of @p path, into @p out. */
static void
parent_of(const char *path, char *out, size_t max)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL) {
        snprintf(out, max, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n == 0u) {
        snprintf(out, max, "/");
        return;
    }
    if (n >= max) {
        n = max - 1u;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

int
tiku_trk_evlist_add(tiku_trk_evlist_t *l, tiku_trk_evop_t op,
                    const tiku_trk_model_t *m, const char *from, int64_t at)
{
    tiku_trk_event_t *e;

    if (l == NULL || m == NULL) {
        return 0;
    }
    if (l->count >= TIKU_TRK_EVENTS_MAX) {
        /* Saying so is the point: a caller that silently lost notices would
         * leave the view believing it is current. */
        l->overflowed = 1;
        return 0;
    }
    e = &l->ev[l->count++];
    memset(e, 0, sizeof *e);
    e->op = op;
    e->model = *m;
    e->at = at;
    snprintf(e->path, sizeof e->path, "%s", m->path);
    if (from != NULL) {
        snprintf(e->from, sizeof e->from, "%s", from);
    }
    parent_of(e->path, e->dir, sizeof e->dir);
    return 1;
}

/** @brief Whether the facts a row displays have moved. */
static int
facts_differ(const tiku_trk_model_t *a, const tiku_trk_model_t *b)
{
    return a->facts.size != b->facts.size ||
           a->facts.mtime != b->facts.mtime ||
           a->facts.perm != b->facts.perm ||
           a->kind != b->kind ||
           /* A link losing its target changes nothing else about the entry,
            * so without this term the diff cannot see it happen and the row
            * goes on claiming the link is live. */
           a->facts.link_broken != b->facts.link_broken ||
           strcmp(a->facts.meta, b->facts.meta) != 0 ||
           strcmp(a->type, b->type) != 0;
}

/** @brief Index of @p path in a listing, or -1. */
static int
find_path(const tiku_trk_model_t *set, int n, const char *path)
{
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(set[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_notify_diff(const tiku_trk_model_t *was, int nwas,
                     const tiku_trk_model_t *now, int nnow, int64_t at,
                     tiku_trk_evlist_t *out)
{
    unsigned char paired[256];
    int i, j, before;

    if (out == NULL) {
        return 0;
    }
    before = out->count;
    memset(paired, 0, sizeof paired);

    /* A removal and a creation sharing a node id in one pass are a MOVE.
     * Reported as two events the row would be destroyed and rebuilt, losing
     * its selection, its place in the list and its saved position -- which
     * is precisely what a rename must not do. */
    for (i = 0; i < nwas; i++) {
        if (find_path(now, nnow, was[i].path) >= 0) {
            continue;                      /* still there                */
        }
        for (j = 0; j < nnow && j < (int)sizeof paired; j++) {
            if (paired[j] || was[i].node_id == 0 ||
                now[j].node_id != was[i].node_id) {
                continue;
            }
            if (find_path(was, nwas, now[j].path) >= 0) {
                continue;                  /* that one is not new         */
            }
            paired[j] = 1;
            (void)tiku_trk_evlist_add(out, TIKU_TRK_EV_MOVED, &now[j],
                                      was[i].path, at);
            break;
        }
        if (j >= nnow || j >= (int)sizeof paired) {
            (void)tiku_trk_evlist_add(out, TIKU_TRK_EV_REMOVED, &was[i],
                                      NULL, at);
        }
    }
    for (j = 0; j < nnow; j++) {
        int k;

        if (j < (int)sizeof paired && paired[j]) {
            continue;
        }
        k = find_path(was, nwas, now[j].path);
        if (k < 0) {
            (void)tiku_trk_evlist_add(out, TIKU_TRK_EV_CREATED, &now[j],
                                      NULL, at);
        } else if (facts_differ(&was[k], &now[j])) {
            (void)tiku_trk_evlist_add(out, TIKU_TRK_EV_CHANGED, &now[j],
                                      NULL, at);
        }
    }
    return out->count - before;
}

/*---------------------------------------------------------------------------*/
/* Parking                                                                   */
/*---------------------------------------------------------------------------*/

void
tiku_trk_park_clear(tiku_trk_park_t *p)
{
    if (p != NULL) {
        p->count = 0;
    }
}

int
tiku_trk_park_add(tiku_trk_park_t *p, const tiku_trk_event_t *e)
{
    if (p == NULL || e == NULL) {
        return 0;
    }
    if (p->count >= (int)(sizeof p->ev / sizeof p->ev[0])) {
        /* Full.  The OLDEST notice gives way, as the device's own ring does:
         * the newest is the state the row will actually need when it
         * appears, and dropping it instead would silently replay stale
         * facts over fresh ones. */
        int i;

        for (i = 0; i < p->count - 1; i++) {
            p->ev[i] = p->ev[i + 1];
        }
        p->count--;
    }
    p->ev[p->count++] = *e;
    return 1;
}

int
tiku_trk_park_take(tiku_trk_park_t *p, const char *path,
                   tiku_trk_event_t *out, int max)
{
    int i, n = 0, w = 0;

    if (p == NULL || path == NULL) {
        return 0;
    }
    /* Walked forwards and compacted in place, so what comes out is in the
     * order it arrived: two changes to one node replayed backwards would
     * leave the earlier value showing. */
    for (i = 0; i < p->count; i++) {
        if (strcmp(p->ev[i].path, path) == 0 && n < max) {
            /* Only what is actually HANDED OVER is removed.  Taking the
             * first few and discarding the rest would lose notices the
             * caller never saw -- silently, and precisely for the node it
             * asked about. */
            if (out != NULL) {
                out[n] = p->ev[i];
            }
            n++;
            continue;
        }
        p->ev[w++] = p->ev[i];
    }
    p->count = w;
    return (n > max) ? max : n;
}

int
tiku_trk_park_expire(tiku_trk_park_t *p, int64_t now)
{
    int i, w = 0, dropped = 0;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < p->count; i++) {
        if (now - p->ev[i].at >= TIKU_TRK_PARK_LIFETIME_US) {
            dropped++;
            continue;
        }
        p->ev[w++] = p->ev[i];
    }
    p->count = w;
    return dropped;
}

int
tiku_trk_park_count(const tiku_trk_park_t *p)
{
    return (p != NULL) ? p->count : 0;
}
