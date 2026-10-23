/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richcells.c - rectangle geometry, atomic edits and input ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Apply one primitive against the current generation. */
static tiku_rich_status_t
apply(tiku_richdoc_t *doc, tiku_rich_edit_t edit)
{
    tiku_rich_transaction_t tx = {doc->generation, &edit, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Create a labelled 3-by-4 table with deterministic geometry. */
static tiku_richdoc_t *
fixture(const tiku_rich_limits_t *limits)
{
    tiku_richdoc_t *doc;
    char text[8];

    assert(tiku_richdoc_create(limits, &doc) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .table = {.rows = 3, .columns = 4, .width = 240, .padding = 2,
                  .border = 1}}) == TIKU_RICH_OK);
    for (unsigned i = 0; i < 12; i++) {
        snprintf(text, sizeof text, "%u:%u", i / 4, i % 4);
        assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = doc->root->paragraphs[i + 1]->value.id,
            .text = text, .length = strlen(text)}) == TIKU_RICH_OK);
    }
    tiku_richdoc_clear_history(doc);
    return doc;
}

/** @brief Return a direct cell's stable identity. */
static uint64_t
cell(tiku_richdoc_t *doc, unsigned ordinal)
{
    return doc->root->blocks->nodes[2 + 2 * ordinal].id;
}

/** @brief Shape one byte with fixed metrics for independent geometry checks. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
    size_t size, const uint8_t *bounds, tiku_rich_shape_t *out)
{
    (void)ctx; (void)chars; (void)text; (void)size; (void)bounds;
    *out = (tiku_rich_shape_t){.bytes = 1, .advance = 6,
        .ascent = 8, .descent = 2, .caret_count = 2,
        .carets = {{0,0}, {1,6}}};
    return TIKU_RICH_OK;
}

/** @brief Complete queued layout within a finite work bound. */
static void
settle(tiku_richview_t *view)
{
    unsigned steps = 0;

    while (tiku_richview_step(view, 1) == TIKU_RICH_PENDING)
        assert(++steps < 20000);
    assert(tiku_richview_status(view) == TIKU_RICH_OK);
}

/** @brief Create a view with fixed ten-point text metrics. */
static tiku_richview_t *
view_for(tiku_richdoc_t *doc)
{
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = 240,
        .provider = {.shape = shape}};

    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    settle(view);
    return view;
}

