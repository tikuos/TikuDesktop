/*
 * TikuDesktop Translation Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richtables.c - table persistence, damaged structure, preview and PDF.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio.h"
#include "tiku_richoutput.h"
#include "tiku_pdf.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief Import a two-column table with a multi-paragraph first cell. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_rich_paragraph_source_t paragraphs[] = {
        {.text = "Sensor report", .length = 13},
        {.text = "Temperature", .length = 11},
        {.text = "degrees C", .length = 9},
        {.text = "24.5", .length = 4},
        {.text = "Humidity", .length = 8},
        {.text = "61 percent", .length = 10},
        {.text = "End of report", .length = 13}};
    tiku_rich_block_source_t blocks[] = {
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH},
        {.kind = TIKU_RICH_BLOCK_TABLE, .table = {2, 2, 200, 4, 1, 0xff707070}},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 2},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 3, .paragraph = 1},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 3, .paragraph = 2},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 2},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 6, .paragraph = 3},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 2},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 8, .paragraph = 4},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 2},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 10, .paragraph = 5},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = 6}};
    tiku_rich_source_t source = {.paragraphs = paragraphs,
        .paragraph_count = 7, .blocks = blocks, .block_count = 12,
        .page = {260, 180, 18, 18, 18, 18}};
    tiku_richdoc_t *doc;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Round-trip canonical structure and reject malformed cell ownership. */
static void
files(tiku_richsnapshot_t *snapshot)
{
    unsigned char *xml, *again;
    size_t length, count;
    tiku_richdoc_t *copy = NULL;
    tiku_richsnapshot_t *other;
    char *at;
    char path[] = "/tmp/tiku-richtables-XXXXXX";
    int fd = mkstemp(path);

    assert(fd >= 0 && !close(fd));
    assert(tiku_richio_save(path, snapshot, NULL) == TIKU_RICHIO_OK);
    assert(tiku_richio_load(path, NULL, &copy) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(copy, &other) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_blocks(other) == 12);
    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) ==
           TIKU_RICHIO_OK);
    assert(tiku_richio_write_xml(other, NULL, &again, &count) ==
           TIKU_RICHIO_OK);
    assert(count == length && !memcmp(xml, again, length));
    free(again);
    tiku_richsnapshot_release(other);
    tiku_richdoc_release(copy);
    at = strstr((char *)xml, " tables");
    assert(at);
    at[1] = 'x';
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) ==
           TIKU_RICHIO_UNSUPPORTED && !copy);
    at[1] = 't';
    at = strstr((char *)xml, "kind=\"2\" parent=\"2\"");
    assert(at);
    at[17] = '3';
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) ==
           TIKU_RICHIO_DAMAGED && !copy);
    free(xml);
    assert(!unlink(path));
}

/** @brief Require the gray grid and dark text in the rendered page. */
static void
pixels(const uint32_t *data, size_t count, size_t minimum_borders)
{
    size_t borders = 0, ink = 0;

    for (size_t i = 0; i < count; i++) {
        uint32_t color = data[i] & 0xffffff;

        borders += color == 0x707070;
        ink += color < 0x404040;
    }
    if ((minimum_borders != SIZE_MAX && borders <= minimum_borders) || ink <= 100)
        fprintf(stderr, "table pixels: %zu border, %zu text\n", borders, ink);
    assert((minimum_borders == SIZE_MAX || borders > minimum_borders) &&
           ink > 100);
}

/** @brief Check alpha shading inside the padding, away from text and borders. */
static void
fill_pixel(uint32_t actual, uint32_t color)
{
    unsigned alpha = color >> 24;

    for (unsigned shift = 0; shift < 24; shift += 8) {
        int expected = (((color >> shift) & 255) * alpha +
                        255 * (255 - alpha) + 127) / 255;

        assert(abs((int)((actual >> shift) & 255) - expected) <= 1);
    }
}

