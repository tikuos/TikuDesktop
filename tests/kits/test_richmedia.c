/*
 * TikuDesktop Translation Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richmedia.c - embedded image files, corruption checks, preview and PDF.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio.h"
#include "tiku_richoutput.h"
#include "tiku_pdf.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const uint32_t pixels[] = {0xffff0000, 0x8000ff00, 0xff0000ff, 0};
static const tiku_rich_image_t image = {2, 2, pixels, 40, 20,
    "Camera <A> & café\nnext line"};

/** @brief Build an image-only paragraph with compact paper settings. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_run_source_t run = {.length = 3, .image = &image,
        .link = "https://example.org/camera"};
    tiku_rich_paragraph_source_t p = {.text = "\xef\xbf\xbc", .length = 3,
        .runs = &run, .run_count = 1};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1,
        .page = {180, 120, 18, 18, 18, 18}};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Compare image bytes, alpha, dimensions and alternate text. */
static void
same(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *s;
    tiku_rich_paragraph_t p;
    const tiku_rich_image_t *copy;

    assert(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(s, 0, &p) == TIKU_RICH_OK);
    assert(p.length == 3 && p.run_count == 1 && p.runs[0].image);
    assert(!strcmp(p.runs[0].link, "https://example.org/camera"));
    copy = p.runs[0].image;
    assert(copy->pixel_width == 2 && copy->pixel_height == 2);
    assert(copy->width == 40 && copy->height == 20);
    assert(!strcmp(copy->alt, image.alt));
    assert(!memcmp(copy->pixels, pixels, sizeof pixels));
    tiku_richsnapshot_release(s);
}

/** @brief Refuse tampered pixels and missing required-feature declarations. */
static void
corruption(unsigned char *xml, size_t length)
{
    tiku_richdoc_t *doc = NULL;
    char *p = strstr((char *)xml, "ffff00008000ff00ff0000ff00000000");
    char saved;
    tiku_richio_options_t options = {.memory_bytes = 2048};

    assert(p);
    saved = *p;
    *p = 'g';
    assert(tiku_richio_read_xml(xml, length, NULL, &doc) ==
           TIKU_RICHIO_DAMAGED && !doc);
    *p = saved;
    p = strstr((char *)xml, "inline-images");
    assert(p);
    *p = 'x';
    assert(tiku_richio_read_xml(xml, length, NULL, &doc) ==
           TIKU_RICHIO_UNSUPPORTED && !doc);
    *p = 'i';
    assert(tiku_richio_read_xml(xml, length, &options, &doc) ==
           TIKU_RICHIO_LIMIT && !doc);
    p = strstr((char *)xml, "pixel-width=\"2");
    assert(p);
    p[13] = '3';
    assert(tiku_richio_read_xml(xml, length, NULL, &doc) ==
           TIKU_RICHIO_DAMAGED && !doc);
    p[13] = '2';
}

/** @brief Round-trip both native XML and a complete package on disk. */
static void
files(tiku_richsnapshot_t *snapshot)
{
    unsigned char *xml;
    size_t length;
    tiku_richdoc_t *copy = NULL;
    char path[] = "/tmp/tiku-richmedia-XXXXXX";
    int fd = mkstemp(path);

    assert(fd >= 0 && !close(fd));
    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) ==
           TIKU_RICHIO_OK);
    assert(strstr((char *)xml, "inline-images"));
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) == TIKU_RICHIO_OK);
    same(copy);
    tiku_richdoc_release(copy);
    corruption(xml, length);
    free(xml);
    assert(tiku_richio_save(path, snapshot, NULL) == TIKU_RICHIO_OK);
    assert(tiku_richio_load(path, NULL, &copy) == TIKU_RICHIO_OK);
    same(copy);
    tiku_richdoc_release(copy);
    assert(!unlink(path));
}

