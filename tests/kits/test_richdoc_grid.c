/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_grid.c - grid edits, independent cell oracles and anchor maps.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/** @brief Apply one primitive using the current generation. */
static tiku_rich_status_t
edit(tiku_richdoc_t *doc, tiku_rich_edit_t value)
{
    tiku_rich_transaction_t tx = {doc->generation, &value, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Make a table whose original cells contain distinct letters. */
static tiku_richdoc_t *
fixture(unsigned rows, unsigned columns)
{
    tiku_richdoc_t *doc;
    char text;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .table = {rows, columns, 0, 4, 1, 0xff707070}}) == TIKU_RICH_OK);
    for (size_t i = 1; i <= (size_t)rows * columns; i++) {
        text = 'A' + (i - 1) % 26;
        assert(edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = doc->root->paragraphs[i]->value.id,
            .text = &text, .length = 1}) == TIKU_RICH_OK);
    }
    tiku_richdoc_clear_history(doc);
    return doc;
}

/** @brief Current table identity in these single-table fixtures. */
static uint64_t
table(tiku_richdoc_t *doc)
{
    return doc->root->blocks->nodes[1].id;
}

/** @brief Expected original text and stable identity of a surviving cell. */
typedef struct {
    uint64_t paragraph, cell;
    char text;
} expected_t;

/** @brief Validate row-major identity, ownership and exact paragraph content. */
static void
verify(tiku_richdoc_t *doc, expected_t expected[256],
       unsigned rows, unsigned columns)
{
    tiku_rd_root_t *root = doc->root;
    const tiku_rich_block_t *grid = &root->blocks->nodes[1];

    assert(grid->table.rows == rows && grid->table.columns == columns);
    assert(root->count == (size_t)rows * columns + 2);
    assert(root->blocks->count == (size_t)rows * columns * 2 + 3);
    for (size_t i = 0; i < (size_t)rows * columns; i++) {
        const tiku_rich_block_t *cell = &root->blocks->nodes[2 + 2 * i];
        const tiku_rich_block_t *leaf = cell + 1;
        const tiku_rich_paragraph_t *p = &root->paragraphs[i + 1]->value;

        assert(cell->kind == TIKU_RICH_BLOCK_CELL && cell->parent == grid->id);
        assert(leaf->kind == TIKU_RICH_BLOCK_PARAGRAPH);
        assert(leaf->parent == cell->id && leaf->id == p->id);
        assert(p->length == (size_t)!!expected[i].text);
        if (p->length) assert(p->text[0] == expected[i].text);
        if (expected[i].paragraph) {
            assert(p->id == expected[i].paragraph);
            assert(cell->id == expected[i].cell);
        } else {
            expected[i].paragraph = p->id;
            expected[i].cell = cell->id;
        }
    }
}

/** @brief Generate a reproducible small-grid edit choice. */
static unsigned
random_value(unsigned *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}

/** @brief Compare 2000 edits and their undo/redo with an independent matrix. */
static void
generated(void)
{
    tiku_richdoc_t *doc = fixture(2, 3);
    expected_t current[256] = {{0}}, next[256];
    unsigned rows = 2, columns = 3, seed = 0x47298a5;

    for (size_t i = 0; i < 6; i++) current[i].text = 'A' + i;
    verify(doc, current, rows, columns);
    for (size_t trial = 0; trial < 2000; trial++) {
        int column = (random_value(&seed) >> 16) & 1;
        unsigned size = column ? columns : rows;
        int inserting = size == 1 ||
            (size < 16 && ((random_value(&seed) >> 16) & 1));
        unsigned index = random_value(&seed) % (size + !!inserting);
        unsigned new_rows = rows + (!column ? (inserting ? 1 : -1) : 0);
        unsigned new_cols = columns + (column ? (inserting ? 1 : -1) : 0);
        tiku_rich_edit_t change = {.kind = TIKU_RICH_TABLE_GRID,
            .object = table(doc), .grid = {.index = index,
                .operation = column ?
                    (inserting ? TIKU_RICH_COLUMN_INSERT :
                                 TIKU_RICH_COLUMN_REMOVE) :
                    (inserting ? TIKU_RICH_ROW_INSERT : TIKU_RICH_ROW_REMOVE)}};
        size_t out = 0;

        memset(next, 0, sizeof next);
        for (unsigned r = 0; r <= rows; r++) {
            if (!column && inserting && r == index) out += columns;
            if (r == rows || (!column && !inserting && r == index)) continue;
            for (unsigned c = 0; c <= columns; c++) {
                if (column && inserting && c == index) out++;
                if (c == columns || (column && !inserting && c == index))
                    continue;
                next[out++] = current[r * columns + c];
            }
        }
        assert(out == (size_t)new_rows * new_cols);
        assert(edit(doc, change) == TIKU_RICH_OK);
        verify(doc, next, new_rows, new_cols);
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        verify(doc, current, rows, columns);
        assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
        verify(doc, next, new_rows, new_cols);
        memcpy(current, next, sizeof current);
        rows = new_rows;
        columns = new_cols;
        tiku_richdoc_clear_history(doc);
    }
    tiku_richdoc_release(doc);
}

