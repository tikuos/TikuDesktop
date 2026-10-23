/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_cell_drag.c - cell shading and captured column-divider edits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include "tiku_richview.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

/** @brief Apply a single primitive to the current document. */
static tiku_rich_status_t
apply(tiku_richdoc_t *doc, tiku_rich_edit_t edit)
{
    tiku_rich_transaction_t tx = {doc->generation, &edit, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Create a two-row, three-column table with ten-point cell insets. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_richdoc_t *doc;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .table = {2, 3, 300, 4, 1, 0xff707070}}) == TIKU_RICH_OK);
    return doc;
}

/** @brief Provide fixed metrics for deterministic empty-cell geometry. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
      size_t length, const uint8_t *boundaries, tiku_rich_shape_t *out)
{
    (void)ctx; (void)chars; (void)text; (void)length; (void)boundaries;
    *out = (tiku_rich_shape_t){.bytes = 1, .advance = 6,
        .ascent = 8, .descent = 2, .caret_count = 2,
        .carets = {{0, 0}, {1, 6}}};
    return TIKU_RICH_OK;
}

/** @brief Finish incremental work with a bounded number of iterations. */
static void
settle(tiku_richview_t *view)
{
    unsigned steps = 0;

    while (tiku_richview_step(view, 1) == TIKU_RICH_PENDING)
        assert(++steps < 10000);
    assert(tiku_richview_status(view) == TIKU_RICH_OK);
}

/** @brief Create a completed layout and independently owned view session. */
static tiku_richview_t *
view_for(tiku_richdoc_t *doc)
{
    tiku_richlayout_options_t options = {.width = 300,
        .provider = {.shape = shape}};
    tiku_richview_t *view;

    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    settle(view);
    return view;
}

/** @brief Deliver a primary-pointer event in viewport-local pixels. */
static tiku_rich_status_t
pointer(tiku_richview_t *view, int type, int x, int y)
{
    tiku_event_t event = {.type = type, .button = 1, .x = x, .y = y};
    int handled;

    return tiku_richview_event(view, &event, &handled);
}

/** @brief Keep fills with their cells through history and grid changes. */
static void
shading(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *old, *snapshot;
    tiku_rich_block_t cell;
    uint64_t paragraph = doc->root->paragraphs[1]->value.id;
    tiku_richcmd_context_t context = {.first = {doc->root->body,
        paragraph, 0, 0}};
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_CELL_SET,
        .value.color = 0x8044aaff};
    tiku_rich_edit_t grid = {.kind = TIKU_RICH_TABLE_GRID,
        .object = doc->root->blocks->nodes[1].id,
        .grid = {TIKU_RICH_COLUMN_INSERT, 0}};
    uint64_t generation;

    context.last = context.first;
    assert(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    assert(tiku_richcmd_execute(doc, &context, &command) == TIKU_RICH_OK);
    generation = doc->generation;
    assert(tiku_richcmd_execute(doc, &context, &command) == TIKU_RICH_NO_CHANGE);
    assert(doc->generation == generation);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_cell(snapshot, paragraph, &cell) == TIKU_RICH_OK);
    assert(cell.background == command.value.color);
    assert(tiku_richsnapshot_cell(old, paragraph, &cell) == TIKU_RICH_OK);
    assert(!cell.background);
    assert(apply(doc, grid) == TIKU_RICH_OK);
    assert(!doc->root->blocks->nodes[2].background);
    assert(doc->root->blocks->nodes[4].background == command.value.color);
    grid.grid = (typeof(grid.grid)){TIKU_RICH_COLUMN_REMOVE, 1};
    assert(apply(doc, grid) == TIKU_RICH_OK);
    assert(tiku_rd_find(doc->root, paragraph) == SIZE_MAX);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[4].background == command.value.color);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(!doc->root->blocks->nodes[2].background);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[2].background == command.value.color);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_CELL_SET,
        .object = grid.object, .background = 1}) == TIKU_RICH_INVALID_RANGE);
    assert(tiku_richsnapshot_cell(snapshot, grid.object, &cell) ==
           TIKU_RICH_INVALID_RANGE && !cell.id);
    tiku_richsnapshot_release(snapshot);
    tiku_richsnapshot_release(old);
    tiku_richdoc_release(doc);
}

