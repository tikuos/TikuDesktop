/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_textview.c - the document's text, caret and scroll.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <string.h>

#include "tiku_textview.h"

/** @brief Replace one line's text with @p len bytes of @p text. */
static void
line_set(tiku_textview_line_t *l, const char *text, int len)
{
    char *copy;

    if (len < 0) {
        len = 0;
    }
    copy = malloc((size_t)len + 1u);
    if (copy == NULL) {
        return;                 /* the old text stands rather than a NULL */
    }
    if (len > 0 && text != NULL) {
        memcpy(copy, text, (size_t)len);
    }
    copy[len] = '\0';
    free(l->text);
    l->text = copy;
    l->len = len;
}

/** @brief A NUL-closed copy of @p n bytes of @p src. */
static char *
dupn(const char *src, int n)
{
    char *p = malloc((size_t)n + 1u);

    if (p == NULL) {
        return NULL;
    }
    if (n > 0 && src != NULL) {
        memcpy(p, src, (size_t)n);
    }
    p[n] = '\0';
    return p;
}

/** @brief Empty the journal, freeing every entry's text. */
static void
journal_clear(tiku_textview_t *tv)
{
    int i;

    for (i = 0; i < tv->undo_n; i++) {
        free(tv->undo[i].text);
        tv->undo[i].text = NULL;
    }
    tv->undo_n = 0;
    tv->undo_at = 0;
    tv->saved_at = 0;
}

/**
 * @brief Write one edit into the journal, folding a keystroke run.
 *
 * A single character continuing the run above -- typed just after the
 * last, or rubbed out just before it -- grows that entry rather than
 * making a new one, so a word is one step to undo.  Anything else
 * starts a fresh entry, and the redo history above the present is let
 * go the moment a new edit lands.  Quiet while undo/redo is replaying.
 */
static void
record(tiku_textview_t *tv, int kind, int line, int col,
       const char *text, int len)
{
    struct tiku_textview_edit *top;
    int i;

    if (tv == NULL || !tv->recording) {
        return;
    }
    for (i = tv->undo_at; i < tv->undo_n; i++) {
        free(tv->undo[i].text);
        tv->undo[i].text = NULL;
    }
    tv->undo_n = tv->undo_at;

    if (len == 1 && text[0] != '\n' && tv->undo_n > 0) {
        top = &tv->undo[tv->undo_n - 1];
        if (top->coalesce && top->kind == kind && top->line == line) {
            if (kind == 1 && top->col + top->len == col) {
                char *g = dupn(top->text, top->len + 1);

                if (g != NULL) {
                    g[top->len] = text[0];
                    free(top->text);
                    top->text = g;
                    top->len++;
                }
                return;         /* typing forward: appended */
            }
            if (kind == 2 && col == top->col) {
                char *g = dupn(top->text, top->len + 1);

                if (g != NULL) {
                    g[top->len] = text[0];
                    free(top->text);
                    top->text = g;
                    top->len++;
                }
                return;         /* forward delete at one place: appended */
            }
            if (kind == 2 && col + 1 == top->col) {
                char *g = malloc((size_t)top->len + 2u);

                if (g != NULL) {
                    g[0] = text[0];
                    memcpy(g + 1, top->text, (size_t)top->len + 1u);
                    free(top->text);
                    top->text = g;
                    top->len++;
                    top->col = col;
                }
                return;         /* backspace run: prepended */
            }
        }
    }

    if (tv->undo_n >= TIKU_TEXTVIEW_UNDO) {
        free(tv->undo[0].text);
        for (i = 1; i < tv->undo_n; i++) {
            tv->undo[i - 1] = tv->undo[i];
        }
        tv->undo_n--;
        tv->saved_at = (tv->saved_at > 0) ? tv->saved_at - 1 : -1;
    }
    top = &tv->undo[tv->undo_n];
    top->kind = kind;
    top->line = line;
    top->col = col;
    top->text = dupn(text, len);
    top->len = len;
    top->coalesce = (len == 1 && text[0] != '\n');
    top->group = tv->group_open;
    tv->undo_n++;
    tv->undo_at = tv->undo_n;
}

void
tiku_textview_group_begin(tiku_textview_t *tv)
{
    if (tv != NULL) {
        tv->group_seq++;
        tv->group_open = tv->group_seq;
    }
}

