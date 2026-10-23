/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_faults.c - allocation failure and ID exhaustion on import.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include "tiku_richcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richdoc faults:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Allocation count, retained bytes and one failed attempt. */
typedef struct {
    size_t calls, fail_at, live, bytes;
} faults_t;

/** @brief Preserve the alignment promised by malloc. */
typedef union {
    max_align_t align;
    size_t bytes;
} prefix_t;

static unsigned injected;

/** @brief Fail one chosen allocation and account for successful blocks. */
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
    f->live++;
    f->bytes += bytes;
    return p + 1;
}

/** @brief Release and subtract the exact allocation size. */
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

/** @brief Create with the per-document failure allocator. */
static tiku_rich_status_t
create(faults_t *f, tiku_richdoc_t **doc)
{
    const tiku_rich_limits_t limits = {
        .allocator = {f, allocate, release}
    };

    return tiku_richdoc_create(&limits, doc);
}

/** @brief Fail every construction allocation, including the owner itself. */
static void
creation(void)
{
    faults_t f;
    tiku_richdoc_t *doc;
    tiku_rich_status_t status;
    size_t nth;

    for (nth = 1; nth < 64; nth++) {
        memset(&f, 0, sizeof f);
        f.fail_at = nth;
        doc = (tiku_richdoc_t *)(uintptr_t)1;
        status = create(&f, &doc);
        if (status == TIKU_RICH_OK) {
            tiku_richdoc_release(doc);
            CHECK(!f.live && !f.bytes);
            break;
        }
        CHECK(status == TIKU_RICH_NO_MEMORY && !doc);
        CHECK(!f.live && !f.bytes);
    }
    CHECK(nth > 3 && nth < 64);
}

/** @brief Fail each import allocation while a previous snapshot remains. */
static void
imports(void)
{
    tiku_rich_style_source_t style = {
        .name = "Body", .chars = {.present = TIKU_RICH_SIZE, .size = 12}
    };
    tiku_rich_run_source_t runs[2] = {
        {.length = 5, .link = "https://example.org/first"},
        {.length = 6, .link = "#second"}
    };
    tiku_rich_paragraph_source_t p[2] = {
        {.text = "first", .length = 5, .style = 1,
         .runs = &runs[0], .run_count = 1},
        {.text = "second", .length = 6, .style = 1,
         .runs = &runs[1], .run_count = 1}
    };
    tiku_rich_source_t source = {.styles = &style, .style_count = 1,
        .paragraphs = p, .paragraph_count = 2};
    faults_t f;
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *old;
    tiku_rich_status_t status;
    size_t nth, used, live;
    char buffer[32];
    size_t length;

    for (nth = 1; nth < 64; nth++) {
        memset(&f, 0, sizeof f);
        CHECK(create(&f, &doc) == TIKU_RICH_OK);
        CHECK(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
        used = tiku_richdoc_memory(doc);
        CHECK(used == f.bytes);
        live = f.live;
        f.fail_at = f.calls + nth;
        status = tiku_richdoc_import(doc, &source, 1);
        f.fail_at = 0;
        if (status != TIKU_RICH_OK) {
            CHECK(status == TIKU_RICH_NO_MEMORY);
            CHECK(tiku_richdoc_generation(doc) == 1);
            CHECK(tiku_richdoc_memory(doc) == used);
            CHECK(f.bytes == used && f.live == live);
        }
        CHECK(tiku_richsnapshot_text(old, buffer, sizeof buffer, &length) ==
              TIKU_RICH_OK && !length && !buffer[0]);
        tiku_richdoc_release(doc);
        CHECK(f.live);
        tiku_richsnapshot_release(old);
        CHECK(!f.live && !f.bytes);
        if (status == TIKU_RICH_OK)
            break;
    }
    CHECK(nth > 5 && nth < 64);
}

/** @brief Failed snapshot creation retains no additional root reference. */
static void
snapshot_failure(void)
{
    faults_t f = {0};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *s = (tiku_richsnapshot_t *)(uintptr_t)1;
    size_t before;

    CHECK(create(&f, &doc) == TIKU_RICH_OK);
    before = f.bytes;
    f.fail_at = f.calls + 1;
    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_NO_MEMORY && !s);
    CHECK(f.bytes == before);
    tiku_richdoc_release(doc);
    CHECK(!f.live && !f.bytes);
}

