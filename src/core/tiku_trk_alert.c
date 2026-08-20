/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_alert.c - alert state, button layout, and the progress rows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_alert.h"

#include <stdio.h>
#include <string.h>

/** @brief Button geometry, shared by the drawing and the hit test. */
#define BTN_W   96
#define BTN_H   24
#define BTN_GAP 8
#define BTN_PAD 12

void
tiku_trk_alert_reset(tiku_trk_alert_t *a)
{
    if (a != NULL) {
        memset(a, 0, sizeof *a);
        a->chosen = -1;
        a->cancel = -1;
    }
}

void
tiku_trk_alert_open(tiku_trk_alert_t *a, tiku_trk_alert_kind_t kind, int tag,
                    const char *text, const char *buttons)
{
    const char *p = buttons;

    if (a == NULL) {
        return;
    }
    tiku_trk_alert_reset(a);
    a->kind = kind;
    a->tag = tag;
    snprintf(a->text, sizeof a->text, "%s", (text != NULL) ? text : "");
    while (p != NULL && *p != '\0' && a->nbutton < TIKU_TRK_ALERT_BUTTONS) {
        const char *bar = strchr(p, '|');
        size_t n = (bar != NULL) ? (size_t)(bar - p) : strlen(p);

        if (n >= sizeof a->button[0]) {
            n = sizeof a->button[0] - 1u;
        }
        memcpy(a->button[a->nbutton], p, n);
        a->button[a->nbutton][n] = '\0';
        a->nbutton++;
        p = (bar != NULL) ? bar + 1 : p + n;
    }
    /* Escape presses the FIRST button.  The original puts Cancel leftmost
     * and makes it the escape route, so the safe answer is the one a
     * keyboard reflex reaches. */
    a->cancel = (a->nbutton > 0) ? 0 : -1;
    a->dflt = a->nbutton - 1;
    {
        /* A destructive verb never rides Return: while the default names
         * an action that erases or overwrites, it steps LEFT toward the
         * safer answers -- the delete question's Return lands on Move to
         * Trash, an overwrite's on Cancel -- and the destructive answer
         * takes an aimed click (Mac HIG ch3; NeXT UIG ch5: the default
         * should be the safest of the alternatives). */
        static const char *harm[] = { "Delete", "Replace", "Replace all",
                                      "Empty Trash" };
        int harmful = 1;

        while (harmful && a->dflt > 0) {
            size_t k;

            harmful = 0;
            for (k = 0; k < sizeof harm / sizeof harm[0]; k++) {
                if (strcmp(a->button[a->dflt], harm[k]) == 0) {
                    harmful = 1;
                }
            }
            if (harmful) {
                a->dflt--;
            }
        }
    }
    a->open = 1;
}

void
tiku_trk_alert_for_conflict(tiku_trk_alert_t *a, const tiku_trk_progress_t *pr)
{
    char text[512];
    const char *leaf;

    if (a == NULL || pr == NULL) {
        return;
    }
    leaf = strrchr(pr->conflict_path, '/');
    leaf = (leaf != NULL) ? leaf + 1 : pr->conflict_path;
    snprintf(text, sizeof text,
             "An item named \"%.80s\" already exists here.", leaf);
    /* The four-way form when a folder is involved or more than one
     * collision is pending; otherwise the short one.  The engine already
     * decided which -- this only renders it (FS-018, FS-019). */
    if (pr->conflict_many) {
        tiku_trk_alert_open(a, TIKU_TRK_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_CONFLICT, text,
                            "Cancel|Skip|Skip all|Replace all");
    } else {
        tiku_trk_alert_open(a, TIKU_TRK_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_CONFLICT, text,
                            "Cancel|Replace");
    }
}

void
tiku_trk_alert_for_error(tiku_trk_alert_t *a, const tiku_trk_progress_t *pr)
{
    if (a == NULL || pr == NULL) {
        return;
    }
    /* A delete failure offers no way on: there is nothing to continue to,
     * which is why the engine carries the flag rather than the shell
     * guessing from the message. */
    tiku_trk_alert_open(a, TIKU_TRK_ALERT_STOP, TIKU_TRK_ALERT_TAG_ERROR,
                        pr->error,
                        pr->error_can_continue ? "Cancel|OK" : "Cancel");
}

