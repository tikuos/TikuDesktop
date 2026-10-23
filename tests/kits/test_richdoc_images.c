/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_images.c - image ownership, transactions and retained layout.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include "tiku_richlayout.h"
#include "tiku_richview.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t pixels[] = {0xffff0000, 0x8000ff00, 0xff0000ff, 0};
static const tiku_rich_image_t picture = {2, 2, pixels, 24, 24, "A & B"};

/** @brief Publish a single operation on the first paragraph. */
static tiku_rich_status_t
edit(tiku_richdoc_t *doc, tiku_rich_edit_kind_t kind, size_t first,
     size_t last, const tiku_rich_image_t *image)
{
    tiku_rich_edit_t e = {.kind = kind,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .start = first, .end = last, .image = image};
    tiku_rich_transaction_t tx = {0, &e, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Check imported object mapping and reject malformed resources. */
static void
validation(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_image_t bad = picture;
    tiku_rich_run_source_t run = {.length = 3, .image = &bad};
    tiku_rich_paragraph_source_t p = {.text = "\xef\xbf\xbc", .length = 3,
        .runs = &run, .run_count = 1};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    bad.width = NAN;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID);
    bad = picture;
    bad.pixel_width = UINT32_MAX;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_LIMIT);
    bad = picture;
    bad.pixels = NULL;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID);
    bad = picture;
    p.text = "abc";
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_RANGE);
    p.text = "\xef\xbf\xbc";
    run.image = NULL;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_UNSUPPORTED);
    run.image = &picture;
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[0]->value.runs[0].image->pixels != pixels);
    assert(!memcmp(doc->root->paragraphs[0]->value.runs[0].image->pixels,
                   pixels, sizeof pixels));
    assert(edit(doc, TIKU_RICH_IMAGE_SET, 1, 3, &picture) ==
           TIKU_RICH_INVALID_RANGE);
    assert(edit(doc, TIKU_RICH_IMAGE_INSERT, 1, 3, &picture) ==
           TIKU_RICH_INVALID_RANGE);
    tiku_richdoc_release(doc);
}

/** @brief Check resize sharing, undo, deletion, split/join and styled paste. */
static void
ownership(void)
{
    tiku_richdoc_t *doc, *other;
    tiku_richsnapshot_t *old;
    tiku_richfragment_t *fragment;
    tiku_rich_image_t resized;
    tiku_rich_paragraph_t p;
    tiku_rich_pos_t first, last;
    size_t empty_bytes;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    empty_bytes = tiku_richdoc_memory(doc);
    assert(edit(doc, TIKU_RICH_IMAGE_INSERT, 0, 0, &picture) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(old, 0, &p) == TIKU_RICH_OK);
    resized = *p.runs[0].image;
    resized.width = 48;
    resized.alt = "Resized";
    assert(edit(doc, TIKU_RICH_IMAGE_SET, 0, 3, &resized) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[0]->value.runs[0].image->pixels ==
           resized.pixels);
    assert(p.runs[0].image->width == 24);
    assert(!strcmp(p.runs[0].image->alt, "A & B"));
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[0]->value.runs[0].image->width == 24);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    assert(edit(doc, TIKU_RICH_SPLIT, 0, 0, NULL) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[1]->value.runs[0].image->width == 48);
    assert(edit(doc, TIKU_RICH_JOIN, 0, 0, NULL) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[0]->value.runs[0].image->width == 48);
    first = (tiku_rich_pos_t){1, p.id, 0, TIKU_RICH_BEFORE};
    last = (tiku_rich_pos_t){1, p.id, 3, TIKU_RICH_AFTER};
    assert(tiku_richfragment_create(old, first, last, &fragment) ==
           TIKU_RICH_OK);
    assert(tiku_richdoc_create(NULL, &other) == TIKU_RICH_OK);
    first.paragraph = last.paragraph = other->root->paragraphs[0]->value.id;
    last.byte = 0;
    assert(tiku_richdoc_paste_fragment(other, 0, first, last, fragment) ==
           TIKU_RICH_OK);
    assert(other->root->paragraphs[0]->value.runs[0].image->pixels !=
           resized.pixels);
    tiku_richfragment_release(fragment);
    tiku_richsnapshot_release(old);
    assert(edit(doc, TIKU_RICH_REPLACE, 0, 3, NULL) == TIKU_RICH_OK);
    tiku_richdoc_clear_history(doc);
    assert(tiku_richdoc_memory(doc) <= empty_bytes + 64);
    tiku_richdoc_release(doc);
    assert(!memcmp(other->root->paragraphs[0]->value.runs[0].image->pixels,
                   pixels, sizeof pixels));
    tiku_richdoc_release(other);
}

/** @brief Allocation counter with an independently armed failure index. */
typedef struct { size_t calls, fail, live; } allocator_t;
/** @brief Allocate with an injected failure, tracking successful ownership. */
static void *
allocate(void *ctx, size_t bytes)
{
    allocator_t *a = ctx;
    void *p;

    if (++a->calls == a->fail) return NULL;
    p = malloc(bytes);
    if (p) a->live++;
    return p;
}
/** @brief Release one successful allocation. */
static void
release(void *ctx, void *p)
{
    allocator_t *a = ctx;

    assert(a->live);
    a->live--;
    free(p);
}

