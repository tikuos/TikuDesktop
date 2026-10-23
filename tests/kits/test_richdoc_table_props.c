/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_table_props.c - proportional columns, geometry and history.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include "tiku_richview.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/** @brief Apply one edit to the current document generation. */
static tiku_rich_status_t
apply(tiku_richdoc_t *doc, tiku_rich_edit_t edit)
{
    tiku_rich_transaction_t tx = {doc->generation, &edit, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Create a two-by-two table whose first column is one quarter wide. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_richdoc_t *doc;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .table = {2, 2, 0, 4, 1, 0xff707070, {1, 3}}}) == TIKU_RICH_OK);
    return doc;
}

/** @brief Reject incomplete weights, nonfinite values and inactive metadata. */
static void
validation(void)
{
    tiku_rich_table_t table = {.rows = 2, .columns = 2};

    assert(tiku_rich_table_validate(&table) == TIKU_RICH_OK);
    assert(tiku_rich_table_fraction(&table, 0) == 0.5);
    assert(!tiku_rich_table_fraction(&table, 2));
    table.weights[0] = 1;
    assert(tiku_rich_table_validate(&table) == TIKU_RICH_INVALID);
    table.weights[1] = 3;
    assert(tiku_rich_table_fraction(&table, 0) == 0.25);
    table.weights[2] = 1;
    assert(tiku_rich_table_validate(&table) == TIKU_RICH_INVALID);
    table.weights[2] = 0;
    for (unsigned i = 0; i < 64; i++) {
        float saved = table.weights[i];

        table.weights[i] = NAN;
        assert(tiku_rich_table_validate(&table) == TIKU_RICH_INVALID);
        table.weights[i] = INFINITY;
        assert(tiku_rich_table_validate(&table) == TIKU_RICH_INVALID);
        table.weights[i] = -1;
        assert(tiku_rich_table_validate(&table) == TIKU_RICH_INVALID);
        table.weights[i] = saved;
    }
    table.weights[0] = 1001;
    assert(!tiku_rich_table_fraction(&table, 0));
    table.weights[0] = 0.0001f;
    assert(tiku_rich_table_validate(&table) == TIKU_RICH_INVALID);
    table.weights[0] = 0;
    assert(tiku_rich_table_validate(&table) == TIKU_RICH_INVALID);
}

/** @brief Preserve identities, selection and held snapshots through property undo. */
static void
properties(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *old;
    tiku_rich_block_t node = doc->root->blocks->nodes[1], found;
    tiku_richcmd_context_t context = {.first = {doc->root->body,
        doc->root->paragraphs[1]->value.id, 0, 0}};
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_TABLE_SET,
        .value.table = {2, 2, 300, 6, 2, 0xff224466, {3, 1}}};
    tiku_rich_edit_t edits[2] = {{.kind = TIKU_RICH_TABLE_SET,
        .object = node.id, .table = node.table}, {.kind = TIKU_RICH_TABLE_SET,
        .object = UINT64_MAX, .table = node.table}};
    tiku_rich_transaction_t tx = {0, edits, 2};
    uint64_t generation;
    unsigned column;

    context.last = context.first;
    assert(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_table(old, context.first.paragraph, &found,
                                   &column) == TIKU_RICH_OK);
    assert(found.id == node.id && column == 0);
    assert(tiku_richsnapshot_table(old, doc->root->paragraphs[4]->value.id,
                                   &found, &column) == TIKU_RICH_OK);
    assert(column == 1);
    assert(tiku_richsnapshot_table(old, doc->root->paragraphs[0]->value.id,
                                   &found, &column) == TIKU_RICH_INVALID_RANGE);
    assert(!found.id && !column);
    assert(tiku_richcmd_execute(doc, &context, &command) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[1].id == node.id);
    assert(doc->root->blocks->nodes[1].table.weights[0] == 3);
    assert(doc->root->blocks->nodes[1].table.color == 0xff224466);
    assert(old->root->blocks->nodes[1].table.weights[0] == 1);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[1].table.weights[0] == 1);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    generation = doc->generation;
    assert(tiku_richcmd_execute(doc, &context, &command) == TIKU_RICH_NO_CHANGE);
    assert(doc->generation == generation);
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_INVALID_RANGE);
    assert(doc->generation == generation);
    assert(doc->root->blocks->nodes[1].table.weights[0] == 3);
    command.value.table.rows = 3;
    assert(tiku_richcmd_execute(doc, &context, &command) ==
           TIKU_RICH_INVALID_RANGE);
    context.generation = generation - 1;
    assert(tiku_richcmd_execute(doc, &context, &command) == TIKU_RICH_STALE);
    tiku_richsnapshot_release(old);
    tiku_richdoc_release(doc);
}

