/*
 * TikuDesktop Translation Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richcontinuation.c - repeated-header persistence, preview and PDF.
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

/** @brief Import a long table with a shaded, linked and illustrated header. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_rich_paragraph_source_t paragraphs[38] = {{.text = "Report", .length = 6}};
    tiku_rich_block_source_t nodes[75] = {{.kind = TIKU_RICH_BLOCK_PARAGRAPH},
        {.kind = TIKU_RICH_BLOCK_TABLE, .table = {18,2,200,4,1,0xff707070,{3,2},1}}};
    uint32_t pixel = 0xffff0000;
    tiku_rich_image_t picture = {1, 1, &pixel, 12, 8, "Red sample"};
    tiku_rich_run_source_t link = {.length = 7,
        .link = "https://example.com/header",
        .chars = {.present = TIKU_RICH_BOLD | TIKU_RICH_FOREGROUND,
                  .bold = 1, .foreground = 0xff224466}};
    tiku_rich_run_source_t image = {.length = 3, .image = &picture};
    char text[36][24];
    tiku_rich_source_t source;
    tiku_richdoc_t *doc;

    for (unsigned i = 0; i < 36; i++) {
        unsigned n = 2 + 2 * i;

        snprintf(text[i], sizeof text[i], "Item%02u", i);
        paragraphs[i + 1] = (tiku_rich_paragraph_source_t){
            .text = text[i], .length = strlen(text[i])};
        nodes[n] = (tiku_rich_block_source_t){.kind = TIKU_RICH_BLOCK_CELL,
            .parent = 2, .background = i < 2 ? 0xffddeeff : 0};
        nodes[n + 1] = (tiku_rich_block_source_t){
            .kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = n + 1,
            .paragraph = i + 1};
    }
    paragraphs[1] = (tiku_rich_paragraph_source_t){.text = "Heading", .length = 7,
        .runs = &link, .run_count = 1};
    paragraphs[2] = (tiku_rich_paragraph_source_t){.text = "\xef\xbf\xbc", .length = 3,
        .runs = &image, .run_count = 1};
    paragraphs[37] = (tiku_rich_paragraph_source_t){.text = "End", .length = 3};
    nodes[74] = (tiku_rich_block_source_t){.kind = TIKU_RICH_BLOCK_PARAGRAPH,
        .paragraph = 37};
    source = (tiku_rich_source_t){.paragraphs = paragraphs, .paragraph_count = 38,
        .blocks = nodes, .block_count = 75, .page = {260,180,18,18,18,18}};
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Replace one metadata token and require refusal without a document. */
static void
reject(const unsigned char *xml, size_t length, const char *from, const char *to)
{
    const char *at = strstr((const char *)xml, from);
    size_t old = strlen(from), next = strlen(to), offset;
    char *changed = malloc(length + next + 1);
    tiku_richdoc_t *copy = NULL;

    assert(at && changed);
    offset = (size_t)(at - (const char *)xml);
    memcpy(changed, xml, offset);
    memcpy(changed + offset, to, next);
    memcpy(changed + offset + next, at + old, length - offset - old + 1);
    assert(tiku_richio_read_xml(changed, length + next - old, NULL, &copy) !=
           TIKU_RICHIO_OK && !copy);
    free(changed);
}

/** @brief Load exact header properties and reject undeclared/damaged metadata. */
static tiku_richdoc_t *
roundtrip(tiku_richsnapshot_t *snapshot)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *copy;
    tiku_rich_block_t table;
    unsigned char *xml, *again;
    size_t length, size;

    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) == TIKU_RICHIO_OK);
    assert(strstr((char *)xml, " table-headers"));
    assert(tiku_richio_read_xml(xml, length, NULL, &doc) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(doc, &copy) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_block(copy, 1, &table) == TIKU_RICH_OK);
    assert(table.table.header_rows == 1);
    assert(tiku_richio_write_xml(copy, NULL, &again, &size) == TIKU_RICHIO_OK);
    assert(length == size && !memcmp(xml, again, size));
    reject(xml, length, " table-headers", "");
    reject(xml, length, " tables", "");
    reject(xml, length, "<header rows=\"1\"/>", "<header rows=\"19\"/>");
    reject(xml, length, "<header rows=\"1\"/>", "<header rows=\"0\"/>");
    reject(xml, length, "<header rows=\"1\"/>",
        "<header rows=\"1\"/><header rows=\"1\"/>");
    reject(xml, length, "<header rows=\"1\"/>", "<header rows=\"-1\"/>");
    free(xml);
    free(again);
    tiku_richsnapshot_release(copy);
    return doc;
}

/** @brief Preserve a header child without any column-weight children. */
static void
equal_headers(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_block_t table;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_SET};
    tiku_rich_transaction_t tx = {0, &edit, 1};
    tiku_richdoc_t *copy;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richsnapshot_block(snapshot, 1, &table);
    tiku_richsnapshot_release(snapshot);
    edit.object = table.id;
    edit.table = table.table;
    memset(edit.table.weights, 0, sizeof edit.table.weights);
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    copy = roundtrip(snapshot);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(copy);
}

/** @brief Count solid colour samples without depending on font edge coverage. */
static size_t
colour_count(const uint32_t *pixels, size_t count, uint32_t colour)
{
    size_t matches = 0;

    for (size_t i = 0; i < count; i++)
        if ((pixels[i] & 0xffffff) == colour) matches++;
    return matches;
}

