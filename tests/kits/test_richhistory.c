/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richhistory.c - shared-storage history accounting and bounded retention.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Independently recorded allocator blocks and root membership masks. */
typedef struct {
    void *pointer;
    size_t bytes;
    uint64_t mask;
} allocation_t;

/** @brief Test allocation registry, exact live bytes and one refusal index. */
typedef struct {
    allocation_t blocks[8192];
    size_t used, calls, fail;
} heap_t;

/** @brief Record real allocation extents without reading kit headers. */
static void *
allocate(void *context, size_t bytes)
{
    heap_t *heap = context;
    size_t i;
    void *pointer;

    if (++heap->calls == heap->fail) return NULL;
    pointer = malloc(bytes);
    assert(pointer);
    for (i = 0; i < 8192 && heap->blocks[i].pointer; i++) {}
    assert(i < 8192);
    heap->blocks[i] = (allocation_t){pointer, bytes, 0};
    heap->used += bytes;
    return pointer;
}

/** @brief Remove an exact allocation, detecting unknown or repeated frees. */
static void
release(void *context, void *pointer)
{
    heap_t *heap = context;
    size_t i;

    for (i = 0; i < 8192 && heap->blocks[i].pointer != pointer; i++) {}
    assert(i < 8192);
    heap->used -= heap->blocks[i].bytes;
    heap->blocks[i] = (allocation_t){0};
    free(pointer);
}

/** @brief Mark the allocation containing a public or private interior pointer. */
static void
mark(heap_t *heap, const void *pointer, uint64_t mask)
{
    uintptr_t address = (uintptr_t)pointer, first;
    size_t i;

    if (!pointer) return;
    for (i = 0; i < 8192; i++) {
        first = (uintptr_t)heap->blocks[i].pointer;
        if (first && address >= first &&
            address - first < heap->blocks[i].bytes) {
            heap->blocks[i].mask |= mask;
            return;
        }
    }
    assert(!"unknown allocation in history oracle");
}

/** @brief Mark reachable storage independently of the kit's census helpers. */
static void
mark_root(heap_t *heap, const tiku_rd_root_t *root, uint64_t mask)
{
    const tiku_rich_paragraph_t *p;
    const tiku_rich_image_t *image;
    size_t i, j;

    mark(heap, root, mask);
    mark(heap, root->paragraphs, mask);
    mark(heap, root->styles, mask);
    mark(heap, root->blocks, mask);
    mark(heap, root->bookmarks, mask);
    for (i = 0; i < root->count; i++) {
        mark(heap, root->paragraphs[i], mask);
        p = &root->paragraphs[i]->value;
        mark(heap, p->text, mask);
        mark(heap, p->runs, mask);
        for (j = 0; j < p->run_count; j++) {
            mark(heap, p->runs[j].link, mask);
            image = p->runs[j].image;
            mark(heap, image, mask);
            if (image) {
                mark(heap, image->pixels, mask);
                mark(heap, image->alt, mask);
            }
        }
    }
}

/** @brief Enumerate history roots and metadata with distinct membership bits. */
static unsigned
mark_stack(heap_t *heap, const tiku_rd_history_t *entry, unsigned bit)
{
    while (entry) {
        assert(bit < 63);
        mark_root(heap, entry->root, UINT64_C(1) << bit++);
        mark(heap, entry, UINT64_C(1) << 63);
        mark(heap, entry->maps, UINT64_C(1) << 63);
        mark(heap, entry->bindings, UINT64_C(1) << 63);
        entry = entry->next;
    }
    return bit;
}

