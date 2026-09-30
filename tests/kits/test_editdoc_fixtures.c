/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc_fixtures.c - the document engine on the baseline
 * fixtures: each one read from its file and loaded, the edit script run
 * and checked against the model after every step's end, the large
 * replacement undone and redone, and the costs printed beside the
 * Editor's and the IDE's (test_editor_baseline, test_ide_baseline).
 * Timings are printed, never asserted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_editdoc_private.h"

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

/* The engine's allocations, counted: each block carries its size. */
static size_t live_bytes, peak_bytes;
static unsigned long allocations;

/** @brief What the engine held for one fixture, for the second table. */
typedef struct {
    size_t held;                /* bytes held after the script          */
    size_t peak;                /* the most held at once                */
    double per_edit;            /* allocations per scripted edit        */
} held_t;

static held_t held[FX_COUNT];

/** @brief malloc, counted. */
static void *
c_alloc(size_t n)
{
    size_t *p = n <= SIZE_MAX - 16 ? malloc(n + 16) : NULL;

    if (!p)
        return NULL;
    *p = n;
    allocations++;
    live_bytes += n;
    if (live_bytes > peak_bytes)
        peak_bytes = live_bytes;
    return (char *)p + 16;
}

/** @brief realloc, counted. */
static void *
c_realloc(void *old, size_t n)
{
    size_t was;
    char *p;

    if (!old)
        return c_alloc(n);
    was = *(size_t *)((char *)old - 16);
    p = n <= SIZE_MAX - 16 ? realloc((char *)old - 16, n + 16) : NULL;
    if (!p)
        return NULL;
    *(size_t *)p = n;
    allocations++;
    live_bytes = live_bytes - was + n;
    if (live_bytes > peak_bytes)
        peak_bytes = live_bytes;
    return p + 16;
}

/** @brief free, counted. */
static void
c_free(void *p)
{
    if (p) {
        live_bytes -= *(size_t *)((char *)p - 16);
        free((char *)p - 16);
    }
}

/** @brief Read @p path whole into @p b, as an application would. */
static int
read_file(const char *path, fx_buf_t *b)
{
    struct stat info;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    size_t used = 0;

    memset(b, 0, sizeof *b);
    if (fd < 0 || fstat(fd, &info)) {
        if (fd >= 0)
            close(fd);
        return 0;
    }
    fx_room(b, (size_t)info.st_size);
    while (used < (size_t)info.st_size) {
        ssize_t got = read(fd, b->p + used, (size_t)info.st_size - used);

        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        used += (size_t)got;
    }
    close(fd);
    b->n = used;
    b->p[used] = 0;
    return used == (size_t)info.st_size;
}

/** @brief Whether @p doc holds exactly @p m's bytes. */
static int
holds(tiku_editdoc_t *doc, const fx_buf_t *m)
{
    tiku_edit_snapshot_t snap;
    int same;

    if (tiku_editdoc_snapshot(doc, &snap) != TIKU_EDIT_OK)
        return 0;
    same = snap.length == m->n && !memcmp(snap.text, m->p, m->n);
    tiku_edit_snapshot_free(&snap);
    return same;
}

/** @brief Apply one scripted edit as one transaction; typed characters
 *         and line breaks carry the typing flag. */
static tiku_edit_status_t
apply(tiku_editdoc_t *doc, const fx_op_t *op)
{
    tiku_edit_replace_t r;
    tiku_edit_transaction_t t;

    r.range.start = tiku_editdoc_line_start(doc, op->y0) + (int64_t)op->x0;
    r.range.end = tiku_editdoc_line_start(doc, op->y1) + (int64_t)op->x1;
    r.text = op->text;
    r.length = op->len;
    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    t.flags = op->kind == FX_REPLACE ? 0 : TIKU_EDIT_TYPING;
    return tiku_editdoc_apply(doc, &t);
}

/** @brief Run the script on @p doc: each edit timed, the model kept in
 *         step, the text before the replacement kept in @p before. */
static void
script(int fx, tiku_editdoc_t *doc, fx_buf_t *m, fx_buf_t *before,
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
        check(apply(doc, &op) == TIKU_EDIT_OK, fx, "a scripted edit");
        t0 = fx_now() - t0;
        if (op.kind == FX_TYPE)
            fx_time(&r->type, t0);
        else if (op.kind == FX_NEWLINE)
            fx_time(&r->newline, t0);
        else
            r->replace = t0;
    }
}