void
tiku_textview_group_end(tiku_textview_t *tv)
{
    if (tv != NULL) {
        tv->group_open = 0;
    }
}

static int sel_delete(tiku_textview_t *tv);

void
tiku_textview_init(tiku_textview_t *tv)
{
    if (tv == NULL) {
        return;
    }
    memset(tv, 0, sizeof *tv);
    /* One empty line, never zero: every operation below may assume the
     * caret has a line to stand on, which is what keeps them short. */
    line_set(&tv->line[0], "", 0);
    tv->nline = 1;
    tv->recording = 1;          /* the journal listens from the start */
}

void
tiku_textview_free(tiku_textview_t *tv)
{
    int i;

    if (tv == NULL) {
        return;
    }
    for (i = 0; i < tv->nline; i++) {
        free(tv->line[i].text);
        tv->line[i].text = NULL;
        tv->line[i].len = 0;
    }
    tv->nline = 0;
    journal_clear(tv);
    tiku_textview_init(tv);
}

void
tiku_textview_set(tiku_textview_t *tv, const char *text)
{
    const char *p, *nl;

    if (tv == NULL) {
        return;
    }
    tiku_textview_free(tv);
    tv->nline = 0;
    p = (text != NULL) ? text : "";
    for (;;) {
        int len;

        nl = strchr(p, '\n');
        len = (nl != NULL) ? (int)(nl - p) : (int)strlen(p);
        if (len > TIKU_TEXTVIEW_LINE_MAX - 1) {
            len = TIKU_TEXTVIEW_LINE_MAX - 1;
        }
        if (tv->nline >= TIKU_TEXTVIEW_LINES_MAX) {
            break;
        }
        memset(&tv->line[tv->nline], 0, sizeof tv->line[0]);
        line_set(&tv->line[tv->nline], p, len);
        tv->nline++;
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
        if (*p == '\0') {
            /* The newline that ENDS a file closes the last line rather
             * than starting an empty one, or a file grows a blank line
             * every time it is opened and written back. */
            break;
        }
    }
    if (tv->nline == 0) {
        line_set(&tv->line[0], "", 0);
        tv->nline = 1;
    }
    tv->cy = 0;
    tv->cx = 0;
    tv->selecting = 0;
    journal_clear(tv);          /* a fresh document has nothing to undo */
    tv->recording = 1;
    tv->top = 0;
}

int
tiku_textview_lines(const tiku_textview_t *tv)
{
    return (tv != NULL && tv->nline > 0) ? tv->nline : 0;
}

const char *
tiku_textview_line(const tiku_textview_t *tv, int i)
{
    if (tv == NULL || i < 0 || i >= tv->nline ||
        tv->line[i].text == NULL) {
        return "";
    }
    return tv->line[i].text;
}

int
tiku_textview_line_len(const tiku_textview_t *tv, int i)
{
    if (tv == NULL || i < 0 || i >= tv->nline) {
        return 0;
    }
    return tv->line[i].len;
}

int
tiku_textview_modified(const tiku_textview_t *tv)
{
    /* Not a flag that only ever goes up: the DISTANCE from the mark the
     * last save left, so undoing back to a save reads as unmodified. */
    return (tv != NULL) ? (tv->undo_at != tv->saved_at) : 0;
}

void
tiku_textview_saved(tiku_textview_t *tv)
{
    if (tv != NULL) {
        tv->saved_at = tv->undo_at;
        /* The run is closed at a save, so the next keystroke starts a
         * new entry -- or typing after a save would grow the entry the
         * mark points at, and the change would not read as one. */
        if (tv->undo_n > 0) {
            tv->undo[tv->undo_n - 1].coalesce = 0;
        }
    }
}

void
tiku_textview_caret(const tiku_textview_t *tv, int *line, int *col)
{
    if (line != NULL) { *line = (tv != NULL) ? tv->cy : 0; }
    if (col != NULL)  { *col  = (tv != NULL) ? tv->cx : 0; }
}

int
tiku_textview_top(const tiku_textview_t *tv)
{
    return (tv != NULL) ? tv->top : 0;
}