/** @brief Aggregate ceilings include temporary imports and held snapshots. */
static void
budget(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_limits_t limits = {0};
    tiku_rich_paragraph_source_t p = {0};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richsnapshot_t *snapshots[64];
    tiku_rich_status_t status;
    size_t baseline, n = 0;
    char text[4096];

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    baseline = tiku_richdoc_memory(doc);
    tiku_richdoc_release(doc);
    limits.memory_bytes = baseline + 1024;
    CHECK(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    memset(text, 'a', sizeof text);
    p.text = text;
    p.length = sizeof text;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_LIMIT);
    CHECK(tiku_richdoc_memory(doc) == baseline);
    do {
        CHECK(n < 64);
        status = tiku_richdoc_snapshot(doc, &snapshots[n]);
        if (status == TIKU_RICH_OK)
            n++;
    } while (status == TIKU_RICH_OK);
    CHECK(status == TIKU_RICH_LIMIT && n > 0 && !snapshots[n]);
    while (n)
        tiku_richsnapshot_release(snapshots[--n]);
    CHECK(tiku_richdoc_memory(doc) == baseline);
    tiku_richdoc_release(doc);
}

/** @brief The final ID is issued once, and generations never wrap. */
static void
exhaustion(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_source_t source = {0};
    tiku_richsnapshot_t *s;
    tiku_rich_paragraph_t p;
    size_t before;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    doc->next_id = UINT64_MAX;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(!doc->next_id);
    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_paragraph(s, 0, &p) == TIKU_RICH_OK);
    CHECK(p.id == UINT64_MAX);
    before = tiku_richdoc_memory(doc);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_LIMIT);
    CHECK(tiku_richdoc_generation(doc) == 2);
    CHECK(tiku_richdoc_memory(doc) == before);
    doc->generation = UINT64_MAX;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_LIMIT);
    CHECK(doc->generation == UINT64_MAX);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Add a named style whose copied storage participates in editing. */
static void
load_style(tiku_richdoc_t *doc)
{
    const tiku_rich_style_source_t style = {.name = "Body",
        .chars = {.present = TIKU_RICH_SIZE, .size = 12}};
    const tiku_rich_paragraph_source_t paragraph = {.style = 1};
    const tiku_rich_source_t source = {.styles = &style, .style_count = 1,
        .paragraphs = &paragraph, .paragraph_count = 1};

    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
}

/** @brief Fail every edit allocation while preserving the existing redo. */
static void
edit_failures(void)
{
    faults_t f;
    tiku_richdoc_t *doc;
    tiku_rich_edit_t edits[3];
    tiku_rich_transaction_t tx = {.edits = edits, .count = 3};
    tiku_rich_status_t status;
    size_t nth, used, history;
    uint64_t generation;

    for (nth = 1; nth < 64; nth++) {
        memset(&f, 0, sizeof f);
        CHECK(create(&f, &doc) == TIKU_RICH_OK);
        load_style(doc);
        memset(edits, 0, sizeof edits);
        edits[0].paragraph = doc->root->paragraphs[0]->value.id;
        edits[0].text = "abc";
        edits[0].length = 3;
        tx.count = 1;
        CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
        CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        edits[1] = edits[0];
        edits[1].kind = TIKU_RICH_FORMAT;
        edits[1].end = 3;
        edits[1].chars.present = TIKU_RICH_BOLD;
        edits[1].chars.bold = 1;
        edits[2] = edits[0];
        edits[2].start = edits[2].end = 3;
        tx.count = 3;
        used = f.bytes;
        history = tiku_richdoc_history_memory(doc);
        generation = doc->generation;
        f.fail_at = f.calls + nth;
        status = tiku_richdoc_apply(doc, &tx);
        f.fail_at = 0;
        if (status != TIKU_RICH_OK) {
            CHECK(status == TIKU_RICH_NO_MEMORY);
            CHECK(f.bytes == used && tiku_richdoc_memory(doc) == used);
            CHECK(tiku_richdoc_history_memory(doc) == history);
            CHECK(tiku_richdoc_can_redo(doc) && !tiku_richdoc_can_undo(doc));
            CHECK(doc->generation == generation && !doc->root->bytes);
        } else {
            CHECK(!tiku_richdoc_can_redo(doc));
            CHECK(doc->generation == generation + 1 && doc->root->bytes == 6);
        }
        f.fail_at = f.calls + 1;
        CHECK(tiku_richdoc_undo(doc) ==
              (status == TIKU_RICH_OK ? TIKU_RICH_OK : TIKU_RICH_NO_CHANGE));
        CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
        CHECK(f.calls + 1 == f.fail_at);
        tiku_richdoc_release(doc);
        CHECK(!f.live && !f.bytes);
        if (status == TIKU_RICH_OK)
            break;
    }
    CHECK(nth > 10 && nth < 64);
}

/** @brief Saved state for an allocation-failure publication check. */
typedef struct {
    tiku_rd_root_t *root;
    size_t bytes, history;
    uint64_t generation, next_id;
    tiku_rich_pos_t positions[2];
} checkpoint_t;

