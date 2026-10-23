/*
 * TikuDesktop Translation Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richcellclip.c - selected cell envelopes and hostile byte validation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richclip.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Capture a table with private body text, an image and styled empties. */
static tiku_richsnapshot_t *
fixture(tiku_rich_cells_t *cells, int nested)
{
    uint32_t pixel = 0xff123456;
    tiku_rich_image_t image = {1, 1, &pixel, 10, 12, "Camera"};
    tiku_rich_run_source_t run = {.length = 3, .image = &image};
    tiku_rich_style_source_t styles[] = {{.name = "Private"},
        {.name = "Body", .chars = {.present = TIKU_RICH_BOLD, .bold = 1}}};
    tiku_rich_paragraph_source_t p[] = {
        {.text = "PRIVATE", .length = 7, .style = 1},
        {.text = "ab", .length = 2, .style = 2},
        {.text = "SECRET", .length = 6},
        {.text = "\xef\xbf\xbc", .length = 3, .runs = &run, .run_count = 1},
        {.style = 2, .paragraph = {.present = TIKU_RICH_ALIGNMENT,
                                   .align = TIKU_RICH_CENTER}}};
    tiku_rich_block_source_t blocks[10] = {
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH},
        {.kind = TIKU_RICH_BLOCK_TABLE, .table = {.rows = 2, .columns = 2,
            .width = 200, .padding = 3, .border = 1, .color = 0xff707070, .header_rows = 1,
            .weights = {1, 3}}}};
    tiku_rich_source_t source = {.styles = styles, .style_count = 2,
        .paragraphs = p, .paragraph_count = 5, .blocks = blocks, .block_count = 10};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_block_t table;

    for (size_t i = 0; i < 4; i++) {
        blocks[2 + i * 2] = (tiku_rich_block_source_t){
            .kind = TIKU_RICH_BLOCK_CELL, .parent = 2, .background = 0xffaabbcc};
        blocks[3 + i * 2] = (tiku_rich_block_source_t){
            .kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 3 + i * 2, .paragraph = i + 1};
    }
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    if (nested) {
        tiku_rich_paragraph_t value;
        tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_INSERT,
            .table = {.rows = 1, .columns = 1, .header_rows = 1, .padding = 3,
                      .border = 1, .color = 0xff707070}};
        tiku_rich_transaction_t tx = {0, &edit, 1};

        assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
        assert(tiku_richsnapshot_paragraph(snapshot, 1, &value) == TIKU_RICH_OK);
        edit.paragraph = value.id;
        tiku_richsnapshot_release(snapshot);
        assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
        assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
        assert(tiku_richsnapshot_paragraph(snapshot, 2, &value) == TIKU_RICH_OK);
        edit = (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = value.id, .text = "NESTED", .length = 6};
        tiku_richsnapshot_release(snapshot);
        assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    }
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richsnapshot_block(snapshot, 1, &table);
    *cells = (tiku_rich_cells_t){table.id, 0, 0, 2, 1};
    tiku_richdoc_release(doc);
    return snapshot;
}

/** @brief Require rejection and cleared outputs. */
static void
refused(const void *data, size_t length, const tiku_richio_options_t *options,
        tiku_richio_status_t expected)
{
    tiku_richdoc_t *doc = (void *)1;
    tiku_rich_cells_t cells = {.table = 1};

    assert(tiku_richcellclip_read(data, length, options, &doc, &cells) == expected);
    assert(!doc && !cells.table);
}

/** @brief Stop at a specified codec cancellation poll. */
static int
cancel(void *ctx)
{
    size_t *state = ctx;

    return ++state[0] == state[1];
}

