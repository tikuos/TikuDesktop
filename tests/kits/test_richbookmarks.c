/*
 * TikuDesktop Document Kit tests.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_richbookmarks.c - persistent points, edit mapping and failure atomicity.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include "tiku_richsession.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Read a bookmark without retaining an extra model allocation. */
static tiku_rich_pos_t
point(tiku_richdoc_t *doc, const char *name)
{
    tiku_richsnapshot_t snapshot = {.root = doc->root};
    tiku_rich_pos_t out;

    assert(tiku_richsnapshot_find_bookmark(&snapshot, name, &out) == TIKU_RICH_OK);
    return out;
}

/** @brief Assert logical positions including insertion affinity. */
static void
same(tiku_rich_pos_t a, tiku_rich_pos_t b)
{
    assert(a.body == b.body && a.paragraph == b.paragraph);
    assert(a.byte == b.byte && a.affinity == b.affinity);
}

/** @brief Publish a single valid model operation. */
static void
edit(tiku_richdoc_t *doc, tiku_rich_edit_t operation)
{
    tiku_rich_transaction_t tx = {0, &operation, 1};

    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
}

/** @brief Exercise names, URI decoding, snapshots, dirty state and guards. */
static void
names(void)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_pos_t position, out;
    tiku_rich_bookmark_t value;
    uint64_t generation;
    const char *bad[] = {"", "#", "#bad%", "#bad%00", "#bad%FF", "#bad%0A"};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    position = (tiku_rich_pos_t){doc->root->body,
        doc->root->paragraphs[0]->value.id, 0, TIKU_RICH_BEFORE};
    assert(tiku_richdoc_bookmark_set(doc, 0, "Résumé & 1", position) == TIKU_RICH_OK);
    assert(tiku_richdoc_dirty(doc));
    generation = doc->generation;
    assert(tiku_richdoc_bookmark_set(doc, 0, "Résumé & 1", position) == TIKU_RICH_NO_CHANGE);
    assert(doc->generation == generation);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_resolve_link(snapshot, "#R%C3%A9sum%C3%A9%20%26%201", &out) == TIKU_RICH_OK);
    same(position, out);
    assert(tiku_richsnapshot_resolve_link(snapshot, "#missing", &out) == TIKU_RICH_NOT_FOUND);
    assert(!out.paragraph);
    assert(tiku_richsnapshot_resolve_link(snapshot, "https://example.org", &out) == TIKU_RICH_UNSUPPORTED);
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        memset(&out, 0xff, sizeof out);
        assert(tiku_richsnapshot_resolve_link(snapshot, bad[i], &out) != TIKU_RICH_OK);
        assert(!out.paragraph && !out.body && !out.byte);
    }
    assert(tiku_richdoc_bookmark_remove(doc, 0, "Résumé & 1") == TIKU_RICH_OK);
    assert(tiku_richsnapshot_bookmarks(snapshot) == 1);
    assert(tiku_richsnapshot_bookmark(snapshot, 0, &value) == TIKU_RICH_OK);
    assert(!strcmp(value.name, "Résumé & 1"));
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    same(point(doc, value.name), position);
    assert(tiku_richdoc_bookmark_set(doc, doc->generation - 1, "Other", position) == TIKU_RICH_STALE);
    tiku_richdoc_set_read_only(doc, 1);
    assert(tiku_richdoc_bookmark_remove(doc, 0, value.name) == TIKU_RICH_READ_ONLY);
    assert(tiku_richdoc_bookmark_set(doc, 0, "Other", position) == TIKU_RICH_READ_ONLY);
    tiku_richdoc_set_read_only(doc, 0);
    position.byte = 1;
    assert(tiku_richdoc_bookmark_set(doc, 0, "Other", position) == TIKU_RICH_INVALID_RANGE);
    assert(tiku_richdoc_bookmark_remove(doc, 0, "Other") == TIKU_RICH_NOT_FOUND);
    tiku_richdoc_release(doc);
    assert(tiku_richsnapshot_bookmark(snapshot, 1, &value) == TIKU_RICH_INVALID_RANGE);
    assert(!value.name[0] && !value.position.paragraph);
    tiku_richsnapshot_release(snapshot);
}