/** @brief Check every ordered pair of endpoints against an independent matrix. */
static void
rectangles(void)
{
    tiku_richdoc_t *doc = fixture(NULL);
    tiku_richview_t *view = view_for(doc);
    tiku_richsnapshot_t *snapshot;
    tiku_rich_cells_t range;
    tiku_rich_block_t node;
    tiku_rich_rect_t boxes[3];
    size_t count;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    for (unsigned a = 0; a < 12; a++) for (unsigned b = 0; b < 12; b++) {
        unsigned top = a / 4 < b / 4 ? a / 4 : b / 4;
        unsigned left = a % 4 < b % 4 ? a % 4 : b % 4;
        unsigned bottom = a / 4 > b / 4 ? a / 4 : b / 4;
        unsigned right = a % 4 > b % 4 ? a % 4 : b % 4;

        assert(tiku_richsnapshot_cells(snapshot, cell(doc, a), cell(doc, b),
                                       &range) == TIKU_RICH_OK);
        assert(range.row == top && range.column == left &&
               range.rows == bottom - top + 1 && range.columns == right - left + 1);
        assert(tiku_richview_select_cells(view, 0, &range) == TIKU_RICH_OK);
        assert(tiku_richview_cells_rectangles(view, boxes, 3, &count) == TIKU_RICH_OK);
        assert(count == range.rows);
        for (unsigned row = 0; row < range.rows; row++) {
            assert(boxes[row].x == 60 * left && boxes[row].width == 60 * range.columns);
            assert(boxes[row].height == 16);
            for (unsigned col = 0; col < range.columns; col++) {
                assert(tiku_richsnapshot_cell_at(snapshot, &range,
                    row * range.columns + col, &node) == TIKU_RICH_OK);
                assert(node.id == cell(doc, (top + row) * 4 + left + col));
            }
        }
        assert(tiku_richview_cells_shade(view, 0xffaabbcc) == TIKU_RICH_OK);
        assert(tiku_richview_cells_shade(view, 0xffaabbcc) == TIKU_RICH_NO_CHANGE);
        for (unsigned i = 0; i < 12; i++) {
            int selected = i / 4 >= top && i / 4 <= bottom &&
                           i % 4 >= left && i % 4 <= right;

            assert(doc->root->blocks->nodes[2 + 2 * i].background ==
                   (selected ? 0xffaabbcc : 0));
        }
        assert(!snapshot->root->blocks->nodes[2].background);
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        assert(tiku_richview_cells(view, &range) == TIKU_RICH_NOT_FOUND);
        settle(view);
    }
    range = (tiku_rich_cells_t){doc->root->blocks->nodes[1].id, 0,0,3,4};
    assert(tiku_richview_select_cells(view, 0, &range) == TIKU_RICH_OK);
    boxes[0].x = 999;
    assert(tiku_richview_cells_rectangles(view, boxes, 1, &count) == TIKU_RICH_LIMIT);
    assert(count == 3 && boxes[0].x == 999);
    range.columns = UINT_MAX;
    assert(tiku_richview_select_cells(view, 0, &range) == TIKU_RICH_INVALID_RANGE);
    range.columns = 0;
    assert(tiku_richview_select_cells(view, 0, &range) == TIKU_RICH_INVALID_RANGE);
    assert(tiku_richview_select_cells(view, doc->generation - 1, NULL) == TIKU_RICH_STALE);
    tiku_richsnapshot_release(snapshot);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Check exact quoting, atomic clear, snapshots, nesting and media refusal. */
static void
contents(void)
{
    tiku_richdoc_t *doc = fixture(NULL);
    tiku_rich_cells_t range = {doc->root->blocks->nodes[1].id, 0,1,2,2};
    tiku_richsnapshot_t *snapshot;
    char text[128] = "untouched";
    size_t length;
    tiku_richanchor_t *anchor;
    tiku_rich_pos_t position = {doc->root->body,
        doc->root->paragraphs[2]->value.id, 2, TIKU_RICH_AFTER}, moved;
    const char *expected = "\"0:1\"\t\"0:2\"\n\"1:1\"\t\"1:2\"";

    tiku_richdoc_snapshot(doc, &snapshot);
    assert(tiku_richsnapshot_cells_text(snapshot, &range, NULL, 0, &length) == TIKU_RICH_OK);
    assert(length == strlen(expected));
    assert(tiku_richsnapshot_cells_text(snapshot, &range, text, 2, &length) == TIKU_RICH_LIMIT);
    assert(!strcmp(text, "untouched"));
    assert(tiku_richsnapshot_cells_text(snapshot, &range, text, sizeof text, &length) == TIKU_RICH_OK);
    assert(!strcmp(text, expected));
    assert(tiku_richanchor_create(doc, position, &anchor) == TIKU_RICH_OK);
    assert(tiku_richdoc_cells_clear(doc, doc->generation, &range) == TIKU_RICH_OK);
    assert(tiku_richdoc_cells_clear(doc, doc->generation, &range) == TIKU_RICH_NO_CHANGE);
    tiku_richanchor_get(anchor, &moved);
    assert(!moved.byte && moved.paragraph == position.paragraph);
    for (unsigned i = 0; i < 12; i++)
        assert(doc->root->paragraphs[i + 1]->value.length ==
               (i < 8 && i % 4 >= 1 && i % 4 <= 2 ? 0 : 3));
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    tiku_richanchor_get(anchor, &moved);
    assert(moved.byte == 2);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
        .paragraph = position.paragraph, .end = 3,
        .text = "a\"\tb\nc", .length = 6}) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_snapshot(doc, &snapshot);
    assert(tiku_richsnapshot_cells_text(snapshot, &range, text, sizeof text, &length) == TIKU_RICH_OK);
    assert(!strcmp(text, "\"a\"\"\tb\nc\"\t\"0:2\"\n\"1:1\"\t\"1:2\""));
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = position.paragraph, .table = {.rows = 1, .columns = 1}}) == TIKU_RICH_OK);
    {
        uint64_t inner = 0;
        tiku_richsnapshot_t now = {.root = doc->root};
        tiku_rich_cells_t invalid;

        for (size_t i = 0; i < doc->root->blocks->count; i++)
            if (doc->root->blocks->nodes[i].kind == TIKU_RICH_BLOCK_TABLE &&
                doc->root->blocks->nodes[i].id != range.table)
                inner = doc->root->blocks->nodes[i].id;
        assert(inner);
        size_t at = tiku_rd_block_find(doc->root, inner);
        assert(tiku_richsnapshot_cells(&now, cell(doc, 0),
            doc->root->blocks->nodes[at + 1].id, &invalid) == TIKU_RICH_INVALID_RANGE);
        assert(!invalid.table);
        assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_CELLS_SET,
            .cells = range, .background = 0xff123456}) == TIKU_RICH_OK);
        assert(!doc->root->blocks->nodes[at + 1].background);
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = tiku_rd_grid_paragraph(doc->root, inner, 0),
            .text = "nested", .length = 6}) == TIKU_RICH_OK);
    }
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_snapshot(doc, &snapshot);
    strcpy(text, "retained");
    assert(tiku_richsnapshot_cells_text(snapshot, &range, text, sizeof text, &length) == TIKU_RICH_UNSUPPORTED);
    assert(!strcmp(text, "retained"));
    assert(tiku_richdoc_cells_clear(doc, 0, &range) == TIKU_RICH_OK);
    for (size_t i = 0; i < doc->root->count; i++)
        assert(strcmp(doc->root->paragraphs[i]->value.text, "nested"));
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    uint32_t pixel = 0xffaa0000;
    tiku_rich_image_t image = {1,1,&pixel,8,8,"sample"};
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_INSERT,
        .paragraph = position.paragraph, .image = &image}) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_snapshot(doc, &snapshot);
    assert(tiku_richsnapshot_cells_text(snapshot, &range, text, sizeof text, &length) == TIKU_RICH_UNSUPPORTED);
    assert(!strcmp(text, "retained"));
    assert(tiku_richdoc_cells_clear(doc, 0, &range) == TIKU_RICH_OK);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[2]->value.runs[0].image);
    tiku_richanchor_release(anchor);
    tiku_richdoc_release(doc);
    assert(tiku_richsnapshot_cells_text(snapshot, &range, NULL, 0, &length) == TIKU_RICH_UNSUPPORTED);
    tiku_richsnapshot_release(snapshot);
}