/** @brief Round-trip selected cells after releasing the source and packet. */
static void
transfer(void)
{
    tiku_rich_cells_t cells, result;
    tiku_richsnapshot_t *source = fixture(&cells, 0), *snapshot;
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_t p;
    tiku_rich_block_t cell;
    unsigned char *data;
    char text[64], *xml;
    size_t length, bytes;

    assert(tiku_richsnapshot_cells_text(source, &cells, NULL, 0, &bytes) == TIKU_RICH_UNSUPPORTED);
    assert(tiku_richsnapshot_cells_fallback(source, &cells, text, sizeof text, &bytes) == TIKU_RICH_OK);
    assert(!strcmp(text, "\"ab\"\n\"[Picture]\""));
    assert(tiku_richcellclip_write(source, &cells, NULL, &data, &length) == TIKU_RICHIO_OK);
    xml = malloc(length);
    assert(xml);
    memcpy(xml, data + 16, length - 16);
    xml[length - 16] = 0;
    assert(!strstr(xml, "PRIVATE") && !strstr(xml, "SECRET") && !strstr(xml, "Private"));
    free(xml);
    tiku_richsnapshot_release(source);
    assert(tiku_richcellclip_read(data, length, NULL, &doc, &result) == TIKU_RICHIO_OK);
    memset(data, 0xa5, length);
    free(data);
    assert(result.rows == 2 && result.columns == 1);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    assert(tiku_richsnapshot_styles(snapshot) == 1);
    assert(tiku_richsnapshot_paragraphs(snapshot) == 4);
    assert(tiku_richsnapshot_block(snapshot, 1, &cell) == TIKU_RICH_OK);
    assert(cell.table.width == 50 && cell.table.header_rows == 1);
    assert(cell.table.weights[0] == 1 && !cell.table.weights[1]);
    assert(tiku_richsnapshot_paragraph(snapshot, 2, &p) == TIKU_RICH_OK);
    assert(p.runs[0].image->pixels[0] == 0xff123456);
    assert(!strcmp(p.runs[0].image->alt, "Camera"));
    assert(tiku_richsnapshot_cell_at(snapshot, &result, 1, &cell) == TIKU_RICH_OK);
    assert(cell.background == 0xffaabbcc);
    tiku_richsnapshot_release(snapshot);
    source = fixture(&cells, 0);
    cells.row = cells.column = cells.rows = cells.columns = 1;
    assert(tiku_richcellclip_write(source, &cells, NULL, &data, &length) == TIKU_RICHIO_OK);
    tiku_richsnapshot_release(source);
    assert(tiku_richcellclip_read(data, length, NULL, &doc, &result) == TIKU_RICHIO_OK);
    free(data);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(snapshot, 1, &p) == TIKU_RICH_OK);
    assert(!p.length && p.style && p.paragraph.align == TIKU_RICH_CENTER);
    assert(tiku_richsnapshot_block(snapshot, 1, &cell) == TIKU_RICH_OK);
    assert(cell.table.width == 150 && !cell.table.header_rows);
    assert(cell.table.weights[0] == 3 && !cell.table.weights[1]);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
}

/** @brief Check framing, all truncations, byte limits and cancellation. */
static void
hostile(void)
{
    tiku_rich_cells_t cells, result;
    tiku_richsnapshot_t *snapshot = fixture(&cells, 0);
    tiku_richdoc_t *doc;
    unsigned char *data, *encoded;
    size_t length, size, state[2] = {0}, polls;
    tiku_richio_options_t options = {.cancel = cancel, .ctx = state};

    assert(tiku_richcellclip_write(snapshot, &cells, NULL, &data, &length) == TIKU_RICHIO_OK);
    refused(NULL, length, NULL, TIKU_RICHIO_INVALID);
    refused(data, SIZE_MAX, NULL, TIKU_RICHIO_LIMIT);
    for (size_t i = 0; i < length; i++) refused(data, i, NULL, TIKU_RICHIO_DAMAGED);
    for (unsigned i = 0; i < 16; i++) {
        data[i] ^= 0xff;
        refused(data, length, NULL, i == 7 ? TIKU_RICHIO_UNSUPPORTED : TIKU_RICHIO_DAMAGED);
        data[i] ^= 0xff;
    }
    assert(tiku_richcellclip_write(snapshot, &cells, &options, &encoded, &size) == TIKU_RICHIO_OK);
    free(encoded);
    polls = state[0];
    for (size_t i = 1; i <= polls; i++) {
        state[0] = 0; state[1] = i;
        assert(tiku_richcellclip_write(snapshot, &cells, &options, &encoded, &size) == TIKU_RICHIO_CANCELLED);
        assert(!encoded && !size);
    }
    state[0] = state[1] = 0;
    assert(tiku_richcellclip_read(data, length, &options, &doc, &result) == TIKU_RICHIO_OK);
    tiku_richdoc_release(doc);
    polls = state[0];
    for (size_t i = 1; i <= polls; i++) {
        state[0] = 0; state[1] = i;
        refused(data, length, &options, TIKU_RICHIO_CANCELLED);
    }
    options = (tiku_richio_options_t){.xml_bytes = length - 17};
    refused(data, length, &options, TIKU_RICHIO_LIMIT);
    options = (tiku_richio_options_t){.document.memory_bytes = 1};
    refused(data, length, &options, TIKU_RICHIO_LIMIT);
    assert(tiku_richcellclip_write(snapshot, &cells, &options, &encoded, &size) == TIKU_RICHIO_LIMIT);
    assert(!encoded && !size);
    tiku_richsnapshot_release(snapshot);
    free(data);
}