void
tiku_trk_alert_button_rect(const tiku_trk_alert_t *a, int i, int w, int h,
                           int *bx, int *by, int *bw, int *bh)
{
    int from_right;

    if (a == NULL || i < 0 || i >= a->nbutton) {
        return;
    }
    /* Laid out from the right edge: the default action is the rightmost,
     * which is where the pointer already is after reading the text. */
    from_right = a->nbutton - 1 - i;
    if (bx != NULL) {
        *bx = w - BTN_PAD - BTN_W - from_right * (BTN_W + BTN_GAP);
    }
    if (by != NULL) { *by = h - BTN_PAD - BTN_H; }
    if (bw != NULL) { *bw = BTN_W; }
    if (bh != NULL) { *bh = BTN_H; }
}

int
tiku_trk_alert_button_at(const tiku_trk_alert_t *a, int w, int h, int x,
                         int y)
{
    int i;

    if (a == NULL || !a->open) {
        return -1;
    }
    for (i = 0; i < a->nbutton; i++) {
        int bx, by, bw, bh;

        tiku_trk_alert_button_rect(a, i, w, h, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            return i;
        }
    }
    return -1;
}

tiku_trk_conflict_t
tiku_trk_alert_conflict_answer(const tiku_trk_alert_t *a)
{
    if (a == NULL || a->chosen < 0) {
        return TIKU_TRK_CONFLICT_ASK;
    }
    if (a->nbutton == 2) {
        /* Cancel | Replace */
        return (a->chosen == 1) ? TIKU_TRK_CONFLICT_REPLACE
                                : TIKU_TRK_CONFLICT_SKIP;
    }
    /* Cancel | Skip | Skip all | Replace all */
    switch (a->chosen) {
    case 1:  return TIKU_TRK_CONFLICT_SKIP;
    case 2:  return TIKU_TRK_CONFLICT_SKIP_ALL;
    case 3:  return TIKU_TRK_CONFLICT_REPLACE_ALL;
    default: return TIKU_TRK_CONFLICT_SKIP;
    }
}

/*---------------------------------------------------------------------------*/

void
tiku_trk_progress_reset(tiku_trk_progress_win_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof *p);
    }
}

const char *
tiku_trk_progress_caption(tiku_trk_op_t op)
{
    switch (op) {
    case TIKU_TRK_OP_COPY:      return "Copying: ";
    case TIKU_TRK_OP_MOVE:      return "Moving: ";
    case TIKU_TRK_OP_DUPLICATE: return "Duplicating: ";
    case TIKU_TRK_OP_TRASH:     return "Emptying Trash... ";
    case TIKU_TRK_OP_DELETE:    return "Deleting: ";
    case TIKU_TRK_OP_LINK:      return "Creating links: ";
    default:                    return "";
    }
}

void
tiku_trk_progress_update(tiku_trk_progress_win_t *p, int i,
                         const tiku_trk_progress_t *pr, int busy,
                         const char *dest)
{
    int k, any = 0;

    if (p == NULL || i < 0 || i >= TIKU_TRK_PROGRESS_ROWS) {
        return;
    }
    if (!busy) {
        memset(&p->row[i], 0, sizeof p->row[i]);
    } else if (pr != NULL) {
        p->row[i].used = 1;
        snprintf(p->row[i].caption, sizeof p->row[i].caption, "%s",
                 tiku_trk_progress_caption(pr->op));
        /* The name is copied even when the counter has not moved: it is
         * the one field a throttled repaint must not lose (FS-075). */
        if (pr->current[0] != '\0') {
            snprintf(p->row[i].current, sizeof p->row[i].current, "%s",
                     pr->current);
        }
        if (dest != NULL) {
            snprintf(p->row[i].dest, sizeof p->row[i].dest, "%s", dest);
        }
        p->row[i].done = pr->done + pr->skipped + pr->failed;
        p->row[i].total = pr->total;
    }
    /* The window exists only while a job does: it appears with the first
     * and goes with the last, rather than sitting empty (FS-072). */
    for (k = 0; k < TIKU_TRK_PROGRESS_ROWS; k++) {
        any |= p->row[k].used;
    }
    p->open = any;
}

/*---------------------------------------------------------------------------*/
/* The pause and stop buttons (FS-074)                                       */
/*---------------------------------------------------------------------------*/

#define PROG_ROW_H  48
#define PROG_TOP    14
#define PROG_BTN    18
#define PROG_BTN_GAP 4

void
tiku_trk_progress_button_rect(int i, int which, int w, int *bx, int *by,
                              int *bw, int *bh)
{
    /* Right-hand end of the row, stop outermost: the destructive one is
     * furthest from the pause a user is aiming for. */
    int right = w - 10;
    int x = right - PROG_BTN - (which == TIKU_TRK_PROGRESS_HIT_STOP
                                    ? 0 : PROG_BTN + PROG_BTN_GAP);

    if (bx != NULL) { *bx = x; }
    if (by != NULL) { *by = PROG_TOP + i * PROG_ROW_H + 4; }
    if (bw != NULL) { *bw = PROG_BTN; }
    if (bh != NULL) { *bh = PROG_BTN; }
}

