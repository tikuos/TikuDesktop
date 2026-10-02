/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc_undo.c - the history of transactions: redo branches,
 * joined typing, saved state and a byte budget kept by whole steps.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_editdoc_private.h"

#include <string.h>

/** @brief What one record charges the budget: its bytes, pieces and
 *         bookkeeping. */
static size_t
record_cost(const tiku_ed_record_t *r)
{
    return sizeof *r + r->npieces * sizeof *r->pieces + r->payload_len +
           (r->nbefore + r->nafter) * sizeof *r->more;
}

void
tiku_ed_history_init(tiku_ed_history_t *h, size_t target)
{
    memset(h, 0, sizeof *h);
    h->target = target;
    h->base_state = 1;
    h->saved_state = 1;
    h->next_state = 2;
}

void
tiku_ed_record_free(tiku_ed_record_t *r)
{
    tiku_ed_free(r->pieces);
    tiku_ed_free(r->payload);
    tiku_ed_free(r->more);
    memset(r, 0, sizeof *r);
}

void
tiku_ed_history_free(tiku_ed_history_t *h)
{
    size_t i;

    for (i = 0; i < h->count; i++)
        tiku_ed_record_free(&h->records[i]);
    tiku_ed_free(h->records);
    h->records = NULL;
    h->count = h->cap = h->current = 0;
    h->cost = 0;
}

uint64_t
tiku_ed_history_state(const tiku_ed_history_t *h)
{
    return h->current ? h->records[h->current - 1].state_after
                      : h->base_state;
}

int
tiku_ed_history_reserve(tiku_ed_history_t *h)
{
    size_t n;
    tiku_ed_record_t *grown;

    /* The new record goes at current; the redo branch above it is
     * dropped, so only current + 1 slots are needed. */
    if (h->current < h->cap)
        return 1;
    n = h->cap ? h->cap * 2 : 16;
    if (n > SIZE_MAX / sizeof *grown)
        return 0;
    grown = tiku_ed_realloc(h->records, n * sizeof *grown);
    if (!grown)
        return 0;
    h->records = grown;
    h->cap = n;
    return 1;
}

void
tiku_ed_history_push(tiku_ed_history_t *h, tiku_ed_record_t *r)
{
    size_t i;

    for (i = h->current; i < h->count; i++) {
        h->cost -= h->records[i].cost;
        tiku_ed_record_free(&h->records[i]);
    }
    r->cost = record_cost(r);
    h->records[h->current] = *r;
    h->count = ++h->current;
    h->cost += r->cost;
}

void
tiku_ed_history_prune(tiku_ed_history_t *h)
{
    while (h->cost > h->target && h->count > 1 && h->current > 0) {
        h->base_state = h->records[0].state_after;
        h->cost -= h->records[0].cost;
        tiku_ed_record_free(&h->records[0]);
        memmove(h->records, h->records + 1,
                (h->count - 1) * sizeof *h->records);
        h->count--;
        h->current--;
    }
}

void
tiku_ed_history_clear(tiku_ed_history_t *h)
{
    size_t i;

    for (i = 0; i < h->count; i++)
        tiku_ed_record_free(&h->records[i]);
    h->count = h->current = 0;
    h->cost = 0;
    h->base_state = h->next_state++;
    h->saved_state = h->base_state;
    h->open = 0;
}

int
tiku_ed_history_joins(const tiku_ed_history_t *h, const tiku_ed_record_t *r)
{
    const tiku_ed_record_t *last;
    const tiku_ed_piece_t *p, *q;

    if (!h->open || h->current == 0 || h->current != h->count)
        return 0;
    last = &h->records[h->current - 1];
    if (r->typed == TIKU_ED_TYPED_NONE || r->typed != last->typed ||
        r->npieces != 1 || last->npieces != 1 || r->nbefore || r->nafter ||
        last->nbefore || last->nafter ||
        r->view.slot != last->view.slot ||
        r->view.generation != last->view.generation ||
        last->state_after == h->saved_state)
        return 0;
    p = &last->pieces[0];
    q = &r->pieces[0];
    if (r->typed == TIKU_ED_TYPED_INSERT)
        /* Insertions join only immediately after the last one. */
        return q->removed == 0 && q->pre == p->post + p->added;
    /* Backspace ends where the last deletion was, Delete starts there. */
    return q->pre + q->removed == p->post || q->pre == p->post;
}

int
tiku_ed_history_merge(const tiku_ed_history_t *h, const tiku_ed_record_t *r,
                      char **payload, size_t *payload_len,
                      tiku_ed_piece_t *piece)
{
    const tiku_ed_record_t *last = &h->records[h->current - 1];
    const tiku_ed_piece_t *p = &last->pieces[0], *q = &r->pieces[0];
    const char *old = last->payload, *add = r->payload;
    size_t len = last->payload_len, extra = r->payload_len;
    char *out;

    if (extra > SIZE_MAX - len)
        return 0;
    out = tiku_ed_alloc(len + extra ? len + extra : 1);
    if (!out)
        return 0;
    *piece = *p;
    if (r->typed == TIKU_ED_TYPED_INSERT) {
        /* removed, added, then the new added bytes. */
        memcpy(out, old, len);
        memcpy(out + len, add + q->added_at, (size_t)q->added);
        piece->added = p->added + q->added;
    } else if (q->pre + q->removed == p->post) {
        /* Backspace: the new bytes come first. */
        memcpy(out, add + q->removed_at, (size_t)q->removed);
        memcpy(out + q->removed, old, len);
        piece->pre = piece->post = q->pre;
        piece->removed = p->removed + q->removed;
    } else {
        /* Delete: the new bytes follow. */
        memcpy(out, old, len);
        memcpy(out + len, add + q->removed_at, (size_t)q->removed);
        piece->removed = p->removed + q->removed;
    }
    piece->removed_at = 0;
    piece->added_at = (size_t)piece->removed;
    *payload = out;
    *payload_len = len + extra;
    return 1;
}

char *
tiku_ed_history_replace_last(tiku_ed_history_t *h, char *payload,
                             size_t payload_len, const tiku_ed_piece_t *piece)
{
    tiku_ed_record_t *last = &h->records[h->current - 1];
    char *old = last->payload;

    h->cost -= last->cost;
    last->pieces[0] = *piece;
    last->payload = payload;
    last->payload_len = payload_len;
    last->state_after = h->next_state++;
    last->cost = record_cost(last);
    h->cost += last->cost;
    return old;
}
