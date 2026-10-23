/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richlayout.c - shared glyph, wrap, hit and selection geometry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout.h"
#include "tiku_richtext.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richlayout:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)
#define NEAR(a, b) CHECK(fabs((a) - (b)) < 0.00001)

/** @brief Deterministic provider state, including malformed-result probes. */
typedef struct { int refs, invalid, calls; } fonts_t;

/** @brief Supply fixed advances and an fi ligature with an interior caret. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
      size_t length, const uint8_t *boundaries, tiku_rich_shape_t *out)
{
    fonts_t *fonts = ctx;
    size_t end = 1;

    fonts->calls++;
    CHECK(length && length <= 4096 && boundaries[0] == 1);
    CHECK(chars->present & TIKU_RICH_SIZE);
    CHECK(chars->size >= 1);
    while (end < length && !(boundaries[end] & 1)) end++;
    memset(out, 0, sizeof *out);
    out->bytes = end;
    out->advance = chars->size / 2;
    out->ascent = chars->size * 0.8;
    out->descent = chars->size * 0.2;
    out->glyph_count = 1;
    out->glyphs[0] = (tiku_rich_glyph_t){7, (uint8_t)text[0], 0, 0};
    out->caret_count = 2;
    out->carets[1] = (tiku_rich_shape_caret_t){end, out->advance};
    if (length >= 2 && text[0] == 'f' && text[1] == 'i' && boundaries[2]) {
        out->bytes = 2;
        out->advance = chars->size * 0.8;
        out->glyphs[0].glyph = 900;
        out->caret_count = 3;
        out->carets[1] = (tiku_rich_shape_caret_t){1, out->advance / 2};
        out->carets[2] = (tiku_rich_shape_caret_t){2, out->advance};
    }
    if (fonts->invalid == 1) out->bytes = length + 1;
    if (fonts->invalid == 2) out->advance = NAN;
    if (fonts->invalid == 3) out->caret_count = 35;
    if (fonts->invalid == 4) out->carets[1].byte = 0;
    if (fonts->invalid == 5) out->glyphs[0].x = INFINITY;
    if (fonts->invalid == 6) return TIKU_RICH_PENDING;
    if (fonts->invalid == 7) out->ascent = out->descent = 0;
    return TIKU_RICH_OK;
}

/** @brief Retain one provider reference for an outstanding layout. */
static void
retain(void *ctx)
{
    ((fonts_t *)ctx)->refs++;
}

/** @brief Release one provider reference on completion, error or cancellation. */
static void
release(void *ctx)
{
    CHECK(((fonts_t *)ctx)->refs > 0);
    ((fonts_t *)ctx)->refs--;
}

/** @brief Create a source document with one or more imported paragraphs. */
static tiku_richdoc_t *
document(const tiku_rich_paragraph_source_t *p, size_t count)
{
    tiku_richdoc_t *doc;
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = count};

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Construct a job independently retaining its source snapshot. */
static tiku_richlayout_t *
job(tiku_richdoc_t *doc, fonts_t *fonts, double width)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_t *layout;
    tiku_richlayout_options_t options = {.width = width,
        .provider = {fonts, shape, retain, release, 42}};

    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richlayout_create(snapshot, &options, &layout) == TIKU_RICH_OK);
    CHECK(fonts->refs == 1);
    CHECK(tiku_richlayout_generation(layout) ==
          tiku_richsnapshot_generation(snapshot));
    CHECK(tiku_richlayout_fonts(layout) == 42);
    tiku_richsnapshot_release(snapshot);
    return layout;
}

/** @brief Complete with one work unit per call and verify pending isolation. */
static void
finish(tiku_richlayout_t *layout)
{
    tiku_rich_status_t status;
    tiku_rich_line_t line;
    size_t steps = 0;

    CHECK(tiku_richlayout_step(layout, 0) == TIKU_RICH_PENDING);
    do {
        CHECK(!tiku_richlayout_lines(layout));
        CHECK(!tiku_richlayout_clusters(layout));
        CHECK(tiku_richlayout_line(layout, 0, &line) == TIKU_RICH_PENDING);
        CHECK(!line.paragraph);
        status = tiku_richlayout_step(layout, 1);
        CHECK(++steps < 1000000);
    } while (status == TIKU_RICH_PENDING);
    CHECK(status == TIKU_RICH_OK);
    CHECK(tiku_richlayout_step(layout, 99) == TIKU_RICH_OK);
}