void
tiku_textview_place(tiku_textview_t *tv, int line, int col)
{
    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    if (line < 0) { line = 0; }
    if (line >= tv->nline) { line = tv->nline - 1; }
    tv->cy = line;
    if (col < 0) { col = 0; }
    if (col > tv->line[line].len) { col = tv->line[line].len; }
    tv->cx = col;
}

void
tiku_textview_select_begin(tiku_textview_t *tv)
{
    if (tv == NULL) {
        return;
    }
    if (!tv->selecting) {
        tv->ay = tv->cy;
        tv->ax = tv->cx;
        tv->selecting = 1;
    }
}

void
tiku_textview_select_none(tiku_textview_t *tv)
{
    if (tv != NULL) {
        tv->selecting = 0;
    }
}

void
tiku_textview_select_all(tiku_textview_t *tv)
{
    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    tv->ay = 0;
    tv->ax = 0;
    tv->selecting = 1;
    tv->cy = tv->nline - 1;
    tv->cx = tv->line[tv->cy].len;
}

int
tiku_textview_selection(const tiku_textview_t *tv,
                        int *y0, int *x0, int *y1, int *x1)
{
    int ay, ax, by, bx;

    if (tv == NULL || !tv->selecting) {
        return 0;
    }
    if (tv->ay == tv->cy && tv->ax == tv->cx) {
        return 0;               /* an anchor at the caret is no span */
    }
    ay = tv->ay; ax = tv->ax;
    by = tv->cy; bx = tv->cx;
    if (ay > by || (ay == by && ax > bx)) {
        int ty = ay, tx = ax;

        ay = by; ax = bx;
        by = ty; bx = tx;
    }
    if (y0 != NULL) { *y0 = ay; }
    if (x0 != NULL) { *x0 = ax; }
    if (y1 != NULL) { *y1 = by; }
    if (x1 != NULL) { *x1 = bx; }
    return 1;
}

size_t
tiku_textview_selected(const tiku_textview_t *tv, char *out, size_t max)
{
    int y0, x0, y1, x1, line;
    size_t used = 0;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (!tiku_textview_selection(tv, &y0, &x0, &y1, &x1)) {
        return 0;
    }
    for (line = y0; line <= y1; line++) {
        const char *text = tv->line[line].text;
        int len = tv->line[line].len;
        int c0 = (line == y0) ? x0 : 0;
        int c1 = (line == y1) ? x1 : len;
        size_t n;

        if (c0 > len) { c0 = len; }
        if (c1 > len) { c1 = len; }
        n = (size_t)((c1 > c0) ? c1 - c0 : 0);
        /* room for these characters, the newline this line carries only
         * if it is NOT the last, and the terminator -- reserving a
         * newline the last line never has would drop its final byte. */
        if (used + n + (size_t)((line < y1) ? 1u : 0u) + 1u > max) {
            break;              /* whole characters or none of them */
        }
        memcpy(out + used, text + c0, n);
        used += n;
        if (line < y1) {
            out[used++] = '\n';
        }
    }
    out[used] = '\0';
    return used;
}

void
tiku_textview_insert_text(tiku_textview_t *tv, const char *text)
{
    size_t i;

    int ol, oc, was_rec;
    char *clean;
    size_t n = 0;

    if (tv == NULL || text == NULL) {
        return;
    }
    (void)sel_delete(tv);       /* a paste over a selection replaces it */
    ol = tv->cy;
    oc = tv->cx;
    /* Cleaned to what a file may hold FIRST, so the journal records
     * exactly what landed -- undoing a paste then removes just those
     * characters, not the ones the paste itself dropped. */
    clean = malloc(strlen(text) + 1u);
    if (clean == NULL) {
        return;
    }
    for (i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n' ||
            ((unsigned char)text[i] >= 32u &&
             (unsigned char)text[i] < 127u)) {
            clean[n++] = text[i];
        }
    }
    clean[n] = '\0';
    was_rec = tv->recording;
    tv->recording = 0;          /* the run lands as ONE journal entry */
    for (i = 0; i < n; i++) {
        if (clean[i] == '\n') {
            tiku_textview_newline(tv);
        } else {
            tiku_textview_insert(tv, clean[i]);
        }
    }
    tv->recording = was_rec;
    record(tv, 1, ol, oc, clean, (int)n);
    free(clean);
}

