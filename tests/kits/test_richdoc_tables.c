/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_tables.c - table ownership, structural boundaries and history.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include "tiku_richview.h"
#include "tiku_richpages.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Apply one structural edit with a fresh generation. */
static tiku_rich_status_t
apply(tiku_richdoc_t *doc, tiku_rich_edit_t edit)
{
    tiku_rich_transaction_t tx = {doc->generation, &edit, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Build a two-by-two table after the initial paragraph. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_richdoc_t *doc;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .table = {2, 2, 0, 4, 1, 0xff000000}}) == TIKU_RICH_OK);
    return doc;
}

/** @brief Validate the index against the hierarchy after each operation. */
static void
consistent(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *s;
    tiku_rich_block_source_t *source;
    tiku_rich_block_t node, parent;
    tiku_rich_source_t input = {0};
    size_t p = 0;

    assert(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    input.block_count = tiku_richsnapshot_blocks(s);
    input.paragraph_count = tiku_richsnapshot_paragraphs(s);
    source = calloc(input.block_count, sizeof *source);
    assert(source);
    input.blocks = source;
    for (size_t i = 0; i < input.block_count; i++) {
        assert(tiku_richsnapshot_block(s, i, &node) == TIKU_RICH_OK);
        source[i].kind = node.kind;
        source[i].table = node.table;
        if (node.kind == TIKU_RICH_BLOCK_PARAGRAPH) {
            assert(node.id == doc->root->paragraphs[p]->value.id);
            source[i].paragraph = p++;
            assert(tiku_richsnapshot_container(s, node.id) == node.parent);
        }
        for (size_t j = 0; j < i; j++) {
            tiku_richsnapshot_block(s, j, &parent);
            assert(parent.id != node.id);
            if (parent.id == node.parent) source[i].parent = j + 1;
        }
        assert(source[i].parent || node.parent == doc->root->body);
    }
    assert(p == input.paragraph_count);
    assert(tiku_rd_blocks_validate(&input) == TIKU_RICH_OK);
    free(source);
    tiku_richsnapshot_release(s);
}

/** @brief Keep cell edits bounded while preserving snapshots and undo. */
static void
editing(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *old;
    tiku_richanchor_t *anchor;
    tiku_rich_pos_t pos;
    uint64_t cell = doc->root->paragraphs[1]->value.id;
    uint64_t table = doc->root->blocks->nodes[1].id;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_REPLACE,
        .paragraph = cell, .text = "abcd", .length = 4};

    assert(doc->root->count == 6 && doc->root->blocks->count == 11);
    consistent(doc);
    assert(apply(doc, edit) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_JOIN,
        .paragraph = cell}) == TIKU_RICH_INVALID_RANGE);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_SPLIT,
        .paragraph = cell, .start = 2, .end = 2}) == TIKU_RICH_OK);
    consistent(doc);
    assert(doc->root->count == 7 && old->root->count == 6);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_JOIN,
        .paragraph = cell}) == TIKU_RICH_OK);
    consistent(doc);
    assert(!strcmp(doc->root->paragraphs[1]->value.text, "abcd"));
    pos = (tiku_rich_pos_t){doc->root->body, cell, 2, TIKU_RICH_AFTER};
    assert(tiku_richanchor_create(doc, pos, &anchor) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_REMOVE,
        .object = table}) == TIKU_RICH_OK);
    consistent(doc);
    assert(doc->root->count == 2 && old->root->count == 6);
    assert(tiku_richanchor_get(anchor, &pos) == TIKU_RICH_OK);
    assert(pos.paragraph == doc->root->paragraphs[0]->value.id && !pos.byte);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richanchor_get(anchor, &pos) == TIKU_RICH_OK);
    assert(pos.paragraph == cell && pos.byte == 2);
    consistent(doc);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    tiku_richanchor_release(anchor);
    tiku_richdoc_release(doc);
    assert(old->root->blocks->nodes[1].id == table);
    assert(!strcmp(old->root->paragraphs[1]->value.text, "abcd"));
    tiku_richsnapshot_release(old);
}

