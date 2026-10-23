/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richsession_faults.c - session construction and publication failures.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richsession faults:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Selected failure and exact accounting for successful allocations. */
typedef struct {
    size_t calls, fail_at, live, bytes;
} faults_t;

/** @brief Maintain malloc alignment while tracking allocation sizes. */
typedef union {
    max_align_t align;
    size_t bytes;
} prefix_t;

static unsigned injected;

/** @brief Fail one selected allocation and account for the others. */
static void *
allocate(void *ctx, size_t bytes)
{
    faults_t *f = ctx;
    prefix_t *p;

    if (++f->calls == f->fail_at) {
        injected++;
        return NULL;
    }
    CHECK(bytes <= SIZE_MAX - sizeof *p);
    p = malloc(sizeof *p + bytes);
    CHECK(p);
    p->bytes = bytes;
    f->bytes += bytes;
    f->live++;
    return p + 1;
}

/** @brief Release one aligned allocation and subtract its full size. */
static void
release(void *ctx, void *pointer)
{
    faults_t *f = ctx;
    prefix_t *p = (prefix_t *)pointer - 1;

    CHECK(f->live && f->bytes >= p->bytes);
    f->live--;
    f->bytes -= p->bytes;
    free(p);
}

/** @brief Create a document under the deterministic failure allocator. */
static tiku_richdoc_t *
create(faults_t *f)
{
    const tiku_rich_limits_t limits = {.allocator = {f, allocate, release}};
    tiku_richdoc_t *doc;

    CHECK(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    return doc;
}

/** @brief Count committed changes without allocating in notification. */
static void
changed(void *ctx, tiku_richdoc_t *doc, const tiku_rich_change_t *change)
{
    unsigned *calls = ctx;
    tiku_richsession_t *session;
    tiku_richsnapshot_t snapshot = {.root = doc->root};

    CHECK(change->generation == doc->generation);
    for (session = doc->sessions; session; session = session->next) {
        CHECK(tiku_richsnapshot_position(&snapshot,
            session->anchor->position) == TIKU_RICH_OK);
        CHECK(tiku_richsnapshot_position(&snapshot,
            session->caret->position) == TIKU_RICH_OK);
    }
    (*calls)++;
}

/** @brief Fail each allocation while registering sessions and observers. */
static void
construction(void)
{
    faults_t f;
    tiku_richdoc_t *doc;
    tiku_richsession_t *session;
    tiku_richwatch_t *watch;
    tiku_rich_status_t status;
    size_t nth, live, bytes;
    unsigned calls = 0;

    for (nth = 1; nth < 16; nth++) {
        f = (faults_t){0};
        doc = create(&f);
        live = f.live;
        bytes = f.bytes;
        f.fail_at = f.calls + nth;
        status = tiku_richsession_create(doc, &session);
        f.fail_at = 0;
        if (status != TIKU_RICH_OK) {
            CHECK(status == TIKU_RICH_NO_MEMORY && !session);
            CHECK(f.live == live && f.bytes == bytes);
            CHECK(!doc->sessions && !doc->anchors && doc->refs == 1);
            CHECK(!doc->anchor_count && !tiku_richdoc_dirty(doc));
        } else
            CHECK(tiku_richsession_destroy(session) == TIKU_RICH_OK);
        f.fail_at = f.calls + 1;
        CHECK(tiku_richwatch_create(doc, changed, &calls, &watch) ==
              TIKU_RICH_NO_MEMORY && !watch);
        CHECK(f.live == live && f.bytes == bytes);
        CHECK(!doc->watches && !doc->watch_count && doc->refs == 1);
        f.fail_at = 0;
        tiku_richdoc_release(doc);
        CHECK(!f.live && !f.bytes && !calls);
        if (status == TIKU_RICH_OK)
            break;
    }
    CHECK(nth == 4);
}

/** @brief Build live content and retain a redo group before the tested edit. */
static void
prepare(tiku_richdoc_t *doc, tiku_richsession_t **a, tiku_richsession_t **b)
{
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_REPLACE_TEXT,
        .value.text = {"alpha\nbeta", 10}};

    CHECK(tiku_richsession_create(doc, a) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, b) == TIKU_RICH_OK);
    CHECK(tiku_richsession_execute(*a, 0, &cmd) == TIKU_RICH_OK);
    cmd.value.text.data = "!";
    cmd.value.text.length = 1;
    CHECK(tiku_richsession_execute(*a, 0, &cmd) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(doc->undo && doc->redo);
}

