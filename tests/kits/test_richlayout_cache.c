/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richlayout_cache.c - unchanged-paragraph reuse and reflow equivalence.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview.h"
#include "tiku_richlayout_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Provider lifetime and shaping-call counters. */
typedef struct { size_t calls, refs; } fonts_t;

/** @brief Shape one grapheme using size-dependent metrics. */
static tiku_rich_status_t
shape(void *context, const tiku_rich_chars_t *chars, const char *text,
      size_t length, const uint8_t *boundaries, tiku_rich_shape_t *out)
{
    fonts_t *fonts = context;
    size_t end = 1;
    double width = chars->size * 0.5;

    fonts->calls++;
    while (end < length && !boundaries[end]) end++;
    *out = (tiku_rich_shape_t){.bytes = end, .glyph_count = 1,
        .caret_count = 2, .advance = width, .ascent = chars->size * 0.75,
        .descent = chars->size * 0.25,
        .glyphs = {{1, (unsigned char)*text, 0, 0}},
        .carets = {{0, 0}, {end, width}}};
    if (*text == '?') out->warnings = TIKU_RICH_LAYOUT_FALLBACK;
    return TIKU_RICH_OK;
}

/** @brief Count a provider reference owned by a view or layout. */
static void
retain(void *context)
{
    ((fonts_t *)context)->refs++;
}

/** @brief Drop a counted provider reference. */
static void
release(void *context)
{
    fonts_t *fonts = context;

    assert(fonts->refs);
    fonts->refs--;
}

/** @brief Complete incremental view work without exposing partial geometry. */
static void
finish(tiku_richview_t *view)
{
    tiku_rich_status_t status;
    size_t steps = 0;

    do {
        status = tiku_richview_step(view, 1);
        assert(++steps < 1000000);
        if (status == TIKU_RICH_PENDING) assert(!tiku_richview_layout(view));
    } while (status == TIKU_RICH_PENDING);
    assert(status == TIKU_RICH_OK);
}

/** @brief Complete a standalone layout with bounded calls. */
static void
complete(tiku_richlayout_t *layout)
{
    tiku_rich_status_t status;
    size_t work = 0;

    do {
        status = tiku_richlayout_step(layout, 7);
        assert(++work < 1000000);
    } while (status == TIKU_RICH_PENDING);
    assert(status == TIKU_RICH_OK);
    assert(!layout->previous && !layout->cache_index);
}

/** @brief Create a current-generation layout with optional prior geometry. */
static tiku_richlayout_t *
layout_for(tiku_richdoc_t *doc, const tiku_richlayout_options_t *options,
           const tiku_richlayout_t *previous)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_t *layout;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richlayout_create_reusing(snapshot, options, previous,
                                         &layout) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    return layout;
}

/** @brief Check geometry allowing only small floating-point translation error. */
static void
rect_equal(tiku_rich_rect_t a, tiku_rich_rect_t b)
{
    assert(fabs(a.x - b.x) < 1e-7 && fabs(a.y - b.y) < 1e-7);
    assert(fabs(a.width - b.width) < 1e-7 && fabs(a.height - b.height) < 1e-7);
}

/** @brief Compare cached geometry, hits and carets with independent full reflow. */
static void
oracle(const tiku_richlayout_t *layout)
{
    tiku_richlayout_t *cold;

    assert(tiku_richlayout_create(&layout->source, &layout->options, &cold) ==
           TIKU_RICH_OK);
    complete(cold);
    assert(layout->nlines == cold->nlines && layout->nclusters == cold->nclusters);
    assert(layout->nglyphs == cold->nglyphs && layout->ncarets == cold->ncarets);
    assert(layout->ngrids == cold->ngrids && layout->warnings == cold->warnings);
    assert(fabs(layout->y - cold->y) < 1e-7);
    if (cold->nglyphs)
        assert(!memcmp(layout->glyphs, cold->glyphs, cold->nglyphs * sizeof *cold->glyphs));
    if (cold->ncarets)
        assert(!memcmp(layout->carets, cold->carets, cold->ncarets * sizeof *cold->carets));
    for (size_t i = 0; i < cold->nlines; i++) {
        tiku_rich_line_t a = layout->lines[i], b = cold->lines[i];
        tiku_rich_pos_t p, q;
        tiku_rich_rect_t r, s;

        rect_equal(a.box, b.box);
        assert(fabs(a.baseline - b.baseline) < 1e-7);
        assert(fabs(a.marker_x - b.marker_x) < 1e-7);
        a.box = b.box; a.baseline = b.baseline; a.marker_x = b.marker_x;
        assert(!memcmp(&a, &b, sizeof a));
        assert(tiku_richlayout_hit(layout, a.box.x + 0.3, a.box.y + 0.3, &p) ==
               TIKU_RICH_OK);
        assert(tiku_richlayout_hit(cold, a.box.x + 0.3, a.box.y + 0.3, &q) ==
               TIKU_RICH_OK);
        assert(p.paragraph == q.paragraph && p.byte == q.byte && p.affinity == q.affinity);
        assert(tiku_richlayout_caret(layout, p, &r) == TIKU_RICH_OK);
        assert(tiku_richlayout_caret(cold, q, &s) == TIKU_RICH_OK);
        rect_equal(r, s);
    }
    for (size_t i = 0; i < cold->nclusters; i++) {
        tiku_rich_cluster_t a = layout->clusters[i], b = cold->clusters[i];

        rect_equal(a.box, b.box);
        assert(fabs(a.baseline - b.baseline) < 1e-7);
        a.box = b.box; a.baseline = b.baseline;
        assert(!memcmp(&a, &b, sizeof a));
    }
    for (size_t i = 0; i < cold->ngrids; i++) {
        tiku_rich_grid_box_t a = layout->grids[i], b = cold->grids[i];

        rect_equal(a.box, b.box);
        a.box = b.box;
        assert(!memcmp(&a, &b, sizeof a));
    }
    tiku_richlayout_release(cold);
}