/** @brief Retain nested structure while providing an explicitly lossy TSV. */
static void
nested(void)
{
    tiku_rich_cells_t cells, result;
    tiku_richsnapshot_t *source = fixture(&cells, 1), *snapshot;
    tiku_richdoc_t *doc;
    unsigned char *data;
    char text[128], untouched[2] = {'X', 'Y'};
    size_t length, bytes, tables = 0;

    assert(tiku_richsnapshot_cells_text(source, &cells, NULL, 0, &bytes) == TIKU_RICH_UNSUPPORTED);
    assert(tiku_richsnapshot_cells_fallback(source, &cells, NULL, 0, &bytes) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_cells_fallback(source, &cells, untouched, sizeof untouched, &length) == TIKU_RICH_LIMIT);
    assert(untouched[0] == 'X' && untouched[1] == 'Y' && length == bytes);
    assert(tiku_richsnapshot_cells_fallback(source, &cells, text, sizeof text, &length) == TIKU_RICH_OK);
    assert(length == bytes && !strcmp(text, "\"ab\nNESTED\n\"\n\"[Picture]\""));
    assert(tiku_richcellclip_write(source, &cells, NULL, &data, &length) == TIKU_RICHIO_OK);
    tiku_richsnapshot_release(source);
    assert(tiku_richcellclip_read(data, length, NULL, &doc, &result) == TIKU_RICHIO_OK);
    free(data);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    assert(tiku_richsnapshot_paragraphs(snapshot) == 6);
    for (size_t i = 0; i < tiku_richsnapshot_blocks(snapshot); i++) {
        tiku_rich_block_t block;

        assert(tiku_richsnapshot_block(snapshot, i, &block) == TIKU_RICH_OK);
        if (block.kind != TIKU_RICH_BLOCK_TABLE) continue;
        if (++tables == 2) assert(block.table.header_rows == 1 && block.table.padding == 3);
    }
    assert(tables == 2);
    assert(tiku_richsnapshot_cells_fallback(snapshot, &result, text, sizeof text, &bytes) == TIKU_RICH_OK);
    assert(!strcmp(text, "\"ab\nNESTED\n\"\n\"[Picture]\""));
    tiku_richsnapshot_release(snapshot);
}

/** @brief Run selective rich-cell interchange and malformed-input checks. */
int
main(int argc, char **argv)
{
    if (argc == 2) {
        tiku_rich_cells_t cells;
        tiku_richsnapshot_t *snapshot = fixture(&cells, 1);

        assert(tiku_richio_save(argv[1], snapshot, NULL) == TIKU_RICHIO_OK);
        tiku_richsnapshot_release(snapshot);
        return 0;
    }
    transfer();
    hostile();
    nested();
    puts("richcellclip: selected content, empty styles, media, framing, limits and cancellation PASS");
    return 0;
}