/** @brief Refuse invalid dimensions, stale edits and cross-cell replacement. */
static void
refusals(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_pos_t a = {1, doc->root->paragraphs[1]->value.id, 0, 0};
    tiku_rich_pos_t b = {1, doc->root->paragraphs[2]->value.id, 0, 0};
    tiku_rich_table_t table = {2, 2, 0, 4, 1, 0xff000000, {0}, 0};
    uint64_t generation = doc->generation;
    tiku_richsnapshot_t *s;
    tiku_richfragment_t *fragment;

    assert(tiku_richdoc_replace_range(doc, 0, a, b, "bad", 3) ==
           TIKU_RICH_INVALID_RANGE);
    assert(doc->generation == generation);
    assert(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    assert(tiku_richfragment_create(s, a, b, &fragment) ==
           TIKU_RICH_UNSUPPORTED && !fragment);
    table.width = NAN;
    assert(tiku_rich_table_validate(&table) == TIKU_RICH_INVALID);
    table.width = 0;
    table.rows = 256;
    table.columns = 64;
    assert(tiku_rich_table_validate(&table) == TIKU_RICH_INVALID);
    tiku_richdoc_set_read_only(doc, 1);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_REMOVE,
        .object = doc->root->blocks->nodes[1].id}) == TIKU_RICH_READ_ONLY);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Nested table blocks keep their containing cell on deletion. */
static void
nested(void)
{
    tiku_richdoc_t *doc = fixture();
    uint64_t paragraph = doc->root->paragraphs[1]->value.id;
    uint64_t outer = doc->root->blocks->nodes[1].id;

    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = paragraph, .table = {1, 2, 0, 2, 1, 0xff000000}}) ==
           TIKU_RICH_OK);
    consistent(doc);
    assert(doc->root->count == 9);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_REMOVE,
        .object = outer}) == TIKU_RICH_OK);
    consistent(doc);
    assert(doc->root->count == 2);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    consistent(doc);
    tiku_richdoc_release(doc);
}

/** @brief Supply fixed six-point ASCII advances and ten-point line metrics. */
static tiku_rich_status_t
shape(void *context, const tiku_rich_chars_t *chars, const char *text,
      size_t length, const uint8_t *boundaries, tiku_rich_shape_t *out)
{
    (void)context; (void)chars; (void)text;
    assert(length && boundaries[0] && boundaries[1]);
    *out = (tiku_rich_shape_t){.bytes = 1, .advance = 6,
        .ascent = 8, .descent = 2, .caret_count = 2,
        .carets = {{0, 0}, {1, 6}}};
    return TIKU_RICH_OK;
}

/** @brief Check column geometry, hit testing, visual navigation and Tab. */
static void
layout(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = 200,
        .provider = {.shape = shape}};
    const tiku_richlayout_t *layout;
    tiku_rich_grid_box_t grid;
    tiku_rich_pos_t position, moved;
    tiku_event_t event = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_TAB};
    int handled;

    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    while (tiku_richview_step(view, 1) == TIKU_RICH_PENDING) { }
    assert(tiku_richview_status(view) == TIKU_RICH_OK);
    layout = tiku_richview_layout(view);
    assert(tiku_richlayout_grids(layout) == 5);
    assert(tiku_richlayout_grid(layout, 0, &grid) == TIKU_RICH_OK);
    assert(grid.box.y == 10 && grid.box.width == 200 && grid.box.height == 40);
    assert(grid.line == 1 && grid.line_count == 4);
    assert(tiku_richlayout_grid(layout, 2, &grid) == TIKU_RICH_OK);
    assert(grid.box.x == 100 && grid.box.y == 10 && grid.box.height == 20);
    assert(tiku_richlayout_height(layout) == 60);
    assert(tiku_richlayout_hit(layout, 50, 15, &position) == TIKU_RICH_OK);
    assert(position.paragraph == doc->root->paragraphs[1]->value.id);
    assert(tiku_richlayout_hit(layout, 150, 15, &position) == TIKU_RICH_OK);
    assert(position.paragraph == doc->root->paragraphs[2]->value.id);
    assert(tiku_richlayout_move(layout, position, TIKU_RICH_MOVE_DOWN,
        NAN, &moved) == TIKU_RICH_OK);
    assert(moved.paragraph == doc->root->paragraphs[4]->value.id);
    assert(tiku_richsession_select(tiku_richview_session(view), 0,
        position, position) == TIKU_RICH_OK);
    assert(tiku_richview_event(view, &event, &handled) == TIKU_RICH_OK);
    assert(handled);
    tiku_richsession_range(tiku_richview_session(view), &position, &moved);
    assert(position.paragraph == doc->root->paragraphs[3]->value.id);
    event.modifiers = TIKU_MOD_SHIFT;
    assert(tiku_richview_event(view, &event, &handled) == TIKU_RICH_OK);
    tiku_richsession_range(tiku_richview_session(view), &position, &moved);
    assert(position.paragraph == doc->root->paragraphs[2]->value.id);
    assert(tiku_richview_viewport(view, 20, 400, 1) == TIKU_RICH_OK);
    while (tiku_richview_step(view, 1) == TIKU_RICH_PENDING) { }
    assert(tiku_richview_status(view) == TIKU_RICH_OK);
    layout = tiku_richview_layout(view);
    assert(tiku_richlayout_grid(layout, 0, &grid) == TIKU_RICH_OK);
    assert(grid.box.width == 22);
    assert(tiku_richlayout_warnings(layout) & TIKU_RICH_LAYOUT_OVERFLOW);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Insertion selects the first cell and undo restores the origin. */