/** @brief Apply one edit without changing cache ownership. */
static void
edit(tiku_richdoc_t *doc, tiku_rich_edit_t change)
{
    tiku_rich_transaction_t tx = {0, &change, 1};

    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
}

/** @brief Replace one completed cache and check its full-reflow equivalent. */
static void
advance(tiku_richdoc_t *doc, tiku_richlayout_options_t *options,
        tiku_richlayout_t **cache, size_t reused)
{
    tiku_richlayout_t *next = layout_for(doc, options, *cache);

    tiku_richlayout_release(*cache);
    complete(next);
    assert(tiku_richlayout_reused(next) == reused);
    oracle(next);
    *cache = next;
}

/** @brief Verify that one edited paragraph does not reshape its neighbours. */
static void
view_reuse(void)
{
    tiku_rich_paragraph_source_t p[3] = {
        {.text = "first paragraph", .length = 15},
        {.text = "a\tb c\n", .length = 6},
        {.text = "unchanged tail", .length = 14}
    };
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 3};
    fonts_t fonts = {0};
    tiku_richlayout_options_t options = {.width = 80,
        .provider = {&fonts, shape, retain, release, 1}};
    tiku_richdoc_t *doc;
    tiku_richview_t *view;
    size_t calls;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    finish(view);
    calls = fonts.calls;
    assert(tiku_richsession_type(tiku_richview_session(view), 0, "X", 1) ==
           TIKU_RICH_OK);
    finish(view);
    assert(fonts.calls - calls == 17);
    assert(tiku_richlayout_reused(tiku_richview_layout(view)) == 2);
    oracle(tiku_richview_layout(view));
    assert(fonts.refs == 2);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
    assert(!fonts.refs);
}

/** @brief Reuse paragraph identities through splits, joins, lists and undo. */
static void
editing(void)
{
    tiku_rich_paragraph_source_t p[5] = {
        {.text = "head?", .length = 5},
        {.text = "a\tb c\n", .length = 6, .list = {TIKU_RICH_LIST_DECIMAL, 0, 9}},
        {.text = "item", .length = 4, .list = {TIKU_RICH_LIST_DECIMAL, 0, 0}},
        {.text = "e\xcc\x81 long line\n", .length = 14,
         .paragraph = {.present = TIKU_RICH_ALIGNMENT | TIKU_RICH_SPACING,
                       .align = TIKU_RICH_JUSTIFY, .before = 2.5, .after = 3.25}},
        {0}
    };
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 5};
    fonts_t fonts = {0};
    tiku_richlayout_options_t options = {.width = 82,
        .defaults = {.present = TIKU_RICH_SIZE, .size = 11.7f},
        .leading = 1.3, .tab = 23,
        .provider = {&fonts, shape, retain, release, 1}};
    tiku_richdoc_t *doc;
    tiku_richlayout_t *cache;
    uint64_t first, item;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    cache = layout_for(doc, &options, NULL);
    complete(cache);
    first = doc->root->paragraphs[0]->value.id;
    item = doc->root->paragraphs[1]->value.id;
    advance(doc, &options, &cache, 5);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
        .paragraph = first, .start = 4, .end = 5, .text = "longer", .length = 6});
    advance(doc, &options, &cache, 4);
    assert(!cache->warnings);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_SPLIT,
        .paragraph = item, .start = 2, .end = 2});
    advance(doc, &options, &cache, 3);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    advance(doc, &options, &cache, 3);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_JOIN, .paragraph = first});
    advance(doc, &options, &cache, 2);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    advance(doc, &options, &cache, 2);
    options.width = 31;
    advance(doc, &options, &cache, 0);
    options.provider.generation++;
    advance(doc, &options, &cache, 0);
    options.tab = 29;
    advance(doc, &options, &cache, 0);
    options.leading = 2;
    advance(doc, &options, &cache, 0);
    options.defaults.size = 15;
    advance(doc, &options, &cache, 0);
    tiku_richlayout_release(cache);
    tiku_richdoc_release(doc);
    assert(!fonts.refs);
}

