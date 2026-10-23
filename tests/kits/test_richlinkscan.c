/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richlinkscan.c - hyperlink occurrences, boundaries and retained scans.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlinkscan.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Track scan ownership and refuse selected allocations. */
typedef struct { size_t live; int fail; } heap_t;
/** @brief Allocate counted memory unless failure is enabled. */
static void *
allocate(void *context, size_t bytes)
{
    heap_t *heap = context;
    void *pointer = heap->fail ? NULL : malloc(bytes);

    if (pointer) heap->live++;
    return pointer;
}
/** @brief Free exactly one tracked allocation. */
static void
release(void *context, void *pointer)
{
    heap_t *heap = context;

    assert(pointer && heap->live);
    heap->live--;
    free(pointer);
}

/** @brief Build repeated, adjacent, differently styled and table-cell links. */
static tiku_richdoc_t *
document(heap_t *heap)
{
    tiku_rich_limits_t limits = {.allocator = {heap, allocate, release}};
    tiku_richdoc_t *doc;
    tiku_rich_run_source_t runs[5] = {
        {.length = 1, .link = "https://example.org"},
        {.length = 1, .link = "https://example.org",
            .chars = {.present = TIKU_RICH_BOLD, .bold = 1}},
        {.length = 1}, {.length = 1, .link = "#here"},
        {.length = 1, .link = "mailto:reader@example.org"}};
    tiku_rich_run_source_t repeated = {.length = 2, .link = "https://example.org"};
    tiku_rich_run_source_t cell = {.length = 3, .link = "#here"};
    tiku_rich_paragraph_source_t p[] = {
        {.text = "ab cd", .length = 5, .runs = runs, .run_count = 5},
        {.text = "ef", .length = 2, .runs = &repeated, .run_count = 1},
        {0}, {.text = "ghi", .length = 3, .runs = &cell, .run_count = 1}};
    tiku_rich_block_source_t blocks[] = {
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = 0},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = 1},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = 2},
        {.kind = TIKU_RICH_BLOCK_TABLE, .table = {.rows = 1, .columns = 1}},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 4},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 5, .paragraph = 3}};
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 4,
        .blocks = blocks, .block_count = 6};

    assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Read a source-relative position without relying on generated IDs. */
static tiku_rich_pos_t
position(const tiku_richsnapshot_t *snapshot, size_t paragraph, size_t byte)
{
    tiku_rich_paragraph_t p;

    assert(tiku_richsnapshot_paragraph(snapshot, paragraph, &p) == TIKU_RICH_OK);
    return (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot), p.id, byte,
        TIKU_RICH_AFTER};
}

/** @brief Compare a bounded scan with one exact expected occurrence. */
static void
expect(const tiku_richsnapshot_t *snapshot, tiku_rich_pos_t from, int forward,
    size_t paragraph, size_t first, size_t last, const char *target)
{
    tiku_richlinkscan_t *scan;
    tiku_rich_link_match_t match;
    tiku_rich_status_t status;
    size_t steps = 0;

    assert(tiku_richlinkscan_create(snapshot, from, forward, &scan) == TIKU_RICH_OK);
    assert(tiku_richlinkscan_step(scan, 0) == TIKU_RICH_PENDING);
    assert(tiku_richlinkscan_result(scan, &match) == TIKU_RICH_PENDING && !match.target);
    do { status = tiku_richlinkscan_step(scan, 1); assert(++steps < 30); }
    while (status == TIKU_RICH_PENDING);
    assert(status == (target ? TIKU_RICH_OK : TIKU_RICH_NOT_FOUND));
    assert(tiku_richlinkscan_step(scan, 1) == status);
    assert(tiku_richlinkscan_result(scan, &match) == status);
    if (target) {
        assert(match.first.paragraph == position(snapshot, paragraph, first).paragraph);
        assert(match.first.byte == first && match.last.byte == last);
        assert(match.first.body == match.last.body && match.first.paragraph == match.last.paragraph);
        assert(match.generation == tiku_richsnapshot_generation(snapshot));
        assert(!strcmp(match.target, target));
    } else assert(!match.target && !match.generation);
    tiku_richlinkscan_release(scan);
}