/** @brief Keep other cells' caret offsets and restore deleted caret bindings. */
static void
anchors(void)
{
    tiku_richdoc_t *doc = fixture(2, 3);
    tiku_richanchor_t *lost, *kept, *late;
    tiku_rich_pos_t a = {1, doc->root->paragraphs[2]->value.id, 1, 0};
    tiku_rich_pos_t b = {1, doc->root->paragraphs[3]->value.id, 1, 0}, p;
    tiku_richsnapshot_t *snapshot;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richanchor_create(doc, a, &lost) == TIKU_RICH_OK);
    assert(tiku_richanchor_create(doc, b, &kept) == TIKU_RICH_OK);
    assert(edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_GRID,
        .object = table(doc), .grid = {TIKU_RICH_COLUMN_REMOVE, 1}}) ==
           TIKU_RICH_OK);
    assert(tiku_richanchor_get(lost, &p) == TIKU_RICH_OK);
    assert(p.paragraph == b.paragraph && !p.byte);
    assert(tiku_richanchor_get(kept, &p) == TIKU_RICH_OK);
    assert(p.paragraph == b.paragraph && p.byte == 1);
    assert(snapshot->root->count == 8 && doc->root->count == 6);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richanchor_get(lost, &p) == TIKU_RICH_OK);
    assert(p.paragraph == a.paragraph && p.byte == 1);
    assert(edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_GRID,
        .object = table(doc), .grid = {TIKU_RICH_ROW_INSERT, 2}}) ==
           TIKU_RICH_OK);
    p = (tiku_rich_pos_t){1, doc->root->paragraphs[7]->value.id, 0, 0};
    assert(tiku_richanchor_create(doc, p, &late) == TIKU_RICH_OK);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richanchor_get(late, &p) == TIKU_RICH_OK);
    assert(p.paragraph == doc->root->paragraphs[4]->value.id && !p.byte);
    tiku_richanchor_release(lost);
    tiku_richanchor_release(kept);
    tiku_richanchor_release(late);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
}

/** @brief Nested subtrees survive insertion and leave with their outer row. */
static void
nested(void)
{
    tiku_richdoc_t *doc = fixture(2, 2);
    uint64_t cell = doc->root->paragraphs[1]->value.id, inner, leaf;
    tiku_richanchor_t *anchor;
    tiku_rich_pos_t p;

    assert(edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = cell, .table = {1, 1, 0, 2, 1, 0xff000000}}) ==
           TIKU_RICH_OK);
    leaf = doc->root->paragraphs[2]->value.id;
    inner = tiku_rd_table_for(doc->root, leaf);
    p = (tiku_rich_pos_t){1, leaf, 0, 0};
    assert(tiku_richanchor_create(doc, p, &anchor) == TIKU_RICH_OK);
    assert(edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_GRID,
        .object = table(doc), .grid = {TIKU_RICH_COLUMN_INSERT, 0}}) ==
           TIKU_RICH_OK);
    assert(tiku_rd_table_for(doc->root, leaf) == inner);
    assert(tiku_rd_grid_cell(doc->root, table(doc), leaf) == 1);
    assert(edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_GRID,
        .object = table(doc), .grid = {TIKU_RICH_ROW_REMOVE, 0}}) ==
           TIKU_RICH_OK);
    assert(tiku_rd_block_find(doc->root, inner) == SIZE_MAX);
    assert(tiku_richanchor_get(anchor, &p) == TIKU_RICH_OK);
    assert(p.paragraph == tiku_rd_grid_paragraph(doc->root, table(doc), 1));
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_rd_table_for(doc->root, leaf) == inner);
    assert(tiku_richanchor_get(anchor, &p) == TIKU_RICH_OK);
    assert(p.paragraph == leaf);
    tiku_richanchor_release(anchor);
    tiku_richdoc_release(doc);
}