static void
session(void)
{
    tiku_richdoc_t *doc;
    tiku_richsession_t *session;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_TABLE_INSERT,
        .value.table = {2, 2, 0, 4, 1, 0xff000000}};
    tiku_rich_pos_t first, last;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_OK);
    tiku_richsession_range(session, &first, &last);
    assert(first.paragraph == doc->root->paragraphs[1]->value.id);
    assert(first.paragraph == last.paragraph && first.byte == last.byte);
    command.kind = TIKU_RICHCMD_UNDO;
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_OK);
    tiku_richsession_range(session, &first, &last);
    assert(first.paragraph == doc->root->paragraphs[0]->value.id);
    command.kind = TIKU_RICHCMD_REDO;
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_OK);
    tiku_richsession_range(session, &first, &last);
    assert(first.paragraph == doc->root->paragraphs[1]->value.id);
    assert(tiku_richsession_destroy(session) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Reject malformed owners and keep a body editable after deletion. */
static void
imports(void)
{
    tiku_rich_paragraph_source_t paragraphs[] = {
        {.text = "A", .length = 1}, {.text = "B", .length = 1}};
    tiku_rich_block_source_t blocks[] = {
        {.kind = TIKU_RICH_BLOCK_TABLE, .table = {1, 2, 0, 0, 1, 0xff000000}},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 1},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 2},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 1},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 4, .paragraph = 1}};
    tiku_rich_source_t source = {.paragraphs = paragraphs, .paragraph_count = 2,
        .blocks = blocks, .block_count = 5};
    tiku_richdoc_t *doc;
    tiku_rd_root_t *before;
    tiku_richanchor_t *anchor;
    tiku_rich_pos_t position;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    before = doc->root;
    blocks[4].parent = 2;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID);
    assert(doc->root == before);
    blocks[4].parent = 4;
    blocks[0].table.columns = 3;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID);
    blocks[0].table.columns = 2;
    blocks[4].paragraph = 0;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID);
    blocks[4].paragraph = 1;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    consistent(doc);
    position = (tiku_rich_pos_t){1, doc->root->paragraphs[1]->value.id, 1, 0};
    assert(tiku_richanchor_create(doc, position, &anchor) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_REMOVE,
        .object = doc->root->blocks->nodes[0].id}) == TIKU_RICH_OK);
    assert(doc->root->count == 1 && !doc->root->paragraphs[0]->value.length);
    assert(tiku_richanchor_get(anchor, &position) == TIKU_RICH_OK);
    assert(position.paragraph == doc->root->paragraphs[0]->value.id);
    consistent(doc);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(doc->root->count == 2);
    tiku_richanchor_release(anchor);
    tiku_richdoc_release(doc);
}

