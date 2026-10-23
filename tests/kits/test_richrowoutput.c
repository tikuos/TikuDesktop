/*
 * TikuDesktop Translation Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richrowoutput.c - fragmented nested rows in preview and decoded PDF.
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

/** @brief Import two nested columns, repeated headers, a picture and hyperlinks. */
static tiku_richdoc_t *
fixture(int prefix)
{
    char left[256] = "", right[256] = "", side[128] = "";
    uint32_t pixel = 0xffff0000;
    tiku_rich_image_t image = {1,1,&pixel,12,24,"Red rectangle"};
    tiku_rich_run_source_t picture = {.length = 3, .image = &image};
    tiku_rich_run_source_t linked = {.link = "https://example.com/left"};
    tiku_rich_paragraph_source_t p[12] = {
        {.text = "Left header"}, {.text = "Right header"},
        {.text = "Before nested"}, {.text = "Inner left"}, {.text = "Inner right"},
        {.text = left}, {.text = right}, {.text = "\xef\xbf\xbc", .runs = &picture, .run_count = 1},
        {.text = "After nested"}, {.text = side}, {.text = "End of report"}
    };
    tiku_rich_block_source_t b[] = {
        {.kind = TIKU_RICH_BLOCK_TABLE, .table = {2,2,224,3,1,0xff707070,{0},1}},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 1, .background = 0xffddeecc},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 2, .paragraph = 0},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 1, .background = 0xffddeecc},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 4, .paragraph = 1},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 1, .background = 0xffeeeedd},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 6, .paragraph = 2},
        {.kind = TIKU_RICH_BLOCK_TABLE, .parent = 6,
         .table = {2,2,0,1,1,0xff707070,{0},1}},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 8, .background = 0xffddeeff},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 9, .paragraph = 3},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 8, .background = 0xffddeeff},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 11, .paragraph = 4},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 8, .background = 0xffffeecc},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 13, .paragraph = 5},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 8, .background = 0xffeeddff},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 15, .paragraph = 6},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 15, .paragraph = 7},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 6, .paragraph = 8},
        {.kind = TIKU_RICH_BLOCK_CELL, .parent = 1, .background = 0xffeeffee},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .parent = 19, .paragraph = 9},
        {.kind = TIKU_RICH_BLOCK_PARAGRAPH, .paragraph = 10}
    };
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 11,
        .blocks = b, .block_count = sizeof b / sizeof *b,
        .page = {260,180,18,18,18,18}};
    tiku_richdoc_t *doc;

    for (unsigned i = 0; i < 24; i++) snprintf(left + strlen(left), 9, "Left%02u\n", i);
    for (unsigned i = 0; i < 13; i++) snprintf(right + strlen(right), 10, "Right%02u\n", i);
    for (unsigned i = 0; i < 9; i++) snprintf(side + strlen(side), 9, "Side%02u\n", i);
    for (size_t i = 0; i < 11; i++) {
        p[i].length = strlen(p[i].text);
        p[i].chars = (tiku_rich_chars_t){.present = TIKU_RICH_SIZE, .size = 8};
    }
    linked.length = p[5].length;
    p[5].runs = &linked;
    p[5].run_count = 1;
    if (!prefix) {
        memmove(p + 2, p + 3, 8 * sizeof *p);
        memmove(b + 6, b + 7, 14 * sizeof *b);
        source.paragraph_count--;
        source.block_count--;
        for (size_t i = 0; i < source.block_count; i++) {
            if (b[i].parent > 7) b[i].parent--;
            if (b[i].kind == TIKU_RICH_BLOCK_PARAGRAPH && b[i].paragraph > 2)
                b[i].paragraph--;
        }
    }
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Count non-overlapping occurrences of one expected word. */
static unsigned
occurrences(const char *text, const char *word)
{
    unsigned count = 0;

    while ((text = strstr(text, word))) { count++; text += strlen(word); }
    return count;
}

/** @brief Read one solid pixel independent of glyph antialiasing. */
static uint32_t
pixel(const uint32_t *pixels, int width, double x, double y)
{
    return pixels[(size_t)(int)ceil(y) * width + (int)ceil(x)] & 0xffffff;
}

/** @brief Check source-aware fills, closed borders and blank page margins. */
static void
geometry(tiku_richpages_t *pages, size_t index, tiku_surface_t *surface,
         const tiku_pdf_pixmap_t *render)
{
    tiku_rich_page_t page;

    tiku_richpages_page(pages, index, &page);
    for (size_t i = 0; i < page.piece_count; i++) {
        tiku_rich_page_piece_t piece;

        tiku_richpages_piece(pages, index, i, &piece);
        if (piece.kind != TIKU_RICH_PAGE_CELL || piece.cell.box.height < 4) continue;
        double x = 18 + piece.cell.box.x, y = 18 + piece.cell.box.y;

        assert(pixel(surface->px, surface->w, x + 1, y + 1) ==
               (piece.cell.background & 0xffffff));
        assert(pixel(render->px, render->w, x + 1, y + 1) ==
               (piece.cell.background & 0xffffff));
        assert(pixel(surface->px, surface->w, x, y + 2) == 0x707070);
        assert(pixel(render->px, render->w, x, y + 2) == 0x707070);
    }
    for (int y = 0; y < 180; y++)
        for (int x = 0; x < 260; x++) {
            if (x >= 18 && x < 242 && y >= 18 && y < 162) continue;
            assert((surface->px[y * 260 + x] & 0xffffff) == 0xffffff);
            assert((render->px[y * render->w + x] & 0xffffff) == 0xffffff);
        }
}