/** @brief Check forward-inclusive and backward-exclusive occurrence starts. */
static void
navigation(void)
{
    heap_t heap = {0};
    tiku_richdoc_t *doc = document(&heap);
    tiku_richsnapshot_t *snapshot;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    expect(snapshot, position(snapshot, 0, 0), 1, 0, 0, 2, "https://example.org");
    expect(snapshot, position(snapshot, 0, 0), 0, 0, 0, 0, NULL);
    expect(snapshot, position(snapshot, 0, 1), 1, 0, 3, 4, "#here");
    expect(snapshot, position(snapshot, 0, 2), 0, 0, 0, 2, "https://example.org");
    expect(snapshot, position(snapshot, 0, 3), 1, 0, 3, 4, "#here");
    expect(snapshot, position(snapshot, 0, 4), 0, 0, 3, 4, "#here");
    expect(snapshot, position(snapshot, 0, 4), 1, 0, 4, 5, "mailto:reader@example.org");
    expect(snapshot, position(snapshot, 1, 0), 0, 0, 4, 5, "mailto:reader@example.org");
    expect(snapshot, position(snapshot, 1, 0), 1, 1, 0, 2, "https://example.org");
    expect(snapshot, position(snapshot, 1, 2), 1, 3, 0, 3, "#here");
    expect(snapshot, position(snapshot, 3, 3), 1, 0, 0, 0, NULL);
    expect(snapshot, position(snapshot, 3, 3), 0, 3, 0, 3, "#here");
    tiku_richsnapshot_release(snapshot); tiku_richdoc_release(doc);
    assert(!heap.live);
}

/** @brief Refuse allocation and invalid input, and cancel at every scan step. */
static void
ownership(void)
{
    heap_t heap = {0};
    tiku_richdoc_t *doc = document(&heap);
    tiku_richsnapshot_t *snapshot;
    tiku_richlinkscan_t *scan = (void *)1;
    tiku_rich_link_match_t match;
    tiku_rich_pos_t from;
    size_t baseline;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    from = position(snapshot, 3, 3);
    baseline = heap.live;
    heap.fail = 1;
    assert(tiku_richlinkscan_create(snapshot, from, 0, &scan) == TIKU_RICH_NO_MEMORY && !scan);
    heap.fail = 0;
    assert(tiku_richlinkscan_create(snapshot, from, 2, &scan) == TIKU_RICH_INVALID && !scan);
    for (size_t work = 0; work < 20; work++) {
        assert(tiku_richlinkscan_create(snapshot, from, 0, &scan) == TIKU_RICH_OK);
        tiku_richlinkscan_step(scan, work);
        tiku_richlinkscan_release(scan);
        assert(heap.live == baseline);
    }
    assert(tiku_richlinkscan_create(snapshot, from, 0, &scan) == TIKU_RICH_OK);
    tiku_rich_paragraph_t old;
    assert(tiku_richsnapshot_paragraph(snapshot, 3, &old) == TIKU_RICH_OK);
    tiku_rich_edit_t change = {.kind = TIKU_RICH_LINK_SET, .paragraph = old.id,
        .start = 0, .end = 3, .link = "#changed"};
    tiku_rich_transaction_t tx = {0, &change, 1};
    uint64_t generation = tiku_richsnapshot_generation(snapshot);
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot); tiku_richdoc_release(doc);
    heap.fail = 1;
    assert(tiku_richlinkscan_step(scan, 100) == TIKU_RICH_OK);
    assert(tiku_richlinkscan_result(scan, &match) == TIKU_RICH_OK);
    assert(!strcmp(match.target, "#here") && match.first.byte == 0 && match.last.byte == 3);
    assert(match.generation == generation);
    tiku_richlinkscan_release(scan);
    assert(!heap.live);
}

/** @brief Run hyperlink navigation and retained-lifetime regressions. */
int
main(void)
{
    navigation(); ownership();
    puts("richlinkscan: navigation, coalescing, cells, cancellation and ownership pass");
    return 0;
}
