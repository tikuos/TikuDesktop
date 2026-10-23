/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richruler.c - indentation geometry, bounds, capture and history.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richruler.h"
#include "tiku_richdoc_private.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Supply six-point graphemes without platform fonts. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
    size_t length, const uint8_t *boundaries, tiku_rich_shape_t *out)
{
    size_t end = 1;

    (void)ctx; (void)chars; (void)text;
    while (end < length && !boundaries[end]) end++;
    *out = (tiku_rich_shape_t){.bytes = end, .glyph_count = 1,
        .caret_count = 2, .advance = 6, .ascent = 9, .descent = 3,
        .glyphs = {{1, 1, 0, 0}}, .carets = {{0, 0}, {end, 6}}};
    return TIKU_RICH_OK;
}

/** @brief Complete a bounded layout before reading ruler geometry. */
static void
finish(tiku_richview_t *view)
{
    unsigned steps = 0;
    tiku_rich_status_t status;

    do {
        status = tiku_richview_step(view, 100);
        assert(++steps < 10000);
    } while (status == TIKU_RICH_PENDING);
    assert(status == TIKU_RICH_OK);
}

/** @brief Read text boundaries independently of the paragraph alignment. */
static void
geometry(void)
{
    tiku_richdoc_t *doc;
    tiku_richview_t *view;
    tiku_rich_ruler_t ruler;
    tiku_richlayout_options_t options = {.width = 300,
        .provider = {.shape = shape}};
    tiku_rich_paragraph_source_t p = {.text = "text", .length = 4,
        .paragraph = {.present = TIKU_RICH_INDENT | TIKU_RICH_ALIGNMENT,
            .left = 20, .right = 30, .first = -10,
            .align = TIKU_RICH_CENTER}};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    finish(view);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(ruler.origin == 0 && ruler.width == 300);
    assert(ruler.position[TIKU_RICH_RULER_FIRST] == 10);
    assert(ruler.position[TIKU_RICH_RULER_LEFT] == 20);
    assert(ruler.position[TIKU_RICH_RULER_RIGHT] == 270);
    assert(ruler.enabled == 7 && !ruler.mixed && ruler.paragraphs == 1);
    assert(ruler.minimum[1] == 10 && ruler.maximum[1] == 269);
    assert(ruler.minimum[2] == 21 && ruler.maximum[2] == 300);
    tiku_rich_ruler_t preview;
    uint64_t generation = doc->generation;
    assert(tiku_richruler_preview(&ruler, 1, -100, &preview) == TIKU_RICH_OK);
    assert(preview.position[1] == 10 && preview.position[0] == 0);
    assert(tiku_richruler_preview(&ruler, 1, 1000, &preview) == TIKU_RICH_OK);
    assert(preview.position[1] == 269 && preview.position[0] == 259);
    assert(doc->generation == generation);
    assert(tiku_richruler_apply(view, &ruler, 1, 20) == TIKU_RICH_NO_CHANGE);
    assert(doc->generation == generation && !tiku_richdoc_dirty(doc));
    assert(tiku_richruler_apply(view, &ruler, 1, 40) == TIKU_RICH_OK);
    assert(doc->generation == generation + 1);
    assert(doc->root->paragraphs[0]->value.paragraph.left == 40);
    assert(doc->root->paragraphs[0]->value.paragraph.first == -10);
    assert(doc->root->paragraphs[0]->value.paragraph.right == 30);
    assert(doc->root->paragraphs[0]->value.paragraph.align == TIKU_RICH_CENTER);
    assert(tiku_richruler_apply(view, &ruler, 2, 100) == TIKU_RICH_STALE);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(!tiku_richdoc_dirty(doc));
    finish(view);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(tiku_richruler_apply(view, &ruler, 0, 50) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[0]->value.paragraph.first == 30);
    finish(view);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(tiku_richruler_apply(view, &ruler, 2, -100) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[0]->value.paragraph.right == 249);
    assert(doc->root->paragraphs[0]->value.paragraph.left == 20);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Select paragraph endpoints through the public session interface. */
static void
select_range(tiku_richdoc_t *doc, tiku_richview_t *view,
    size_t first, size_t last, size_t byte)
{
    tiku_rich_pos_t a = {doc->root->body, doc->root->paragraphs[first]->value.id,
        0, TIKU_RICH_AFTER};
    tiku_rich_pos_t b = {doc->root->body, doc->root->paragraphs[last]->value.id,
        byte, TIKU_RICH_BEFORE};

    assert(tiku_richsession_select(tiku_richview_session(view), 0, a, b) ==
           TIKU_RICH_OK);
}

/** @brief Preserve per-paragraph values, inherited styles and list spacing. */
static void
selections(void)
{
    tiku_richdoc_t *doc;
    tiku_richview_t *view;
    tiku_rich_ruler_t ruler;
    tiku_richlayout_options_t options = {.width = 300, .provider = {.shape = shape}};
    tiku_rich_style_source_t style = {.name = "Inset", .paragraph = {
        .present = TIKU_RICH_INDENT, .left = 20, .right = 30, .first = -10}};
    tiku_rich_paragraph_source_t p[3] = {
        {.text = "one", .length = 3, .style = 1},
        {.text = "two", .length = 3, .paragraph = {.present = TIKU_RICH_INDENT,
            .left = 40, .right = 50, .first = 12},
            .list = {.kind = TIKU_RICH_LIST_DECIMAL, .level = 2, .restart = 1000}},
        {.text = "three", .length = 5}};
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 3,
        .styles = &style, .style_count = 1};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    finish(view);
    select_range(doc, view, 0, 2, 0);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(ruler.paragraphs == 2 && ruler.mixed == 7);
    assert(ruler.maximum[1] == 237);
    assert(tiku_richruler_apply(view, &ruler, 1, 100) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[0]->value.paragraph.left == 100);
    assert(doc->root->paragraphs[0]->value.paragraph.first == -10);
    assert(doc->root->paragraphs[0]->value.style != 0);
    assert(doc->root->paragraphs[1]->value.paragraph.left == 34);
    assert(doc->root->paragraphs[1]->value.paragraph.first == 12);
    assert(doc->root->paragraphs[1]->value.paragraph.right == 50);
    assert(doc->root->paragraphs[1]->value.list.restart == 1000);
    assert(!doc->root->paragraphs[2]->value.paragraph.present);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(!doc->root->paragraphs[0]->value.paragraph.present);
    assert(doc->root->paragraphs[1]->value.paragraph.left == 40);
    assert(!tiku_richdoc_dirty(doc));
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    finish(view);
    select_range(doc, view, 1, 1, 0);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(ruler.position[1] == 100 && ruler.position[0] == 112);
    tiku_rich_line_t line;
    assert(tiku_richlayout_line(tiku_richview_layout(view), 1, &line) == TIKU_RICH_OK);
    assert(line.box.x == ruler.position[0]);
    tiku_richview_reflow(view); finish(view);
    assert(tiku_richlayout_reused(tiku_richview_layout(view)) == 3);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(ruler.position[1] == 100 && ruler.position[0] == 112);
    assert(tiku_richview_viewport(view, 600, 200, 2) == TIKU_RICH_OK);
    assert(tiku_richruler_apply(view, &ruler, 0, 130) == TIKU_RICH_STALE);
    finish(view);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(ruler.position[0] == 112 && ruler.width == 300 && ruler.zoom == 2);
    select_range(doc, view, 1, 1, 1);
    assert(tiku_richruler_apply(view, &ruler, 0, 130) == TIKU_RICH_STALE);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    tiku_richsession_set_read_only(tiku_richview_session(view), 1);
    assert(tiku_richruler_apply(view, &ruler, 0, 130) == TIKU_RICH_READ_ONLY);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK && !ruler.enabled);
    tiku_richsession_set_read_only(tiku_richview_session(view), 0);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    select_range(doc, view, 0, 0, 0);
    assert(tiku_richruler_apply(view, &ruler, 0, 130) == TIKU_RICH_STALE);
    tiku_richview_destroy(view); tiku_richdoc_release(doc);
}

