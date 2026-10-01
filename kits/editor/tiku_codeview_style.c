/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_style.c - colouring through tiku_syntax.  A line of C is
 * read from the state the lines above it leave, so each line's starting
 * state is kept; a change makes the states below it unknown, and reading
 * resumes from the first unknown line and stops once a line past the
 * change starts in the state it was kept in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_editdoc_private.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

void
tiku_codeview_set_language(tiku_codeview_t *view, tiku_syntax_lang_t lang)
{
    if (view == NULL)
        return;
    if (lang != TIKU_SYNTAX_BASIC && lang != TIKU_SYNTAX_C)
        lang = TIKU_SYNTAX_NONE;
    if (view->lang != lang) {
        view->lang = lang;
        tiku_cv_style_reset(view);
    }
}

tiku_syntax_lang_t
tiku_codeview_language(const tiku_codeview_t *view)
{
    return view != NULL ? view->lang : TIKU_SYNTAX_NONE;
}

void
tiku_cv_style_reset(tiku_codeview_t *v)
{
    v->sty_valid = 0;
    v->sty_hint = 0;
    v->sty_dirty = -1;
    if (v->states != NULL && v->nstates > 0)
        v->states[0] = TIKU_SYNTAX_OPEN;
}

/** @brief Drop the state table; colouring starts again from line 0. */
static void
states_free(tiku_codeview_t *v)
{
    tiku_ed_free(v->states);
    v->states = NULL;
    v->nstates = 0;
    v->states_cap = 0;
    tiku_cv_style_reset(v);
}

void
tiku_cv_style_free(tiku_codeview_t *v)
{
    states_free(v);
    tiku_ed_free(v->line_buf);
    v->line_buf = NULL;
    v->line_cap = 0;
    tiku_ed_free(v->runs);
    v->runs = NULL;
    v->nruns = 0;
    v->runs_cap = 0;
}

/** @brief Room for @p n states; 0 when there is none. */
static int
states_room(tiku_codeview_t *v, int64_t n)
{
    int64_t cap;
    unsigned char *grown;

    if (n <= v->states_cap)
        return 1;
    cap = n + n / 4 + 16;
    if (cap < 0 || (uint64_t)cap > SIZE_MAX)
        return 0;
    grown = tiku_ed_realloc(v->states, (size_t)cap);
    if (grown == NULL)
        return 0;
    v->states = grown;
    v->states_cap = cap;
    return 1;
}

/** @brief A table that follows a document of @p lines, started afresh
 *         when it did not; 0 when there is no room for one. */
static int
states_follow(tiku_codeview_t *v, int64_t lines)
{
    if (v->states != NULL && v->nstates == lines)
        return 1;
    states_free(v);
    if (!states_room(v, lines))
        return 0;
    v->nstates = lines;
    tiku_cv_style_reset(v);
    return 1;
}

void
tiku_cv_style_change(tiku_codeview_t *v, int64_t first, int64_t last_old,
                     int64_t last_new, int64_t lines)
{
    int64_t delta = last_new - last_old, tail, dirty;

    if (v->states == NULL)
        return;
    if (first < 0 || last_old < first || last_new < first ||
        v->nstates != lines - delta || last_old >= v->nstates ||
        !states_room(v, lines)) {
        states_free(v);
        return;
    }
    tail = v->nstates - (last_old + 1);
    if (tail > 0)
        memmove(&v->states[last_new + 1], &v->states[last_old + 1],
                (size_t)tail);
    v->nstates = lines;
    if (v->sty_hint > last_old)
        v->sty_hint += delta;
    else if (v->sty_hint > first)
        v->sty_hint = first;
    dirty = v->sty_dirty;
    if (dirty > last_old)
        dirty += delta;
    else if (dirty >= first)
        dirty = last_new;
    v->sty_dirty = dirty > last_new ? dirty : last_new;
    if (v->sty_valid > first)
        v->sty_valid = first;
    if (v->sty_hint < v->sty_valid)
        v->sty_hint = v->sty_valid;
}

/** @brief @p line copied out with a NUL, or NULL when it cannot be: too
 *         long for the lexers' counts, or no room for the copy. */
static const char *
line_text(tiku_codeview_t *v, int64_t line)
{
    tiku_edit_range_t r;
    size_t n, got = 0;

    r.start = tiku_editdoc_line_start(v->doc, line);
    r.end = tiku_editdoc_line_end(v->doc, line);
    if (r.start < 0 || r.end < r.start || r.end - r.start >= INT_MAX)
        return NULL;
    n = (size_t)(r.end - r.start);
    if (n + 1 > v->line_cap) {
        size_t cap = n + 1 + n / 4 + 64;
        char *grown = tiku_ed_realloc(v->line_buf, cap);

        if (grown == NULL)
            return NULL;
        v->line_buf = grown;
        v->line_cap = cap;
    }
    if (tiku_editdoc_read(v->doc, r, v->line_buf, v->line_cap, &got) !=
        TIKU_EDIT_OK)
        return NULL;
    return v->line_buf;
}

/** @brief The state @p line starts in, reading forward from the last line
 *         known; -1 when a line on the way cannot be read. */
static int
state_at(tiku_codeview_t *v, int64_t line)
{
    while (v->sty_valid < line) {
        int64_t j = v->sty_valid;
        const char *text = line_text(v, j);
        tiku_syntax_state_t left = TIKU_SYNTAX_OPEN;

        if (text == NULL)
            return -1;
        tiku_syntax_runs(v->lang, text, v->states[j], &left, NULL, NULL);
        if (j + 1 > v->sty_dirty && j + 1 <= v->sty_hint &&
            v->states[j + 1] == (unsigned char)left) {
            v->sty_valid = v->sty_hint;
            v->sty_dirty = -1;
        } else {
            v->states[j + 1] = (unsigned char)left;
            v->sty_valid = j + 1;
            if (v->sty_hint < v->sty_valid)
                v->sty_hint = v->sty_valid;
        }
    }
    return v->states[line];
}

/** @brief Keep one run of the line being coloured. */
static void
add_run(void *ctx, tiku_ink_t ink, int len)
{
    tiku_codeview_t *v = ctx;

    if (v->runs_failed)
        return;
    if (v->nruns == v->runs_cap) {
        size_t cap = v->runs_cap ? v->runs_cap * 2 : 32;
        tiku_span_t *grown;

        if (cap > SIZE_MAX / sizeof *grown) {
            v->runs_failed = 1;
            return;
        }
        grown = tiku_ed_realloc(v->runs, cap * sizeof *grown);
        if (grown == NULL) {
            v->runs_failed = 1;
            return;
        }
        v->runs = grown;
        v->runs_cap = cap;
    }
    v->runs[v->nruns].ink = ink;
    v->runs[v->nruns].len = len;
    v->nruns++;
}

int
tiku_cv_style_line(tiku_codeview_t *v, int64_t line)
{
    tiku_syntax_state_t in = TIKU_SYNTAX_OPEN;
    const char *text;

    v->nruns = 0;
    if (v->lang == TIKU_SYNTAX_NONE)
        return 0;
    if (v->lang == TIKU_SYNTAX_C) {
        int state;

        if (!states_follow(v, tiku_editdoc_lines(v->doc)))
            return 0;
        state = state_at(v, line);
        if (state < 0)
            return 0;
        in = (tiku_syntax_state_t)state;
    }
    text = line_text(v, line);
    if (text == NULL)
        return 0;
    v->runs_failed = 0;
    tiku_syntax_runs(v->lang, text, in, NULL, add_run, v);
    if (v->runs_failed) {
        v->nruns = 0;
        return 0;
    }
    return 1;
}