/** @brief Deliver cell input and require the shared handler to own it. */
static tiku_rich_status_t
event(tiku_richview_t *view, int type, int x, int y, unsigned mods, unsigned key)
{
    tiku_event_t e = {.type = type, .x = x, .y = y, .modifiers = mods,
        .button = 1, .key = key};
    int handled;
    tiku_rich_status_t status = tiku_richview_event(view, &e, &handled);

    assert(handled);
    return status;
}

/** @brief Exercise pointer extension, independent views and edit guards. */
static void
interaction(void)
{
    tiku_richdoc_t *doc = fixture(NULL);
    tiku_richview_t *view = view_for(doc), *other = view_for(doc);
    tiku_rich_cells_t range;
    tiku_rich_pos_t first, last;
    tiku_rich_rect_t rows[3];
    size_t count;
    uint64_t generation = doc->generation;

    assert(event(view, TIKU_EVENT_POINTER_DOWN, 65, 15, TIKU_MOD_OPTION, 0) == TIKU_RICH_OK);
    assert(event(view, TIKU_EVENT_POINTER_MOVE, 185, 50, 0, 0) == TIKU_RICH_OK);
    assert(event(view, TIKU_EVENT_POINTER_UP, 185, 50, 0, 0) == TIKU_RICH_NO_CHANGE);
    assert(tiku_richview_cells(view, &range) == TIKU_RICH_OK);
    assert(range.row == 0 && range.column == 1 && range.rows == 3 && range.columns == 3);
    assert(tiku_richview_cells(other, &range) == TIKU_RICH_NOT_FOUND);
    assert(doc->generation == generation);
    doc->initiator = other->session;
    assert(tiku_richview_select_all(view) == TIKU_RICH_BUSY);
    assert(tiku_richview_cells(view, &range) == TIKU_RICH_OK);
    doc->initiator = NULL;
    {
        tiku_richcmd_t command = {.kind = TIKU_RICHCMD_BOLD};
        tiku_richcmd_state_t state;

        assert(tiku_richview_query(view, command.kind, &state) == TIKU_RICH_OK);
        assert(!state.enabled && state.status == TIKU_RICH_UNSUPPORTED);
        assert(tiku_richview_execute(view, 0, &command) == TIKU_RICH_UNSUPPORTED);
        assert(tiku_richview_execute(view, generation - 1, &command) == TIKU_RICH_STALE);
        assert(tiku_richview_query(view, TIKU_RICHCMD_DELETE, &state) == TIKU_RICH_OK);
        assert(state.enabled);
        assert(tiku_richview_query(view, TIKU_RICHCMD_CELL_SET, &state) == TIKU_RICH_OK);
        assert(state.enabled);
    }
    assert(event(view, TIKU_EVENT_KEY_DOWN, 0, 0, TIKU_MOD_CMD, 'b') == TIKU_RICH_UNSUPPORTED);
    assert(event(view, TIKU_EVENT_KEY_DOWN, 0, 0, 0, TIKU_KEY_RETURN) == TIKU_RICH_UNSUPPORTED);
    assert(tiku_richview_cells(view, &range) == TIKU_RICH_OK);
    assert(tiku_richview_select_cells(other, 0, &range) == TIKU_RICH_OK);
    tiku_richsession_set_read_only(view->session, 1);
    assert(tiku_richview_cells_clear(view) == TIKU_RICH_READ_ONLY);
    assert(tiku_richview_cells_shade(view, 1) == TIKU_RICH_READ_ONLY);
    tiku_richsession_set_read_only(view->session, 0);
    assert(tiku_richview_cells_shade(view, 0xff112233) == TIKU_RICH_OK);
    assert(tiku_richview_cells(other, &range) == TIKU_RICH_NOT_FOUND);
    assert(tiku_richview_cells(view, &range) == TIKU_RICH_OK);
    tiku_richview_viewport(view, 480, 60, 2);
    settle(view);
    tiku_richview_scroll(view, 12);
    assert(tiku_richview_cells_rectangles(view, rows, 3, &count) == TIKU_RICH_OK);
    assert(rows[0].x == 60 && rows[0].width == 180 && count == 3);
    assert(event(view, TIKU_EVENT_POINTER_DOWN, 10, 2, TIKU_MOD_SHIFT, 0) == TIKU_RICH_OK);
    assert(event(view, TIKU_EVENT_POINTER_UP, 10, 2, 0, 0) == TIKU_RICH_NO_CHANGE);
    assert(tiku_richview_cells(view, &range) == TIKU_RICH_OK && range.columns == 2);
    assert(event(view, TIKU_EVENT_KEY_DOWN, 0, 0, 0, TIKU_KEY_ESCAPE) == TIKU_RICH_OK);
    assert(tiku_richview_cells(view, &range) == TIKU_RICH_NOT_FOUND);
    range = (tiku_rich_cells_t){doc->root->blocks->nodes[1].id, 0,0,1,1};
    assert(tiku_richview_select_cells(view, 0, &range) == TIKU_RICH_OK);
    tiku_richsession_selection(view->session, &first, &last);
    first.byte = last.byte = 1;
    assert(tiku_richsession_select(view->session, 0, first, last) == TIKU_RICH_OK);
    assert(tiku_richview_cells(view, &range) == TIKU_RICH_NOT_FOUND);
    tiku_richview_destroy(view);
    tiku_richview_destroy(other);
    tiku_richdoc_release(doc);
}