/** @brief Commands select an inserted cell and preserve originating undo. */
static void
commands(void)
{
    tiku_richdoc_t *doc = fixture(2, 3);
    tiku_richsession_t *session;
    tiku_rich_pos_t start = {1, doc->root->paragraphs[5]->value.id, 1, 0};
    tiku_rich_pos_t a, b;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_ROW_AFTER};

    assert(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    assert(tiku_richsession_select(session, 0, start, start) == TIKU_RICH_OK);
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_OK);
    tiku_richsession_range(session, &a, &b);
    assert(a.paragraph == tiku_rd_grid_paragraph(doc->root, table(doc), 7));
    assert(a.paragraph == b.paragraph && !a.byte && !b.byte);
    command.kind = TIKU_RICHCMD_UNDO;
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_OK);
    tiku_richsession_range(session, &a, &b);
    assert(a.paragraph == start.paragraph && a.byte == 1);
    command.kind = TIKU_RICHCMD_COLUMN_BEFORE;
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_OK);
    tiku_richsession_range(session, &a, &b);
    assert(a.paragraph == tiku_rd_grid_paragraph(doc->root, table(doc), 5));
    command.kind = TIKU_RICHCMD_COLUMN_REMOVE;
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_OK);
    tiku_richsession_range(session, &a, &b);
    assert(a.paragraph == start.paragraph && !a.byte);
    tiku_richsession_destroy(session);
    tiku_richdoc_release(doc);
}

/** @brief Refuse invalid axes, last-axis removal and exhausted identities. */
static void
refusals(void)
{
    tiku_richdoc_t *doc = fixture(1, 1);
    tiku_rd_root_t *before = doc->root;
    tiku_rich_edit_t change = {.kind = TIKU_RICH_TABLE_GRID,
        .object = table(doc), .grid = {TIKU_RICH_ROW_REMOVE, 0}};
    tiku_richcmd_state_t state;
    tiku_richcmd_context_t context = {.first = {1,
        doc->root->paragraphs[1]->value.id, 0, 0}};

    context.last = context.first;
    assert(tiku_richcmd_query(doc, &context, TIKU_RICHCMD_ROW_REMOVE,
                             &state) == TIKU_RICH_OK && !state.enabled);
    assert(edit(doc, change) == TIKU_RICH_INVALID_RANGE);
    change.grid.operation = TIKU_RICH_COLUMN_REMOVE;
    assert(edit(doc, change) == TIKU_RICH_INVALID_RANGE);
    change.grid.operation = TIKU_RICH_COLUMN_INSERT;
    change.grid.index = 2;
    assert(edit(doc, change) == TIKU_RICH_INVALID_RANGE);
    change.grid.index = 0;
    change.grid.operation = (tiku_rich_grid_op_t)-1;
    assert(edit(doc, change) == TIKU_RICH_INVALID_RANGE);
    change.grid.operation = TIKU_RICH_ROW_INSERT;
    doc->next_id = UINT64_MAX;
    assert(edit(doc, change) == TIKU_RICH_LIMIT);
    assert(doc->root == before && doc->next_id == UINT64_MAX);
    tiku_richdoc_release(doc);
    doc = fixture(1, 64);
    change.object = table(doc);
    change.grid.operation = TIKU_RICH_COLUMN_INSERT;
    assert(edit(doc, change) == TIKU_RICH_LIMIT);
    tiku_richdoc_release(doc);
}

/** @brief Run grid operations without a native window or system fonts. */
int
main(void)
{
    generated();
    anchors();
    nested();
    commands();
    refusals();
    puts("Rich grid: 2000 matrix/undo oracles, nested cells, anchors, limits PASS");
    return 0;
}
