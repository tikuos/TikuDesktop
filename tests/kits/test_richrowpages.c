/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richrowpages.c - tall-row fragments and independently advancing cells.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richpages_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Use the requested font size as a deterministic line height. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
      size_t length, const uint8_t *bounds, tiku_rich_shape_t *out)
{
    (void)ctx; (void)text; (void)length; (void)bounds;
    *out = (tiku_rich_shape_t){.bytes = 1, .advance = 1,
        .ascent = chars->size * .8, .descent = chars->size * .2,
        .caret_count = 2, .carets = {{0,0}, {1,1}}};
    return TIKU_RICH_OK;
}

/** @brief Import one tall row with unequal column lengths and line heights. */
static tiku_richdoc_t *
fixture(const tiku_rich_limits_t *limits, unsigned seed)
{
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_source_t text[] = {
        {.text = "A\nB\nC\nD\nE\nF\nG\nH\nI", .length = 17,
         .chars = {.present = TIKU_RICH_SIZE, .size = 10}},
        {.text = "a\nb\nc\nd\ne", .length = 9,
         .chars = {.present = TIKU_RICH_SIZE, .size = 13}}
    };
    tiku_rich_block_source_t nodes[] = {
        {.kind = TIKU_RICH_BLOCK_TABLE, .table = {1,2,120,2,1,0xff000000}},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 1, .background = 0xffeeeeee},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 2},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 1},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 4, .paragraph = 1}
    };
    tiku_rich_source_t source = {.paragraphs = text, .paragraph_count = 2,
        .blocks = nodes, .block_count = 5, .page = {160,80,20,20,20,20}};

    if (seed) {
        text[0].length = 2 * (seed % 9 + 1) - 1;
        text[1].length = 2 * (seed / 9 % 5 + 1) - 1;
        text[0].chars.size = 7 + seed % 15;
        text[1].chars.size = 6 + seed % 17;
        source.page.height = 76 + 4 * (seed % 10);
    }
    assert(tiku_richdoc_create(limits, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Complete a snapshot job using a requested step quantum. */
static tiku_richpages_t *
paginate(tiku_richdoc_t *doc, size_t work, tiku_rich_status_t expected)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_options_t options = {.provider = {.shape = shape}};
    tiku_richpages_t *pages;
    tiku_rich_status_t status;
    size_t calls = 0;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richpages_create(snapshot, &options, 0, &pages) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    do {
        status = tiku_richpages_step(pages, work);
        assert(++calls < 100000);
    } while (status == TIKU_RICH_PENDING);
    assert(status == expected);
    return pages;
}

/** @brief Mark lines in repeated headers, including complete nested content. */
static void
header_lines(const tiku_richlayout_t *layout, unsigned *headers)
{
    const tiku_rich_table_t *tables[8] = {0};
    unsigned cells[8] = {0};

    for (size_t i = 0; i < layout->ngrids; i++) {
        const tiku_rich_grid_box_t *box = &layout->grids[i];
        unsigned depth = box->depth / 2;

        if (box->kind == TIKU_RICH_BLOCK_TABLE) {
            size_t at = tiku_rd_block_find(layout->source.root, box->id);

            tables[depth] = &layout->source.root->blocks->nodes[at].table;
            cells[depth] = 0;
        } else if (cells[depth]++ < tables[depth]->header_rows * tables[depth]->columns)
            for (size_t n = box->line; n < box->line + box->line_count; n++)
                headers[n] = 1;
    }
}

/** @brief Count whole lines and independently verify their printable positions. */
static void
check_range(const tiku_richlayout_t *layout, size_t first, size_t count,
            double origin, double height, unsigned *seen)
{
    assert(first <= layout->nlines && count <= layout->nlines - first);
    for (size_t i = first; i < first + count; i++) {
        const tiku_rich_line_t *line = &layout->lines[i];

        assert(line->box.y - origin >= -1e-8);
        assert(line->box.y + line->box.height - origin <= height + 1e-8);
        seen[i]++;
    }
}

/** @brief Every body line occurs once; repeated headers and boxes remain in bounds. */
static void
coverage(const tiku_richpages_t *pages)
{
    const tiku_richlayout_t *layout = tiku_richpages_layout(pages);
    unsigned *seen = calloc(layout->nlines, sizeof *seen);
    unsigned *headers = calloc(layout->nlines, sizeof *headers);
    tiku_rich_page_setup_t setup;
    double height;

    assert(seen && headers);
    header_lines(layout, headers);
    tiku_richpages_setup(pages, &setup);
    height = setup.height - setup.top - setup.bottom;
    for (size_t p = 0; p < tiku_richpages_count(pages); p++) {
        tiku_rich_page_t page;
        tiku_rich_page_piece_t piece;

        assert(tiku_richpages_page(pages, p, &page) == TIKU_RICH_OK);
        assert(page.line_count || page.piece_count);
        check_range(layout, page.first_line, page.line_count,
                    page.source_top, height, seen);
        check_range(layout, page.header_first, page.header_count,
                    page.header_top, height, seen);
        for (size_t i = 0; i < page.piece_count; i++) {
            assert(tiku_richpages_piece(pages, p, i, &piece) == TIKU_RICH_OK);
            if (piece.kind == TIKU_RICH_PAGE_CELL) {
                assert(piece.cell.box.y >= 0 && piece.cell.box.height >= 0);
                assert(piece.cell.box.y + piece.cell.box.height <= height + 1e-8);
                continue;
            }
            check_range(layout, piece.first_line, piece.line_count,
                        piece.source_top, height, seen);
            if (piece.kind != TIKU_RICH_PAGE_RANGE) continue;
            for (size_t g = 0; g < layout->ngrids; g++) {
                const tiku_rich_grid_box_t *box = &layout->grids[g];

                if (box->kind != TIKU_RICH_BLOCK_CELL || box->depth < piece.depth ||
                    box->line < piece.first_line ||
                    box->line >= piece.first_line + piece.line_count) continue;
                assert(box->line + box->line_count <= piece.first_line + piece.line_count);
                assert(box->box.y - piece.source_top >= -1e-8);
                assert(box->box.y + box->box.height - piece.source_top <= height + 1e-8);
            }
        }
        memset(&piece, 255, sizeof piece);
        assert(tiku_richpages_piece(pages, p, page.piece_count, &piece) ==
               TIKU_RICH_INVALID_RANGE && !piece.line_count && !piece.cell.id);
    }
    for (size_t i = 0; i < layout->nlines; i++)
        assert(headers[i] ? seen[i] >= 1 : seen[i] == 1);
    free(seen);
    free(headers);
}

/** @brief Compare output pieces independently of the pagination work quantum. */
static void
equivalent(const tiku_richpages_t *a, const tiku_richpages_t *b)
{
    assert(tiku_richpages_count(a) == tiku_richpages_count(b));
    for (size_t p = 0; p < tiku_richpages_count(a); p++) {
        tiku_rich_page_t x, y;

        tiku_richpages_page(a, p, &x);
        tiku_richpages_page(b, p, &y);
        assert(x.first_line == y.first_line && x.line_count == y.line_count);
        assert(x.source_top == y.source_top && x.piece_count == y.piece_count);
        assert(x.header_first == y.header_first && x.header_count == y.header_count);
        for (size_t i = 0; i < x.piece_count; i++) {
            tiku_rich_page_piece_t u, v;

            tiku_richpages_piece(a, p, i, &u);
            tiku_richpages_piece(b, p, i, &v);
            assert(u.kind == v.kind && u.first_line == v.first_line);
            assert(u.line_count == v.line_count && u.source_top == v.source_top);
            assert(u.cell.id == v.cell.id && u.cell.box.y == v.cell.box.y);
            assert(u.cell.box.height == v.cell.box.height && u.depth == v.depth);
        }
    }
}

/** @brief Unequal cells use three independently packed line groups. */
static void
columns(void)
{
    tiku_richdoc_t *doc = fixture(NULL, 0);
    tiku_richpages_t *pages = paginate(doc, 1, TIKU_RICH_OK);
    tiku_richpages_t *fast = paginate(doc, 65536, TIKU_RICH_OK);

    assert(tiku_richpages_count(pages) == 3);
    coverage(pages);
    equivalent(pages, fast);
    for (size_t p = 0; p < 3; p++) {
        tiku_rich_page_t page;
        tiku_rich_page_piece_t piece;

        tiku_richpages_page(pages, p, &page);
        assert(!page.line_count && page.piece_count == 4);
        tiku_richpages_piece(pages, p, 0, &piece);
        assert(piece.kind == TIKU_RICH_PAGE_CELL && piece.cell.box.height == 36);
        tiku_richpages_piece(pages, p, 1, &piece);
        assert(piece.first_line == p * 3 && piece.line_count == 3);
        tiku_richpages_piece(pages, p, 2, &piece);
        assert(piece.kind == TIKU_RICH_PAGE_CELL && piece.cell.box.height == 36);
        tiku_richpages_piece(pages, p, 3, &piece);
        assert(piece.first_line == 9 + p * 2 && piece.line_count == (p == 2 ? 1 : 2));
    }
    tiku_richpages_release(fast);
    tiku_richdoc_release(doc);
    coverage(pages);
    tiku_richpages_release(pages);
}

/** @brief Compare 300 two-column fragments against independent integer packing. */
static void
generated(void)
{
    for (unsigned seed = 1; seed <= 300; seed++) {
        tiku_richdoc_t *doc = fixture(NULL, seed);
        tiku_richpages_t *pages = paginate(doc, 1, TIKU_RICH_OK);
        unsigned room = 30 + 4 * (seed % 10);
        unsigned left = seed % 9 + 1, right = seed / 9 % 5 + 1;
        unsigned lp = room / (7 + seed % 15), rp = room / (6 + seed % 17);
        unsigned a = (left + lp - 1) / lp, b = (right + rp - 1) / rp;

        if (tiku_richpages_count(pages) != (a > b ? a : b))
            fprintf(stderr, "seed %u: %zu pages, expected %u (room %u, lines %u/%u)\n",
                    seed, tiku_richpages_count(pages), a > b ? a : b, room, left, right);
        assert(tiku_richpages_count(pages) == (a > b ? a : b));
        coverage(pages);
        assert(!tiku_richdoc_dirty(doc));
        tiku_richpages_release(pages);
        tiku_richdoc_release(doc);
    }
}

/** @brief Fixed-capacity source builder for nested table fixtures. */
typedef struct {
    tiku_rich_paragraph_source_t paragraphs[128];
    tiku_rich_block_source_t blocks[256];
    size_t p, n;
    char long_text[201];
} source_t;

/** @brief Add a ten-point paragraph to one source cell or the body. */
static void
paragraph(source_t *source, size_t parent, const char *text)
{
    assert(source->p < 128 && source->n < 256);
    source->blocks[source->n++] = (tiku_rich_block_source_t){
        .kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = parent, .paragraph = source->p};
    source->paragraphs[source->p++] = (tiku_rich_paragraph_source_t){
        .text = text, .length = strlen(text),
        .chars = {.present = TIKU_RICH_SIZE, .size = 10}};
}

/** @brief Nest a two-row single-column table inside the preceding row's cell. */
static void
table(source_t *source, size_t parent, unsigned depth, unsigned headers)
{
    size_t owner = ++source->n, cell;

    assert(source->n < 256);
    source->blocks[owner - 1] = (tiku_rich_block_source_t){
        .kind = TIKU_RICH_BLOCK_TABLE, .parent = parent,
        .table = {2,1,0,0,1,0xff808080,{0},headers}};
    for (unsigned row = 0; row < 2; row++) {
        cell = ++source->n;
        source->blocks[cell - 1] = (tiku_rich_block_source_t){
            .kind = TIKU_RICH_BLOCK_CELL, .parent = owner,
            .background = row ? 0 : 0xffddeeff};
        if (!row) paragraph(source, cell, "Header");
        else if (depth > 1) {
            paragraph(source, cell, "Before");
            table(source, cell, depth - 1, headers);
            paragraph(source, cell, "After");
        } else paragraph(source, cell, source->long_text);
    }
}

/** @brief Import nested headers, long rows and following body text. */
static tiku_richdoc_t *
nested_fixture(unsigned depth, unsigned headers, const tiku_rich_limits_t *limits)
{
    source_t source = {0};
    tiku_rich_source_t input;
    tiku_richdoc_t *doc;

    for (size_t i = 0; i < 199; i++) source.long_text[i] = i % 2 ? '\n' : 'X';
    paragraph(&source, 0, "Introduction");
    table(&source, 0, depth, headers);
    paragraph(&source, 0, "Conclusion");
    input = (tiku_rich_source_t){.paragraphs = source.paragraphs,
        .paragraph_count = source.p, .blocks = source.blocks,
        .block_count = source.n, .page = {200,196,18,18,18,18}};
    assert(tiku_richdoc_create(limits, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &input, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Preserve fitting cell-local keep groups and flag oversized groups. */
static void
keeps(void)
{
    for (unsigned kind = 0; kind < 3; kind++) {
        source_t source = {0};
        tiku_rich_source_t input;
        tiku_richdoc_t *doc;
        tiku_richpages_t *pages;
        tiku_rich_page_piece_t piece;

        source.blocks[source.n++] = (tiku_rich_block_source_t){
            .kind = TIKU_RICH_BLOCK_TABLE, .table = {1,1,120,2,1,0xff000000}};
        source.blocks[source.n++] = (tiku_rich_block_source_t){
            .kind = TIKU_RICH_BLOCK_CELL, .parent = 1};
        paragraph(&source, 2, "A\nB");
        paragraph(&source, 2, kind == 2 ? "C\nD\nE\nF" : kind ? "C" : "C\nD");
        if (kind == 1) paragraph(&source, 2, "D");
        paragraph(&source, 2, "G\nH");
        source.paragraphs[1].paragraph = (tiku_rich_para_t){
            .present = TIKU_RICH_KEEP, .keep_lines = kind != 1, .keep_next = kind == 1};
        input = (tiku_rich_source_t){.paragraphs = source.paragraphs,
            .paragraph_count = source.p, .blocks = source.blocks,
            .block_count = source.n, .page = {160,80,20,20,20,20}};
        assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
        assert(tiku_richdoc_import(doc, &input, 0) == TIKU_RICH_OK);
        pages = paginate(doc, 1, TIKU_RICH_OK);
        coverage(pages);
        if (kind < 2) {
            assert(!tiku_richpages_warnings(pages));
            tiku_richpages_piece(pages, 0, 1, &piece);
            assert(piece.kind == TIKU_RICH_PAGE_TEXT && piece.line_count == 2);
        } else assert(tiku_richpages_warnings(pages) & TIKU_RICH_PAGE_KEEP_BROKEN);
        tiku_richpages_release(pages);
        tiku_richdoc_release(doc);
    }
}

/** @brief Nested continuations keep line ownership through eight table levels. */
static void
nesting(void)
{
    for (unsigned depth = 1; depth <= 8; depth++)
        for (unsigned headers = 0; headers <= 1; headers++) {
            tiku_richdoc_t *doc = nested_fixture(depth, headers, NULL);
            tiku_richpages_t *slow = paginate(doc, 1, TIKU_RICH_OK);
            tiku_richpages_t *fast = paginate(doc, 65536, TIKU_RICH_OK);

            assert(tiku_richpages_count(slow) > 6);
            coverage(slow);
            equivalent(slow, fast);
            tiku_richpages_release(slow);
            tiku_richpages_release(fast);
            tiku_richdoc_release(doc);
        }
}

/** @brief Move a nested header to the next page when its first body line cannot fit. */
static void
header_with_body(void)
{
    tiku_richdoc_t *doc = nested_fixture(2, 1, NULL);
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_PAGE_SETUP,
                             .page = {200,76,18,18,18,18}};
    tiku_rich_transaction_t tx = {0, &edit, 1};
    tiku_richpages_t *pages;
    tiku_rich_page_t page;
    tiku_rich_page_piece_t piece;

    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    pages = paginate(doc, 1, TIKU_RICH_OK);
    coverage(pages);
    /* Introduction is page zero; page one fits Before but no nested body line. */
    tiku_richpages_page(pages, 1, &page);
    assert(page.piece_count && page.header_count);
    for (size_t i = 0; i < page.piece_count; i++) {
        tiku_richpages_piece(pages, 1, i, &piece);
        assert(piece.kind != TIKU_RICH_PAGE_RANGE);
    }
    tiku_richpages_page(pages, 2, &page);
    assert(page.piece_count > 2);
    tiku_richpages_piece(pages, 2, 1, &piece);
    assert(piece.kind == TIKU_RICH_PAGE_RANGE && piece.depth == 3);
    tiku_richpages_release(pages);
    tiku_richdoc_release(doc);
}

/** @brief Count exact live blocks and refuse one selected allocation. */
typedef struct { size_t call, fail, live; } fault_t;

/** @brief Allocate a counted block unless its call index is selected. */
static void *
allocate(void *context, size_t bytes)
{
    fault_t *fault = context;
    void *p;

    if (++fault->call == fault->fail) return NULL;
    p = malloc(bytes);
    assert(p);
    fault->live++;
    return p;
}

/** @brief Release one counted model allocation. */
static void
release(void *context, void *pointer)
{
    fault_t *fault = context;

    assert(fault->live);
    fault->live--;
    free(pointer);
}

/** @brief Refusal and cancellation release layout, cursors and page pieces. */
static void
failures(void)
{
    fault_t fault = {0};
    tiku_rich_limits_t limits = {.allocator = {&fault, allocate, release}};
    tiku_richdoc_t *doc = nested_fixture(3, 1, &limits);
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_options_t options = {.provider = {.shape = shape}};
    tiku_richpages_t *pages;
    tiku_rich_status_t status;
    size_t live, memory, nth, calls = 0;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    live = fault.live;
    memory = tiku_richdoc_memory(doc);
    for (nth = 1; nth < 300; nth++) {
        fault.fail = fault.call + nth;
        status = tiku_richpages_create(snapshot, &options, 0, &pages);
        if (status == TIKU_RICH_OK)
            do { status = tiku_richpages_step(pages, 1); }
            while (status == TIKU_RICH_PENDING);
        assert(status == TIKU_RICH_OK || status == TIKU_RICH_NO_MEMORY);
        if (status != TIKU_RICH_OK) assert(!tiku_richpages_count(pages));
        tiku_richpages_release(pages);
        assert(fault.live == live && tiku_richdoc_memory(doc) == memory);
        if (status == TIKU_RICH_OK) break;
    }
    assert(nth > 20 && nth < 300);
    fault.fail = 0;
    assert(tiku_richpages_create(snapshot, &options, 0, &pages) == TIKU_RICH_OK);
    while (tiku_richpages_step(pages, 1) == TIKU_RICH_PENDING) calls++;
    tiku_richpages_release(pages);
    for (size_t stop = 0; stop <= calls; stop += 7) {
        assert(tiku_richpages_create(snapshot, &options, 0, &pages) == TIKU_RICH_OK);
        for (size_t i = 0; i < stop; i++) tiku_richpages_step(pages, 1);
        tiku_richpages_release(pages);
        assert(fault.live == live && tiku_richdoc_memory(doc) == memory);
    }
    assert(tiku_richpages_create(snapshot, &options, 2, &pages) == TIKU_RICH_OK);
    do { status = tiku_richpages_step(pages, 1); } while (status == TIKU_RICH_PENDING);
    assert(status == TIKU_RICH_LIMIT && !tiku_richpages_count(pages));
    tiku_richpages_release(pages);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    assert(!fault.live);
    printf("Tall rows: %zu allocation refusals, %zu cancellation points PASS\n",
           nth - 1, calls / 7 + 1);
}

/** @brief Run independent line coverage, nested headers and fault regressions. */
int
main(void)
{
    columns();
    generated();
    nesting();
    keeps();
    header_with_body();
    failures();
    puts("Tall rows: unequal metrics, exact coverage and eight-level nesting PASS");
    return 0;
}