/** @brief Paginate with single-step work and a finite completion bound. */
static tiku_richpages_t *
pages(tiku_richdoc_t *doc, tiku_rich_status_t expected)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_options_t options = {.provider = {.shape = shape}};
    tiku_richpages_t *pages;
    tiku_rich_status_t status;
    unsigned calls = 0;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richpages_create(snapshot, &options, 20, &pages) ==
           TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    do {
        status = tiku_richpages_step(pages, 1);
        assert(++calls < 10000);
    } while (status == TIKU_RICH_PENDING);
    assert(status == expected);
    return pages;
}

/** @brief Keep a complete table on one page and refuse clipped output. */
static void
pagination(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richpages_t *result;
    tiku_rich_page_t page;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_PAGE_SETUP,
        .page = {236, 76, 18, 18, 18, 18}};

    assert(apply(doc, edit) == TIKU_RICH_OK);
    result = pages(doc, TIKU_RICH_OK);
    assert(tiku_richpages_count(result) == 3);
    assert(tiku_richpages_page(result, 0, &page) == TIKU_RICH_OK);
    assert(!page.first_line && page.line_count == 1 && !page.source_top);
    assert(tiku_richpages_page(result, 1, &page) == TIKU_RICH_OK);
    assert(page.first_line == 1 && page.line_count == 4 &&
           page.source_top == 10);
    assert(tiku_richpages_page(result, 2, &page) == TIKU_RICH_OK);
    assert(page.first_line == 5 && page.line_count == 1 &&
           page.source_top == 50);
    tiku_richpages_release(result);
    edit.page.height = 72;
    assert(apply(doc, edit) == TIKU_RICH_OK);
    result = pages(doc, TIKU_RICH_OK);
    assert(tiku_richpages_count(result) == 2);
    tiku_richpages_release(result);
    edit.page.height = 96;
    assert(apply(doc, edit) == TIKU_RICH_OK);
    result = pages(doc, TIKU_RICH_OK);
    assert(tiku_richpages_count(result) == 1);
    tiku_richpages_release(result);
    edit = (tiku_rich_edit_t){.kind = TIKU_RICH_PARAGRAPH_FORMAT,
        .paragraph = doc->root->paragraphs[2]->value.id,
        .props = {.present = TIKU_RICH_PAGE_BREAK, .break_before = 1}};
    assert(apply(doc, edit) == TIKU_RICH_OK);
    result = pages(doc, TIKU_RICH_UNSUPPORTED);
    tiku_richpages_release(result);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    edit = (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[5]->value.id,
        .table = {1, 1, 300, 4, 1, 0xff000000}};
    assert(apply(doc, edit) == TIKU_RICH_OK);
    result = pages(doc, TIKU_RICH_LIMIT);
    tiku_richpages_release(result);
    tiku_richdoc_release(doc);
}

/** @brief Pasting paragraphs within one cell preserves their owner. */
static void
paste(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *snapshot;
    tiku_richfragment_t *fragment;
    tiku_rich_pos_t a = {1, doc->root->paragraphs[1]->value.id, 0, 0}, b;

    assert(tiku_richdoc_replace_range(doc, 0, a, a, "one\ntwo", 7) ==
           TIKU_RICH_OK);
    b = (tiku_rich_pos_t){1, doc->root->paragraphs[2]->value.id, 3, 0};
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richfragment_create(snapshot, a, b, &fragment) ==
           TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    assert(tiku_richdoc_paste_fragment(doc, 0, b, b, fragment) ==
           TIKU_RICH_OK);
    consistent(doc);
    assert(!strcmp(doc->root->paragraphs[2]->value.text, "twoone"));
    assert(!strcmp(doc->root->paragraphs[3]->value.text, "two"));
    assert(doc->root->count == 8);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    consistent(doc);
    assert(doc->root->count == 7);
    tiku_richfragment_release(fragment);
    tiku_richdoc_release(doc);
}