/** @brief Configure pending typing, a reversed range or styled paste. */
static tiku_richcmd_t
choose(tiku_richsession_t *session, const tiku_richfragment_t *fragment,
       int mode)
{
    tiku_richdoc_t *doc = session->doc;
    tiku_rich_pos_t first = {doc->root->body,
        doc->root->paragraphs[0]->value.id, 2, TIKU_RICH_AFTER};
    tiku_rich_pos_t last = {doc->root->body,
        doc->root->paragraphs[1]->value.id, 3, TIKU_RICH_BEFORE};
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_BOLD};

    if (mode == 0) {
        CHECK(tiku_richsession_select(session, 0, first, first) == TIKU_RICH_OK);
        CHECK(tiku_richsession_execute(session, 0, &cmd) == TIKU_RICH_OK);
        cmd.kind = TIKU_RICHCMD_REPLACE_TEXT;
        cmd.value.text.data = "W\r\nXYZ";
        cmd.value.text.length = 6;
    } else {
        CHECK(tiku_richsession_select(session, 0, last, first) == TIKU_RICH_OK);
        if (mode == 2) {
            cmd.kind = TIKU_RICHCMD_PASTE_FRAGMENT;
            cmd.value.fragment = fragment;
        }
    }
    return cmd;
}

/** @brief Capture a fragment under the same allocator before injecting. */
static tiku_richfragment_t *
capture(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richfragment_t *fragment;
    tiku_rich_pos_t first = {doc->root->body,
        doc->root->paragraphs[0]->value.id, 0, TIKU_RICH_AFTER};
    tiku_rich_pos_t last = {doc->root->body,
        doc->root->paragraphs[1]->value.id, 4, TIKU_RICH_AFTER};

    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richfragment_create(snapshot, first, last, &fragment) ==
          TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    return fragment;
}

/** @brief State retained for exact comparison after a refused command. */
typedef struct {
    tiku_richdoc_t doc;
    tiku_rd_history_t undo;
    tiku_richsession_t a, b;
    tiku_richanchor_t anchors[4];
    size_t live, bytes, used;
} saved_t;

/** @brief Capture live document, session, anchor and memory-accounting data. */
static saved_t
save(tiku_richdoc_t *doc, tiku_richsession_t *a, tiku_richsession_t *b,
     const faults_t *f)
{
    saved_t saved = {.doc = *doc, .undo = *doc->undo, .a = *a, .b = *b,
        .anchors = {*a->anchor, *a->caret, *b->anchor, *b->caret},
        .live = f->live, .bytes = f->bytes, .used = tiku_richdoc_memory(doc)};

    return saved;
}

/** @brief Assert failed commands preserve all observable and history state. */
static void
unchanged(const saved_t *saved, tiku_richdoc_t *doc,
          tiku_richsession_t *a, tiku_richsession_t *b, const faults_t *f)
{
    CHECK(!memcmp(&saved->doc, doc, sizeof *doc));
    CHECK(!memcmp(&saved->undo, doc->undo, sizeof *doc->undo));
    CHECK(!memcmp(&saved->a, a, sizeof *a));
    CHECK(!memcmp(&saved->b, b, sizeof *b));
    CHECK(!memcmp(&saved->anchors[0], a->anchor, sizeof *a->anchor));
    CHECK(!memcmp(&saved->anchors[1], a->caret, sizeof *a->caret));
    CHECK(!memcmp(&saved->anchors[2], b->anchor, sizeof *b->anchor));
    CHECK(!memcmp(&saved->anchors[3], b->caret, sizeof *b->caret));
    CHECK(f->live == saved->live && f->bytes == saved->bytes);
    CHECK(tiku_richdoc_memory(doc) == saved->used);
}