/** @brief Render shared table geometry and independently decode its PDF. */
static void
output(tiku_richsnapshot_t *snapshot, size_t minimum_borders)
{
    tiku_richfont_t *fonts;
    tiku_richpages_t *pages;
    tiku_richlayout_options_t options = {0};
    tiku_surface_t *surface = tiku_surface_new(260, 180, 0xffffff);
    tiku_pdf_t *pdf;
    tiku_pdf_pixmap_t pixmap;
    tiku_rich_grid_box_t table, cell;
    tiku_rich_block_t node;
    unsigned char *bytes;
    size_t length;
    char path[] = "/tmp/tiku-richtables-pdf-XXXXXX";

    assert(tiku_richfont_system(NULL, &fonts) == TIKU_RICH_OK);
    options.provider = tiku_richfont_provider(fonts);
    assert(tiku_richpages_create(snapshot, &options, 0, &pages) == TIKU_RICH_OK);
    while (tiku_richpages_step(pages, 1) == TIKU_RICH_PENDING) { }
    assert(tiku_richpages_status(pages) == TIKU_RICH_OK);
    assert(tiku_richpages_count(pages) == 1);
    assert(tiku_richsnapshot_block(snapshot, 1, &node) == TIKU_RICH_OK);
    assert(tiku_richlayout_grid(tiku_richpages_layout(pages), 0, &table) ==
           TIKU_RICH_OK);
    assert(tiku_richlayout_grid(tiku_richpages_layout(pages), 2, &cell) ==
           TIKU_RICH_OK);
    assert(fabs(cell.box.x - table.box.x - table.box.width *
        tiku_rich_table_fraction(&node.table, 0)) < 0.001);
    assert(tiku_richoutput_page(fonts, pages, 0, surface, 0, 0, 1) ==
           TIKU_RICH_OK);
    pixels(surface->px, 260 * 180, minimum_borders);
    assert(tiku_richoutput_pdf(fonts, pages, NULL, NULL, &bytes, &length) ==
           TIKU_RICH_OK);
    assert(tiku_pdf_decode(bytes, length, &pdf) == TIKU_PDF_OK);
    assert(!tiku_pdf_repaired(pdf));
    assert(!tiku_pdf_render(pdf, 0, 1, 0, &pixmap));
    pixels(pixmap.px, (size_t)pixmap.w * pixmap.h, minimum_borders);
    assert(tiku_richlayout_grid(tiku_richpages_layout(pages), 1, &table) ==
           TIKU_RICH_OK);
    if (table.background) {
        int x = (int)ceil(18 + table.box.x + table.border + 1);
        int y = (int)ceil(18 + table.box.y + table.border + 1);

        fill_pixel(surface->px[y * 260 + x], table.background);
        fill_pixel(pixmap.px[y * pixmap.w + x], table.background);
    }
    if (node.table.weights[0]) {
        int x = (int)lround(18 + cell.box.x);
        int y = (int)lround(18 + cell.box.y + cell.box.height / 2);

        assert((surface->px[y * 260 + x] & 0xffffff) == 0x707070);
        assert((pixmap.px[y * pixmap.w + x] & 0xffffff) == 0x707070);
    }
    {
        int fd = mkstemp(path);
        FILE *file = fd >= 0 ? fdopen(fd, "wb") : NULL;

        assert(file && fwrite(bytes, 1, length, file) == length);
        assert(!fclose(file));
        printf("Table PDF fixture: %s\n", path);
    }
    free(bytes);
    tiku_pdf_pixmap_free(&pixmap);
    tiku_pdf_close(pdf);
    tiku_surface_free(surface);
    tiku_richpages_release(pages);
    tiku_richfont_release(fonts);
}

/** @brief Refuse damaged or undeclared fill metadata without loading a model. */
static void
bad_fill(const unsigned char *xml, size_t length, const char *from,
         const char *to, tiku_richio_status_t expected)
{
    const char *at = strstr((const char *)xml, from);
    size_t old = strlen(from), next = strlen(to);
    char *changed = malloc(length + next + 1);
    tiku_richdoc_t *copy = NULL;

    assert(at && changed);
    memcpy(changed, xml, (size_t)(at - (const char *)xml));
    memcpy(changed + (at - (const char *)xml), to, next);
    memcpy(changed + (at - (const char *)xml) + next, at + old,
           length - (size_t)(at - (const char *)xml) - old + 1);
    assert(tiku_richio_read_xml(changed, length + next - old, NULL, &copy) ==
           expected && !copy);
    free(changed);
}

/** @brief Preserve opaque/translucent fills and render borderless cells. */
static void
shading(void)
{
    for (unsigned pass = 0; pass < 2; pass++) {
        tiku_richdoc_t *doc = fixture(), *copy;
        tiku_richsnapshot_t *snapshot, *reloaded;
        tiku_rich_block_t block;
        tiku_rich_edit_t edits[2] = {{.kind = TIKU_RICH_TABLE_SET},
            {.kind = TIKU_RICH_CELL_SET,
             .background = pass ? 0x8044aaff : 0xff44aaff}};
        tiku_rich_transaction_t tx = {0, edits, 2};
        unsigned char *xml;
        size_t length;

        assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
        tiku_richsnapshot_block(snapshot, 1, &block);
        edits[0].object = block.id;
        edits[0].table = block.table;
        edits[0].table.border = pass;
        tiku_richsnapshot_block(snapshot, 2, &block);
        edits[1].object = block.id;
        tiku_richsnapshot_release(snapshot);
        assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
        assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
        files(snapshot);
        assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) ==
               TIKU_RICHIO_OK);
        assert(strstr((char *)xml, " cell-shading"));
        assert(tiku_richio_read_xml(xml, length, NULL, &copy) == TIKU_RICHIO_OK);
        assert(tiku_richdoc_snapshot(copy, &reloaded) == TIKU_RICH_OK);
        tiku_richsnapshot_block(reloaded, 2, &block);
        assert(block.background == edits[1].background);
        output(reloaded, pass ? 200 : SIZE_MAX);
        bad_fill(xml, length, " cell-shading", "", TIKU_RICHIO_UNSUPPORTED);
        bad_fill(xml, length, " tables", "", TIKU_RICHIO_DAMAGED);
        bad_fill(xml, length, "<fill color=", "<column color=",
                 TIKU_RICHIO_UNSUPPORTED);
        bad_fill(xml, length, "/></node>", "/><fill color=\"0\"/></node>",
                 TIKU_RICHIO_UNSUPPORTED);
        bad_fill(xml, length, "<fill color=\"", "<fill color=\"-",
                 TIKU_RICHIO_DAMAGED);
        free(xml);
        tiku_richsnapshot_release(reloaded);
        tiku_richsnapshot_release(snapshot);
        tiku_richdoc_release(copy);
        tiku_richdoc_release(doc);
    }
}