/** @brief Create two paragraphs, two anchors and an existing redo group. */
static tiku_richdoc_t *
structure_fixture(faults_t *f, tiku_richanchor_t *anchors[2])
{
    tiku_richdoc_t *doc;
    tiku_rich_style_source_t styles[3] = {
        {.name = "Body"}, {.name = "Heading"},
        {.name = "Emphasis", .kind = TIKU_RICH_CHARACTER_STYLE}
    };
    tiku_rich_run_source_t runs[2] = {
        {.length = 4, .link = "https://example.org/left"},
        {.length = 4, .link = "#right"}
    };
    tiku_rich_paragraph_source_t p[2] = {
        {.text = "abcd", .length = 4, .style = 1,
         .runs = &runs[0], .run_count = 1,
         .list = {TIKU_RICH_LIST_DECIMAL, 0, 4}},
        {.text = "EFGH", .length = 4, .runs = &runs[1], .run_count = 1,
         .list = {TIKU_RICH_LIST_DECIMAL, 0, 0}}
    };
    tiku_rich_source_t source = {.styles = styles, .style_count = 3,
        .paragraphs = p, .paragraph_count = 2};
    tiku_rich_edit_t op = {.text = "!", .length = 1};
    tiku_rich_transaction_t tx = {.edits = &op, .count = 1};
    size_t i;

    CHECK(create(f, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    op.paragraph = doc->root->paragraphs[0]->value.id;
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    for (i = 0; i < 2; i++) {
        tiku_rich_pos_t pos = {doc->root->body,
            doc->root->paragraphs[i]->value.id, 2, TIKU_RICH_AFTER};

        CHECK(tiku_richanchor_create(doc, pos, &anchors[i]) == TIKU_RICH_OK);
    }
    return doc;
}

/** @brief Dispatch a structural or range operation with deterministic inputs. */
static tiku_rich_status_t
structure_operation(tiku_richdoc_t *doc, unsigned kind)
{
    tiku_rich_edit_t op = {.paragraph = doc->root->paragraphs[0]->value.id,
        .kind = kind ? TIKU_RICH_JOIN : TIKU_RICH_SPLIT};
    tiku_rich_transaction_t tx = {doc->generation, &op, 1};
    tiku_rich_pos_t first = {doc->root->body, op.paragraph, 1,
                             TIKU_RICH_BEFORE};
    tiku_rich_pos_t last = {doc->root->body,
        doc->root->paragraphs[1]->value.id, 2, TIKU_RICH_AFTER};
    tiku_rich_chars_t chars = {.present = TIKU_RICH_BOLD, .bold = 1};
    tiku_rich_style_spec_t spec = {.name = "Edited",
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}};

    if (!kind)
        op.start = op.end = 2;
    if (kind < 2)
        return tiku_richdoc_apply(doc, &tx);
    if (kind == 2)
        return tiku_richdoc_replace_range(doc, doc->generation, first, last,
                                          "x\r\ny", 4);
    if (kind == 3)
        return tiku_richdoc_format_range(doc, doc->generation, first, last,
                                          &chars);
    switch (kind) {
    case 4:
        op.kind = TIKU_RICH_PARAGRAPH_FORMAT;
        op.props.present = TIKU_RICH_SPACING;
        op.props.before = 12;
        op.chars = chars;
        break;
    case 5:
        op.kind = TIKU_RICH_PARAGRAPH_STYLE_SET;
        op.style = doc->root->styles[1].value.id;
        break;
    case 6:
        op.kind = TIKU_RICH_CHARACTER_STYLE_SET;
        op.style = doc->root->styles[2].value.id;
        op.start = 1;
        op.end = 3;
        break;
    case 7:
    case 8:
        op.kind = TIKU_RICH_STYLE_DEFINE;
        op.definition = &spec;
        op.style = kind == 8 ? doc->root->styles[0].value.id : 0;
        break;
    case 9:
        op.kind = TIKU_RICH_STYLE_REMOVE;
        op.style = doc->root->styles[1].value.id;
        break;
    case 10:
        op.kind = TIKU_RICH_LIST_SET;
        op.list = (tiku_rich_list_t){TIKU_RICH_LIST_DECIMAL, 2, 9};
        break;
    default:
        op.kind = TIKU_RICH_LINK_SET;
        op.start = 1;
        op.end = 3;
        op.link = kind == 11 ? "https://example.org/new" : NULL;
        break;
    }
    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Compare captured anchor values without relying on struct padding. */
static void
expect_positions(tiku_richanchor_t *anchors[2], const checkpoint_t *before)
{
    size_t i;
    tiku_rich_pos_t p;

    for (i = 0; i < 2; i++) {
        CHECK(tiku_richanchor_get(anchors[i], &p) == TIKU_RICH_OK);
        CHECK(p.body == before->positions[i].body);
        CHECK(p.paragraph == before->positions[i].paragraph);
        CHECK(p.byte == before->positions[i].byte);
        CHECK(p.affinity == before->positions[i].affinity);
    }
}

/** @brief Fail each structural allocation, including anchor history records. */
static void
structure_failures(unsigned kind)
{
    faults_t f;
    tiku_richdoc_t *doc;
    tiku_richanchor_t *anchors[2];
    checkpoint_t before;
    tiku_rich_status_t status;
    size_t nth, i;

    for (nth = 1; nth < 128; nth++) {
        memset(&f, 0, sizeof f);
        doc = structure_fixture(&f, anchors);
        before = (checkpoint_t){.root = doc->root, .bytes = f.bytes,
            .history = doc->history_bytes, .generation = doc->generation,
            .next_id = doc->next_id};
        for (i = 0; i < 2; i++)
            before.positions[i] = anchors[i]->position;
        f.fail_at = f.calls + nth;
        status = structure_operation(doc, kind);
        f.fail_at = 0;
        if (status != TIKU_RICH_OK) {
            CHECK(status == TIKU_RICH_NO_MEMORY);
            CHECK(doc->root == before.root && f.bytes == before.bytes);
            CHECK(tiku_richdoc_memory(doc) == before.bytes);
            CHECK(doc->history_bytes == before.history);
            CHECK(doc->generation == before.generation);
            CHECK(doc->next_id == before.next_id);
            CHECK(tiku_richdoc_can_redo(doc) && !tiku_richdoc_can_undo(doc));
            expect_positions(anchors, &before);
        } else {
            CHECK(!tiku_richdoc_can_redo(doc));
            f.fail_at = f.calls + 1;
            CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
            CHECK(doc->root == before.root);
            expect_positions(anchors, &before);
            CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
            CHECK(f.calls + 1 == f.fail_at);
        }
        tiku_richdoc_release(doc);
        for (i = 0; i < 2; i++)
            tiku_richanchor_release(anchors[i]);
        CHECK(!f.live && !f.bytes);
        if (status == TIKU_RICH_OK)
            break;
    }
    CHECK(nth > 5 && nth < 128);
}

/** @brief Anchor creation failure retains neither an ID nor a document ref. */
static void
anchor_failure(void)
{
    faults_t f = {0};
    tiku_richdoc_t *doc;
    tiku_richanchor_t *anchor = (void *)(uintptr_t)1;
    tiku_rich_pos_t p;
    size_t before;

    CHECK(create(&f, &doc) == TIKU_RICH_OK);
    p = (tiku_rich_pos_t){doc->root->body,
        doc->root->paragraphs[0]->value.id, 0, TIKU_RICH_AFTER};
    before = f.bytes;
    f.fail_at = f.calls + 1;
    CHECK(tiku_richanchor_create(doc, p, &anchor) == TIKU_RICH_NO_MEMORY);
    CHECK(!anchor && !doc->anchor_count && doc->refs == 1);
    CHECK(doc->next_anchor == 1 && f.bytes == before);
    tiku_richdoc_release(doc);
    CHECK(!f.live && !f.bytes);
}

/** @brief Create searchable text without retaining editing history. */
static tiku_richdoc_t *
search_fixture(faults_t *f)
{
    tiku_richdoc_t *doc;
    const tiku_rich_paragraph_source_t paragraph = {
        .text = "abcabc", .length = 6
    };
    const tiku_rich_source_t source = {
        .paragraphs = &paragraph, .paragraph_count = 1
    };

    CHECK(create(f, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Search creation is failure-atomic; stepping performs no allocation. */
static void
search_failures(unsigned flags)
{
    faults_t f;
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *s;
    tiku_richsearch_t *search;
    tiku_rich_match_t match;
    tiku_rich_pos_t pos, end;
    tiku_rich_status_t status;
    size_t nth, used;

    for (nth = 1; nth < 16; nth++) {
        memset(&f, 0, sizeof f);
        doc = search_fixture(&f);
        CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
        pos = (tiku_rich_pos_t){1, doc->root->paragraphs[0]->value.id,
                                0, TIKU_RICH_AFTER};
        end = pos;
        end.byte = 6;
        used = f.bytes;
        f.fail_at = f.calls + nth;
        status = tiku_richsearch_create_options(s, pos, end, "abc", 3,
                                                flags, &search);
        f.fail_at = 0;
        if (status != TIKU_RICH_OK) {
            CHECK(status == TIKU_RICH_NO_MEMORY && !search);
            CHECK(f.bytes == used && tiku_richdoc_memory(doc) == used);
            CHECK(atomic_load(&doc->root->refs) == 2);
        } else {
            f.fail_at = f.calls + 1;
            if (flags) {
                tiku_rich_status_t next;
                unsigned steps = 0;

                do {
                    next = tiku_richsearch_next(search, 1, &match);
                    CHECK(++steps < 100);
                } while (next == TIKU_RICH_PENDING);
                CHECK(next == TIKU_RICH_NOT_FOUND);
            } else {
            CHECK(tiku_richsearch_next(search, 1, &match) == TIKU_RICH_PENDING);
            CHECK(tiku_richsearch_next(search, 2, &match) == TIKU_RICH_OK);
            CHECK(match.first.byte == 0 && match.last.byte == 3);
            CHECK(tiku_richsearch_next(search, 3, &match) == TIKU_RICH_OK);
            CHECK(match.first.byte == 3 && match.last.byte == 6);
            CHECK(tiku_richsearch_next(search, 1, &match) ==
                  TIKU_RICH_NOT_FOUND);
            }
            CHECK(f.calls + 1 == f.fail_at);
            tiku_richsearch_release(search);
            CHECK(f.bytes == used);
        }
        f.fail_at = 0;
        doc->memory->limit = used;
        CHECK(tiku_richsearch_create(s, pos, pos, "a", 1, &search) ==
              TIKU_RICH_LIMIT && !search);
        CHECK(f.bytes == used);
        tiku_richdoc_release(doc);
        tiku_richsnapshot_release(s);
        CHECK(!f.live && !f.bytes);
        if (status == TIKU_RICH_OK)
            break;
    }
    CHECK(nth == (flags ? 6 : 4));
}

/** @brief Create a styled source and transfer its selected content. */
static tiku_richfragment_t *
fragment_fixture(faults_t *f)
{
    tiku_rich_style_source_t styles[2] = {
        {.name = "Body", .chars = {.present = TIKU_RICH_SIZE, .size = 12}},
        {.name = "Detail", .parent = 1}
    };
    const tiku_rich_run_source_t run = {.length = 4, .link = "#copied"};
    const tiku_rich_paragraph_source_t p[2] = {
        {.text = "abcd", .length = 4, .style = 2,
         .runs = &run, .run_count = 1},
        {.text = "EFGH", .length = 4, .style = 1,
         .runs = &run, .run_count = 1}
    };
    const tiku_rich_source_t source = {.styles = styles, .style_count = 2,
        .paragraphs = p, .paragraph_count = 2};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *s;
    tiku_richfragment_t *fragment;
    tiku_rich_pos_t first, last;

    CHECK(create(f, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    first = (tiku_rich_pos_t){1, doc->root->paragraphs[0]->value.id, 1, 0};
    last = (tiku_rich_pos_t){1, doc->root->paragraphs[1]->value.id, 3, 0};
    CHECK(tiku_richfragment_create(s, first, last, &fragment) == TIKU_RICH_OK);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
    return fragment;
}

/** @brief Capture failure releases partial paragraphs, links and style closure. */
static void
fragment_capture_failures(void)
{
    faults_t f = {0};
    tiku_richfragment_t *source = fragment_fixture(&f), *fragment;
    tiku_richsnapshot_t *s, *held;
    tiku_rich_status_t status;
    tiku_rich_pos_t first, last;
    tiku_rich_selection_t state;
    size_t nth, used = f.bytes, live = f.live;

    f.fail_at = f.calls + 1;
    CHECK(tiku_richfragment_snapshot(source, &s) == TIKU_RICH_NO_MEMORY);
    CHECK(!s && f.bytes == used && f.live == live);
    f.fail_at = 0;
    CHECK(tiku_richfragment_snapshot(source, &s) == TIKU_RICH_OK);
    used = f.bytes;
    live = f.live;
    first = (tiku_rich_pos_t){1, s->root->paragraphs[0]->value.id, 0, 0};
    last = (tiku_rich_pos_t){1, s->root->paragraphs[1]->value.id, 3, 0};
    f.fail_at = f.calls + 1;
    CHECK(tiku_richsnapshot_selection(s, first, last, &state) == TIKU_RICH_OK);
    CHECK(f.calls + 1 == f.fail_at && state.chars.size == 12);
    for (nth = 1; nth < 64; nth++) {
        f.fail_at = f.calls + nth;
        status = tiku_richfragment_create(s, first, last, &fragment);
        f.fail_at = 0;
        if (status == TIKU_RICH_OK) {
            CHECK(tiku_richfragment_snapshot(fragment, &held) == TIKU_RICH_OK);
            tiku_richfragment_release(fragment);
            CHECK(held->root->bytes == 6);
            tiku_richsnapshot_release(held);
        } else {
            CHECK(status == TIKU_RICH_NO_MEMORY && !fragment);
        }
        CHECK(f.bytes == used && f.live == live);
        CHECK(atomic_load(&source->root->refs) == 2);
        if (status == TIKU_RICH_OK)
            break;
    }
    CHECK(nth > 10 && nth < 64);
    tiku_richsnapshot_release(s);
    tiku_richfragment_release(source);
    CHECK(!f.bytes && !f.live);
}

/** @brief Paste refusal preserves content, history, anchors and ID cursors. */
static void
expect_paste_refusal(tiku_richdoc_t *doc, faults_t *f,
                     tiku_richanchor_t *anchors[2], const checkpoint_t *before)
{
    CHECK(doc->root == before->root && f->bytes == before->bytes);
    CHECK(tiku_richdoc_memory(doc) == before->bytes);
    CHECK(doc->history_bytes == before->history);
    CHECK(doc->generation == before->generation);
    CHECK(doc->next_id == before->next_id);
    CHECK(tiku_richdoc_can_redo(doc) && !tiku_richdoc_can_undo(doc));
    expect_positions(anchors, before);
}

/** @brief Fail each destination allocation and check independent ownership. */
static void
fragment_paste_failures(void)
{
    faults_t source = {0}, f;
    tiku_richfragment_t *fragment = fragment_fixture(&source);
    tiku_richdoc_t *doc;
    tiku_richanchor_t *anchors[2];
    checkpoint_t before;
    tiku_rich_status_t status;
    tiku_rich_pos_t first, last;
    size_t nth, i, source_bytes = source.bytes;

    for (nth = 1; nth < 128; nth++) {
        memset(&f, 0, sizeof f);
        doc = structure_fixture(&f, anchors);
        before = (checkpoint_t){.root = doc->root, .bytes = f.bytes,
            .history = doc->history_bytes, .generation = doc->generation,
            .next_id = doc->next_id};
        for (i = 0; i < 2; i++)
            before.positions[i] = anchors[i]->position;
        first = before.positions[0];
        last = before.positions[1];
        f.fail_at = f.calls + nth;
        status = tiku_richdoc_paste_fragment(doc, doc->generation,
                                             first, last, fragment);
        f.fail_at = 0;
        CHECK(source.bytes == source_bytes);
        if (status != TIKU_RICH_OK) {
            CHECK(status == TIKU_RICH_NO_MEMORY);
            expect_paste_refusal(doc, &f, anchors, &before);
        } else {
            CHECK(!tiku_richdoc_can_redo(doc));
            tiku_richfragment_release(fragment);
            CHECK(!source.live && !source.bytes);
            CHECK(!strcmp(doc->root->paragraphs[1]->value.runs[0].link,
                          "#copied"));
            f.fail_at = f.calls + 1;
            CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
            CHECK(doc->root == before.root);
            expect_positions(anchors, &before);
            CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
            CHECK(f.calls + 1 == f.fail_at);
        }
        tiku_richdoc_release(doc);
        for (i = 0; i < 2; i++)
            tiku_richanchor_release(anchors[i]);
        CHECK(!f.live && !f.bytes);
        if (status == TIKU_RICH_OK)
            break;
    }
    CHECK(nth > 15 && nth < 128);
}

/** @brief Refuse document ceilings and exhausted IDs without publishing. */
static void
fragment_limits(void)
{
    faults_t source = {0}, f = {0};
    tiku_richfragment_t *fragment = fragment_fixture(&source), *bad;
    tiku_richanchor_t *anchors[2];
    tiku_richdoc_t *doc = structure_fixture(&f, anchors);
    tiku_richsnapshot_t *s;
    tiku_rich_limits_t limits = doc->limits;
    checkpoint_t before;
    size_t kind, i, budget = doc->memory->limit;

    before = (checkpoint_t){.root = doc->root, .bytes = f.bytes,
        .history = doc->history_bytes, .generation = doc->generation,
        .next_id = doc->next_id};
    for (i = 0; i < 2; i++)
        before.positions[i] = anchors[i]->position;
    for (kind = 0; kind < 7; kind++) {
        switch (kind) {
        case 0: doc->limits.text_bytes = 8; break;
        case 1: doc->limits.paragraphs = 1; break;
        case 2: doc->limits.runs = 1; break;
        case 3: doc->limits.styles = 3; break;
        case 4: doc->limits.history_bytes = 1; break;
        case 5: doc->memory->limit = f.bytes + 1024; break;
        default: doc->next_id = UINT64_MAX; before.next_id = UINT64_MAX;
        }
        CHECK(tiku_richdoc_paste_fragment(doc, 0, before.positions[0],
            before.positions[1], fragment) == TIKU_RICH_LIMIT);
        expect_paste_refusal(doc, &f, anchors, &before);
        doc->limits = limits;
        doc->memory->limit = budget;
    }
    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    doc->memory->limit = f.bytes;
    CHECK(tiku_richfragment_create(s, before.positions[0], before.positions[1],
                                  &bad) == TIKU_RICH_LIMIT && !bad);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
    for (i = 0; i < 2; i++)
        tiku_richanchor_release(anchors[i]);
    tiku_richfragment_release(fragment);
    CHECK(!f.live && !f.bytes && !source.live && !source.bytes);
}

/** @brief Create direct formatting, two anchors and an existing redo group. */
static tiku_richdoc_t *
command_fixture(faults_t *f, tiku_richanchor_t *anchors[2],
                tiku_richcmd_kind_t kind)
{
    tiku_richdoc_t *doc = structure_fixture(f, anchors);
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_FORMAT,
        .paragraph = doc->root->paragraphs[0]->value.id, .end = 4,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}};
    tiku_rich_transaction_t tx = {.edits = &edit, .count = 1};

    if (kind == TIKU_RICHCMD_IMAGE_SET) {
        static const uint32_t pixel = 0xff010203;
        const tiku_rich_image_t image = {1, 1, &pixel, 12, 12, "Old"};
        tiku_rich_edit_t insert = {.kind = TIKU_RICH_IMAGE_INSERT,
            .paragraph = edit.paragraph, .image = &image};
        tiku_rich_transaction_t image_tx = {0, &insert, 1};

        CHECK(tiku_richdoc_apply(doc, &image_tx) == TIKU_RICH_OK);
    }
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    if (kind >= TIKU_RICHCMD_TABLE_REMOVE &&
        kind <= TIKU_RICHCMD_CELL_SET) {
        tiku_rich_edit_t insert = {.kind = TIKU_RICH_TABLE_INSERT,
            .paragraph = edit.paragraph, .table = {2, 2, 0, 4, 1, 0xff000000}};
        tiku_rich_transaction_t table_tx = {0, &insert, 1};

        CHECK(tiku_richdoc_apply(doc, &table_tx) == TIKU_RICH_OK);
    }
    tiku_richdoc_clear_history(doc);
    edit = (tiku_rich_edit_t){.paragraph = edit.paragraph,
                             .text = "!", .length = 1};
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    return doc;
}

/** @brief Supply a valid, changing payload for each public command identity. */
static tiku_richcmd_t
command_payload(tiku_richdoc_t *doc, tiku_richcmd_kind_t kind,
                const tiku_richfragment_t *fragment, int update_style)
{
    tiku_richcmd_t command = {.kind = kind};

    switch (kind) {
    case TIKU_RICHCMD_REPLACE_TEXT:
        command.value.text.data = "x\r\ny";
        command.value.text.length = 4;
        break;
    case TIKU_RICHCMD_PASTE_FRAGMENT:
        command.value.fragment = fragment;
        break;
    case TIKU_RICHCMD_CHARACTER_FORMAT:
        command.value.character.values =
            (tiku_rich_chars_t){.present = TIKU_RICH_SIZE, .size = 18};
        command.value.character.clear = TIKU_RICH_BOLD;
        break;
    case TIKU_RICHCMD_PARAGRAPH_FORMAT:
        command.value.paragraph.values =
            (tiku_rich_para_t){.present = TIKU_RICH_SPACING, .before = 8};
        command.value.paragraph.chars =
            (tiku_rich_chars_t){.present = TIKU_RICH_SIZE, .size = 20};
        break;
    case TIKU_RICHCMD_PARAGRAPH_STYLE:
    case TIKU_RICHCMD_STYLE_REMOVE:
        command.value.style = doc->root->styles[1].value.id;
        break;
    case TIKU_RICHCMD_CHARACTER_STYLE:
        command.value.style = doc->root->styles[2].value.id;
        break;
    case TIKU_RICHCMD_LIST_SET:
        command.value.list = (tiku_rich_list_t){TIKU_RICH_LIST_DECIMAL, 2, 9};
        break;
    case TIKU_RICHCMD_LINK_SET:
        command.value.link = "https://example.org/command";
        break;
    case TIKU_RICHCMD_STYLE_DEFINE:
        command.value.definition.value = (tiku_rich_style_spec_t){
            .name = "Command", .chars = {.present = TIKU_RICH_SIZE, .size = 16}};
        if (update_style)
            command.value.definition.style = doc->root->styles[0].value.id;
        break;
    default:
        break;
    }
    return command;
}

/** @brief Refuse each command allocation without losing redo or anchor state. */
static void
command_failures(tiku_richcmd_kind_t kind, int update_style)
{
    faults_t source = {0}, f;
    tiku_richfragment_t *fragment = fragment_fixture(&source);
    tiku_richdoc_t *doc;
    tiku_richanchor_t *anchors[2];
    checkpoint_t before;
    tiku_rich_status_t status;
    tiku_richcmd_t command;
    tiku_richcmd_context_t context;
    tiku_richcmd_state_t state;
    size_t nth, i;

    for (nth = 1; nth < 128; nth++) {
        memset(&f, 0, sizeof f);
        doc = command_fixture(&f, anchors, kind);
        before = (checkpoint_t){.root = doc->root, .bytes = f.bytes,
            .history = doc->history_bytes, .generation = doc->generation,
            .next_id = doc->next_id};
        for (i = 0; i < 2; i++)
            before.positions[i] = anchors[i]->position;
        context = (tiku_richcmd_context_t){.generation = doc->generation,
            .first = before.positions[0], .last = before.positions[1]};
        command = command_payload(doc, kind, fragment, update_style);
        if (kind >= TIKU_RICHCMD_TABLE_INSERT &&
            kind <= TIKU_RICHCMD_CELL_SET) {
            context.first = (tiku_rich_pos_t){doc->root->body,
                doc->root->paragraphs[kind != TIKU_RICHCMD_TABLE_INSERT]->
                    value.id, 0, TIKU_RICH_BEFORE};
            context.last = context.first;
            command.value.table = (tiku_rich_table_t){
                2, 2, 0, 4, 1, 0xff000000, {0}, 1};
            if (kind == TIKU_RICHCMD_TABLE_SET) {
                command.value.table.weights[0] = 2;
                command.value.table.weights[1] = 1;
            }
            if (kind == TIKU_RICHCMD_CELL_SET)
                command.value.color = 0xffddeeff;
        }
        if (kind == TIKU_RICHCMD_IMAGE_INSERT ||
            kind == TIKU_RICHCMD_IMAGE_SET) {
            static const uint32_t pixel = 0xffff0000;
            static const tiku_rich_image_t image =
                {1, 1, &pixel, 24, 18, "New image"};

            context.first = (tiku_rich_pos_t){doc->root->body,
                doc->root->paragraphs[0]->value.id, 0, TIKU_RICH_BEFORE};
            context.last = context.first;
            context.last.byte = kind == TIKU_RICHCMD_IMAGE_SET ? 3 : 0;
            command.value.image = &image;
        }
        f.fail_at = f.calls + 1;
        CHECK(tiku_richcmd_query(doc, &context, kind, &state) == TIKU_RICH_OK);
        CHECK(state.enabled && f.calls + 1 == f.fail_at);
        CHECK(f.bytes == before.bytes);
        f.fail_at = f.calls + nth;
        status = tiku_richcmd_execute(doc, &context, &command);
        f.fail_at = 0;
        if (status != TIKU_RICH_OK) {
            CHECK(status == TIKU_RICH_NO_MEMORY);
            expect_paste_refusal(doc, &f, anchors, &before);
        } else {
            CHECK(!tiku_richdoc_can_redo(doc));
            CHECK(doc->generation == before.generation + 1);
            f.fail_at = f.calls + 1;
            context.generation = 0;
            command.kind = TIKU_RICHCMD_UNDO;
            CHECK(tiku_richcmd_execute(doc, &context, &command) == TIKU_RICH_OK);
            CHECK(doc->root == before.root);
            expect_positions(anchors, &before);
            command.kind = TIKU_RICHCMD_REDO;
            CHECK(tiku_richcmd_execute(doc, &context, &command) == TIKU_RICH_OK);
            CHECK(f.calls + 1 == f.fail_at);
        }
        tiku_richdoc_release(doc);
        for (i = 0; i < 2; i++)
            tiku_richanchor_release(anchors[i]);
        CHECK(!f.live && !f.bytes);
        if (status == TIKU_RICH_OK)
            break;
    }
    CHECK(nth > 3 && nth < 128);
    tiku_richfragment_release(fragment);
    CHECK(!source.live && !source.bytes);
}

/** @brief Exercise every allocating catalog command and style update. */
static void
commands(void)
{
    size_t i;
    tiku_richcmd_kind_t kind;

    for (i = 0; i < tiku_richcmd_count(); i++) {
        kind = tiku_richcmd_at(i)->kind;
        if (kind != TIKU_RICHCMD_UNDO && kind != TIKU_RICHCMD_REDO)
            command_failures(kind, 0);
    }
    command_failures(TIKU_RICHCMD_STYLE_DEFINE, 1);
}

/** @brief Run allocation, aggregate-budget, editing and ID-boundary checks. */
int
main(void)
{
    creation();
    imports();
    snapshot_failure();
    budget();
    exhaustion();
    edit_failures();
    structure_failures(0);
    structure_failures(1);
    structure_failures(2);
    structure_failures(3);
    structure_failures(4);
    structure_failures(5);
    structure_failures(6);
    structure_failures(7);
    structure_failures(8);
    structure_failures(9);
    structure_failures(10);
    structure_failures(11);
    structure_failures(12);
    anchor_failure();
    search_failures(0);
    search_failures(TIKU_RICH_SEARCH_WHOLE_WORD);
    fragment_capture_failures();
    fragment_paste_failures();
    fragment_limits();
    commands();
    printf("PASS: rich document faults, %u injected allocation failures\n",
           injected);
    return 0;
}