int
tiku_textview_undo(tiku_textview_t *tv)
{
    struct tiku_textview_edit *e;

    if (tv == NULL || tv->undo_at <= 0) {
        return 0;
    }
    {
        int g = tv->undo[tv->undo_at - 1].group;

        tv->recording = 0;
        tv->selecting = 0;
        do {
            e = &tv->undo[tv->undo_at - 1];
            tiku_textview_place(tv, e->line, e->col);
            if (e->kind == 1) {
                int k = e->len;     /* inserted: take it back out */

                while (k-- > 0) {
                    tiku_textview_delete(tv);
                }
            } else {
                tiku_textview_insert_text(tv, e->text);
            }
            tv->undo_at--;
        } while (g != 0 && tv->undo_at > 0 &&
                 tv->undo[tv->undo_at - 1].group == g);
        tv->recording = 1;
    }
    /* The run below is closed, so a new edit after an undo is its own
     * entry rather than a fold into a pre-undo one. */
    if (tv->undo_at > 0) {
        tv->undo[tv->undo_at - 1].coalesce = 0;
    }
    return 1;
}

int
tiku_textview_redo(tiku_textview_t *tv)
{
    struct tiku_textview_edit *e;

    if (tv == NULL || tv->undo_at >= tv->undo_n) {
        return 0;
    }
    {
        int g = tv->undo[tv->undo_at].group;

        tv->recording = 0;
        tv->selecting = 0;
        do {
            e = &tv->undo[tv->undo_at];
            tiku_textview_place(tv, e->line, e->col);
            if (e->kind == 1) {
                tiku_textview_insert_text(tv, e->text);
            } else {
                int k = e->len;

                while (k-- > 0) {
                    tiku_textview_delete(tv);
                }
            }
            tv->undo_at++;
        } while (g != 0 && tv->undo_at < tv->undo_n &&
                 tv->undo[tv->undo_at].group == g);
        tv->recording = 1;
    }
    if (tv->undo_at > 0) {
        tv->undo[tv->undo_at - 1].coalesce = 0;
    }
    return 1;
}

int
tiku_textview_can_undo(const tiku_textview_t *tv)
{
    return (tv != NULL && tv->undo_at > 0);
}

int
tiku_textview_can_redo(const tiku_textview_t *tv)
{
    return (tv != NULL && tv->undo_at < tv->undo_n);
}

/**
 * @brief Bring the caret back inside the buffer.
 *
 * A caret column can be left past its line's end -- a selection anchor
 * outlives the edit that shortened the line under it.  An edit that then
 * reads `len - cx` gets a negative count that becomes a huge size_t and
 * runs a memcpy off the end (SIGABRT under FORTIFY).  Every edit clamps
 * first so that cannot happen.
 */
static void
caret_clamp(tiku_textview_t *tv)
{
    if (tv->cy < 0) {
        tv->cy = 0;
    }
    if (tv->cy >= tv->nline) {
        tv->cy = tv->nline - 1;
    }
    if (tv->cx < 0) {
        tv->cx = 0;
    }
    if (tv->cy >= 0 && tv->cx > tv->line[tv->cy].len) {
        tv->cx = tv->line[tv->cy].len;
    }
}

/**
 * @brief Remove the selected span, caret left where it began.
 *
 * Counted as characters and the newlines between the lines, then taken
 * from the far end by the ordinary backspace -- which already folds a
 * line into the one before it -- so this owns no second copy of that.
 * The flag is dropped FIRST, so the backspace it calls does not see a
 * selection and turn back on itself.
 */
static int
sel_delete(tiku_textview_t *tv)
{
    int y0, x0, y1, x1, count, i;

    int was_rec;
    char *span;

    if (!tiku_textview_selection(tv, &y0, &x0, &y1, &x1)) {
        return 0;
    }
    /* A selection endpoint can point past its line after the buffer
     * shrank under it; bring both ends back inside first. */
    if (y1 >= tv->nline) {
        y1 = tv->nline - 1;
        x1 = tv->line[y1].len;
    }
    if (x0 > tv->line[y0].len) {
        x0 = tv->line[y0].len;
    }
    if (x1 > tv->line[y1].len) {
        x1 = tv->line[y1].len;
    }
    if (y0 == y1) {
        count = x1 - x0;
    } else {
        count = tv->line[y0].len - x0;
        for (i = y0 + 1; i < y1; i++) {
            count += tv->line[i].len;
        }
        count += x1;
        count += (y1 - y0);     /* the line breaks the span crosses */
    }
    /* The span kept whole FIRST, so the journal holds it as one delete
     * a single undo restores -- rather than the string of backspaces
     * that actually take it apart below. */
    span = dupn("", count);
    if (span != NULL) {
        (void)tiku_textview_selected(tv, span, (size_t)count + 1u);
    }
    tv->selecting = 0;
    was_rec = tv->recording;
    tv->recording = 0;
    tv->cy = y1;
    tv->cx = x1;
    while (count-- > 0) {
        tiku_textview_backspace(tv);
    }
    tv->recording = was_rec;
    if (span != NULL) {
        record(tv, 2, y0, x0, span, (int)strlen(span));
        free(span);
    }
    return 1;
}