/** @brief Preserve edited grid dimensions and text through a native reload. */
static void
edited_grid(void)
{
    tiku_richdoc_t *doc = fixture(), *copy;
    tiku_richsnapshot_t *snapshot, *reloaded;
    tiku_rich_block_t block;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_GRID,
        .grid = {TIKU_RICH_COLUMN_INSERT, 1}};
    tiku_rich_transaction_t tx = {0, &edit, 1};
    unsigned char *xml, *again;
    size_t length, count;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_block(snapshot, 1, &block) == TIKU_RICH_OK);
    edit.object = block.id;
    tiku_richsnapshot_release(snapshot);
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    edit.grid.operation = TIKU_RICH_ROW_REMOVE;
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) ==
           TIKU_RICHIO_OK);
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(copy, &reloaded) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_block(reloaded, 1, &block) == TIKU_RICH_OK);
    assert(block.table.rows == 1 && block.table.columns == 3);
    assert(tiku_richsnapshot_paragraphs(reloaded) == 6);
    assert(tiku_richio_write_xml(reloaded, NULL, &again, &count) ==
           TIKU_RICHIO_OK);
    assert(length == count && !memcmp(xml, again, count));
    output(reloaded, 80);
    free(xml);
    free(again);
    tiku_richsnapshot_release(snapshot);
    tiku_richsnapshot_release(reloaded);
    tiku_richdoc_release(doc);
    tiku_richdoc_release(copy);
}

/** @brief Preserve relative widths and reject missing/invalid column metadata. */
static void
columns(void)
{
    tiku_richdoc_t *doc = fixture(), *copy;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_block_t block;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_SET};
    tiku_rich_transaction_t tx = {0, &edit, 1};
    unsigned char *xml;
    size_t length;
    char *at, *end;
    char saved[64];

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richsnapshot_block(snapshot, 1, &block);
    edit.object = block.id;
    edit.table = block.table;
    edit.table.weights[0] = 3;
    edit.table.weights[1] = 2;
    tiku_richsnapshot_release(snapshot);
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    files(snapshot);
    output(snapshot, 200);
    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) ==
           TIKU_RICHIO_OK);
    at = strstr((char *)xml, " table-columns");
    assert(at);
    memset(at, ' ', 14);
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) ==
           TIKU_RICHIO_UNSUPPORTED && !copy);
    memcpy(at, " table-columns", 14);
    at = strstr((char *)xml, "<column weight-f32=\"");
    assert(at);
    end = strstr(at, "/>") + 2;
    assert((size_t)(end - at) < sizeof saved);
    memcpy(saved, at, (size_t)(end - at));
    memset(at, ' ', (size_t)(end - at));
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) ==
           TIKU_RICHIO_DAMAGED && !copy);
    memcpy(at, saved, (size_t)(end - at));
    at = strstr(at, "weight-f32=\"") + 12;
    memcpy(saved, at, 8);
    memcpy(at, "00000000", 8);
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) ==
           TIKU_RICHIO_DAMAGED && !copy);
    memcpy(at, "7f800000", 8);
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) ==
           TIKU_RICHIO_DAMAGED && !copy);
    memcpy(at, saved, 8);
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) == TIKU_RICHIO_OK);
    free(xml);
    tiku_richdoc_release(copy);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
}

/** @brief Run round trips and output tests or write a desktop fixture. */
int
main(int argc, char **argv)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *snapshot;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    if (argc == 2)
        assert(tiku_richio_save(argv[1], snapshot, NULL) == TIKU_RICHIO_OK);
    else {
        files(snapshot);
        output(snapshot, 200);
        edited_grid();
        columns();
        shading();
        puts("Rich tables: native hierarchy, corrupt input, preview/PDF PASS");
    }
    tiku_richsnapshot_release(snapshot);
    return 0;
}