/** @brief Store identical pixels once while preserving each occurrence's metadata. */
static void
shared_pixels(void)
{
    tiku_rich_image_t pictures[4] = {image, image, image, image};
    uint32_t separate[4], different[4];
    tiku_rich_run_source_t runs[4];
    tiku_rich_paragraph_source_t p = {
        .text = "\xef\xbf\xbc\xef\xbf\xbc\xef\xbf\xbc\xef\xbf\xbc",
        .length = 12, .runs = runs, .run_count = 4};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richdoc_t *doc, *copy;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t paragraph;
    unsigned char *xml;
    size_t length;
    const char *encoded;

    memcpy(separate, pixels, sizeof separate);
    memcpy(different, pixels, sizeof different);
    different[0] = 0xff123456;
    pictures[3].pixels = different;
    pictures[1].pixels = separate;
    pictures[1].width = 15;
    pictures[1].alt = "second occurrence";
    pictures[2].height = 50;
    for (size_t i = 0; i < 4; i++)
        runs[i] = (tiku_rich_run_source_t){.length = 3, .image = &pictures[i]};
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) == TIKU_RICHIO_OK);
    assert(strstr((char *)xml, "image-references"));
    encoded = strstr((char *)xml, "ffff00008000ff00ff0000ff00000000");
    assert(encoded && !strstr(encoded + 1, "ffff00008000ff00ff0000ff00000000"));
    assert(tiku_richio_read_xml(xml, length, NULL, &copy) == TIKU_RICHIO_OK);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    free(xml);
    assert(tiku_richdoc_snapshot(copy, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(snapshot, 0, &paragraph) == TIKU_RICH_OK);
    assert(paragraph.run_count == 4);
    for (size_t i = 0; i < 4; i++) {
        const tiku_rich_image_t *got = paragraph.runs[i].image;

        assert(got && got->pixel_width == 2 && got->pixel_height == 2);
        assert(got->width == pictures[i].width && got->height == pictures[i].height);
        assert(!strcmp(got->alt, pictures[i].alt));
        assert(!memcmp(got->pixels, pictures[i].pixels, sizeof pixels));
    }
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(copy);
}

/** @brief Replace one XML substring and check the exact refusal category. */
static void
tamper(const unsigned char *xml, const char *before, const char *after,
       tiku_richio_status_t expected)
{
    const char *at = strstr((const char *)xml, before);
    size_t prefix, length;
    char *changed;
    tiku_richdoc_t *doc = (void *)1;

    assert(at);
    prefix = (size_t)(at - (const char *)xml);
    length = strlen((const char *)xml) - strlen(before) + strlen(after);
    changed = malloc(length + 1);
    assert(changed);
    memcpy(changed, xml, prefix);
    memcpy(changed + prefix, after, strlen(after));
    strcpy(changed + prefix + strlen(after), at + strlen(before));
    assert(tiku_richio_read_xml(changed, length, NULL, &doc) == expected);
    if (expected == TIKU_RICHIO_OK) tiku_richdoc_release(doc);
    else assert(!doc);
    free(changed);
}

/** @brief Require declared, backward, dimension-matched, empty pixel references. */
static void
reference_guards(void)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_run_source_t run = {.length = 3, .image = &image};
    tiku_rich_paragraph_source_t p[3];
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 3};
    unsigned char *xml;
    size_t length;

    for (size_t i = 0; i < 3; i++)
        p[i] = (tiku_rich_paragraph_source_t){.text = "\xef\xbf\xbc",
            .length = 3, .runs = &run, .run_count = 1};
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richio_write_xml(snapshot, NULL, &xml, &length) == TIKU_RICHIO_OK);
    tamper(xml, " image-references", "", TIKU_RICHIO_UNSUPPORTED);
    tamper(xml, " inline-images", "", TIKU_RICHIO_DAMAGED);
    tamper(xml, "ref=\"1\"", "ref=\"0\"", TIKU_RICHIO_DAMAGED);
    tamper(xml, "ref=\"1\"", "ref=\"2\"", TIKU_RICHIO_DAMAGED);
    tamper(xml, "ref=\"1\"", "ref=\"3\"", TIKU_RICHIO_DAMAGED);
    tamper(xml, "ref=\"1\"", "ref=\"-1\"", TIKU_RICHIO_DAMAGED);
    tamper(xml, "ref=\"1\"/>", "ref=\"1\">ffff</image>", TIKU_RICHIO_DAMAGED);
    tamper(xml, "ref=\"1\"/>", "ref=\"1\"><bad/></image>", TIKU_RICHIO_DAMAGED);
    tamper(xml, "pixel-width=\"2\" pixel-height=\"2\"",
           "pixel-width=\"1\" pixel-height=\"4\"", TIKU_RICHIO_DAMAGED);
    {
        char *last = strstr((char *)xml, "ref=\"1\"");
        tiku_richdoc_t *copy;

        assert(last);
        last = strstr(last + 1, "ref=\"1\"");
        assert(last);
        last[5] = '2';
        assert(tiku_richio_read_xml(xml, length, NULL, &copy) == TIKU_RICHIO_OK);
        tiku_richdoc_release(copy);
    }
    free(xml);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
}