/** @brief Fail every allocation in session typing, formatting and paste. */
static void
commands(int mode)
{
    faults_t f;
    tiku_richdoc_t *doc;
    tiku_richsession_t *a, *b;
    tiku_richwatch_t *watch;
    tiku_richfragment_t *fragment;
    tiku_richcmd_t cmd;
    tiku_rich_status_t status;
    saved_t saved;
    size_t nth;
    unsigned calls;

    for (nth = 1; nth < 128; nth++) {
        f = (faults_t){0};
        calls = 0;
        doc = create(&f);
        prepare(doc, &a, &b);
        fragment = capture(doc);
        cmd = choose(a, fragment, mode == 3 ? 0 : mode);
        if (mode == 3)
            CHECK(tiku_richsession_type(a, 0, "m", 1) == TIKU_RICH_OK);
        CHECK(tiku_richwatch_create(doc, changed, &calls, &watch) ==
              TIKU_RICH_OK);
        saved = save(doc, a, b, &f);
        f.fail_at = f.calls + nth;
        status = mode == 3 ? tiku_richsession_type(a, 0, "XYZ", 3) :
                            tiku_richsession_execute(a, 0, &cmd);
        f.fail_at = 0;
        if (status != TIKU_RICH_OK) {
            CHECK(status == TIKU_RICH_NO_MEMORY && !calls);
            unchanged(&saved, doc, a, b, &f);
            if (mode != 3)
                CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
            else {
                CHECK(doc->undo->groupable);
                CHECK(tiku_richsession_type(a, 0, "XYZ", 3) == TIKU_RICH_OK);
                CHECK(doc->undo->next == saved.undo.next);
            }
        } else {
            CHECK(calls == 1 && doc->generation == saved.doc.generation + 1);
            CHECK(!doc->redo && !doc->initiator && !doc->notifying);
            f.fail_at = f.calls + 1;
            CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
            CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
            CHECK(f.fail_at == f.calls + 1 && calls == 3);
            f.fail_at = 0;
        }
        CHECK(tiku_richwatch_destroy(watch) == TIKU_RICH_OK);
        CHECK(tiku_richsession_destroy(a) == TIKU_RICH_OK);
        CHECK(tiku_richsession_destroy(b) == TIKU_RICH_OK);
        tiku_richfragment_release(fragment);
        tiku_richdoc_release(doc);
        CHECK(!f.live && !f.bytes);
        if (status == TIKU_RICH_OK)
            break;
    }
    CHECK(nth > 3 && nth < 128);
}

/** @brief Pending formats and queries allocate nothing; history is budgeted. */
static void
budgets(void)
{
    faults_t f = {0};
    tiku_richdoc_t *doc = create(&f);
    tiku_richsession_t *a, *b;
    tiku_richwatch_t *watch;
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_BOLD};
    tiku_richcmd_state_t state;
    tiku_rich_selection_t format;
    saved_t saved;
    unsigned calls = 0;

    prepare(doc, &a, &b);
    CHECK(tiku_richwatch_create(doc, changed, &calls, &watch) == TIKU_RICH_OK);
    f.fail_at = f.calls + 1;
    CHECK(tiku_richsession_execute(a, 0, &cmd) == TIKU_RICH_OK);
    CHECK(tiku_richsession_query(a, cmd.kind, &state) == TIKU_RICH_OK);
    CHECK(tiku_richsession_format(a, &format) == TIKU_RICH_OK);
    CHECK(format.chars.bold && state.check == TIKU_RICHCMD_CHECK_ON);
    CHECK(f.fail_at == f.calls + 1 && !calls);
    f.fail_at = 0;
    doc->limits.history_bytes = 1;
    saved = save(doc, a, b, &f);
    cmd.kind = TIKU_RICHCMD_REPLACE_TEXT;
    cmd.value.text.data = "X";
    cmd.value.text.length = 1;
    CHECK(tiku_richsession_execute(a, 0, &cmd) == TIKU_RICH_LIMIT);
    unchanged(&saved, doc, a, b, &f);
    CHECK(!calls);
    doc->generation = UINT64_MAX;
    cmd.kind = TIKU_RICHCMD_ITALIC;
    CHECK(tiku_richsession_execute(a, 0, &cmd) == TIKU_RICH_LIMIT);
    CHECK(!memcmp(&saved.a.pending, &a->pending, sizeof a->pending));
    CHECK(tiku_richwatch_destroy(watch) == TIKU_RICH_OK);
    CHECK(tiku_richsession_destroy(a) == TIKU_RICH_OK);
    CHECK(tiku_richsession_destroy(b) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    CHECK(!f.live && !f.bytes);
}

/** @brief Run registration, per-view atomicity and allocation-free checks. */
int
main(void)
{
    construction();
    commands(0);
    commands(1);
    commands(2);
    commands(3);
    budgets();
    printf("PASS: rich session faults, %u injected allocation failures\n",
           injected);
    return 0;
}