/** @brief Propagate picture widths and replace an only table inside a cell. */
static void
nested_picture(void)
{
    const uint32_t pixel = 0xff000000;
    const tiku_rich_image_t image = {.pixel_width = 1, .pixel_height = 1,
        .pixels = &pixel, .width = 60, .height = 12, .alt = "sample"};
    tiku_rich_run_source_t run = {.length = 3, .image = &image};
    tiku_rich_paragraph_source_t paragraphs[] = {
        {.text = "\xef\xbf\xbc", .length = 3, .runs = &run, .run_count = 1},
        {.text = "x", .length = 1}};
    tiku_rich_block_source_t blocks[] = {
        {.kind = TIKU_RICH_BLOCK_TABLE, .table = {1, 1, 0, 4, 1, 0xff000000}},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 1},
        {.kind = TIKU_RICH_BLOCK_TABLE, .parent = 2,
         .table = {1, 2, 0, 2, 1, 0xff000000}},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 3},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 4},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 3},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 6, .paragraph = 1}};
    tiku_rich_source_t source = {.paragraphs = paragraphs, .paragraph_count = 2,
        .blocks = blocks, .block_count = 7};
    tiku_richdoc_t *doc;
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = 100,
        .provider = {.shape = shape}};
    tiku_rich_grid_box_t grid;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    while (tiku_richview_step(view, 1) == TIKU_RICH_PENDING) { }
    assert(tiku_richview_status(view) == TIKU_RICH_OK);
    assert(tiku_richlayout_grid(tiku_richview_layout(view), 0, &grid) ==
           TIKU_RICH_OK);
    assert(grid.box.width == 142);
    assert(tiku_richlayout_grid(tiku_richview_layout(view), 2, &grid) ==
           TIKU_RICH_OK);
    assert(grid.box.width == 132 && grid.box.x == 5 && grid.box.y == 5);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_REMOVE,
        .object = doc->root->blocks->nodes[2].id}) == TIKU_RICH_OK);
    assert(doc->root->count == 1 && !doc->root->paragraphs[0]->value.length);
    assert(doc->root->blocks->count == 3);
    consistent(doc);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    consistent(doc);
    assert(doc->root->blocks->count == 7);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Selected allocator failure and outstanding allocation count. */
typedef struct { size_t calls, fail, live; } faults_t;

/** @brief Refuse the selected model allocation. */
static void *
allocate(void *context, size_t size)
{
    faults_t *f = context;
    void *p;

    if (++f->calls == f->fail) return NULL;
    p = malloc(size);
    if (p) f->live++;
    return p;
}

/** @brief Count releases from failed candidates and retained history. */
static void
release(void *context, void *p)
{
    faults_t *f = context;

    if (p) { assert(f->live); f->live--; }
    free(p);
}

/** @brief Cell split/join allocation failures preserve the exact old root. */
static void
faults(int joining)
{
    for (size_t nth = 1; nth < 64; nth++) {
        faults_t f = {0};
        tiku_rich_limits_t limits = {.allocator = {&f, allocate, release}};
        tiku_richdoc_t *doc;
        tiku_rd_root_t *before;
        tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_INSERT,
            .table = {1, 2, 0, 4, 1, 0xff000000}};
        tiku_rich_status_t status;
        size_t memory;
        uint64_t generation, next;

        assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
        edit.paragraph = doc->root->paragraphs[0]->value.id;
        assert(apply(doc, edit) == TIKU_RICH_OK);
        edit = (tiku_rich_edit_t){.kind = TIKU_RICH_SPLIT,
            .paragraph = doc->root->paragraphs[1]->value.id};
        if (joining) {
            assert(apply(doc, edit) == TIKU_RICH_OK);
            edit.kind = TIKU_RICH_JOIN;
        }
        before = doc->root;
        generation = doc->generation;
        next = doc->next_id;
        memory = tiku_richdoc_memory(doc);
        f.fail = f.calls + nth;
        status = apply(doc, edit);
        f.fail = 0;
        if (status != TIKU_RICH_OK) {
            assert(status == TIKU_RICH_NO_MEMORY && doc->root == before);
            assert(doc->generation == generation && doc->next_id == next);
            assert(tiku_richdoc_memory(doc) == memory);
        }
        consistent(doc);
        tiku_richdoc_release(doc);
        assert(!f.live);
        if (status == TIKU_RICH_OK) { assert(nth > 5); return; }
    }
    assert(0);
}

/** @brief Run structural model regressions without fonts or a window. */
int
main(void)
{
    editing();
    refusals();
    nested();
    layout();
    session();
    imports();
    pagination();
    paste();
    nested_picture();
    faults(0);
    faults(1);
    puts("Rich tables: hierarchy, cell boundaries, snapshots and undo PASS");
    return 0;
}