/** @brief Compare unique-union and worst-state charges to allocator evidence. */
static void
oracle(tiku_richdoc_t *doc, heap_t *heap)
{
    size_t roots[63] = {0}, total = 0, minimum = SIZE_MAX, i;
    unsigned bit, count;

    assert(tiku_richdoc_memory(doc) == heap->used);
    if (!doc->undo && !doc->redo) {
        assert(!tiku_richdoc_history_memory(doc));
        assert(!tiku_richdoc_history_retained_memory(doc));
        return;
    }
    for (i = 0; i < 8192; i++) heap->blocks[i].mask = 0;
    mark_root(heap, doc->root, 1);
    count = mark_stack(heap, doc->undo, 1);
    count = mark_stack(heap, doc->redo, count);
    for (i = 0; i < 8192; i++) {
        if (heap->blocks[i].mask) total += heap->blocks[i].bytes;
        for (bit = 0; bit < count; bit++)
            if (heap->blocks[i].mask & (UINT64_C(1) << bit))
                roots[bit] += heap->blocks[i].bytes;
    }
    for (bit = 0; bit < count; bit++)
        if (roots[bit] < minimum) minimum = roots[bit];
    assert(tiku_richdoc_history_memory(doc) == total - minimum);
    assert(tiku_richdoc_history_retained_memory(doc) == total - roots[0]);
    assert(tiku_richdoc_history_memory(doc) <= doc->limits.history_bytes);
}

/** @brief Create a tracked document with text in a small table. */
static tiku_richdoc_t *
fixture(heap_t *heap)
{
    tiku_richdoc_t *doc;
    tiku_rich_limits_t limits = {.allocator = {heap, allocate, release}};
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_INSERT,
        .table = {.rows = 2, .columns = 2, .padding = 3}};
    tiku_rich_transaction_t tx = {.edits = &edit, .count = 1};

    assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    edit.paragraph = doc->root->paragraphs[0]->value.id;
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    edit = (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
        .paragraph = doc->root->paragraphs[1]->value.id,
        .text = "abcdefgh", .length = 8};
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    tiku_richdoc_clear_history(doc);
    return doc;
}

/** @brief Publish one edit and compare both memory counters. */
static void
apply(tiku_richdoc_t *doc, heap_t *heap, tiku_rich_edit_t edit)
{
    tiku_rich_transaction_t tx = {.edits = &edit, .count = 1};

    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    oracle(doc, heap);
}

/** @brief Removing any additional oldest group would discard usable history. */
static void
maximal(tiku_richdoc_t *doc, heap_t *heap, tiku_richsnapshot_t **states,
        size_t edits)
{
    const tiku_rd_history_t *at;
    size_t count = 0, bytes = 0, minimum = SIZE_MAX, root_bytes, i;
    unsigned bit;

    for (at = doc->undo; at; at = at->next) count++;
    if (count == edits) return;
    assert(count < edits && count < 62);
    for (i = 0; i < 8192; i++) heap->blocks[i].mask = 0;
    mark_root(heap, doc->root, 1);
    mark_stack(heap, doc->undo, 1);
    mark_root(heap, states[edits - count - 1]->root, UINT64_C(1) << (count + 1));
    for (i = 0; i < 8192; i++)
        if (heap->blocks[i].mask) bytes += heap->blocks[i].bytes;
    for (bit = 0; bit <= count + 1; bit++) {
        root_bytes = 0;
        for (i = 0; i < 8192; i++)
            if (heap->blocks[i].mask & (UINT64_C(1) << bit))
                root_bytes += heap->blocks[i].bytes;
        if (root_bytes < minimum) minimum = root_bytes;
    }
    bytes += tiku_rd_size(doc->undo) + tiku_rd_size(doc->undo->maps);
    assert(bytes - minimum > doc->limits.history_bytes);
}

/** @brief Large unchanged paragraphs do not consume each edit's budget. */
static void
shared_document(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_source_t paragraphs[256] = {0};
    tiku_rich_source_t source = {.paragraphs = paragraphs,
        .paragraph_count = 256};
    tiku_rich_limits_t limits = {.history_bytes = 96 * 1024};
    char text[4096];
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_REPLACE,
        .text = "x", .length = 1};
    tiku_rich_transaction_t tx = {.edits = &edit, .count = 1};
    size_t i;

    memset(text, 'a', sizeof text);
    for (i = 0; i < 256; i++) {
        paragraphs[i].text = text;
        paragraphs[i].length = sizeof text;
    }
    assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    for (i = 0; i < 10; i++) {
        edit.paragraph = doc->root->paragraphs[i]->value.id;
        assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
        assert(tiku_richdoc_history_memory(doc) <= limits.history_bytes);
    }
    for (i = 0; i < 10; i++)
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_NO_CHANGE);
    tiku_richdoc_release(doc);
}

