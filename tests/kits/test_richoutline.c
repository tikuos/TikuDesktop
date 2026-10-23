/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richoutline.c - semantic headings, source ownership and scan refusals.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richoutline.h"
#include "tiku_richsession.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Count allocation ownership and refuse a selected allocation call. */
typedef struct { size_t calls, fail, live; } heap_t;

/** @brief Allocate counted storage unless this is the injected refusal. */
static void *
allocate(void *context, size_t bytes)
{
    heap_t *heap = context;
    void *pointer;

    if (++heap->calls == heap->fail) return NULL;
    pointer = malloc(bytes);
    if (pointer) heap->live++;
    return pointer;
}

/** @brief Balance exactly one successful allocation. */
static void
release(void *context, void *pointer)
{
    heap_t *heap = context;

    assert(pointer && heap->live);
    heap->live--;
    free(pointer);
}

/** @brief Build headings with inheritance, explicit clearing and a nested cell. */
static tiku_richdoc_t *
document(const tiku_rich_limits_t *limits)
{
    tiku_rich_style_source_t styles[] = {
        {.name = "Section", .paragraph = {.present = TIKU_RICH_OUTLINE, .outline = 2}},
        {.name = "Custom", .parent = 1},
        {.name = "Heading 1"}
    };
    tiku_rich_paragraph_source_t p[5] = {
        {.text = "One", .length = 3, .style = 2},
        {.text = "Body", .length = 4, .style = 2,
         .paragraph = {.present = TIKU_RICH_OUTLINE, .outline = 0}},
        {.text = "Not a heading", .length = 13, .style = 3},
        {.text = "\xe5\xad\x90", .length = 3,
         .paragraph = {.present = TIKU_RICH_OUTLINE, .outline = 6}},
        {.paragraph = {.present = TIKU_RICH_OUTLINE, .outline = 1}}
    };
    tiku_rich_block_source_t blocks[] = {
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = 0},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = 1},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = 2},
        {.kind = TIKU_RICH_BLOCK_TABLE, .table = {.rows = 1, .columns = 1}},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 4},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 5, .paragraph = 3},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = 4}
    };
    tiku_rich_source_t source = {.styles = styles, .style_count = 3,
        .paragraphs = p, .paragraph_count = 5, .blocks = blocks, .block_count = 7};
    tiku_richdoc_t *doc;

    assert(tiku_richdoc_create(limits, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Incremental indexing, strict neighboring positions and owned lifetime. */
static void
semantics(void)
{
    tiku_richdoc_t *doc = document(NULL);
    tiku_richsnapshot_t *snapshot;
    tiku_richoutline_t *outline;
    tiku_rich_heading_t entry;
    size_t index;
    uint64_t generation = tiku_richdoc_generation(doc);

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    {
        tiku_rich_paragraph_t first, body;
        tiku_rich_selection_t selected;
        uint64_t id = tiku_richsnapshot_body(snapshot);

        assert(tiku_richsnapshot_paragraph(snapshot, 0, &first) == TIKU_RICH_OK);
        assert(tiku_richsnapshot_paragraph(snapshot, 1, &body) == TIKU_RICH_OK);
        assert(tiku_richsnapshot_selection(snapshot,
            (tiku_rich_pos_t){id, first.id, 0, TIKU_RICH_AFTER},
            (tiku_rich_pos_t){id, body.id, body.length, TIKU_RICH_AFTER},
            &selected) == TIKU_RICH_OK);
        assert(selected.mixed_props & TIKU_RICH_OUTLINE);
    }
    assert(tiku_richoutline_create(snapshot, &outline) == TIKU_RICH_OK);
    assert(tiku_richoutline_generation(outline) == generation);
    assert(tiku_richoutline_count(outline) == 0);
    assert(tiku_richoutline_step(outline, 0) == TIKU_RICH_PENDING);
    assert(tiku_richoutline_step(outline, 1) == TIKU_RICH_PENDING);
    assert(tiku_richoutline_count(outline) == 1);
    assert(tiku_richoutline_get(outline, 0, &entry) == TIKU_RICH_OK);
    assert(entry.level == 2 && entry.paragraph_index == 0 && entry.length == 3);
    assert(!memcmp(entry.text, "One", 3));
    assert(tiku_richoutline_neighbor(outline, entry.position, 1, &index) ==
           TIKU_RICH_PENDING && index == SIZE_MAX);
    assert(tiku_richoutline_step(outline, 2) == TIKU_RICH_PENDING);
    assert(tiku_richoutline_count(outline) == 1);
    assert(tiku_richoutline_step(outline, 1) == TIKU_RICH_PENDING);
    assert(tiku_richoutline_get(outline, 1, &entry) == TIKU_RICH_OK);
    assert(entry.level == 6 && entry.paragraph_index == 3 && entry.length == 3);
    assert(tiku_richoutline_step(outline, 1) == TIKU_RICH_OK);
    assert(tiku_richoutline_count(outline) == 3);
    for (size_t i = 0; i < 3; i++) {
        assert(tiku_richoutline_get(outline, i, &entry) == TIKU_RICH_OK);
        assert(tiku_richsnapshot_position(snapshot, entry.position) == TIKU_RICH_OK);
        assert(tiku_richoutline_neighbor(outline, entry.position, 1, &index) ==
               (i < 2 ? TIKU_RICH_OK : TIKU_RICH_NOT_FOUND));
        assert(index == (i < 2 ? i + 1 : SIZE_MAX));
        assert(tiku_richoutline_neighbor(outline, entry.position, 0, &index) ==
               (i ? TIKU_RICH_OK : TIKU_RICH_NOT_FOUND));
        assert(index == (i ? i - 1 : SIZE_MAX));
        if (entry.length) {
            entry.position.byte = entry.length;
            assert(tiku_richoutline_neighbor(outline, entry.position, 0, &index) ==
                   TIKU_RICH_OK && index == i);
        }
    }
    entry.position.body++;
    assert(tiku_richoutline_neighbor(outline, entry.position, 0, &index) ==
           TIKU_RICH_INVALID_RANGE && index == SIZE_MAX);
    assert(tiku_richoutline_neighbor(outline, entry.position, 2, &index) ==
           TIKU_RICH_INVALID);
    assert(tiku_richoutline_get(outline, 99, &entry) == TIKU_RICH_INVALID_RANGE);
    assert(!entry.text && !entry.level);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    assert(tiku_richoutline_step(outline, 10) == TIKU_RICH_OK);
    assert(tiku_richoutline_get(outline, 1, &entry) == TIKU_RICH_OK);
    assert(!memcmp(entry.text, "\xe5\xad\x90", 3));
    tiku_richoutline_release(outline);
    assert(tiku_richoutline_create(NULL, &outline) == TIKU_RICH_INVALID && !outline);
    assert(tiku_richoutline_step(NULL, 1) == TIKU_RICH_INVALID);
    tiku_richoutline_release(NULL);
}

/** @brief Property changes and mixed-format queries use the normal history path. */
static void
editing(void)
{
    tiku_richdoc_t *doc = document(NULL);
    tiku_richsession_t *session;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_PARAGRAPH_FORMAT,
        .value.paragraph.values = {.present = TIKU_RICH_OUTLINE, .outline = 4}};
    tiku_rich_selection_t selected;
    tiku_richsnapshot_t *snapshot;
    tiku_richoutline_t *old, *fresh;
    tiku_rich_heading_t entry;

    assert(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richoutline_create(snapshot, &old) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_OK);
    assert(tiku_richsession_format(session, &selected) == TIKU_RICH_OK);
    assert(selected.props.outline == 4);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richsession_format(session, &selected) == TIKU_RICH_OK);
    assert(selected.props.outline == 2);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richoutline_create(snapshot, &fresh) == TIKU_RICH_OK);
    assert(tiku_richoutline_step(fresh, SIZE_MAX) == TIKU_RICH_OK);
    assert(tiku_richoutline_step(old, SIZE_MAX) == TIKU_RICH_OK);
    assert(tiku_richoutline_get(old, 0, &entry) == TIKU_RICH_OK && entry.level == 2);
    assert(tiku_richoutline_get(fresh, 0, &entry) == TIKU_RICH_OK && entry.level == 4);
    command.value.paragraph.values.outline = 7;
    uint64_t generation = tiku_richdoc_generation(doc);
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_INVALID_STYLE);
    assert(generation == tiku_richdoc_generation(doc));
    command.value.paragraph.values.outline = 0;
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_OK);
    assert(tiku_richsession_format(session, &selected) == TIKU_RICH_OK);
    assert(!selected.props.outline && (selected.props.present & TIKU_RICH_OUTLINE));
    command.value.paragraph.values.present = 0;
    command.value.paragraph.clear_props = TIKU_RICH_OUTLINE;
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_OK);
    assert(tiku_richsession_format(session, &selected) == TIKU_RICH_OK);
    assert(selected.props.outline == 2);
    tiku_richsnapshot_release(snapshot);
    tiku_richoutline_release(old); tiku_richoutline_release(fresh);
    tiku_richsession_destroy(session); tiku_richdoc_release(doc);
}