/** @brief Compare point mappings with registered anchors after generated edits. */
static void
mapping(void)
{
    tiku_richdoc_t *doc;
    tiku_richanchor_t *anchors[8];
    tiku_rich_pos_t positions[8], at;
    tiku_rich_edit_t op = {0};
    unsigned state = 721;
    size_t index, offset, length;
    char name[16];

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
        .paragraph = doc->root->paragraphs[0]->value.id, .text = "abcdef", .length = 6});
    for (size_t i = 0; i < 8; i++) {
        at = (tiku_rich_pos_t){doc->root->body,
            doc->root->paragraphs[0]->value.id, (i / 2) * 2,
            i % 2 ? TIKU_RICH_AFTER : TIKU_RICH_BEFORE};
        snprintf(name, sizeof name, "point%zu", i);
        assert(tiku_richdoc_bookmark_set(doc, 0, name, at) == TIKU_RICH_OK);
        assert(tiku_richanchor_create(doc, at, &anchors[i]) == TIKU_RICH_OK);
    }
    tiku_richdoc_clear_history(doc);
    for (size_t n = 0; n < 200; n++) {
        for (size_t i = 0; i < 8; i++) {
            snprintf(name, sizeof name, "point%zu", i);
            positions[i] = point(doc, name);
        }
        state = state * 1664525u + 1013904223u;
        index = (state >> 8) % doc->root->count;
        length = doc->root->paragraphs[index]->value.length;
        offset = (state >> 16) % (length + 1);
        op = (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = doc->root->paragraphs[index]->value.id,
            .start = offset, .end = offset, .text = "X", .length = 1};
        if (n % 4 == 0) op.kind = TIKU_RICH_SPLIT;
        else if (n % 4 == 1 && index + 1 < doc->root->count) {
            op.kind = TIKU_RICH_JOIN;
            op.start = op.end = 0;
        } else if (n % 4 == 2 && offset < length) {
            op.end = length;
            op.text = ""; op.length = 0;
        }
        edit(doc, op);
        for (size_t i = 0; i < 8; i++) {
            snprintf(name, sizeof name, "point%zu", i);
            assert(tiku_richanchor_get(anchors[i], &at) == TIKU_RICH_OK);
            same(point(doc, name), at);
        }
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        for (size_t i = 0; i < 8; i++) {
            snprintf(name, sizeof name, "point%zu", i);
            same(point(doc, name), positions[i]);
        }
        assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    }
    for (size_t i = 0; i < 8; i++) tiku_richanchor_release(anchors[i]);
    tiku_richdoc_release(doc);
}

/** @brief Table deletion collapses points like anchors; undo restores the cell. */
static void
tables(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_pos_t at, after;
    tiku_richanchor_t *anchor;
    uint64_t table;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .table = {.rows = 2, .columns = 2}});
    table = doc->root->blocks->nodes[1].id;
    at = (tiku_rich_pos_t){doc->root->body,
        doc->root->paragraphs[4]->value.id, 0, TIKU_RICH_AFTER};
    assert(tiku_richdoc_bookmark_set(doc, 0, "Cell", at) == TIKU_RICH_OK);
    assert(tiku_richanchor_create(doc, at, &anchor) == TIKU_RICH_OK);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_REMOVE, .object = table});
    assert(tiku_richanchor_get(anchor, &after) == TIKU_RICH_OK);
    same(point(doc, "Cell"), after);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    same(point(doc, "Cell"), at);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_GRID, .object = table,
        .grid = {TIKU_RICH_ROW_REMOVE, 1}});
    assert(tiku_richanchor_get(anchor, &after) == TIKU_RICH_OK);
    same(point(doc, "Cell"), after);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    same(point(doc, "Cell"), at);
    tiku_richanchor_release(anchor);
    tiku_richdoc_release(doc);
}

/** @brief Validate import ceilings, duplicate names and scalar boundaries. */
static void
imports(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_bookmark_source_t *marks = calloc(1025, sizeof *marks);
    tiku_rich_paragraph_source_t p = {.text = "é", .length = 2};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1,
        .bookmarks = marks, .bookmark_count = 1024};
    tiku_rich_pos_t at;

    assert(marks && tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    for (size_t i = 0; i < 1025; i++) snprintf(marks[i].name, 64, "Point%zu", i);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    at = point(doc, "Point1023");
    at.byte = 2;
    assert(tiku_richdoc_bookmark_set(doc, 0, "Point1023", at) == TIKU_RICH_OK);
    assert(tiku_richdoc_bookmark_set(doc, 0, "Extra", at) == TIKU_RICH_LIMIT);
    source.bookmark_count = 1025;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_LIMIT);
    source.bookmark_count = 2;
    strcpy(marks[1].name, marks[0].name);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID);
    source.bookmark_count = 1;
    marks[0].byte = 1;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_RANGE);
    marks[0].byte = 2;
    marks[0].after = 2;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID);
    marks[0].after = 1;
    marks[0].paragraph = 1;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_RANGE);
    same(point(doc, "Point1023"), at);
    free(marks);
    tiku_richdoc_release(doc);
}