/** @brief Use actual cell padding and reject selections crossing text owners. */
static void
cells(void)
{
    tiku_richdoc_t *doc;
    tiku_richview_t *view;
    tiku_rich_ruler_t ruler;
    tiku_richlayout_options_t options = {.width = 300, .provider = {.shape = shape}};
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_INSERT,
        .table = {.rows = 1, .columns = 2, .padding = 4, .border = 1}};
    tiku_rich_transaction_t tx = {0, &edit, 1};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    edit.paragraph = doc->root->paragraphs[0]->value.id;
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    finish(view);
    select_range(doc, view, 1, 1, 0);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(ruler.origin == 5 && ruler.width == 140);
    assert(ruler.position[1] == 5 && ruler.position[2] == 145);
    assert(tiku_richruler_apply(view, &ruler, 0, 15) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[1]->value.paragraph.first == 10);
    finish(view);
    select_range(doc, view, 2, 2, 0);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(ruler.origin == 155 && ruler.width == 140);
    select_range(doc, view, 0, 2, 0);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_UNSUPPORTED);
    assert(!ruler.generation && !ruler.paragraphs);
    tiku_richview_destroy(view); tiku_richdoc_release(doc);
}

/** @brief Count allocations and refuse a chosen call. */
typedef struct { size_t calls, fail, live; } heap_t;