void
tiku_textview_insert(tiku_textview_t *tv, char ch)
{
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    tiku_textview_line_t *l;

    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    (void)sel_delete(tv);       /* typing REPLACES what is selected */
    l = &tv->line[tv->cy];
    if (l->len + 1 >= TIKU_TEXTVIEW_LINE_MAX) {
        return;                 /* a full line refuses rather than truncates */
    }
    {
        int ol = tv->cy, oc = tv->cx;

        memcpy(buf, l->text, (size_t)tv->cx);
        buf[tv->cx] = ch;
        memcpy(buf + tv->cx + 1, l->text + tv->cx,
               (size_t)(l->len - tv->cx));
        line_set(l, buf, l->len + 1);
        tv->cx++;
        record(tv, 1, ol, oc, &ch, 1);
    }
}

void
tiku_textview_newline(tiku_textview_t *tv)
{
    tiku_textview_line_t *l;
    int tail, i;

    if (tv == NULL) {
        return;
    }
    (void)sel_delete(tv);       /* a return over a selection replaces it */
    if (tv->nline + 1 >= TIKU_TEXTVIEW_LINES_MAX) {
        return;
    }
    l = &tv->line[tv->cy];
    tail = l->len - tv->cx;
    for (i = tv->nline; i > tv->cy + 1; i--) {
        tv->line[i] = tv->line[i - 1];
    }
    {
        int ol = tv->cy, oc = tv->cx;

        memset(&tv->line[tv->cy + 1], 0, sizeof tv->line[0]);
        line_set(&tv->line[tv->cy + 1], l->text + tv->cx, tail);
        line_set(l, l->text, tv->cx);
        tv->nline++;
        tv->cy++;
        tv->cx = 0;
        record(tv, 1, ol, oc, "\n", 1);
    }
}

/** @brief Fold line @p at into the one before it, caret at the seam. */
static void
join_previous(tiku_textview_t *tv, int at)
{
    tiku_textview_line_t *prev = &tv->line[at - 1];
    tiku_textview_line_t *cur = &tv->line[at];
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    int seam = prev->len;
    int i;

    if (seam + cur->len >= TIKU_TEXTVIEW_LINE_MAX) {
        return;
    }
    memcpy(buf, prev->text, (size_t)seam);
    memcpy(buf + seam, cur->text, (size_t)cur->len);
    line_set(prev, buf, seam + cur->len);
    free(cur->text);
    for (i = at; i < tv->nline - 1; i++) {
        tv->line[i] = tv->line[i + 1];
    }
    memset(&tv->line[tv->nline - 1], 0, sizeof tv->line[0]);
    tv->nline--;
    tv->cy = at - 1;
    tv->cx = seam;
}

void
tiku_textview_backspace(tiku_textview_t *tv)
{
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    tiku_textview_line_t *l;

    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    caret_clamp(tv);
    if (sel_delete(tv)) {
        return;                 /* the selection was the thing to remove */
    }
    l = &tv->line[tv->cy];
    if (tv->cx > 0) {
        char gone = l->text[tv->cx - 1];
        int gl = tv->cy, gc = tv->cx - 1;

        memcpy(buf, l->text, (size_t)(tv->cx - 1));
        memcpy(buf + tv->cx - 1, l->text + tv->cx,
               (size_t)(l->len - tv->cx));
        line_set(l, buf, l->len - 1);
        tv->cx--;
        record(tv, 2, gl, gc, &gone, 1);
    } else if (tv->cy > 0) {
        int gl = tv->cy - 1, gc = tv->line[tv->cy - 1].len;

        join_previous(tv, tv->cy);
        record(tv, 2, gl, gc, "\n", 1);
    }
}