/** @brief Require insertion failures to retain text, generation and history. */
static void
faults(void)
{
    size_t failures = 0;

    for (size_t step = 1; step < 64; step++) {
        allocator_t a = {0};
        tiku_rich_limits_t limits = {.allocator = {&a, allocate, release}};
        tiku_richdoc_t *doc;
        tiku_rich_status_t status;
        size_t bytes;

        assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
        bytes = tiku_richdoc_memory(doc);
        a.fail = a.calls + step;
        status = edit(doc, TIKU_RICH_IMAGE_INSERT, 0, 0, &picture);
        if (status == TIKU_RICH_NO_MEMORY) {
            failures++;
            assert(doc->generation == 1 && !doc->undo && !doc->root->bytes);
            assert(tiku_richdoc_memory(doc) == bytes);
        } else assert(status == TIKU_RICH_OK);
        tiku_richdoc_release(doc);
        assert(!a.live);
        if (status == TIKU_RICH_OK) break;
    }
    assert(failures >= 8);
}

/** @brief Supply one complete grapheme; object runs must bypass this call. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
      size_t length, const uint8_t *boundaries, tiku_rich_shape_t *out)
{
    size_t end = 1;

    (void)ctx; (void)chars;
    assert(memcmp(text, "\xef\xbf\xbc", length < 3 ? length : 3));
    while (end < length && !boundaries[end]) end++;
    *out = (tiku_rich_shape_t){.bytes = end, .advance = 6,
        .ascent = 9, .descent = 3, .caret_count = 2};
    out->carets[1] = (tiku_rich_shape_caret_t){end, 6};
    return TIKU_RICH_OK;
}

/** @brief Image boxes, atomic caret movement and combining text agree. */
static void
layout(void)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_t *layout;
    tiku_richlayout_options_t options = {.width = 60,
        .provider = {.shape = shape}};
    tiku_rich_cluster_t c;
    tiku_rich_pos_t hit;
    tiku_rich_edit_t e = {.kind = TIKU_RICH_REPLACE, .start = 3, .end = 3,
        .text = "\xcc\x81x", .length = 3};
    tiku_rich_transaction_t tx = {0, &e, 1};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(edit(doc, TIKU_RICH_IMAGE_INSERT, 0, 0, &picture) == TIKU_RICH_OK);
    e.paragraph = doc->root->paragraphs[0]->value.id;
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richlayout_create(snapshot, &options, &layout) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    while (tiku_richlayout_step(layout, 1) == TIKU_RICH_PENDING) { }
    assert(tiku_richlayout_status(layout) == TIKU_RICH_OK);
    assert(tiku_richlayout_cluster(layout, 0, &c) == TIKU_RICH_OK);
    assert(c.image && c.first == 0 && c.last == 3 && c.box.width == 24);
    assert(c.box.height >= 24 && c.glyph_count == 0 && c.caret_count == 2);
    assert(tiku_richlayout_hit(layout, 18, 12, &hit) == TIKU_RICH_OK);
    assert(hit.byte == 3);
    tiku_richlayout_release(layout);
}

/** @brief Select pictures and publish one resize edit on pointer release. */
static void
interaction(void)
{
    tiku_richdoc_t *doc;
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = 120,
        .provider = {.shape = shape}};
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = 12, .y = 12};
    tiku_rich_rect_t box;
    uint64_t generation;
    int handled;

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(edit(doc, TIKU_RICH_IMAGE_INSERT, 0, 0, &picture) == TIKU_RICH_OK);
    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    while (tiku_richview_step(view, 1024) == TIKU_RICH_PENDING) { }
    assert(tiku_richview_event(view, &e, &handled) == TIKU_RICH_OK && handled);
    assert(tiku_richview_picture(view, &box) == TIKU_RICH_OK);
    assert(box.width == 24 && box.height == 24);
    e.x = e.y = 24;
    assert(tiku_richview_event(view, &e, &handled) == TIKU_RICH_OK && handled);
    generation = tiku_richdoc_generation(doc);
    e.type = TIKU_EVENT_POINTER_MOVE;
    e.x = e.y = 48;
    assert(tiku_richview_event(view, &e, &handled) == TIKU_RICH_OK && handled);
    assert(tiku_richdoc_generation(doc) == generation);
    assert(tiku_richview_picture(view, &box) == TIKU_RICH_OK);
    assert(box.width == 48 && box.height == 48);
    e.type = TIKU_EVENT_POINTER_UP;
    assert(tiku_richview_event(view, &e, &handled) == TIKU_RICH_OK && handled);
    assert(tiku_richdoc_generation(doc) == generation + 1);
    assert(doc->root->paragraphs[0]->value.runs[0].image->width == 48);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[0]->value.runs[0].image->width == 24);
    while (tiku_richview_step(view, 1024) == TIKU_RICH_PENDING) { }
    e.type = TIKU_EVENT_POINTER_DOWN;
    e.x = e.y = 24;
    assert(tiku_richview_event(view, &e, &handled) == TIKU_RICH_OK);
    e.type = TIKU_EVENT_POINTER_MOVE;
    e.x = e.y = 72;
    assert(tiku_richview_event(view, &e, &handled) == TIKU_RICH_OK);
    e.type = TIKU_EVENT_DEACTIVATED;
    tiku_richview_event(view, &e, &handled);
    assert(tiku_richview_picture(view, &box) == TIKU_RICH_OK && box.width == 24);
    tiku_richdoc_set_read_only(doc, 1);
    e.type = TIKU_EVENT_POINTER_DOWN;
    e.x = e.y = 24;
    tiku_richview_event(view, &e, &handled);
    e.type = TIKU_EVENT_POINTER_MOVE;
    e.x = e.y = 60;
    tiku_richview_event(view, &e, &handled);
    e.type = TIKU_EVENT_POINTER_UP;
    tiku_richview_event(view, &e, &handled);
    assert(doc->root->paragraphs[0]->value.runs[0].image->width == 24);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
}

/** @brief Run checked model, history, ownership, fault and layout cases. */
int
main(void)
{
    validation();
    ownership();
    faults();
    layout();
    interaction();
    puts("rich images: validation, ownership, history, faults and layout pass");
    return 0;
}