/** @brief Compare 100 drag results at different zooms and scroll positions. */
static void
drags(void)
{
    for (unsigned i = 0; i < 100; i++) {
        tiku_richdoc_t *doc = fixture();
        tiku_richview_t *view = view_for(doc);
        tiku_rich_divider_t hit;
        tiku_rich_pos_t before, after, anchor;
        tiku_rich_grid_box_t cell;
        unsigned column = i % 2;
        double zoom = i % 3 == 0 ? 0.5 : i % 3 == 1 ? 1 : 2;
        double edge = 100 * (column + 1);
        int target = (int)((100 * column +
                          (i < 70 ? 30 + i : 50 + i)) * zoom);
        double expected = target / zoom;
        uint64_t generation = doc->generation;

        assert(tiku_richview_viewport(view, 300 * zoom, 20 * zoom, zoom) ==
               TIKU_RICH_OK);
        settle(view);
        assert(tiku_richview_scroll(view, 5) == TIKU_RICH_OK);
        assert(tiku_richview_divider_at(view, edge * zoom, 10 * zoom, &hit) ==
               TIKU_RICH_OK && hit.column == column);
        assert(fabs(hit.minimum - (11 + 100 * column)) < 0.001);
        assert(fabs(hit.maximum - (189 + 100 * column)) < 0.001);
        tiku_richsession_selection(tiku_richview_session(view), &anchor,
                                    &before);
        assert(pointer(view, TIKU_EVENT_POINTER_DOWN,
            (int)(edge * zoom), (int)(10 * zoom)) == TIKU_RICH_OK);
        assert(pointer(view, TIKU_EVENT_POINTER_MOVE,
            target, (int)(10 * zoom)) == TIKU_RICH_OK);
        assert(tiku_richview_divider(view, &hit) == TIKU_RICH_OK);
        assert(fabs(hit.line.x - expected) < 0.001);
        assert(doc->generation == generation &&
               !doc->root->blocks->nodes[1].table.weights[0]);
        assert(pointer(view, TIKU_EVENT_POINTER_UP,
            target, (int)(10 * zoom)) == TIKU_RICH_OK);
        assert(doc->generation == generation + 1);
        assert(doc->root->blocks->nodes[1].table.weights[column ? 0 : 2] == 1);
        settle(view);
        assert(tiku_richlayout_grid(tiku_richview_layout(view),
            1 + column, &cell) == TIKU_RICH_OK &&
            fabs(cell.box.width - (expected - 100 * column)) < 0.001);
        tiku_richsession_selection(tiku_richview_session(view), &anchor,
                                    &after);
        assert(before.paragraph == after.paragraph && before.byte == after.byte);
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        assert(!doc->root->blocks->nodes[1].table.weights[0]);
        assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
        assert(doc->root->blocks->nodes[1].table.weights[column ? 0 : 2] == 1);
        tiku_richview_destroy(view);
        tiku_richdoc_release(doc);
    }
}