/** @brief Check wrapping, affinity and shared selection/hit geometry. */
static void
wrapping(void)
{
    tiku_rich_paragraph_source_t p = {.text = "ab cd", .length = 5};
    tiku_richdoc_t *doc = document(&p, 1);
    fonts_t fonts = {0};
    tiku_richlayout_t *layout = job(doc, &fonts, 18);
    tiku_rich_line_t line;
    tiku_rich_pos_t a, b;
    tiku_rich_rect_t rect;

    tiku_richdoc_release(doc);
    finish(layout);
    CHECK(tiku_richlayout_lines(layout) == 2);
    CHECK(tiku_richlayout_clusters(layout) == 5);
    NEAR(tiku_richlayout_height(layout), 24);
    CHECK(tiku_richlayout_line(layout, 0, &line) == TIKU_RICH_OK);
    CHECK(line.first == 0 && line.last == 3 && line.count == 3);
    CHECK(tiku_richlayout_hit(layout, 18, 2, &a) == TIKU_RICH_OK);
    CHECK(a.byte == 3 && a.affinity == TIKU_RICH_BEFORE);
    CHECK(tiku_richlayout_caret(layout, a, &rect) == TIKU_RICH_OK);
    NEAR(rect.x, 18); NEAR(rect.y, 0);
    a.affinity = TIKU_RICH_AFTER;
    CHECK(tiku_richlayout_caret(layout, a, &rect) == TIKU_RICH_OK);
    NEAR(rect.x, 0); NEAR(rect.y, 12);
    CHECK(tiku_richlayout_hit(layout, 999, 999, &b) == TIKU_RICH_OK);
    CHECK(b.byte == 5);
    CHECK(tiku_richlayout_selection(layout, a, b, 1, &rect) == TIKU_RICH_OK);
    NEAR(rect.x, 0); NEAR(rect.width, 12);
    CHECK(tiku_richlayout_selection(layout, b, a, 1, &rect) ==
          TIKU_RICH_INVALID_RANGE);
    CHECK(tiku_richlayout_hit(layout, -999, -999, &a) == TIKU_RICH_OK);
    CHECK(!a.byte);
    CHECK(tiku_richlayout_hit(layout, NAN, 0, &a) == TIKU_RICH_INVALID);
    tiku_richlayout_release(layout);
    CHECK(!fonts.refs);
}

/** @brief Mixed runs, ligatures and combining sequences share caret mapping. */
static void
clusters(void)
{
    tiku_rich_run_source_t runs[3] = {{.length = 1},
        {.length = 2, .chars = {.present = TIKU_RICH_SIZE, .size = 24}},
        {.length = 5}};
    tiku_rich_paragraph_source_t p = {.text = "Afi e\xcc\x81Z", .length = 8,
        .runs = runs, .run_count = 3};
    fonts_t fonts = {0};
    tiku_richdoc_t *doc = document(&p, 1);
    tiku_richlayout_t *layout = job(doc, &fonts, 100);
    tiku_rich_cluster_t c;
    tiku_rich_glyph_t glyph;
    tiku_rich_rect_t rect;
    tiku_rich_pos_t pos;

    finish(layout);
    CHECK(tiku_richlayout_clusters(layout) == 5);
    NEAR(tiku_richlayout_height(layout), 24);
    CHECK(tiku_richlayout_cluster(layout, 1, &c) == TIKU_RICH_OK);
    CHECK(c.first == 1 && c.last == 3 && c.caret_count == 3);
    NEAR(c.box.x, 6); NEAR(c.box.width, 19.2); NEAR(c.baseline, 19.2);
    CHECK(tiku_richlayout_glyph(layout, c.glyph, &glyph) == TIKU_RICH_OK);
    CHECK(glyph.font == 7 && glyph.glyph == 900);
    CHECK(tiku_richlayout_hit(layout, 15.6, 0, &pos) == TIKU_RICH_OK);
    CHECK(pos.byte == 2);
    CHECK(tiku_richlayout_caret(layout, pos, &rect) == TIKU_RICH_OK);
    NEAR(rect.x, 15.6);
    pos.byte = 5;
    CHECK(tiku_richlayout_caret(layout, pos, &rect) ==
          TIKU_RICH_INVALID_RANGE);
    pos.byte = 7;
    CHECK(tiku_richlayout_caret(layout, pos, &rect) == TIKU_RICH_OK);
    NEAR(rect.x, 37.2);
    tiku_richlayout_release(layout);
    tiku_richdoc_release(doc);
}