/** @brief Allocation-failure counters and outstanding ownership. */
typedef struct { size_t calls, fail, live; } heap_t;

/** @brief Refuse one chosen allocation and count owned blocks. */
static void *
allocate(void *context, size_t bytes)
{
    heap_t *heap = context;
    void *p;

    if (++heap->calls == heap->fail) return NULL;
    p = malloc(bytes);
    if (p) heap->live++;
    return p;
}

/** @brief Count releases, including rejected candidate trees. */
static void
release(void *context, void *pointer)
{
    heap_t *heap = context;

    assert(heap->live);
    heap->live--;
    free(pointer);
}

/** @brief Attempt add, move, remove or text insertion with bookmark mapping. */
static tiku_rich_status_t
attempt(tiku_richdoc_t *doc, unsigned action)
{
    tiku_rich_pos_t at = point(doc, "Point");
    tiku_rich_edit_t op = {.kind = TIKU_RICH_REPLACE,
        .paragraph = at.paragraph, .text = "abc", .length = 3};
    tiku_rich_transaction_t tx = {0, &op, 1};

    if (action == 0) return tiku_richdoc_bookmark_set(doc, 0, "Other", at);
    if (action == 1) {
        at.affinity = TIKU_RICH_BEFORE;
        return tiku_richdoc_bookmark_set(doc, 0, "Point", at);
    }
    if (action == 2) return tiku_richdoc_bookmark_remove(doc, 0, "Point");
    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Exhaust each allocation refusal without changing roots, redo or bytes. */
static void
failures(void)
{
    heap_t heap;
    tiku_richdoc_t *doc;
    tiku_rd_root_t *root;
    tiku_rich_limits_t limits = {.allocator = {&heap, allocate, release}};
    tiku_rich_pos_t at;
    tiku_rich_status_t status;
    size_t bytes, history, nth, failures = 0;
    uint64_t generation;

    for (unsigned action = 0; action < 4; action++) {
        for (nth = 1; nth < 100; nth++) {
            heap = (heap_t){0};
            assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
            at = (tiku_rich_pos_t){doc->root->body,
                doc->root->paragraphs[0]->value.id, 0, TIKU_RICH_AFTER};
            assert(tiku_richdoc_bookmark_set(doc, 0, "Point", at) == TIKU_RICH_OK);
            assert(tiku_richdoc_bookmark_set(doc, 0, "Redo", at) == TIKU_RICH_OK);
            assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
            root = doc->root; generation = doc->generation;
            bytes = tiku_richdoc_memory(doc);
            history = tiku_richdoc_history_memory(doc);
            heap.fail = heap.calls + nth;
            status = attempt(doc, action);
            heap.fail = 0;
            if (status != TIKU_RICH_OK) {
                assert(status == TIKU_RICH_NO_MEMORY);
                assert(doc->root == root && doc->generation == generation);
                assert(tiku_richdoc_memory(doc) == bytes);
                assert(tiku_richdoc_history_memory(doc) == history);
                same(point(doc, "Point"), at);
                assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
                same(point(doc, "Redo"), at);
                failures++;
            }
            tiku_richdoc_release(doc);
            assert(!heap.live);
            if (status == TIKU_RICH_OK) break;
        }
        assert(nth < 100 && nth > 1);
    }
    printf("Bookmarks: %zu atomic allocation refusals PASS\n", failures);
}

/** @brief Coalesced typing maps a point once per edit and restores group ends. */
static void
typing(void)
{
    tiku_richdoc_t *doc;
    tiku_richsession_t *session;
    tiku_rich_pos_t a, b;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    tiku_richsession_selection(session, &a, &b);
    b.affinity = TIKU_RICH_AFTER;
    assert(tiku_richdoc_bookmark_set(doc, 0, "Point", b) == TIKU_RICH_OK);
    tiku_richdoc_clear_history(doc);
    for (size_t i = 0; i < 12; i++) {
        assert(tiku_richsession_type(session, 0, "x", 1) == TIKU_RICH_OK);
        assert(point(doc, "Point").byte == i + 1);
    }
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    same(point(doc, "Point"), b);
    assert(tiku_richdoc_undo(doc) != TIKU_RICH_OK);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    assert(point(doc, "Point").byte == 12);
    tiku_richsession_destroy(session);
    tiku_richdoc_release(doc);
}

/** @brief Run persistent point and target-resolution regressions. */
int
main(void)
{
    names(); mapping(); tables(); imports(); failures(); typing();
    puts("Bookmarks: names, 200 mapping/history cases, tables and limits PASS");
    return 0;
}