/** @brief Shift weights with columns, preserve them with rows and restore history. */
static void
grid(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_GRID,
        .object = doc->root->blocks->nodes[1].id,
        .grid = {TIKU_RICH_COLUMN_INSERT, 1}};
    const tiku_rich_table_t *table;

    assert(apply(doc, edit) == TIKU_RICH_OK);
    table = &doc->root->blocks->nodes[1].table;
    assert(table->columns == 3 && table->weights[0] == 1 &&
           table->weights[1] == 2 && table->weights[2] == 3);
    edit.grid.operation = TIKU_RICH_ROW_INSERT;
    assert(apply(doc, edit) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[1].table.weights[1] == 2);
    edit.grid.operation = TIKU_RICH_COLUMN_REMOVE;
    assert(apply(doc, edit) == TIKU_RICH_OK);
    table = &doc->root->blocks->nodes[1].table;
    assert(table->columns == 2 && table->weights[0] == 1 &&
           table->weights[1] == 3 && !table->weights[2]);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[1].table.weights[1] == 2);
    edit.grid.index = 2;
    assert(apply(doc, edit) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[1].table.weights[1] == 2);
    assert(!doc->root->blocks->nodes[1].table.weights[2]);
    tiku_richdoc_release(doc);
}

/** @brief Supply fixed metrics for deterministic empty-cell layout. */
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

/** @brief Compare weighted grid geometry over many widths and ratios. */
static void
geometry(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = 400,
        .provider = {.shape = shape}};
    tiku_rich_grid_box_t first, second, outer;
    tiku_rich_pos_t hit;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_SET,
        .object = doc->root->blocks->nodes[1].id,
        .table = doc->root->blocks->nodes[1].table};

    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    for (unsigned i = 1; i <= 200; i++) {
        double fraction = (double)i / (i + 201);
        double width = fmax(100 + i, 11 / fraction);
        const tiku_richlayout_t *layout;

        edit.table.weights[0] = (float)i;
        edit.table.weights[1] = 201;
        assert(apply(doc, edit) == TIKU_RICH_OK);
        assert(tiku_richview_viewport(view, 100 + i, 500, 1) == TIKU_RICH_OK);
        while (tiku_richview_step(view, 1) == TIKU_RICH_PENDING) { }
        assert(tiku_richview_status(view) == TIKU_RICH_OK);
        layout = tiku_richview_layout(view);
        assert(tiku_richlayout_grid(layout, 0, &outer) == TIKU_RICH_OK);
        assert(tiku_richlayout_grid(layout, 1, &first) == TIKU_RICH_OK);
        assert(tiku_richlayout_grid(layout, 2, &second) == TIKU_RICH_OK);
        assert(fabs(outer.box.width - width) < 0.001);
        assert(fabs(first.box.width - width * fraction) < 0.001);
        assert(fabs(second.box.x - first.box.width) < 0.001);
        assert(fabs(second.box.width + first.box.width - width) < 0.001);
        assert(tiku_richlayout_hit(layout, first.box.width + 5,
            first.box.y + 5, &hit) == TIKU_RICH_OK);
        assert(hit.paragraph == doc->root->paragraphs[2]->value.id);
    }
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Pictures and nested tables expand the column that contains them. */
static void
minimum(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = 200,
        .provider = {.shape = shape}};
    uint32_t pixel = 0xff445566;
    tiku_rich_image_t image = {.pixels = &pixel,
        .pixel_width = 1, .pixel_height = 1, .width = 100, .height = 10};
    tiku_rich_grid_box_t box;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_block_t table;
    unsigned column;

    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_INSERT,
        .paragraph = doc->root->paragraphs[1]->value.id,
        .image = &image}) == TIKU_RICH_OK);
    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    while (tiku_richview_step(view, 1) == TIKU_RICH_PENDING) { }
    assert(tiku_richview_status(view) == TIKU_RICH_OK);
    assert(tiku_richlayout_grid(tiku_richview_layout(view), 0, &box) ==
           TIKU_RICH_OK && box.box.width == 440);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[1]->value.id,
        .table = {1, 2, 150, 4, 1, 0xff000000, {2, 1}}}) == TIKU_RICH_OK);
    while (tiku_richview_step(view, 1) == TIKU_RICH_PENDING) { }
    assert(tiku_richview_status(view) == TIKU_RICH_OK);
    assert(tiku_richlayout_grid(tiku_richview_layout(view), 0, &box) ==
           TIKU_RICH_OK && box.box.width == 640);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_table(snapshot, doc->root->paragraphs[3]->value.id,
        &table, &column) == TIKU_RICH_OK);
    assert(table.table.width == 150 && column == 1);
    assert(tiku_richsnapshot_table(snapshot, table.id, &table, &column) ==
           TIKU_RICH_INVALID_RANGE);
    assert(!table.id && !column);
    tiku_richsnapshot_release(snapshot);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Run table-property validation, history and independent layout checks. */
int
main(void)
{
    validation();
    properties();
    grid();
    geometry();
    minimum();
    puts("Table properties: validation, history, weights, 200 layouts PASS");
    return 0;
}