/** @brief Named-style changes invalidate shared paragraphs with inherited runs. */
static void
styles(void)
{
    tiku_rich_style_source_t style = {.name = "Body",
        .kind = TIKU_RICH_PARAGRAPH_STYLE,
        .chars = {.present = TIKU_RICH_SIZE, .size = 10}};
    tiku_rich_paragraph_source_t p[2] = {
        {.text = "first", .length = 5, .style = 1},
        {.text = "second", .length = 6, .style = 1}};
    tiku_rich_source_t source = {.styles = &style, .style_count = 1,
        .paragraphs = p, .paragraph_count = 2};
    fonts_t fonts = {0};
    tiku_richlayout_options_t options = {.width = 80,
        .provider = {&fonts, shape, retain, release, 1}};
    tiku_rich_style_spec_t spec = {.kind = TIKU_RICH_PARAGRAPH_STYLE,
        .name = "Body", .chars = {.present = TIKU_RICH_SIZE, .size = 24}};
    tiku_richdoc_t *doc;
    tiku_richlayout_t *cache;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    cache = layout_for(doc, &options, NULL);
    complete(cache);
    advance(doc, &options, &cache, 2);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_STYLE_DEFINE,
        .style = doc->root->styles[0].value.id, .definition = &spec});
    advance(doc, &options, &cache, 0);
    advance(doc, &options, &cache, 2);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    advance(doc, &options, &cache, 0);
    tiku_richlayout_release(cache);
    tiku_richdoc_release(doc);
    assert(!fonts.refs);
}

/** @brief Recompute cell heights and nested grids around reused paragraphs. */
static void
tables(void)
{
    fonts_t fonts = {0};
    tiku_richlayout_options_t options = {.width = 220,
        .provider = {&fonts, shape, retain, release, 1}};
    tiku_richdoc_t *doc;
    tiku_richlayout_t *cache;
    tiku_rich_table_t table = {.rows = 2, .columns = 2, .padding = 4, .border = 1};
    uint64_t id;
    uint32_t pixel = 0xffff0000;
    tiku_rich_image_t image = {.pixels = &pixel, .pixel_width = 1,
        .pixel_height = 1, .width = 16, .height = 23, .alt = "pixel"};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[0]->value.id, .table = table});
    id = doc->root->blocks->nodes[1].id;
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_INSERT,
        .paragraph = doc->root->paragraphs[2]->value.id, .image = &image});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[3]->value.id,
        .table = {.rows = 1, .columns = 1, .padding = 2, .border = 1}});
    cache = layout_for(doc, &options, NULL);
    complete(cache);
    advance(doc, &options, &cache, doc->root->count);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
        .paragraph = doc->root->paragraphs[1]->value.id,
        .text = "many long words to increase row height", .length = 37});
    advance(doc, &options, &cache, doc->root->count - 1);
    table.weights[0] = 1; table.weights[1] = 2;
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_SET,
        .object = id, .table = table});
    advance(doc, &options, &cache, 2);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_CELL_SET,
        .object = doc->root->blocks->nodes[2].id, .background = 0xffabcd00});
    advance(doc, &options, &cache, doc->root->count);
    tiku_richlayout_release(cache);
    tiku_richdoc_release(doc);
    assert(!fonts.refs);
}

/** @brief A report-sized edit shapes only its changed paragraph. */
static void
large_document(void)
{
    tiku_rich_paragraph_source_t *p = calloc(1000, sizeof *p);
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 1000};
    fonts_t fonts = {0};
    tiku_richlayout_options_t options = {.width = 450,
        .provider = {&fonts, shape, retain, release, 1}};
    tiku_richdoc_t *doc;
    tiku_richlayout_t *cache, *next;
    size_t cold, warm;

    assert(p);
    for (size_t i = 0; i < 1000; i++)
        p[i] = (tiku_rich_paragraph_source_t){
            .text = "A paragraph with words, spaces, and enough content to wrap.",
            .length = 58};
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    free(p);
    cache = layout_for(doc, &options, NULL);
    complete(cache);
    cold = fonts.calls;
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
        .paragraph = doc->root->paragraphs[500]->value.id, .text = "X", .length = 1});
    next = layout_for(doc, &options, cache);
    tiku_richlayout_release(cache);
    complete(next);
    warm = fonts.calls - cold;
    assert(tiku_richlayout_reused(next) == 999 && warm == 60);
    printf("1,000 paragraphs: %zu initial shaping calls; %zu after one edit\n", cold, warm);
    oracle(next);
    tiku_richlayout_release(next);
    tiku_richdoc_release(doc);
    assert(!fonts.refs);
}

/** @brief Run cached-reflow regressions. */
int
main(void)
{
    view_reuse();
    editing();
    styles();
    tables();
    large_document();
    puts("PASS richlayout cache");
    return 0;
}
