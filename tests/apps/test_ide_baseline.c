/*
 * TikuOS applications -- test_ide_baseline.c: the BASIC/C IDE on the
 * editor baseline fixtures, before the document engine replaces its
 * text view.  Each fixture is opened through the IDE's own page loader;
 * the ones it refuses are recorded with its message, and the rest run the
 * shared edit script through the text view as the IDE drives it, checked
 * against the model.  The refusals are asserted, so a change to what the
 * IDE opens fails here; the timings are printed, never asserted.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../../apps/ide/tiku_ide_private.h"

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

/** @brief Whether the IDE opens fixture @p fx.  It refuses control bytes
 *         other than tab, CR and LF, and anything past 4,000 lines, 1,023
 *         bytes in a line or 4,096,000 bytes in all. */
static int
opens(int fx)
{
    return fx == FX_BASIC || fx == FX_C || fx == FX_NOTES ||
           fx == FX_CRLF || fx == FX_MIXED;
}

/** @brief Whether the text view holds @p m's text.  It writes a line
 *         break after every line, so a text without a final one reads
 *         back with one. */
static int
holds(const tiku_textview_t *tv, const fx_buf_t *m)
{
    fx_buf_t got;
    int same, closed = m->n && m->p[m->n - 1] == '\n';

    memset(&got, 0, sizeof got);
    got.n = tiku_textview_text(tv, NULL, 0);
    fx_room(&got, got.n);
    tiku_textview_text(tv, got.p, got.n + 1);
    same = got.n == m->n + !closed && !memcmp(got.p, m->p, m->n);
    fx_free(&got);
    return same;
}

/** @brief Apply one scripted edit as the IDE does: a typed character or
 *         line break at the caret, and a replacement as one undo group
 *         over the selected range, as Replace does. */
static void
apply(tiku_textview_t *tv, const fx_op_t *op)
{
    tiku_textview_select_none(tv);
    tiku_textview_place(tv, (int)op->y0, (int)op->x0);
    if (op->kind == FX_TYPE) {
        tiku_textview_insert(tv, op->text[0]);
        return;
    }
    if (op->kind == FX_NEWLINE) {
        tiku_textview_newline(tv);
        return;
    }
    tiku_textview_select_begin(tv);
    tiku_textview_place(tv, (int)op->y1, (int)op->x1);
    tiku_textview_group_begin(tv);
    tiku_textview_insert_text(tv, op->text);
    tiku_textview_group_end(tv);
}

/** @brief Run the script on @p tv, each edit timed, the model kept in
 *         step, the text before the replacement kept in @p before. */
static void
script(int fx, tiku_textview_t *tv, fx_buf_t *m, fx_buf_t *before,
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
        apply(tv, &op);
        t0 = fx_now() - t0;
        if (op.kind == FX_TYPE)
            fx_time(&r->type, t0);
        else if (op.kind == FX_NEWLINE)
            fx_time(&r->newline, t0);
        else
            r->replace = t0;
    }
}

/** @brief Open fixture @p fx from @p dir as an IDE page, then run the
 *         script, undo and redo the replacement, and report. */
static void
run(int fx, const char *dir)
{
    fx_buf_t src, m, before;
    fx_result_t r;
    char *copy;
    edit_t *e = calloc(1, sizeof *e);
    uint64_t t0;
    size_t heap0;

    fx_make(fx, &src);
    check(e && fx_write(dir, fx, &src, e->path, sizeof e->path), fx,
          "written");
    memset(&r, 0, sizeof r);
    memset(&before, 0, sizeof before);
    heap0 = fx_heap();
    t0 = fx_now();
    tiku_ide_edit_load(e);
    r.load = fx_now() - t0;
    r.heap = fx_heap() - heap0;
    r.loaded = !e->load_failed;
    check(r.loaded == opens(fx), fx, "opened or refused");
    if (!r.loaded) {
        snprintf(r.refusal, sizeof r.refusal, "\"%s\"", e->feedback);
        check(!strcmp(e->feedback, "File could not be loaded safely; "
                      "saving and running are blocked"), fx,
              "the refusal's words");
    } else {
        t0 = fx_now();
        copy = malloc(tiku_textview_text(&e->tv, NULL, 0) + 1);
        tiku_textview_text(&e->tv, copy, SIZE_MAX);
        r.copy = fx_now() - t0;
        free(copy);
        memset(&m, 0, sizeof m);
        fx_put(&m, src.p, src.n);
        fx_make_block(src.n);
        script(fx, &e->tv, &m, &before, &r);
        check(holds(&e->tv, &m), fx, "the script's text");
        t0 = fx_now();
        check(tiku_textview_undo(&e->tv), fx, "undo");
        r.undo = fx_now() - t0;
        check(holds(&e->tv, &before), fx, "undo restores the text");
        t0 = fx_now();
        check(tiku_textview_redo(&e->tv), fx, "redo");
        r.redo = fx_now() - t0;
        check(holds(&e->tv, &m), fx, "redo repeats the replacement");
        fx_free(&m);
    }
    fx_report_row(fx, &src, &r);
    tiku_textview_free(&e->tv);
    free(e);
    fx_free(&before);
    fx_free(&src);
}

int
main(void)
{
    char dir[512];
    int fx, kept = fx_dir(dir, sizeof dir);

    fx_report_machine("IDE");
    for (fx = 0; fx < FX_COUNT; fx++)
        if (fx_enabled(fx))
            run(fx, dir);
    fx_free(&fx_block);
    fx_dir_done(dir, kept);
    printf("%s\n", fails ? "FAIL: IDE baseline" :
           "PASS: IDE baseline -- the IDE opens and refuses the fixtures as "
           "recorded, and its text view matches the model");
    return fails != 0;
}