/** @brief Shared targets, image pixels, structures and styles count once. */
static void
resources(void)
{
    heap_t *heap = calloc(1, sizeof *heap);
    tiku_richdoc_t *doc = fixture(heap);
    const uint32_t pixels[] = {0xffff0000, 0xff00ff00, 0xff0000ff, 0};
    tiku_rich_image_t image = {2, 2, pixels, 24, 24, "picture"};
    tiku_rich_style_spec_t style = {.name = "Body",
        .kind = TIKU_RICH_PARAGRAPH_STYLE};
    uint64_t paragraph = doc->root->paragraphs[1]->value.id;
    size_t bytes, reserve, calls;

    assert(tiku_richdoc_bookmark_set(doc, 0, "Table", (tiku_rich_pos_t){
        doc->root->body, paragraph, 4, TIKU_RICH_AFTER}) == TIKU_RICH_OK);
    oracle(doc, heap);
    apply(doc, heap, (tiku_rich_edit_t){.kind = TIKU_RICH_LINK_SET,
        .paragraph = paragraph, .end = 8, .link = "https://example.org"});
    apply(doc, heap, (tiku_rich_edit_t){.kind = TIKU_RICH_FORMAT,
        .paragraph = paragraph, .start = 2, .end = 4,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}});
    assert(doc->root->paragraphs[1]->value.run_count == 3);
    assert(doc->root->paragraphs[1]->value.runs[0].link ==
           doc->root->paragraphs[1]->value.runs[2].link);
    apply(doc, heap, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_INSERT,
        .paragraph = paragraph, .start = 8, .end = 8, .image = &image});
    image = *doc->root->paragraphs[1]->value.runs[3].image;
    image.width = 48;
    apply(doc, heap, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_SET,
        .paragraph = paragraph, .start = 8, .end = 11, .image = &image});
    assert(doc->root->paragraphs[1]->value.runs[3].image->pixels == image.pixels);
    apply(doc, heap, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_INSERT,
        .paragraph = paragraph, .start = 11, .end = 11, .image = &image});
    assert(doc->root->paragraphs[1]->value.runs[4].image->pixels != image.pixels);
    apply(doc, heap, (tiku_rich_edit_t){.kind = TIKU_RICH_STYLE_DEFINE,
        .definition = &style});
    apply(doc, heap, (tiku_rich_edit_t){.kind = TIKU_RICH_CELL_SET,
        .object = doc->root->blocks->nodes[2].id, .background = 0xffabcdef});
    reserve = tiku_richdoc_history_memory(doc);
    calls = heap->calls;
    while (tiku_richdoc_undo(doc) == TIKU_RICH_OK) {
        oracle(doc, heap);
        assert(tiku_richdoc_history_memory(doc) == reserve);
    }
    while (tiku_richdoc_redo(doc) == TIKU_RICH_OK) oracle(doc, heap);
    assert(heap->calls == calls);
    bytes = heap->used - tiku_richdoc_history_retained_memory(doc);
    tiku_richdoc_clear_history(doc);
    assert(heap->used == bytes);
    oracle(doc, heap);
    tiku_richdoc_release(doc);
    assert(!heap->used);
    free(heap);
}

/** @brief Check every state after bounded trimming, branching and coalescing. */
static void
eviction(void)
{
    heap_t *heap = calloc(1, sizeof *heap);
    tiku_richdoc_t *doc = fixture(heap);
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_REPLACE,
        .paragraph = doc->root->paragraphs[1]->value.id,
        .text = "z", .length = 1};
    tiku_richsession_t *session;
    tiku_richsnapshot_t *states[41];
    size_t i, count = 0, calls;

    assert(tiku_richdoc_snapshot(doc, &states[0]) == TIKU_RICH_OK);
    apply(doc, heap, edit);
    doc->limits.history_bytes = tiku_richdoc_history_memory(doc) * 4;
    for (i = 0; i < 40; i++) {
        assert(tiku_richdoc_snapshot(doc, &states[i + 1]) == TIKU_RICH_OK);
        apply(doc, heap, edit);
        maximal(doc, heap, states, i + 2);
    }
    for (i = 0; i < 41; i++) tiku_richsnapshot_release(states[i]);
    calls = heap->calls;
    while (tiku_richdoc_undo(doc) == TIKU_RICH_OK) {
        count++;
        oracle(doc, heap);
    }
    assert(count > 1 && count < 41 && heap->calls == calls);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    oracle(doc, heap);
    apply(doc, heap, edit);
    assert(!tiku_richdoc_can_redo(doc));
    doc->limits.history_bytes = 1024 * 1024;
    assert(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    for (i = 0; i < 20; i++) {
        assert(tiku_richsession_type(session, 0, "q", 1) == TIKU_RICH_OK);
        oracle(doc, heap);
    }
    assert(tiku_richsession_destroy(session) == TIKU_RICH_OK);
    oracle(doc, heap);
    tiku_richdoc_release(doc);
    assert(!heap->used);
    free(heap);
}