void
tiku_textview_delete(tiku_textview_t *tv)
{
    char buf[TIKU_TEXTVIEW_LINE_MAX];
    tiku_textview_line_t *l;

    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    caret_clamp(tv);
    if (sel_delete(tv)) {
        return;
    }
    l = &tv->line[tv->cy];
    if (tv->cx < l->len) {
        char gone = l->text[tv->cx];
        int gl = tv->cy, gc = tv->cx;

        memcpy(buf, l->text, (size_t)tv->cx);
        memcpy(buf + tv->cx, l->text + tv->cx + 1,
               (size_t)(l->len - tv->cx - 1));
        line_set(l, buf, l->len - 1);
        record(tv, 2, gl, gc, &gone, 1);
    } else if (tv->cy + 1 < tv->nline) {
        /* Forwards at a line's end is the same fold as backspace at the
         * next line's head, so it is the same code -- and the caret must
         * come back to where it already was, which join leaves it. */
        int gl = tv->cy, gc = tv->cx;

        join_previous(tv, tv->cy + 1);
        record(tv, 2, gl, gc, "\n", 1);
    }
}

void
tiku_textview_move(tiku_textview_t *tv, tiku_textview_move_t how)
{
    if (tv == NULL || tv->nline <= 0) {
        return;
    }
    switch (how) {
    case TIKU_TEXTVIEW_LEFT:
        if (tv->cx > 0) {
            tv->cx--;
        } else if (tv->cy > 0) {
            tv->cy--;
            tv->cx = tv->line[tv->cy].len;
        }
        break;
    case TIKU_TEXTVIEW_RIGHT:
        if (tv->cx < tv->line[tv->cy].len) {
            tv->cx++;
        } else if (tv->cy + 1 < tv->nline) {
            tv->cy++;
            tv->cx = 0;
        }
        break;
    case TIKU_TEXTVIEW_UP:
        if (tv->cy > 0) {
            tv->cy--;
        }
        break;
    case TIKU_TEXTVIEW_DOWN:
        if (tv->cy + 1 < tv->nline) {
            tv->cy++;
        }
        break;
    case TIKU_TEXTVIEW_HOME:
        tv->cx = 0;
        return;
    case TIKU_TEXTVIEW_END:
        tv->cx = tv->line[tv->cy].len;
        return;
    default:
        return;
    }
    /* Up and down keep the column where they can: a short line clamps
     * the caret to its end rather than off it. */
    if (tv->cx > tv->line[tv->cy].len) {
        tv->cx = tv->line[tv->cy].len;
    }
}

int
tiku_textview_reveal(tiku_textview_t *tv, int rows)
{
    int was;

    if (tv == NULL || rows <= 0) {
        return 0;
    }
    was = tv->top;
    if (tv->cy < tv->top) {
        tv->top = tv->cy;
    }
    if (tv->cy >= tv->top + rows) {
        tv->top = tv->cy - rows + 1;
    }
    if (tv->top < 0) {
        tv->top = 0;
    }
    return tv->top != was;
}

int
tiku_textview_scroll_to(tiku_textview_t *tv, int top, int rows)
{
    int was, most;

    if (tv == NULL) {
        return 0;
    }
    was = tv->top;
    /* Never past the last screenful: scrolling into blank space below a
     * document is a way to lose sight of it. */
    most = (rows > 0 && tv->nline > rows) ? tv->nline - rows : 0;
    if (top < 0) { top = 0; }
    if (top > most) { top = most; }
    tv->top = top;
    return tv->top != was;
}

size_t
tiku_textview_text(const tiku_textview_t *tv, char *out, size_t max)
{
    size_t need = 0;
    int i;

    if (tv == NULL) {
        return 0;
    }
    for (i = 0; i < tv->nline; i++) {
        size_t len = (size_t)tv->line[i].len;

        if (out != NULL && need + len + 1u < max) {
            memcpy(out + need, tv->line[i].text, len);
            out[need + len] = '\n';
        }
        need += len + 1u;
    }
    if (out != NULL && max > 0) {
        out[(need < max) ? need : max - 1u] = '\0';
    }
    return need;
}