/** @brief Load fixture @p fx from @p dir, run the script, undo and redo
 *         the replacement, and report. */
static void
run(int fx, const char *dir)
{
    fx_buf_t src, bytes, m, before;
    fx_result_t r;
    char path[512];
    tiku_editdoc_t *doc = NULL;
    tiku_edit_snapshot_t snap;
    uint64_t t0;
    size_t heap0, live0;
    unsigned long typed;

    fx_make(fx, &src);
    check(fx_write(dir, fx, &src, path, sizeof path), fx, "written");
    memset(&r, 0, sizeof r);
    memset(&before, 0, sizeof before);
    heap0 = fx_heap();
    live0 = live_bytes;
    t0 = fx_now();
    r.loaded = read_file(path, &bytes) &&
               tiku_editdoc_create(NULL, &doc) == TIKU_EDIT_OK &&
               tiku_editdoc_load(doc, bytes.p, bytes.n) == TIKU_EDIT_OK;
    fx_free(&bytes);
    r.load = fx_now() - t0;
    r.heap = fx_heap() - heap0;
    check(r.loaded, fx, "loads");
    if (!r.loaded) {
        snprintf(r.refusal, sizeof r.refusal, "the engine refused it");
        fx_report_row(fx, &src, &r);
        tiku_editdoc_release(doc);
        fx_free(&src);
        return;
    }
    check(tiku_editdoc_lines(doc) == fx_lines(&src), fx, "line count");
    t0 = fx_now();
    check(tiku_editdoc_snapshot(doc, &snap) == TIKU_EDIT_OK, fx, "copy");
    r.copy = fx_now() - t0;
    tiku_edit_snapshot_free(&snap);
    memset(&m, 0, sizeof m);
    fx_put(&m, src.p, src.n);
    fx_make_block(src.n);
    typed = allocations;
    script(fx, doc, &m, &before, &r);
    typed = allocations - typed;
    check(holds(doc, &m) && tiku_editdoc_lines(doc) == fx_lines(&m), fx,
          "the script's text and lines");
    t0 = fx_now();
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK, fx, "undo");
    r.undo = fx_now() - t0;
    check(holds(doc, &before), fx, "undo restores the text");
    t0 = fx_now();
    check(tiku_editdoc_redo(doc) == TIKU_EDIT_OK, fx, "redo");
    r.redo = fx_now() - t0;
    check(holds(doc, &m), fx, "redo repeats the replacement");
    fx_report_row(fx, &src, &r);
    held[fx].held = live_bytes - live0;
    held[fx].peak = peak_bytes - live0;
    held[fx].per_edit = (double)typed / (FX_TYPE_OPS + FX_NEWLINE_OPS + 1);
    tiku_editdoc_release(doc);
    check(live_bytes == live0, fx, "everything freed");
    fx_free(&before);
    fx_free(&m);
    fx_free(&src);
}

int
main(void)
{
    static const tiku_ed_allocator_t counted = { c_alloc, c_realloc, c_free };
    char dir[512];
    int fx, kept = fx_dir(dir, sizeof dir);

    tiku_ed_test_allocator(&counted);
    fx_report_machine("Document engine");
    for (fx = 0; fx < FX_COUNT; fx++) {
        if (!fx_enabled(fx))
            continue;
        peak_bytes = live_bytes;
        run(fx, dir);
    }
    printf("\n| Fixture | Engine MiB after script | Engine peak MiB | "
           "Allocations per edit |\n| --- | ---: | ---: | ---: |\n");
    for (fx = 0; fx < FX_COUNT; fx++)
        if (fx_enabled(fx))
            printf("| %s | %.1f | %.1f | %.2f |\n", fx_info[fx].name,
                   held[fx].held / 1048576.0, held[fx].peak / 1048576.0,
                   held[fx].per_edit);
    tiku_ed_test_allocator(NULL);
    fx_free(&fx_block);
    fx_dir_done(dir, kept);
    printf("%s\n", fails ? "FAIL: editor fixtures" :
           "PASS: editor fixtures -- every fixture loads, the scripts "
           "match the model, the replacement undoes and redoes");
    return fails != 0;
}