/** @brief Budget refusal preserves history, snapshots, IDs and exact bytes. */
static void
boundaries(void)
{
    heap_t *heap = calloc(1, sizeof *heap);
    tiku_richdoc_t *doc = fixture(heap);
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_REPLACE,
        .paragraph = doc->root->paragraphs[1]->value.id,
        .text = "more", .length = 4};
    tiku_rich_transaction_t tx = {.edits = &edit, .count = 1};
    tiku_richsnapshot_t *snapshot;
    size_t reserve, used, retained;
    uint64_t generation;

    apply(doc, heap, edit);
    reserve = tiku_richdoc_history_memory(doc);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_history_retained_memory(doc) == reserve);
    used = heap->used;
    generation = doc->generation;
    doc->limits.history_bytes = reserve - 1;
    for (size_t i = 0; i < 3; i++) {
        assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_LIMIT);
        assert(doc->generation == generation && heap->used == used);
        assert(doc->redo && !doc->undo);
        assert(tiku_richdoc_history_memory(doc) == reserve);
    }
    doc->limits.history_bytes = reserve;
    apply(doc, heap, edit);
    assert(tiku_richdoc_history_retained_memory(doc) < reserve);
    assert(!doc->redo);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    oracle(doc, heap);
    retained = tiku_richdoc_history_retained_memory(doc);
    used = heap->used;
    tiku_richdoc_clear_history(doc);
    assert(heap->used > used - retained);
    assert(tiku_richsnapshot_paragraphs(snapshot) == 6);
    tiku_richsnapshot_release(snapshot);
    oracle(doc, heap);
    tiku_richdoc_release(doc);
    assert(!heap->used);
    free(heap);
}

/** @brief Inject every allocation refusal and retry without losing redo. */
static void
failures(void)
{
    heap_t *heap;
    tiku_richdoc_t *doc;
    tiku_rich_edit_t edit;
    tiku_rich_transaction_t tx = {.edits = &edit, .count = 1};
    tiku_rich_status_t status;
    size_t nth, used, reserve;
    uint64_t generation, next;

    for (nth = 1; nth < 64; nth++) {
        heap = calloc(1, sizeof *heap);
        doc = fixture(heap);
        edit = (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = doc->root->paragraphs[1]->value.id,
            .text = "more", .length = 4};
        apply(doc, heap, edit);
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        used = heap->used;
        reserve = tiku_richdoc_history_memory(doc);
        generation = doc->generation;
        next = doc->next_id;
        heap->fail = heap->calls + nth;
        status = tiku_richdoc_apply(doc, &tx);
        if (status != TIKU_RICH_OK) {
            assert(status == TIKU_RICH_NO_MEMORY);
            assert(heap->used == used && doc->generation == generation);
            assert(doc->next_id == next && doc->redo && !doc->undo);
            assert(tiku_richdoc_history_memory(doc) == reserve);
            oracle(doc, heap);
            heap->fail = 0;
            apply(doc, heap, edit);
        } else oracle(doc, heap);
        tiku_richdoc_release(doc);
        assert(!heap->used);
        free(heap);
        if (status == TIKU_RICH_OK) break;
    }
    assert(nth > 5 && nth < 64);
    printf("rich history: %zu allocation refusals\n", nth - 1);
}

/** @brief Run the independent history-budget regressions. */
int
main(void)
{
    shared_document();
    resources();
    eviction();
    boundaries();
    failures();
    assert(!tiku_richdoc_history_retained_memory(NULL));
    puts("rich history: passed");
    return 0;
}