/** @brief Soft line breaks, tabs, empty paragraphs and list numbering. */
static void
paragraphs(void)
{
    tiku_rich_paragraph_source_t p[4] = {
        {.text = "a\tb\n", .length = 4, .list = {TIKU_RICH_LIST_DECIMAL, 0, 3}},
        {.list = {TIKU_RICH_LIST_DECIMAL, 1, 0}},
        {.text = "x", .length = 1, .list = {TIKU_RICH_LIST_DECIMAL, 0, 0}},
        {.text = "z", .length = 1, .paragraph = {
            .present = TIKU_RICH_ALIGNMENT | TIKU_RICH_SPACING,
            .align = TIKU_RICH_RIGHT, .before = 5, .after = 7}}
    };
    fonts_t fonts = {0};
    tiku_richdoc_t *doc = document(p, 4);
    tiku_richlayout_t *layout = job(doc, &fonts, 120);
    tiku_rich_line_t line;
    tiku_rich_pos_t pos;
    tiku_rich_rect_t rect;

    finish(layout);
    CHECK(tiku_richlayout_lines(layout) == 5);
    CHECK(tiku_richlayout_line(layout, 0, &line) == TIKU_RICH_OK);
    CHECK(!strcmp(line.marker, "3.") && line.last == 3);
    NEAR(line.box.x, 24); NEAR(line.box.width, 54);
    CHECK(tiku_richlayout_hit(layout, 999, 0, &pos) == TIKU_RICH_OK);
    CHECK(pos.byte == 3);
    CHECK(tiku_richlayout_line(layout, 1, &line) == TIKU_RICH_OK);
    CHECK(!line.marker[0] && line.first == 4 && line.last == 4);
    pos.byte = 4;
    CHECK(tiku_richlayout_caret(layout, pos, &rect) == TIKU_RICH_OK);
    NEAR(rect.y, 12);
    CHECK(tiku_richlayout_line(layout, 2, &line) == TIKU_RICH_OK);
    CHECK(!strcmp(line.marker, "1."));
    CHECK(tiku_richlayout_line(layout, 3, &line) == TIKU_RICH_OK);
    CHECK(!strcmp(line.marker, "4."));
    CHECK(tiku_richlayout_line(layout, 4, &line) == TIKU_RICH_OK);
    NEAR(line.box.x, 114); NEAR(line.box.y, 53);
    NEAR(tiku_richlayout_height(layout), 72);
    tiku_richlayout_release(layout);
    tiku_richdoc_release(doc);
}

/** @brief Reject invalid providers and ceilings without publishing geometry. */
static void
refusals(void)
{
    tiku_rich_paragraph_source_t p = {.text = "hello", .length = 5};
    tiku_richdoc_t *doc = document(&p, 1);
    tiku_richsnapshot_t *snapshot;
    fonts_t fonts = {0};
    tiku_richlayout_t *layout;
    tiku_richlayout_options_t options = {.width = 1,
        .provider = {&fonts, shape, retain, release, 0}};
    size_t used = tiku_richdoc_memory(doc);
    int i;

    for (i = 1; i <= 7; i++) {
        fonts.invalid = i;
        layout = job(doc, &fonts, 100);
        CHECK(tiku_richlayout_step(layout, 1000) == TIKU_RICH_INVALID);
        CHECK(tiku_richlayout_step(layout, 1) == TIKU_RICH_INVALID);
        CHECK(!tiku_richlayout_lines(layout));
        tiku_richlayout_release(layout);
        CHECK(!fonts.refs && tiku_richdoc_memory(doc) == used);
    }
    fonts.invalid = 0;
    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    options.memory_bytes = 1;
    CHECK(tiku_richlayout_create(snapshot, &options, &layout) ==
          TIKU_RICH_LIMIT && !layout);
    options.memory_bytes = 0;
    options.lines = 1;
    CHECK(tiku_richlayout_create(snapshot, &options, &layout) == TIKU_RICH_OK);
    CHECK(tiku_richlayout_step(layout, 1000) == TIKU_RICH_LIMIT);
    CHECK(tiku_richlayout_warnings(layout) & TIKU_RICH_LAYOUT_OVERFLOW);
    tiku_richlayout_release(layout);
    options.width = NAN;
    CHECK(tiku_richlayout_create(snapshot, &options, &layout) ==
          TIKU_RICH_INVALID && !layout);
    tiku_richsnapshot_release(snapshot);
    CHECK(tiku_richdoc_memory(doc) == used && !fonts.refs);
    tiku_richdoc_release(doc);
}