/** @brief Reject oversized clears while supporting shading across 4096 cells. */
static void
large(void)
{
    tiku_rich_paragraph_source_t *p = calloc(4097, sizeof *p);
    tiku_rich_block_source_t *b = calloc(8194, sizeof *b);
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 4097,
        .blocks = b, .block_count = 8194};
    tiku_richdoc_t *doc;
    tiku_rich_cells_t cells;
    uint64_t generation;
    size_t memory;

    assert(p && b);
    b[0].kind = TIKU_RICH_BLOCK_PARAGRAPH;
    b[1] = (tiku_rich_block_source_t){.kind = TIKU_RICH_BLOCK_TABLE,
        .table = {.rows = 64, .columns = 64}};
    for (size_t i = 0; i < 4096; i++) {
        p[i + 1] = (tiku_rich_paragraph_source_t){.text = "x", .length = 1};
        b[2 + 2 * i] = (tiku_rich_block_source_t){.kind = TIKU_RICH_BLOCK_CELL,
            .parent = 2};
        b[3 + 2 * i] = (tiku_rich_block_source_t){
            .kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 3 + 2 * i,
            .paragraph = i + 1};
    }
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    free(p); free(b);
    cells = (tiku_rich_cells_t){doc->root->blocks->nodes[1].id, 0,0,64,64};
    generation = doc->generation;
    memory = tiku_richdoc_memory(doc);
    assert(tiku_richdoc_cells_clear(doc, 0, &cells) == TIKU_RICH_LIMIT);
    assert(doc->generation == generation && tiku_richdoc_memory(doc) == memory);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_CELLS_SET,
        .cells = cells, .background = 0xffabcdef}) == TIKU_RICH_OK);
    for (size_t i = 0; i < 4096; i++)
        assert(doc->root->blocks->nodes[2 + 2 * i].background == 0xffabcdef);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    cells.rows = 16;
    assert(tiku_richdoc_cells_clear(doc, 0, &cells) == TIKU_RICH_OK);
    assert(doc->root->bytes == 3072);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(doc->root->bytes == 4096);
    tiku_richdoc_release(doc);
}

