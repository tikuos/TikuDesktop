/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_pagination.c - independent table pagination and header oracles.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richpages_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Shape one byte into a ten-point line. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
      size_t size, const uint8_t *bounds, tiku_rich_shape_t *out)
{
    (void)ctx; (void)chars; (void)text; (void)size; (void)bounds;
    *out = (tiku_rich_shape_t){.bytes = 1, .advance = 1,
        .ascent = 8, .descent = 2, .caret_count = 2,
        .carets = {{0, 0}, {1, 1}}};
    return TIKU_RICH_OK;
}

/** @brief Import a table whose columns have independently varied line counts. */
static tiku_richdoc_t *
fixture(unsigned seed, unsigned rows, unsigned cols, unsigned headers,
        unsigned height, const tiku_rich_limits_t *limits)
{
    tiku_rich_paragraph_source_t paragraphs[82] = {{.text = "A", .length = 1}};
    tiku_rich_block_source_t nodes[163] = {{.kind = TIKU_RICH_BLOCK_PARAGRAPH},
        {.kind = TIKU_RICH_BLOCK_TABLE, .table = {.rows = rows, .columns = cols,
         .width = 160, .padding = 2, .border = 1, .header_rows = headers}}};
    size_t p = 1, n = 2;
    tiku_richdoc_t *doc;
    tiku_rich_source_t source;

    assert(rows <= 20 && cols <= 4);
    for (unsigned r = 0; r < rows; r++)
        for (unsigned c = 0; c < cols; c++) {
            unsigned lines = (seed + 7 * r + c) % 3 + 1;

            nodes[n++] = (tiku_rich_block_source_t){
                .kind = TIKU_RICH_BLOCK_CELL, .parent = 2};
            nodes[n] = (tiku_rich_block_source_t){
                .kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = n, .paragraph = p};
            n++;
            paragraphs[p++] = (tiku_rich_paragraph_source_t){
                .text = "A\nA\nA", .length = 2 * lines - 1};
        }
    nodes[n++] = (tiku_rich_block_source_t){
        .kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = p};
    paragraphs[p++] = (tiku_rich_paragraph_source_t){.text = "A", .length = 1};
    source = (tiku_rich_source_t){.paragraphs = paragraphs, .paragraph_count = p,
        .blocks = nodes, .block_count = n, .page = {200, height + 36, 18,18,18,18}};
    assert(tiku_richdoc_create(limits, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Paginate with a requested work quantum and no partial result. */
static tiku_richpages_t *
paginate(tiku_richdoc_t *doc, size_t work, size_t limit,
         tiku_rich_status_t expected)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_options_t options = {.provider = {.shape = shape}};
    tiku_richpages_t *pages;
    tiku_rich_status_t status;
    size_t steps = 0;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richpages_create(snapshot, &options, limit, &pages) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    do {
        status = tiku_richpages_step(pages, work);
        assert(++steps < 100000);
        if (status != TIKU_RICH_OK) assert(!tiku_richpages_count(pages));
    } while (status == TIKU_RICH_PENDING);
    assert(status == expected);
    return pages;
}

/** @brief Pack measured rows independently, returning zero for impossible rows. */
static size_t
oracle(unsigned seed, unsigned rows, unsigned cols, unsigned headers,
       unsigned height, tiku_rich_page_t *pages)
{
    unsigned heights[20], lines[20], total = 0, head_height = 0, head_lines = 0;
    size_t count = 1, line = 1;
    double y = 10, used = 10;

    pages[0] = (tiku_rich_page_t){.line_count = 1};
    for (unsigned r = 0; r < rows; r++) {
        heights[r] = lines[r] = 0;
        for (unsigned c = 0; c < cols; c++) {
            unsigned n = (seed + 7 * r + c) % 3 + 1;

            if (10 * n + 6 > heights[r]) heights[r] = 10 * n + 6;
            lines[r] += n;
        }
        total += heights[r];
        if (r < headers) { head_height += heights[r]; head_lines += lines[r]; }
    }
    for (unsigned r = 0; r < rows;) {
        unsigned end = !r && total <= height ? rows :
            !r && headers ? (headers < rows ? headers + 1 : rows) : r + 1;
        unsigned group_height = 0, group_lines = 0, repeated = r ? head_height : 0;

        for (unsigned j = r; j < end; j++) {
            group_height += heights[j];
            group_lines += lines[j];
        }
        if (group_height + repeated > height) return 0;
        if (used + group_height > height) {
            pages[count++] = (tiku_rich_page_t){.first_line = line,
                .source_top = y - repeated, .header_first = repeated ? 1 : 0,
                .header_count = repeated ? head_lines : 0,
                .header_top = repeated ? 10 : 0};
            used = repeated;
        }
        pages[count - 1].line_count += group_lines;
        line += group_lines;
        used += group_height;
        y += group_height;
        r = end;
    }
    if (used + 10 > height)
        pages[count++] = (tiku_rich_page_t){.first_line = line, .source_top = y};
    pages[count - 1].line_count++;
    return count;
}

/** @brief Compare 300 independently packed layouts with incremental output. */
static void
generated(void)
{
    for (unsigned seed = 0; seed < 300; seed++) {
        unsigned rows = 2 + seed % 19, cols = 1 + seed % 4;
        unsigned headers = seed % 4, height = 40 + 10 * (seed % 10);
        tiku_rich_page_t expected[32] = {0}, page;
        size_t count;
        tiku_richdoc_t *doc;
        tiku_richpages_t *slow, *fast;
        unsigned header_height = 0;
        int feasible;

        if (headers > rows) headers = rows;
        count = oracle(seed, rows, cols, headers, height, expected);
        for (unsigned r = 0; r < headers; r++) {
            unsigned tallest = 0;

            for (unsigned c = 0; c < cols; c++) {
                unsigned cell = ((seed + 7 * r + c) % 3 + 1) * 10 + 6;

                if (cell > tallest) tallest = cell;
            }
            header_height += tallest;
        }
        feasible = count || (headers < rows && header_height + 16 <= height);
        doc = fixture(seed, rows, cols, headers, height, NULL);
        slow = paginate(doc, 1, 0, feasible ? TIKU_RICH_OK : TIKU_RICH_LIMIT);
        fast = paginate(doc, 100000, 0, feasible ? TIKU_RICH_OK : TIKU_RICH_LIMIT);
        assert(tiku_richpages_count(slow) == tiku_richpages_count(fast));
        if (count || !feasible) assert(tiku_richpages_count(slow) == count);
        for (size_t i = 0; i < count; i++) {
            for (unsigned k = 0; k < 2; k++) {
                tiku_richpages_page(k ? slow : fast, i, &page);
                assert(page.first_line == expected[i].first_line);
                assert(page.line_count == expected[i].line_count);
                assert(page.source_top == expected[i].source_top);
                assert(page.header_first == expected[i].header_first);
                assert(page.header_count == expected[i].header_count);
                assert(page.header_top == expected[i].header_top);
            }
        }
        if (count > 1) {
            tiku_richpages_t *limited = paginate(doc, 1, 1, TIKU_RICH_LIMIT);

            tiku_richpages_release(limited);
        }
        tiku_richpages_release(slow);
        tiku_richpages_release(fast);
        tiku_richdoc_release(doc);
    }
}

/** @brief Apply one current-generation model edit. */
static tiku_rich_status_t
apply(tiku_richdoc_t *doc, tiku_rich_edit_t edit)
{
    tiku_rich_transaction_t tx = {doc->generation, &edit, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Keep header membership during row edits and restore it with undo. */
static void
headers(void)
{
    tiku_richdoc_t *doc = fixture(0, 5, 2, 2, 130, NULL);
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_SET,
        .object = doc->root->blocks->nodes[1].id,
        .table = doc->root->blocks->nodes[1].table};
    tiku_richsnapshot_t *old;
    tiku_rich_block_t table;

    assert(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    edit.table.header_rows = 6;
    assert(apply(doc, edit) == TIKU_RICH_INVALID);
    edit.table.header_rows = 3;
    assert(apply(doc, edit) == TIKU_RICH_OK);
    assert(apply(doc, edit) == TIKU_RICH_NO_CHANGE);
    edit.kind = TIKU_RICH_TABLE_GRID;
    edit.grid.operation = TIKU_RICH_ROW_INSERT;
    assert(apply(doc, edit) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[1].table.header_rows == 4);
    edit.grid.operation = TIKU_RICH_ROW_REMOVE;
    assert(apply(doc, edit) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[1].table.header_rows == 3);
    edit.grid.operation = TIKU_RICH_ROW_INSERT;
    edit.grid.index = 3;
    assert(apply(doc, edit) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[1].table.header_rows == 3);
    for (unsigned i = 0; i < 4; i++)
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(doc->root->blocks->nodes[1].table.header_rows == 2);
    tiku_richsnapshot_block(old, 1, &table);
    assert(table.table.header_rows == 2);
    tiku_richsnapshot_release(old);
    tiku_richdoc_release(doc);
}

/** @brief Keep nested grids in their outer row and refuse oversized content. */
static void
nested(void)
{
    tiku_richdoc_t *doc = fixture(0, 12, 3, 1, 150, NULL);
    uint64_t paragraph = doc->root->paragraphs[8]->value.id;
    tiku_richpages_t *pages;
    const tiku_richlayout_t *layout;
    uint32_t pixel = 0xffff0000;
    tiku_rich_image_t image = {1, 1, &pixel, 12, 300, "Oversized"};

    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = paragraph, .table = {2,2,36,1,0,0,{0},1}}) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[doc->root->count - 1]->value.id,
        .table = {5,2,100,1,0,0,{0},2}}) == TIKU_RICH_OK);
    pages = paginate(doc, 1, 0, TIKU_RICH_OK);
    layout = tiku_richpages_layout(pages);
    for (size_t i = 0; i < layout->ngrids; i++) {
        const tiku_rich_grid_box_t *grid = &layout->grids[i];
        unsigned found = 0;

        if (!grid->depth) continue;
        for (size_t j = 0; j < tiku_richpages_count(pages); j++) {
            tiku_rich_page_t page;

            tiku_richpages_page(pages, j, &page);
            if (grid->line < page.first_line ||
                grid->line >= page.first_line + page.line_count) continue;
            assert(grid->line + grid->line_count <= page.first_line + page.line_count);
            assert(grid->box.y >= page.source_top);
            assert(grid->box.y + grid->box.height - page.source_top <= 150);
            found++;
        }
        assert(found == 1);
    }
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_INSERT,
        .paragraph = paragraph, .image = &image}) == TIKU_RICH_OK);
    tiku_richpages_t *refused = paginate(doc, 1, 0, TIKU_RICH_LIMIT);
    tiku_richpages_release(refused);
    tiku_richdoc_release(doc);
    assert(tiku_richpages_count(pages) > 1);
    tiku_richpages_release(pages);
}