/** @brief Refuse stale, read-only and failed edits and cancel interrupted drags. */
static void
cancellation(void)
{
    for (unsigned i = 0; i < 8; i++) {
        tiku_richdoc_t *doc = fixture();
        tiku_richview_t *view = view_for(doc);
        tiku_richsession_t *session = tiku_richview_session(view);
        tiku_rich_divider_t hit;
        tiku_event_t event = {.type = TIKU_EVENT_KEY_DOWN,
            .key = TIKU_KEY_ESCAPE};
        uint64_t generation = doc->generation;
        size_t memory_limit = doc->memory->limit;
        int handled;

        assert(pointer(view, TIKU_EVENT_POINTER_DOWN, 102, 15) == TIKU_RICH_OK);
        assert(pointer(view, TIKU_EVENT_POINTER_MOVE, 142, 15) == TIKU_RICH_OK);
        assert(tiku_richview_divider(view, &hit) == TIKU_RICH_OK);
        assert(hit.line.x == 140);
        if (i == 0) {
            assert(tiku_richview_event(view, &event, &handled) == TIKU_RICH_OK);
            assert(handled);
        } else if (i == 1) {
            event.type = TIKU_EVENT_DEACTIVATED;
            tiku_richview_event(view, &event, &handled);
        } else if (i == 2) {
            tiku_richview_viewport(view, 320, 400, 1);
            settle(view);
        } else if (i == 3) tiku_richview_scroll(view, 1);
        else if (i == 4) {
            tiku_rich_pos_t position = {doc->root->body,
                doc->root->paragraphs[1]->value.id, 0, 0};

            tiku_richsession_select(session, 0, position, position);
        } else if (i == 5) tiku_richsession_set_read_only(session, 1);
        else if (i == 6) {
            tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_SET,
                .object = doc->root->blocks->nodes[1].id,
                .table = doc->root->blocks->nodes[1].table};

            edit.table.width = 320;
            assert(apply(doc, edit) == TIKU_RICH_OK);
            generation = doc->generation;
            settle(view);
        } else doc->memory->limit = tiku_richdoc_memory(doc);
        if (i == 7) assert(pointer(view, TIKU_EVENT_POINTER_UP, 142, 15) ==
                           TIKU_RICH_LIMIT);
        else (void)pointer(view, TIKU_EVENT_POINTER_UP, 142, 15);
        doc->memory->limit = memory_limit;
        assert(doc->generation == generation &&
               !doc->root->blocks->nodes[1].table.weights[0]);
        assert(tiku_richview_divider(view, &hit) == TIKU_RICH_NOT_FOUND);
        tiku_richview_destroy(view);
        tiku_richdoc_release(doc);
    }
}

/** @brief Clamp against pictures in any row and locate nested dividers. */
static void
constraints(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richview_t *view;
    uint32_t pixel = 0xff000000;
    tiku_rich_image_t image = {1, 1, &pixel, 80, 12, ""};
    tiku_rich_divider_t hit;
    tiku_rich_grid_box_t box;
    uint64_t generation;

    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_INSERT,
        .paragraph = doc->root->paragraphs[4]->value.id,
        .image = &image}) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[2]->value.id,
        .table = {1, 2, 60, 0, 0, 0}}) == TIKU_RICH_OK);
    view = view_for(doc);
    assert(tiku_richview_divider_at(view, 100, 15, &hit) == TIKU_RICH_OK);
    assert(hit.minimum == 90 && hit.maximum == 130);
    assert(pointer(view, TIKU_EVENT_POINTER_DOWN, 100, 15) == TIKU_RICH_OK);
    assert(pointer(view, TIKU_EVENT_POINTER_MOVE, -500, 15) == TIKU_RICH_OK);
    assert(tiku_richview_divider(view, &hit) == TIKU_RICH_OK);
    assert(hit.line.x == 90);
    assert(pointer(view, TIKU_EVENT_POINTER_UP, -500, 15) == TIKU_RICH_OK);
    settle(view);
    for (size_t i = 0; i < tiku_richlayout_grids(tiku_richview_layout(view));
         i++) {
        tiku_richlayout_grid(tiku_richview_layout(view), i, &box);
        if (box.kind != TIKU_RICH_BLOCK_TABLE || !box.depth) continue;
        assert(tiku_richview_divider_at(view, box.box.x + 30,
            box.box.y + 2, &hit) == TIKU_RICH_OK && hit.table == box.id);
    }
    assert(tiku_richview_divider_at(view, 300, 15, &hit) ==
           TIKU_RICH_NOT_FOUND && !hit.table);
    assert(tiku_richview_divider_at(view, NAN, 15, &hit) == TIKU_RICH_INVALID);
    generation = doc->generation;
    assert(pointer(view, TIKU_EVENT_POINTER_DOWN, 90, 15) == TIKU_RICH_OK);
    assert(pointer(view, TIKU_EVENT_POINTER_UP, 90, 15) == TIKU_RICH_NO_CHANGE);
    assert(doc->generation == generation);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Run ownership, geometry, cancellation and resource-refusal checks. */
int
main(void)
{
    shading();
    drags();
    cancellation();
    constraints();
    puts("Cell shading and divider resizing: 100 drag oracles, undo, bounds PASS");
    return 0;
}
