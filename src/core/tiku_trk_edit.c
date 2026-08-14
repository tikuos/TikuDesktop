/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_edit.c - the in-place text editor a rename types into.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_edit.h"

#include <stdio.h>
#include <string.h>

/* The inline fields in the two shells are separate views of one Tracker
 * clipboard.  Keeping the text payload here also makes it impossible for a
 * focused editor's Cmd-X/C/V to fall through into the file clipboard. */
static char edit_clipboard[TIKU_TRK_EDIT_MAX];

static void
copy_selection(tiku_trk_edit_t *e)
{
    int from, to;

    if (tiku_trk_edit_selection(e, &from, &to)) {
        size_t n = (size_t)(to - from);

        if (n >= sizeof edit_clipboard) {
            n = sizeof edit_clipboard - 1u;
        }
        memcpy(edit_clipboard, e->text + from, n);
        edit_clipboard[n] = '\0';
    } else {
        edit_clipboard[0] = '\0';
    }
}

void
tiku_trk_edit_start(tiku_trk_edit_t *e, const char *text)
{
    if (e == NULL) {
        return;
    }
    memset(e, 0, sizeof *e);
    e->deny = NULL;
    snprintf(e->text, sizeof e->text, "%s", (text != NULL) ? text : "");
    e->len = (int)strlen(e->text);
    e->caret = e->len;
    e->sel_a = 0;
    e->sel_b = e->len;
    e->active = 1;
    e->editable = 1;
}

void
tiku_trk_edit_set_editable(tiku_trk_edit_t *e, int editable)
{
    if (e != NULL) {
        e->editable = editable ? 1 : 0;
    }
}

void
tiku_trk_edit_deny(tiku_trk_edit_t *e, const char *deny)
{
    if (e != NULL) {
        e->deny = deny;
    }
}

void
tiku_trk_edit_stop(tiku_trk_edit_t *e)
{
    if (e != NULL) {
        e->active = 0;
    }
}

int
tiku_trk_edit_selection(const tiku_trk_edit_t *e, int *from, int *to)
{
    int lo, hi;

    if (e == NULL || e->sel_a == e->sel_b) {
        return 0;
    }
    lo = (e->sel_a < e->sel_b) ? e->sel_a : e->sel_b;
    hi = (e->sel_a < e->sel_b) ? e->sel_b : e->sel_a;
    if (from != NULL) { *from = lo; }
    if (to != NULL)   { *to = hi; }
    return 1;
}

/** @brief Remove the selected range, leaving the caret where it was. */
static void
delete_selection(tiku_trk_edit_t *e)
{
    int lo, hi;

    if (!tiku_trk_edit_selection(e, &lo, &hi)) {
        return;
    }
    memmove(e->text + lo, e->text + hi, (size_t)(e->len - hi) + 1u);
    e->len -= (hi - lo);
    e->caret = lo;
    e->sel_a = e->sel_b = lo;
}

/** @brief Move the caret, extending or dropping the selection. */
static void
move_caret(tiku_trk_edit_t *e, int to, int shift)
{
    if (to < 0)       { to = 0; }
    if (to > e->len)  { to = e->len; }
    if (shift) {
        /* The anchor stays where the selection began, so shift-left after
         * shift-right shrinks the range rather than starting a new one. */
        if (e->sel_a == e->sel_b) {
            e->sel_a = e->caret;
        }
        e->sel_b = to;
    } else {
        e->sel_a = e->sel_b = to;
    }
    e->caret = to;
}

