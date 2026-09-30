/*
 * TikuOS applications -- test_editor_baseline.c: Editor on the editor
 * baseline fixtures, before the document engine replaces its buffer.
 * Each fixture is opened through Editor's own file loader; the ones it
 * refuses are recorded with its message, and the rest run the shared edit
 * script through its buffer, checked against the model.  The refusals are
 * asserted, so a change to what Editor opens fails here; the timings are
 * printed, never asserted.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../../apps/editor/tiku_editor_private.h"

#include "../fixtures/editor_fixtures.inc"

static int fails;

/** @brief Count a failure and say which fixture and check it was. */
static void
check(int cond, int fx, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-16s %s\n", fx_info[fx].name, what);
        fails++;
    }
}

/** @brief What Editor says about the fixtures it refuses; NULL where it
 *         opens them. */
static const char *
refusal_of(int fx)
{
    if (fx == FX_MIXED)
        return "Mixed LF/CRLF endings; normalize them before editing here.";
    if (fx == FX_CONTROLS)
        return "Invalid UTF-8 or not enough memory; the current document "
               "was kept.";
    return NULL;
}

/** @brief @p m as Editor holds it: each CRLF read as LF. */
static void
as_lf(const fx_buf_t *m, fx_buf_t *out)
{
    size_t i;

    memset(out, 0, sizeof *out);
    fx_room(out, m->n);
    for (i = 0; i < m->n; i++)
        if (!(m->p[i] == '\r' && i + 1 < m->n && m->p[i + 1] == '\n'))
            out->p[out->n++] = m->p[i];
    out->p[out->n] = 0;
}

/** @brief Whether Editor's buffer holds @p m's text. */
static int
holds(const edit_buffer_t *b, const fx_buf_t *m)
{
    fx_buf_t want, got;
    int same;

    as_lf(m, &want);
    memset(&got, 0, sizeof got);
    got.n = tiku_editor_buffer_text(b, NULL, 0);
    fx_room(&got, got.n);
    tiku_editor_buffer_text(b, got.p, got.n + 1);
    same = got.n == want.n && !memcmp(got.p, want.p, want.n);
    fx_free(&want);
    fx_free(&got);
    return same;
}

/** @brief Apply one scripted edit as Editor does: the caret placed, the
 *         range selected when there is one, the text put in its place. */
static int
apply(edit_buffer_t *b, const fx_op_t *op)
{
    tiku_editor_buffer_select_none(b);
    tiku_editor_buffer_place(b, (int)op->y0, (int)op->x0);
    if (op->y1 != op->y0 || op->x1 != op->x0) {
        tiku_editor_buffer_select_begin(b);
        tiku_editor_buffer_place(b, (int)op->y1, (int)op->x1);
    }
    return tiku_editor_buffer_replace(b, op->text);
}

/** @brief Run the script on @p b, each edit timed, the model kept in
 *         step, the text before the replacement kept in @p before. */
static void
script(int fx, edit_buffer_t *b, fx_buf_t *m, fx_buf_t *before,
       fx_result_t *r)
{
    fx_op_t op;
    int i;

    for (i = 0;; i++) {
        uint64_t t0;

        if (i == FX_TYPE_OPS + FX_NEWLINE_OPS)
            fx_put(before, m->p, m->n);
        if (!fx_step(fx, m, i, &op))
            break;
        t0 = fx_now();
        check(apply(b, &op), fx, "a scripted edit");
        t0 = fx_now() - t0;
        if (op.kind == FX_TYPE)
            fx_time(&r->type, t0);
        else if (op.kind == FX_NEWLINE)
            fx_time(&r->newline, t0);
        else
            r->replace = t0;
    }
}

/** @brief Open fixture @p fx from @p dir in Editor, then run the script,
 *         undo and redo the replacement, and report. */
static void
run(int fx, const char *dir)
{
    fx_buf_t src, m, before;
    fx_result_t r;
    char path[512], *copy;
    edit_state_t *st = calloc(1, sizeof *st);
    uint64_t t0;
    size_t heap0;

    fx_make(fx, &src);
    check(st && fx_write(dir, fx, &src, path, sizeof path), fx, "written");
    memset(&r, 0, sizeof r);
    memset(&before, 0, sizeof before);
    heap0 = fx_heap();
    t0 = fx_now();
    r.loaded = tiku_editor_read_document(st, path);
    r.load = fx_now() - t0;
    r.heap = fx_heap() - heap0;
    check(!r.loaded == (refusal_of(fx) != NULL), fx, "opened or refused");
    if (!r.loaded) {
        snprintf(r.refusal, sizeof r.refusal, "\"%s\"", st->note);
        check(!strcmp(st->note, refusal_of(fx)), fx, "the refusal's words");
    } else {
        t0 = fx_now();
        copy = malloc(tiku_editor_buffer_text(&st->tv, NULL, 0) + 1);
        tiku_editor_buffer_text(&st->tv, copy, SIZE_MAX);
        r.copy = fx_now() - t0;
        free(copy);
        memset(&m, 0, sizeof m);
        fx_put(&m, src.p, src.n);
        fx_make_block(src.n);
        script(fx, &st->tv, &m, &before, &r);
        check(holds(&st->tv, &m), fx, "the script's text");
        t0 = fx_now();
        check(tiku_editor_buffer_undo(&st->tv), fx, "undo");
        r.undo = fx_now() - t0;
        check(holds(&st->tv, &before), fx, "undo restores the text");
        t0 = fx_now();
        check(tiku_editor_buffer_redo(&st->tv), fx, "redo");
        r.redo = fx_now() - t0;
        check(holds(&st->tv, &m), fx, "redo repeats the replacement");
        fx_free(&m);
    }
    fx_report_row(fx, &src, &r);
    tiku_editor_buffer_free(&st->tv);
    free(st);
    fx_free(&before);
    fx_free(&src);
}

int
main(void)
{
    char dir[512];
    int fx, kept = fx_dir(dir, sizeof dir);

    fx_report_machine("Editor");
    for (fx = 0; fx < FX_COUNT; fx++)
        if (fx_enabled(fx))
            run(fx, dir);
    fx_free(&fx_block);
    fx_dir_done(dir, kept);
    printf("%s\n", fails ? "FAIL: editor baseline" :
           "PASS: editor baseline -- Editor opens and refuses the fixtures "
           "as recorded, and its buffer matches the model");
    return fails != 0;
}
