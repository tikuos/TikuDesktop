/*
 * TikuDesktop Translation Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richoutput.c - persistent paper, retained preview and PDF interoperability.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richoutput.h"
#include "tiku_richio.h"
#include "tiku_pdf.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief Always request cooperative cancellation. */
static int
cancel(void *ctx)
{
    (void)ctx;
    return 1;
}

/** @brief Compare saved page properties through the native XML codec. */
static void
roundtrip(tiku_richsnapshot_t *snapshot)
{
    unsigned char *xml;
    size_t length;
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *copy;
    tiku_rich_page_setup_t page;
    tiku_rich_paragraph_t paragraph;
    char *required;

    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) == TIKU_RICHIO_OK);
    required = strstr((char *)xml, "links page");
    assert(required && strstr((char *)xml, "<page"));
    assert(tiku_richio_read_xml(xml, length, NULL, &doc) == TIKU_RICHIO_OK);
    assert(tiku_richdoc_snapshot(doc, &copy) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_page(copy, &page) == TIKU_RICH_OK);
    assert(page.width == 300 && page.height == 200 && page.left == 24);
    assert(strstr((char *)xml, "page-breaks"));
    assert(tiku_richsnapshot_paragraph(copy, 7, &paragraph) == TIKU_RICH_OK);
    assert(paragraph.paragraph.present & TIKU_RICH_PAGE_BREAK);
    assert(paragraph.paragraph.break_before == 1);
    required[6] = 'X';
    tiku_richsnapshot_release(copy);
    tiku_richdoc_release(doc);
    assert(tiku_richio_read_xml(xml, length, NULL, &doc) ==
           TIKU_RICHIO_UNSUPPORTED && !doc);
    free(xml);
}

/** @brief Require clipping restoration and ink inside the printed page. */
static void
preview(tiku_richfont_t *fonts, tiku_richpages_t *pages)
{
    tiku_surface_t *surface = tiku_surface_new(360, 260, 0x123456);
    tiku_rect_t clip = {12, 12, 330, 230};
    unsigned ink = 0;

    assert(surface);
    tiku_clip_set(surface, clip);
    assert(tiku_richoutput_page(fonts, pages, 0, surface, 20, 20, 1) ==
           TIKU_RICH_OK);
    assert(!memcmp(&clip, &surface->clip, sizeof clip));
    for (int y = 0; y < 260; y++)
        for (int x = 0; x < 360; x++) {
            uint32_t pixel = surface->px[y * 360 + x] & 0xffffff;
            if (x < 20 || y < 20 || x >= 320 || y >= 220)
                assert(pixel == 0x123456);
            else if (pixel != 0xffffff) ink++;
        }
    assert(ink > 100);
    tiku_surface_free(surface);
}

/** @brief Export vector pages and inspect text, paper size and page count. */
static void
pdf(tiku_richfont_t *fonts, tiku_richpages_t *pages)
{
    unsigned char *data;
    size_t length;
    tiku_pdf_t *decoded = NULL;
    double width, height;
    int rotation;
    char path[] = "/tmp/tiku-richoutput-XXXXXX";
    int fd;
    char *text;

    assert(tiku_richoutput_pdf(fonts, pages, cancel, NULL, &data, &length) ==
           TIKU_RICH_CANCELLED);
    assert(!data && !length);
    assert(tiku_richoutput_pdf(fonts, pages, NULL, NULL, &data, &length) ==
           TIKU_RICH_OK);
    assert(data && length > 1000);
    assert(tiku_pdf_decode(data, length, &decoded) == TIKU_PDF_OK);
    assert(tiku_pdf_pages(decoded) == (int)tiku_richpages_count(pages));
    assert(!tiku_pdf_repaired(decoded));
    assert(!tiku_pdf_page_size(decoded, 0, &width, &height, &rotation));
    assert(width == 300 && height == 200 && !rotation);
    text = tiku_pdf_page_text(decoded, 0);
    assert(text && strstr(text, "Document output"));
    free(text);
    fd = mkstemp(path);
    assert(fd >= 0);
    for (size_t at = 0; at < length;) {
        ssize_t n = write(fd, data + at, length - at);
        assert(n > 0);
        at += (size_t)n;
    }
    assert(!close(fd));
    printf("PDF interoperability fixture: %s\n", path);
    free(data);
    tiku_pdf_close(decoded);
}

/** @brief Produce a multi-page formatted fixture with an installed native font. */
int
main(void)
{
    tiku_richfont_t *fonts;
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_richpages_t *pages;
    tiku_richlayout_options_t options = {.leading = 3};
    tiku_rich_paragraph_source_t paragraph[30] = {0};
    tiku_rich_source_t source = {.paragraphs = paragraph, .paragraph_count = 30,
        .page = {300, 200, 24, 24, 24, 24}};
    tiku_rich_status_t status;

    assert(tiku_richfont_system(NULL, &fonts) == TIKU_RICH_OK);
    options.provider = tiku_richfont_provider(fonts);
    paragraph[0].text = "Document output";
    paragraph[0].length = strlen(paragraph[0].text);
    paragraph[0].chars = (tiku_rich_chars_t){
        .present = TIKU_RICH_BOLD | TIKU_RICH_SIZE, .bold = 1, .size = 18};
    for (size_t i = 1; i < 30; i++) {
        paragraph[i].text = "Readable text with accents: café.";
        paragraph[i].length = strlen(paragraph[i].text);
        paragraph[i].list.kind = TIKU_RICH_LIST_DECIMAL;
    }
    paragraph[7].paragraph = (tiku_rich_para_t){
        .present = TIKU_RICH_PAGE_BREAK, .break_before = 1};
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    roundtrip(snapshot);
    assert(tiku_richpages_create(snapshot, &options, 0, &pages) == TIKU_RICH_OK);
    do { status = tiku_richpages_step(pages, 37); }
    while (status == TIKU_RICH_PENDING);
    assert(status == TIKU_RICH_OK && tiku_richpages_count(pages) > 1);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    preview(fonts, pages);
    pdf(fonts, pages);
    tiku_richpages_release(pages);
    tiku_richfont_release(fonts);
    puts("Rich output: native paper round trip, unknown feature refusal, "
         "preview clipping, cancellation, PDF text and pages PASS");
    return 0;
}