tiku_trk_progress_hit_t
tiku_trk_progress_hit(const tiku_trk_progress_win_t *p, int w, int x, int y,
                      int *row)
{
    int i, k;

    if (p == NULL || !p->open) {
        return TIKU_TRK_PROGRESS_HIT_NONE;
    }
    for (i = 0; i < TIKU_TRK_PROGRESS_ROWS; i++) {
        if (!p->row[i].used) {
            continue;
        }
        for (k = TIKU_TRK_PROGRESS_HIT_PAUSE; k <= TIKU_TRK_PROGRESS_HIT_STOP;
             k++) {
            int bx, by, bw, bh;

            tiku_trk_progress_button_rect(i, k, w, &bx, &by, &bw, &bh);
            if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
                if (row != NULL) { *row = i; }
                return (tiku_trk_progress_hit_t)k;
            }
        }
    }
    return TIKU_TRK_PROGRESS_HIT_NONE;
}

const char *
tiku_trk_progress_row_text(const tiku_trk_progress_win_t *p, int i)
{
    if (p == NULL || i < 0 || i >= TIKU_TRK_PROGRESS_ROWS ||
        !p->row[i].used) {
        return "";
    }
    /* A paused row says what to do about it: a frozen counter with no
     * explanation reads as a hang. */
    return p->row[i].paused ? "Paused: click to resume or stop"
                            : p->row[i].current;
}

int
tiku_trk_progress_wanted(tiku_trk_op_t op, int items)
{
    if (op == TIKU_TRK_OP_LINK) {
        /* Links are instantaneous; a window that appeared and vanished
         * would be a flash nobody can read (FS-070). */
        return (items > TIKU_TRK_PROGRESS_LINK_MIN);
    }
    return 1;
}

void
tiku_trk_progress_sample(tiku_trk_progress_win_t *p, int i, int64_t bytes,
                         int64_t now)
{
    tiku_trk_progress_row_t *r;
    int k;

    if (p == NULL || i < 0 || i >= TIKU_TRK_PROGRESS_ROWS) {
        return;
    }
    r = &p->row[i];
    r->total_bytes = bytes;
    /* A ring of the last few samples: the mean over them is the speed,
     * and one huge file must not set it for the rest of the job. */
    for (k = TIKU_TRK_PROGRESS_SLOTS - 1; k > 0; k--) {
        r->bytes[k] = r->bytes[k - 1];
        r->at[k] = r->at[k - 1];
    }
    r->bytes[0] = bytes;
    r->at[0] = now;
    if (r->nsample < TIKU_TRK_PROGRESS_SLOTS) {
        r->nsample++;
    }
}

int64_t
tiku_trk_progress_speed(const tiku_trk_progress_win_t *p, int i)
{
    const tiku_trk_progress_row_t *r;
    int last;
    int64_t db, dt;

    if (p == NULL || i < 0 || i >= TIKU_TRK_PROGRESS_ROWS) {
        return 0;
    }
    r = &p->row[i];
    if (r->nsample < 2) {
        return 0;                   /* not enough to average anything */
    }
    last = r->nsample - 1;
    db = r->bytes[0] - r->bytes[last];
    dt = r->at[0] - r->at[last];
    if (dt <= 0 || db <= 0) {
        return 0;
    }
    return db * 1000000 / dt;
}

int64_t
tiku_trk_progress_eta(const tiku_trk_progress_win_t *p, int i)
{
    int64_t speed = tiku_trk_progress_speed(p, i);
    const tiku_trk_progress_row_t *r;
    int64_t left;

    if (speed <= 0 || p == NULL) {
        return -1;                  /* a job just started has no estimate
                                     * rather than a wild one */
    }
    r = &p->row[i];
    if (r->total <= 0 || r->done <= 0 || r->done >= r->total) {
        return -1;
    }
    /* Scaled from the items done: bytes are what the speed is in, and the
     * item count is the only measure of what is left. */
    left = r->total_bytes * (r->total - r->done) / r->done;
    return left / speed;
}

int
tiku_trk_progress_show_speed(int64_t since_open)
{
    int64_t cycle = TIKU_TRK_PROGRESS_SPEED_US +
                    TIKU_TRK_PROGRESS_ESTIMATE_US;
    int64_t at;

    if (since_open < 0) {
        return 1;
    }
    at = since_open % cycle;
    return (at < TIKU_TRK_PROGRESS_SPEED_US);
}