/** @brief Allocate counted storage or return an injected refusal. */
static void *
allocate(void *context, size_t bytes)
{
    heap_t *heap = context;
    void *pointer;

    if (++heap->calls == heap->fail) return NULL;
    pointer = malloc(bytes);
    if (pointer) ++heap->live;
    return pointer;
}

/** @brief Release one counted allocation. */
static void
release(void *context, void *pointer)
{
    heap_t *heap = context;

    assert(pointer && heap->live);
    --heap->live;
    free(pointer);
}

/** @brief Refuse each allocation with no content, history or selection change. */
static void
faults(void)
{
    size_t refusals = 0;

    for (size_t failure = 1; failure < 100; failure++) {
        heap_t heap = {0};
        tiku_rich_limits_t limits = {.allocator = {&heap, allocate, release}};
        tiku_richdoc_t *doc;
        tiku_richview_t *view;
        tiku_rich_ruler_t ruler;
        tiku_richlayout_options_t options = {.width = 300, .provider = {.shape = shape}};
        tiku_rich_status_t status;
        size_t baseline;
        tiku_rich_paragraph_source_t p[3] = {
            {.text = "one", .length = 3}, {.text = "two", .length = 3},
            {.text = "three", .length = 5}};
        tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 3};

        assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
        assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
        assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
        finish(view);
        select_range(doc, view, 0, 2, 5);
        assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
        baseline = tiku_richdoc_memory(doc);
        heap.fail = heap.calls + failure;
        status = tiku_richruler_apply(view, &ruler, 1, 30);
        if (status != TIKU_RICH_OK) {
            assert(status == TIKU_RICH_NO_MEMORY);
            assert(tiku_richdoc_memory(doc) == baseline);
            assert(doc->generation == ruler.generation);
            assert(!tiku_richdoc_dirty(doc));
            tiku_rich_ruler_t after;
            assert(tiku_richruler_read(view, &after) == TIKU_RICH_OK);
            assert(after.anchor == ruler.anchor && after.caret == ruler.caret);
            assert(tiku_richdoc_undo(doc) == TIKU_RICH_NO_CHANGE);
            refusals++;
        }
        tiku_richview_destroy(view); tiku_richdoc_release(doc);
        assert(!heap.live);
        if (status == TIKU_RICH_OK) break;
    }
    assert(refusals > 3 && refusals < 98);
    printf("richruler: %zu allocation refusals checked\n", refusals);
}

/** @brief Refuse invalid input and oversized ranges before allocating edits. */
static void
validation(void)
{
    tiku_richdoc_t *doc;
    tiku_richview_t *view, *other;
    tiku_rich_ruler_t ruler, preview;
    tiku_richlayout_options_t options = {.width = 300, .provider = {.shape = shape}};
    tiku_rich_paragraph_source_t *p = calloc(1025, sizeof *p);
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 1025};

    assert(p);
    for (size_t i = 0; i < 1025; i++) { p[i].text = "x"; p[i].length = 1; }
    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    free(p);
    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_PENDING && !ruler.enabled);
    finish(view);
    assert(tiku_richruler_read(NULL, &ruler) == TIKU_RICH_INVALID);
    assert(tiku_richruler_read(view, NULL) == TIKU_RICH_INVALID);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(tiku_richruler_preview(&ruler, 3, 1, &preview) == TIKU_RICH_INVALID);
    assert(!preview.generation);
    assert(tiku_richruler_preview(&ruler, 0, NAN, &preview) == TIKU_RICH_INVALID);
    assert(tiku_richruler_apply(view, &ruler, 1, INFINITY) == TIKU_RICH_INVALID);
    preview = ruler;
    assert(tiku_richruler_preview(&preview, 0, 30, &preview) == TIKU_RICH_OK);
    assert(preview.position[0] == 30);
    assert(tiku_richruler_apply(view, &preview, 0, 30) == TIKU_RICH_STALE);
    assert(tiku_richview_create(doc, &options, &other) == TIKU_RICH_OK);
    finish(other);
    assert(tiku_richruler_apply(other, &ruler, 1, 20) == TIKU_RICH_STALE);
    select_range(doc, view, 0, 1024, 1);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_LIMIT);
    assert(!ruler.enabled && !ruler.paragraphs);
    select_range(doc, view, 0, 1024, 0);
    assert(tiku_richruler_read(view, &ruler) == TIKU_RICH_OK);
    assert(ruler.paragraphs == 1024);
    assert(tiku_richruler_apply(view, &ruler, 0, 12) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[1023]->value.paragraph.first == 12);
    assert(!doc->root->paragraphs[1024]->value.paragraph.present);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK && !tiku_richdoc_dirty(doc));
    tiku_richview_destroy(other); tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Run the headless ruler regressions. */
int
main(void)
{
    geometry(); selections(); cells(); faults(); validation();
    puts("richruler: geometry and editing pass");
    return 0;
}