/** @brief Compare repeated fills/images/links and exact body text on each page. */
static void
check_page(tiku_richfont_t *fonts, tiku_richpages_t *pages,
           tiku_richsnapshot_t *snapshot, tiku_pdf_t *pdf, size_t index)
{
    tiku_rich_page_t page;
    tiku_surface_t *surface = tiku_surface_new(260, 180, 0xffffff);
    tiku_pdf_pixmap_t pixels;
    tiku_pdf_link_t *links;
    char *text = tiku_pdf_page_text(pdf, (int)index);
    const tiku_richlayout_t *layout = tiku_richpages_layout(pages);
    int count, heading;

    assert(surface && text);
    assert(tiku_richpages_page(pages, index, &page) == TIKU_RICH_OK);
    heading = page.header_count ||
        (page.first_line <= 1 && page.first_line + page.line_count > 1);
    assert((strstr(text, "Heading") != NULL) == heading);
    assert(tiku_richoutput_page(fonts, pages, index, surface, 0, 0, 1) == TIKU_RICH_OK);
    assert(!tiku_pdf_render(pdf, (int)index, 1, 0, &pixels));
    assert((colour_count(surface->px, 260 * 180, 0x224466) > 0) == heading);
    assert((colour_count(pixels.px, pixels.w * pixels.h, 0x224466) > 0) == heading);
    count = tiku_pdf_links(pdf, (int)index, &links);
    assert(heading ? count == 7 : count == 0);
    for (int i = 0; i < count; i++) {
        assert(links[i].y0 >= 18 && links[i].y1 <= 162);
        assert(!strcmp(links[i].to.uri, "https://example.com/header"));
    }
    if (heading) {
        tiku_rich_grid_box_t cell;
        double origin = page.header_count ? page.header_top : page.source_top;

        tiku_richlayout_grid(layout, 1, &cell);
        int y = (int)ceil(18 + cell.box.y - origin + 2);
        assert((surface->px[y * 260 + 20] & 0xffffff) == 0xddeeff);
        assert((pixels.px[y * pixels.w + 20] & 0xffffff) == 0xddeeff);
        for (size_t i = 0; i < tiku_richlayout_clusters(layout); i++) {
            tiku_rich_cluster_t c;

            tiku_richlayout_cluster(layout, i, &c);
            if (!c.image) continue;
            int x = (int)ceil(18 + c.box.x + 2);
            y = (int)ceil(18 + c.baseline - c.image->height - origin + 2);
            assert((surface->px[y * 260 + x] & 0xffffff) == 0xff0000);
            assert((pixels.px[y * pixels.w + x] & 0xffffff) == 0xff0000);
        }
    }
    for (size_t i = page.first_line; i < page.first_line + page.line_count; i++) {
        tiku_rich_line_t line;
        tiku_rich_paragraph_t p;

        tiku_richlayout_line(layout, i, &line);
        tiku_richsnapshot_paragraph(snapshot, line.paragraph_index, &p);
        assert(p.id == line.paragraph);
        if (p.length && p.text[0] != '\xef') assert(strstr(text, p.text));
    }
    tiku_pdf_links_free(links, count);
    tiku_pdf_pixmap_free(&pixels);
    tiku_surface_free(surface);
    free(text);
}

/** @brief Independently decode the exported multipage PDF and inspect pages. */
static void
output(tiku_richsnapshot_t *snapshot)
{
    tiku_richfont_t *fonts;
    tiku_richpages_t *pages;
    tiku_richlayout_options_t options = {0};
    unsigned char *bytes;
    size_t length;
    tiku_pdf_t *pdf;

    assert(tiku_richfont_system(NULL, &fonts) == TIKU_RICH_OK);
    options.provider = tiku_richfont_provider(fonts);
    assert(tiku_richpages_create(snapshot, &options, 0, &pages) == TIKU_RICH_OK);
    while (tiku_richpages_step(pages, 1) == TIKU_RICH_PENDING) { }
    assert(tiku_richpages_status(pages) == TIKU_RICH_OK);
    assert(tiku_richpages_count(pages) >= 4);
    assert(tiku_richoutput_pdf(fonts, pages, NULL, NULL, &bytes, &length) == TIKU_RICH_OK);
    assert(tiku_pdf_decode(bytes, length, &pdf) == TIKU_PDF_OK);
    assert(tiku_pdf_pages(pdf) == (int)tiku_richpages_count(pages));
    assert(!tiku_pdf_repaired(pdf));
    for (size_t i = 0; i < tiku_richpages_count(pages); i++)
        check_page(fonts, pages, snapshot, pdf, i);
    tiku_pdf_close(pdf);
    free(bytes);
    tiku_richpages_release(pages);
    tiku_richfont_release(fonts);
}

/** @brief Run continuation tests or produce a native desktop fixture. */
int
main(int argc, char **argv)
{
    tiku_richdoc_t *doc = fixture(), *copy;
    tiku_richsnapshot_t *snapshot;

    tiku_richdoc_snapshot(doc, &snapshot);
    if (argc == 2)
        assert(tiku_richio_save(argv[1], snapshot, NULL) == TIKU_RICHIO_OK);
    else {
        copy = roundtrip(snapshot);
        tiku_richsnapshot_release(snapshot);
        tiku_richdoc_snapshot(copy, &snapshot);
        output(snapshot);
        tiku_richdoc_release(copy);
        equal_headers(doc);
        puts("Repeated headers: native metadata, preview/PDF text/pixels/links PASS");
    }
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    return 0;
}