/** @brief Inspect decoded text, image pixels and hyperlink annotation bounds. */
static void
check_page(tiku_richfont_t *fonts, tiku_richpages_t *pages, tiku_pdf_t *pdf,
           size_t index, unsigned *seen, unsigned *pictures)
{
    tiku_surface_t *surface = tiku_surface_new(260,180,0xffffff);
    tiku_pdf_pixmap_t render;
    tiku_pdf_link_t *links;
    char *text = tiku_pdf_page_text(pdf, (int)index);
    int count;

    assert(surface && text);
    for (unsigned i = 0; i < 46; i++) {
        char token[24];

        if (i < 24) snprintf(token, sizeof token, "Left%02u", i);
        else if (i < 37) snprintf(token, sizeof token, "Right%02u", i - 24);
        else snprintf(token, sizeof token, "Side%02u", i - 37);
        seen[i] += occurrences(text, token);
    }
    seen[46] += occurrences(text, "Before nested");
    seen[47] += occurrences(text, "After nested");
    seen[48] += occurrences(text, "End of report");
    assert(tiku_richoutput_page(fonts, pages, index, surface, 0, 0, 1) == TIKU_RICH_OK);
    assert(!tiku_pdf_render(pdf, (int)index, 1, 0, &render));
    geometry(pages, index, surface, &render);
    for (int i = 0; i < 260 * 180; i++) {
        if ((surface->px[i] & 0xffffff) == 0xff0000) pictures[0]++;
        if ((render.px[i] & 0xffffff) == 0xff0000) pictures[1]++;
    }
    count = tiku_pdf_links(pdf, (int)index, &links);
    assert(count >= 0);
    for (int i = 0; i < count; i++) {
        assert(!strcmp(links[i].to.uri, "https://example.com/left"));
        assert(links[i].y0 >= 18 && links[i].y1 <= 162);
    }
    tiku_pdf_links_free(links, count);
    tiku_surface_free(surface);
    tiku_pdf_pixmap_free(&render);
    free(text);
}

/** @brief Cancel at a selected output callback. */
static int
cancel(void *context)
{
    unsigned *remaining = context;

    return !--*remaining;
}

/** @brief Preview and decode a PDF using exactly the same fragment geometry. */
static void
output(tiku_richsnapshot_t *snapshot, int prefix)
{
    tiku_richfont_t *fonts;
    tiku_richpages_t *pages;
    tiku_richlayout_options_t options = {0};
    tiku_pdf_t *pdf;
    unsigned char *bytes;
    size_t length;
    unsigned seen[49] = {0}, pictures[2] = {0};

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
        check_page(fonts, pages, pdf, i, seen, pictures);
    for (size_t i = 0; i < 49; i++) assert(seen[i] == (i == 46 ? (unsigned)prefix : 1));
    /* Preview covers floor/ceil pixel bounds at a fractional source baseline. */
    assert(pictures[0] >= 12 * 24 && pictures[0] <= 13 * 25);
    assert(pictures[1] == 12 * 24);
    tiku_pdf_close(pdf);
    free(bytes);
    for (unsigned i = 1; i <= 400; i++) {
        unsigned remaining = i;
        tiku_rich_status_t status;

        status = tiku_richoutput_pdf(fonts, pages, cancel, &remaining, &bytes, &length);
        if (status == TIKU_RICH_OK) {
            assert(i > 60 && remaining);
            free(bytes);
            break;
        }
        assert(status == TIKU_RICH_CANCELLED && i < 400);
        assert(!bytes && !length);
    }
    tiku_richpages_release(pages);
    tiku_richfont_release(fonts);
}

/** @brief Run rendering checks or save the native fixture for desktop tests. */
int
main(int argc, char **argv)
{
    tiku_richdoc_t *doc = fixture(1);
    tiku_richsnapshot_t *snapshot;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    if (argc == 2) assert(tiku_richio_save(argv[1], snapshot, NULL) == TIKU_RICHIO_OK);
    else {
        output(snapshot, 1);
        tiku_richsnapshot_release(snapshot);
        tiku_richdoc_release(doc);
        doc = fixture(0);
        assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
        output(snapshot, 0);
        puts("Tall rows: decoded PDF coverage, preview fills/borders, images, links, cancellation PASS");
    }
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    return 0;
}