/** @brief Every index allocation can fail or be cancelled without retained bytes. */
static void
faults(void)
{
    heap_t heap = {0};
    tiku_rich_limits_t limits = {.allocator = {&heap, allocate, release}};
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_source_t paragraphs[70];
    tiku_rich_source_t source = {.paragraphs = paragraphs, .paragraph_count = 70};
    tiku_richsnapshot_t *snapshot;
    size_t baseline, allocations;

    for (size_t i = 0; i < 70; i++) paragraphs[i] =
        (tiku_rich_paragraph_source_t){.text = "H", .length = 1,
        .paragraph = {.present = TIKU_RICH_OUTLINE, .outline = (unsigned char)(i % 6 + 1)}};
    assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    baseline = tiku_richdoc_memory(doc);
    for (size_t fail = 1; fail <= 6; fail++) {
        tiku_richoutline_t *outline = (void *)1;
        tiku_rich_status_t status;

        heap.fail = heap.calls + fail;
        status = tiku_richoutline_create(snapshot, &outline);
        if (status == TIKU_RICH_OK) status = tiku_richoutline_step(outline, SIZE_MAX);
        assert(status == (fail <= 5 ? TIKU_RICH_NO_MEMORY : TIKU_RICH_OK));
        if (outline) assert(tiku_richoutline_step(outline, 1) == status);
        tiku_richoutline_release(outline);
        assert(tiku_richdoc_memory(doc) == baseline);
    }
    heap.fail = 0;
    allocations = heap.live;
    for (size_t cancel = 0; cancel <= 70; cancel++) {
        tiku_richoutline_t *outline;

        assert(tiku_richoutline_create(snapshot, &outline) == TIKU_RICH_OK);
        assert(tiku_richoutline_step(outline, cancel) ==
               (cancel < 70 ? TIKU_RICH_PENDING : TIKU_RICH_OK));
        assert(tiku_richoutline_count(outline) == cancel);
        tiku_richoutline_release(outline);
        assert(tiku_richdoc_memory(doc) == baseline && heap.live == allocations);
    }
    tiku_richsnapshot_release(snapshot); tiku_richdoc_release(doc);
    assert(!heap.live);
}

/** @brief Exercise semantic heading scans and allocation/ownership contracts. */
int
main(void)
{
    semantics(); editing(); faults();
    puts("richoutline: semantics, history, navigation, 5 refusals, 71 cancellations passed");
    return 0;
}