/** @brief Check grapheme-internal styling and paragraph alignment geometry. */
static void
style_and_alignment(void)
{
    const tiku_rich_run_source_t runs[2] = {
        {.length = 1, .chars = {.present = TIKU_RICH_BOLD, .bold = 1}},
        {.length = 3, .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}}
    };
    const tiku_rich_paragraph_source_t p[3] = {
        {.text = "a\xcc\x81x", .length = 4, .runs = runs, .run_count = 2},
        {.text = "ab cd ef", .length = 8, .paragraph = {
            .present = TIKU_RICH_ALIGNMENT, .align = TIKU_RICH_JUSTIFY}},
        {.text = "abc", .length = 3, .paragraph = {
            .present = TIKU_RICH_ALIGNMENT | TIKU_RICH_INDENT,
            .align = TIKU_RICH_CENTER, .left = 4, .right = 6, .first = 2}}
    };
    fonts_t fonts = {0};
    tiku_richdoc_t *doc = document(p, 3);
    tiku_richlayout_t *layout = job(doc, &fonts, 30);
    tiku_rich_cluster_t cluster;
    tiku_rich_line_t line;
    tiku_rich_pos_t first, last;
    tiku_rich_rect_t box;

    finish(layout);
    CHECK(tiku_richlayout_warnings(layout) & TIKU_RICH_LAYOUT_CLUSTER_STYLE);
    CHECK(tiku_richlayout_cluster(layout, 0, &cluster) == TIKU_RICH_OK);
    CHECK(cluster.last == 3 && cluster.chars.bold && !cluster.chars.italic);
    CHECK(tiku_richlayout_cluster(layout, 1, &cluster) == TIKU_RICH_OK);
    CHECK(cluster.first == 3 && cluster.chars.italic && !cluster.chars.bold);
    CHECK(tiku_richlayout_line(layout, 1, &line) == TIKU_RICH_OK);
    NEAR(line.box.width, 30);
    CHECK(line.first == 0 && line.last == 3);
    CHECK(tiku_richlayout_hit(layout, 0, line.box.y, &first) == TIKU_RICH_OK);
    CHECK(tiku_richlayout_hit(layout, 30, line.box.y + 12, &last) ==
          TIKU_RICH_OK);
    CHECK(tiku_richlayout_selection(layout, first, last, 1, &box) ==
          TIKU_RICH_OK);
    NEAR(box.width, 30);
    CHECK(tiku_richlayout_line(layout, 3, &line) == TIKU_RICH_OK);
    NEAR(line.box.x, 6);
    tiku_richlayout_release(layout);
    layout = job(doc, &fonts, 100);
    finish(layout);
    CHECK(tiku_richlayout_line(layout, 2, &line) == TIKU_RICH_OK);
    NEAR(line.box.x, 41);
    tiku_richlayout_release(layout);
    tiku_richdoc_release(doc);
}

/** @brief Compare generated monospaced wrapping against a separate oracle. */
static void
generated(void)
{
    char text[151];
    fonts_t fonts = {0};
    unsigned seed = 571, trial;
    size_t length, column, offset, end, last_space, line;
    tiku_rich_paragraph_source_t p;
    tiku_richdoc_t *doc;
    tiku_richlayout_t *layout;
    tiku_rich_line_t actual;
    tiku_rich_pos_t hit;
    tiku_rich_rect_t caret;

    for (trial = 0; trial < 1000; trial++) {
        seed = seed * 1664525u + 1013904223u;
        length = 1 + seed % 150;
        column = 1 + (seed >> 8) % 19;
        for (offset = 0; offset < length; offset++) {
            seed = seed * 1664525u + 1013904223u;
            text[offset] = (seed >> 24) % 5 ? 'x' : ' ';
        }
        p = (tiku_rich_paragraph_source_t){.text = text, .length = length};
        doc = document(&p, 1);
        layout = job(doc, &fonts, column * 6);
        if (trial & 1) finish(layout);
        else CHECK(tiku_richlayout_step(layout, 10000) == TIKU_RICH_OK);
        offset = line = 0;
        while (offset < length) {
            last_space = offset;
            end = offset;
            while (end < length && end - offset < column) {
                if (text[end] == ' ') last_space = end + 1;
                end++;
            }
            if (end < length && last_space > offset) end = last_space;
            CHECK(tiku_richlayout_line(layout, line, &actual) == TIKU_RICH_OK);
            CHECK(actual.first == offset && actual.last == end);
            NEAR(actual.box.y, line * 12);
            NEAR(actual.box.width, (end - offset) * 6);
            CHECK(tiku_richlayout_hit(layout, 0, actual.box.y, &hit) ==
                  TIKU_RICH_OK);
            CHECK(hit.byte == offset);
            CHECK(tiku_richlayout_caret(layout, hit, &caret) == TIKU_RICH_OK);
            NEAR(caret.y, actual.box.y);
            offset = end;
            line++;
        }
        CHECK(line == tiku_richlayout_lines(layout));
        tiku_richlayout_release(layout);
        tiku_richdoc_release(doc);
        CHECK(!fonts.refs);
    }
}

/** @brief Run deterministic shared-geometry and generated wrap contracts. */
int
main(void)
{
    wrapping();
    clusters();
    paragraphs();
    refusals();
    style_and_alignment();
    generated();
    puts("PASS richlayout (1,000 generated wrap/hit/caret oracles)");
    return 0;
}