/** @brief Require red and blue picture pixels in preview or exported PDF. */
static void
colours(const uint32_t *px, size_t count)
{
    size_t red = 0, blue = 0, green = 0;

    for (size_t i = 0; i < count; i++) {
        uint32_t rgb = px[i] & 0xffffff;

        red += rgb == 0xff0000;
        blue += rgb == 0x0000ff;
        green += rgb == 0x7fff7f;
    }
    assert(red >= 100 && blue >= 100 && green >= 100);
}

/** @brief Keep a picture's external link over its exported page rectangle. */
static void
picture_link(tiku_pdf_t *pdf)
{
    tiku_pdf_link_t *links;
    int count = tiku_pdf_links(pdf, 0, &links);

    assert(count == 1 && links[0].to.uri);
    assert(!strcmp(links[0].to.uri, "https://example.org/camera"));
    assert(links[0].x0 == 18 && links[0].x1 == 58);
    assert(links[0].y0 == 18 && links[0].y1 == 38);
    tiku_pdf_links_free(links, count);
}

/** @brief Render images without reopening their source files. */
static void
output(tiku_richsnapshot_t *snapshot)
{
    tiku_richfont_t *fonts;
    tiku_richpages_t *pages;
    tiku_richlayout_options_t options = {0};
    tiku_surface_t *surface = tiku_surface_new(220, 160, 0x123456);
    tiku_pdf_t *pdf;
    tiku_pdf_pixmap_t pixmap;
    unsigned char *data;
    size_t length;
    tiku_rect_t clip = {0, 0, 220, 160};
    char path[] = "/tmp/tiku-richmedia-pdf-XXXXXX";

    assert(surface);
    assert(tiku_richfont_system(NULL, &fonts) == TIKU_RICH_OK);
    options.provider = tiku_richfont_provider(fonts);
    assert(tiku_richpages_create(snapshot, &options, 0, &pages) == TIKU_RICH_OK);
    while (tiku_richpages_step(pages, 1) == TIKU_RICH_PENDING) { }
    assert(tiku_richpages_status(pages) == TIKU_RICH_OK);
    assert(tiku_richoutput_page(fonts, pages, 0, surface, 20, 20, 1) ==
           TIKU_RICH_OK);
    assert(!memcmp(&surface->clip, &clip, sizeof clip));
    assert(surface->px[0] == 0x123456);
    colours(surface->px, 220 * 160);
    assert(tiku_richoutput_pdf(fonts, pages, NULL, NULL, &data, &length) ==
           TIKU_RICH_OK);
    assert(tiku_pdf_decode(data, length, &pdf) == TIKU_PDF_OK);
    assert(!tiku_pdf_repaired(pdf));
    picture_link(pdf);
    assert(!tiku_pdf_render(pdf, 0, 1, 0, &pixmap));
    colours(pixmap.px, (size_t)pixmap.w * pixmap.h);
    {
        int fd = mkstemp(path);
        FILE *file = fd >= 0 ? fdopen(fd, "wb") : NULL;

        assert(file && fwrite(data, 1, length, file) == length);
        assert(!fclose(file));
        printf("Picture PDF fixture: %s\n", path);
    }
    free(data);
    tiku_pdf_pixmap_free(&pixmap);
    tiku_pdf_close(pdf);
    tiku_surface_free(surface);
    tiku_richpages_release(pages);
    tiku_richfont_release(fonts);
}

/** @brief Exercise exact resources, hostile files and shared output geometry. */
int
main(int argc, char **argv)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *snapshot;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    if (argc == 2) {
        assert(tiku_richio_save(argv[1], snapshot, NULL) == TIKU_RICHIO_OK);
        tiku_richsnapshot_release(snapshot);
        return 0;
    }
    assert(argc == 1);
    files(snapshot);
    shared_pixels();
    reference_guards();
    output(snapshot);
    tiku_richsnapshot_release(snapshot);
    puts("Rich media: native images, corrupt input, alpha preview and PDF PASS");
    return 0;
}
