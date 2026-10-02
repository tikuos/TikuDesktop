/*
 * TikuOS applications -- test_ide_baseline.c: the BASIC/C IDE on the
 * editor baseline fixtures.  Each fixture is opened through the IDE's own
 * page loader, which opens every one of them, and runs the shared edit
 * script through the page's document and view, checked byte for byte
 * against the model.  The timings are printed, never asserted.
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

/** @brief Whether the page holds exactly @p m's bytes. */
static int
holds(const edit_t *e, const fx_buf_t *m)
{
    tiku_edit_range_t all = { 0, tiku_editdoc_length(e->doc) };
    fx_buf_t got;
    size_t n = 0;
    int same;

    memset(&got, 0, sizeof got);
    fx_room(&got, (size_t)all.end);
    same = tiku_editdoc_read(e->doc, all, got.p, (size_t)all.end + 1, &n) ==
           TIKU_EDIT_OK && n == m->n && !memcmp(got.p, m->p, m->n);
    fx_free(&got);
    return same;
}

/** @brief Apply one scripted edit as the page does: the range selected
 *         in its view, the text put in its place. */
static int
apply(edit_t *e, const fx_op_t *op)
{
    tiku_edit_pos_t a = tiku_editdoc_line_start(e->doc, op->y0) + op->x0;
    tiku_edit_pos_t b = tiku_editdoc_line_start(e->doc, op->y1) + op->x1;

    return tiku_codeview_select(e->view, a, b) == TIKU_EDIT_OK &&
           tiku_codeview_insert(e->view, op->text, op->len, 0) ==
           TIKU_EDIT_OK;
}

/** @brief Run the script on the page, each edit timed, the model kept in
 *         step, the text before the replacement kept in @p before. */
static void
script(int fx, edit_t *e, fx_buf_t *m, fx_buf_t *before, fx_result_t *r)
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
        check(apply(e, &op), fx, "a scripted edit");
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
    tiku_edit_range_t all;
    uint64_t t0;
    size_t heap0, n;

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
    r.loaded = !e->load_failed && e->view != NULL;
    check(r.loaded, fx, "opened");
    if (!r.loaded) {
        snprintf(r.refusal, sizeof r.refusal, "\"%s\"", e->feedback);
    } else {
        all = (tiku_edit_range_t){ 0, tiku_editdoc_length(e->doc) };
        t0 = fx_now();
        copy = malloc((size_t)all.end + 1);
        check(copy && tiku_editdoc_read(e->doc, all, copy,
                                        (size_t)all.end + 1, &n) ==
                      TIKU_EDIT_OK, fx, "copied");
        r.copy = fx_now() - t0;
        free(copy);
        memset(&m, 0, sizeof m);
        fx_put(&m, src.p, src.n);
        check(holds(e, &m), fx, "every byte loaded as it is");
        fx_make_block(src.n);
        script(fx, e, &m, &before, &r);
        check(holds(e, &m), fx, "the script's text");
        t0 = fx_now();
        check(tiku_codeview_undo(e->view) == TIKU_EDIT_OK, fx, "undo");
        r.undo = fx_now() - t0;
        check(holds(e, &before), fx, "undo restores the text");
        t0 = fx_now();
        check(tiku_codeview_redo(e->view) == TIKU_EDIT_OK, fx, "redo");
        r.redo = fx_now() - t0;
        check(holds(e, &m), fx, "redo repeats the replacement");
        fx_free(&m);
    }
    fx_report_row(fx, &src, &r);
    tiku_ide_text_free(e);
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
           "PASS: IDE baseline -- the IDE opens every fixture, and its "
           "page matches the model");
    return fails != 0;
}