/** @brief Allocation failure injector with exact live allocation accounting. */
typedef struct { size_t call, fail, live; } faults_t;
/** @brief Fail a selected allocation and count successful reservations. */
static void *
allocate(void *ctx, size_t size)
{
    faults_t *f = ctx;
    void *p;

    if (++f->call == f->fail) return NULL;
    p = malloc(size);
    assert(p);
    f->live++;
    return p;
}
/** @brief Return a counted reservation. */
static void
release(void *ctx, void *p)
{
    faults_t *f = ctx;

    assert(f->live);
    f->live--;
    free(p);
}

/** @brief Refuse each edit allocation without changing document or selection. */
static void
failures(void)
{
    size_t failures = 0;

    for (int clear = 0; clear <= 1; clear++) {
        faults_t f = {0};
        tiku_rich_limits_t limits = {.allocator = {&f, allocate, release}};
        tiku_richdoc_t *doc = fixture(&limits);
        tiku_richview_t *view = view_for(doc);
        tiku_rich_cells_t cells = {doc->root->blocks->nodes[1].id, 0,0,3,4}, read;
        uint64_t generation = doc->generation;
        size_t memory, live, nth;

        assert(tiku_richview_select_cells(view, 0, &cells) == TIKU_RICH_OK);
        memory = tiku_richdoc_memory(doc);
        live = f.live;
        for (nth = 1; nth < 200; nth++) {
            tiku_rich_status_t status;

            f.fail = f.call + nth;
            status = clear ? tiku_richview_cells_clear(view) :
                             tiku_richview_cells_shade(view, 0xffabcdef);
            if (status == TIKU_RICH_OK) break;
            assert(status == TIKU_RICH_NO_MEMORY);
            assert(doc->generation == generation);
            assert(tiku_richdoc_memory(doc) == memory && f.live == live);
            assert(tiku_richview_cells(view, &read) == TIKU_RICH_OK);
            assert(read.table == cells.table && read.rows == 3 && read.columns == 4);
            failures++;
        }
        assert(nth > 1 && nth < 200);
        f.fail = 0;
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        tiku_richview_destroy(view);
        tiku_richdoc_release(doc);
        assert(!f.live);
    }
    printf("Cell rectangles: %zu allocation refusals PASS\n", failures);
}

/** @brief Run rectangular selection, text export, atomic editing and input. */
int
main(void)
{
    rectangles();
    contents();
    interaction();
    large();
    failures();
    puts("Cell rectangles: 144 endpoint oracles, copy, clear and input PASS");
    return 0;
}