/** @brief Count and fail individual allocations. */
typedef struct { size_t call, fail, live; } fault_t;
/** @brief Refuse the selected allocation attempt. */
static void *
allocate(void *ctx, size_t n)
{
    fault_t *f = ctx;
    void *p;

    if (++f->call == f->fail) return NULL;
    p = malloc(n);
    assert(p);
    f->live++;
    return p;
}
/** @brief Account for a released model allocation. */
static void
release(void *ctx, void *p)
{
    fault_t *f = ctx;

    assert(f->live);
    f->live--;
    free(p);
}

/** @brief Refuse each pagination allocation and cancel partially built jobs. */
static void
failures(void)
{
    fault_t f = {0};
    tiku_rich_limits_t limits = {.allocator = {&f, allocate, release}};
    tiku_richdoc_t *doc = fixture(0, 12, 3, 1, 100, &limits);
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_options_t options = {.provider = {.shape = shape}};
    tiku_richpages_t *pages;
    tiku_rich_status_t status;
    size_t live, memory, nth;

    tiku_richdoc_snapshot(doc, &snapshot);
    live = f.live;
    memory = tiku_richdoc_memory(doc);
    for (nth = 1; nth < 200; nth++) {
        f.fail = f.call + nth;
        status = tiku_richpages_create(snapshot, &options, 0, &pages);
        if (status == TIKU_RICH_OK)
            do { status = tiku_richpages_step(pages, 1); }
            while (status == TIKU_RICH_PENDING);
        assert(status == TIKU_RICH_OK || status == TIKU_RICH_NO_MEMORY);
        if (status != TIKU_RICH_OK) assert(!tiku_richpages_count(pages));
        tiku_richpages_release(pages);
        assert(f.live == live && tiku_richdoc_memory(doc) == memory);
        if (status == TIKU_RICH_OK) break;
    }
    assert(nth > 10 && nth < 200);
    f.fail = 0;
    for (size_t steps = 0; steps < 1500; steps += 13) {
        assert(tiku_richpages_create(snapshot, &options, 0, &pages) == TIKU_RICH_OK);
        for (size_t i = 0; i < steps; i++) tiku_richpages_step(pages, 1);
        tiku_richpages_release(pages);
        assert(f.live == live && tiku_richdoc_memory(doc) == memory);
    }
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    assert(!f.live);
    printf("Table pagination: %zu allocation refusals PASS\n", nth - 1);
}

/** @brief Run model, whole-row geometry, budgeting and lifetime checks. */
int
main(void)
{
    generated();
    headers();
    nested();
    failures();
    puts("Table pagination: 300 independent row/header oracles PASS");
    return 0;
}