tiku_trk_edit_result_t
tiku_trk_edit_key(tiku_trk_edit_t *e, tiku_trk_edit_key_t key, int shift,
                  char ch)
{
    if (e == NULL || !e->active) {
        return TIKU_TRK_EDIT_IGNORED;
    }
    switch (key) {
    case TIKU_TRK_EDIT_CHAR:
        if (!e->editable) {
            return TIKU_TRK_EDIT_IGNORED;
        }
        if (ch < 0x20 || ch == 0x7f) {
            return TIKU_TRK_EDIT_IGNORED;
        }
        if (e->deny != NULL && strchr(e->deny, ch) != NULL) {
            /* Swallowed as it is typed, not refused at commit: a character
             * that cannot be in the name should never appear in the field,
             * or the user finishes typing and is then told to start again. */
            return TIKU_TRK_EDIT_IGNORED;
        }
        delete_selection(e);
        if (e->len + 1 >= (int)sizeof e->text) {
            return TIKU_TRK_EDIT_HANDLED;
        }
        memmove(e->text + e->caret + 1, e->text + e->caret,
                (size_t)(e->len - e->caret) + 1u);
        e->text[e->caret] = ch;
        e->len++;
        e->caret++;
        e->sel_a = e->sel_b = e->caret;
        return TIKU_TRK_EDIT_HANDLED;

    case TIKU_TRK_EDIT_LEFT:
        move_caret(e, e->caret - 1, shift);
        return TIKU_TRK_EDIT_HANDLED;
    case TIKU_TRK_EDIT_RIGHT:
        move_caret(e, e->caret + 1, shift);
        return TIKU_TRK_EDIT_HANDLED;
    case TIKU_TRK_EDIT_HOME:
        move_caret(e, 0, shift);
        return TIKU_TRK_EDIT_HANDLED;
    case TIKU_TRK_EDIT_END:
        move_caret(e, e->len, shift);
        return TIKU_TRK_EDIT_HANDLED;

    case TIKU_TRK_EDIT_BACKSPACE:
        if (!e->editable) {
            return TIKU_TRK_EDIT_HANDLED;
        }
        if (tiku_trk_edit_selection(e, NULL, NULL)) {
            delete_selection(e);
        } else if (e->caret > 0) {
            memmove(e->text + e->caret - 1, e->text + e->caret,
                    (size_t)(e->len - e->caret) + 1u);
            e->len--;
            e->caret--;
            e->sel_a = e->sel_b = e->caret;
        }
        return TIKU_TRK_EDIT_HANDLED;

    case TIKU_TRK_EDIT_DELETE:
        if (!e->editable) {
            return TIKU_TRK_EDIT_HANDLED;
        }
        if (tiku_trk_edit_selection(e, NULL, NULL)) {
            delete_selection(e);
        } else if (e->caret < e->len) {
            memmove(e->text + e->caret, e->text + e->caret + 1,
                    (size_t)(e->len - e->caret));
            e->len--;
        }
        return TIKU_TRK_EDIT_HANDLED;

    case TIKU_TRK_EDIT_SELECT_ALL:
        e->sel_a = 0;
        e->sel_b = e->len;
        e->caret = e->len;
        return TIKU_TRK_EDIT_HANDLED;

    case TIKU_TRK_EDIT_COPY:
        copy_selection(e);
        return TIKU_TRK_EDIT_HANDLED;

    case TIKU_TRK_EDIT_CUT:
        if (!e->editable) {
            return TIKU_TRK_EDIT_HANDLED;
        }
        copy_selection(e);
        delete_selection(e);
        return TIKU_TRK_EDIT_HANDLED;

    case TIKU_TRK_EDIT_PASTE: {
        size_t i;

        if (!e->editable) {
            return TIKU_TRK_EDIT_HANDLED;
        }

        delete_selection(e);
        for (i = 0; edit_clipboard[i] != '\0'; i++) {
            (void)tiku_trk_edit_key(e, TIKU_TRK_EDIT_CHAR, 0,
                                    edit_clipboard[i]);
        }
        return TIKU_TRK_EDIT_HANDLED;
    }

    case TIKU_TRK_EDIT_COMMIT:
        e->active = 0;
        return TIKU_TRK_EDIT_COMMITTED;

    case TIKU_TRK_EDIT_CANCEL:
        e->active = 0;
        return TIKU_TRK_EDIT_CANCELLED;

    default:
        return TIKU_TRK_EDIT_IGNORED;
    }
}
