/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richlayout_faults.c - allocation, cancellation and provider refusals.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout.h"
#include "tiku_richview.h"
#include "tiku_richlayout_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL layout faults:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief An allocator with one selected refusal and live-block accounting. */
typedef struct { size_t calls, fail, live, bytes, peak; } faults_t;
/** @brief Preserve alignment while recording the charged allocation size. */
typedef union { max_align_t align; size_t bytes; } allocation_t;
static unsigned injected;

/** @brief Refuse the selected call, otherwise count a malloc-owned block. */
static void *
allocate(void *context, size_t bytes)
{
    faults_t *f = context;
    allocation_t *result;

    if (++f->calls == f->fail) { injected++; return NULL; }
    result = malloc(sizeof *result + bytes);
    CHECK(result);
    result->bytes = bytes;
    f->bytes += bytes;
    if (f->bytes > f->peak) f->peak = f->bytes;
    f->live++;
    return result + 1;
}

/** @brief Release one counted block, including shared snapshot allocations. */
static void
release(void *context, void *pointer)
{
    faults_t *f = context;
    allocation_t *header = (allocation_t *)pointer - 1;

    CHECK(f->live);
    f->live--;
    f->bytes -= header->bytes;
    free(header);
}

/** @brief Supply one grapheme with deterministic font and caret metrics. */
static tiku_rich_status_t
shape(void *context, const tiku_rich_chars_t *chars, const char *text,
      size_t length, const uint8_t *boundaries, tiku_rich_shape_t *out)
{
    size_t end = 1;

    (void)context; (void)chars; (void)text;
    while (end < length && !boundaries[end]) end++;
    *out = (tiku_rich_shape_t){.bytes = end, .glyph_count = 1,
        .caret_count = 2, .advance = 6, .ascent = 9, .descent = 3,
        .glyphs = {{1, 1, 0, 0}}, .carets = {{0, 0}, {end, 6}}};
    return TIKU_RICH_OK;
}

/** @brief Build enough styled text to grow each retained geometry array. */
static tiku_richdoc_t *
document(faults_t *f)
{
    tiku_rich_limits_t limits = {.allocator = {f, allocate, release}};
    tiku_richdoc_t *doc;
    char text[257];
    tiku_rich_paragraph_source_t p[3];
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 3};
    size_t i;

    memset(text, 'x', sizeof text);
    for (i = 0; i < 3; i++)
        p[i] = (tiku_rich_paragraph_source_t){.text = text,
                                             .length = sizeof text};
    CHECK(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Inject at each allocation; errors retain no published geometry. */
static void
allocation_failures(void)
{
    faults_t f = {0};
    tiku_richdoc_t *doc = document(&f);
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_t *layout;
    tiku_richlayout_options_t options = {.width = 30,
        .provider = {.shape = shape}};
    tiku_rich_status_t status;
    size_t base, live, nth;

    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    base = tiku_richdoc_memory(doc);
    live = f.live;
    for (nth = 1; nth < 100; nth++) {
        f.fail = f.calls + nth;
        status = tiku_richlayout_create(snapshot, &options, &layout);
        if (status == TIKU_RICH_OK)
            status = tiku_richlayout_step(layout, 100000);
        if (status != TIKU_RICH_OK) {
            CHECK(status == TIKU_RICH_NO_MEMORY);
            CHECK(!tiku_richlayout_lines(layout));
        }
        tiku_richlayout_release(layout);
        CHECK(f.live == live && tiku_richdoc_memory(doc) == base);
        if (status == TIKU_RICH_OK) break;
    }
    CHECK(nth > 15 && nth < 100);
    f.fail = 0;
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    CHECK(!f.live);
}

/** @brief Cancel during each phase and verify both memory ceilings. */
static void
cancellation(void)
{
    faults_t f = {0};
    tiku_richdoc_t *doc = document(&f);
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_t *layout;
    tiku_richlayout_options_t options = {.width = 30,
        .provider = {.shape = shape}};
    size_t base, i;
    tiku_rich_status_t status;

    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    base = tiku_richdoc_memory(doc);
    for (i = 0; i < 5000; i += 13) {
        CHECK(tiku_richlayout_create(snapshot, &options, &layout) ==
              TIKU_RICH_OK);
        status = tiku_richlayout_step(layout, i);
        CHECK(status == TIKU_RICH_OK || status == TIKU_RICH_PENDING);
        tiku_richlayout_release(layout);
        CHECK(tiku_richdoc_memory(doc) == base);
    }
    options.memory_bytes = 6000;
    CHECK(tiku_richlayout_create(snapshot, &options, &layout) == TIKU_RICH_OK);
    CHECK(tiku_richlayout_step(layout, 100000) == TIKU_RICH_LIMIT);
    tiku_richlayout_release(layout);
    CHECK(tiku_richdoc_memory(doc) == base);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    CHECK(!f.live);
}

/** @brief Refuse each view-construction allocation without leaving observers. */
static void
view_failures(void)
{
    faults_t f = {0};
    tiku_richdoc_t *doc = document(&f);
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = 30,
        .provider = {.shape = shape}};
    tiku_rich_status_t status;
    size_t base = tiku_richdoc_memory(doc), live = f.live, nth;

    for (nth = 1; nth < 32; nth++) {
        f.fail = f.calls + nth;
        status = tiku_richview_create(doc, &options, &view);
        if (status != TIKU_RICH_OK)
            CHECK(status == TIKU_RICH_NO_MEMORY && !view);
        f.fail = 0;
        CHECK(tiku_richview_destroy(view) == TIKU_RICH_OK);
        CHECK(f.live == live && tiku_richdoc_memory(doc) == base);
        if (status == TIKU_RICH_OK) break;
    }
    CHECK(nth > 5 && nth < 32);
    tiku_richdoc_release(doc);
    CHECK(!f.live);
}

/** @brief A giant grapheme exceeds the shaping budget without partial output. */
static void
oversized_cluster(void)
{
    char text[5001];
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_t *layout;
    tiku_rich_paragraph_source_t p = {.text = text, .length = sizeof text};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richlayout_options_t options = {.width = 100,
        .provider = {.shape = shape}};
    size_t i;

    text[0] = 'a';
    for (i = 1; i < sizeof text; i += 2) {
        text[i] = (char)0xcc;
        text[i + 1] = (char)0x81;
    }
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richlayout_create(snapshot, &options, &layout) == TIKU_RICH_OK);
    CHECK(tiku_richlayout_step(layout, 100000) == TIKU_RICH_LIMIT);
    CHECK(!tiku_richlayout_clusters(layout));
    tiku_richlayout_release(layout);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
}

/** @brief Refuse each cache allocation while retaining the completed source. */
static void
cache_failures(void)
{
    faults_t f = {0};
    tiku_richdoc_t *doc = document(&f);
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_t *old, *next;
    tiku_richlayout_options_t options = {.width = 30,
        .provider = {.shape = shape}};
    tiku_rich_status_t status;
    size_t base, live, nth;

    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richlayout_create(snapshot, &options, &old) == TIKU_RICH_OK);
    CHECK(tiku_richlayout_step(old, 100000) == TIKU_RICH_OK);
    base = tiku_richdoc_memory(doc);
    live = f.live;
    for (nth = 1; nth < 100; nth++) {
        f.fail = f.calls + nth;
        status = tiku_richlayout_create_reusing(snapshot, &options, old, &next);
        if (status == TIKU_RICH_OK)
            status = tiku_richlayout_step(next, 100000);
        if (status != TIKU_RICH_OK)
            CHECK(status == TIKU_RICH_NO_MEMORY && !tiku_richlayout_lines(next));
        else CHECK(tiku_richlayout_reused(next) == 3);
        tiku_richlayout_release(next);
        CHECK(f.live == live && tiku_richdoc_memory(doc) == base);
        CHECK(tiku_richlayout_status(old) == TIKU_RICH_OK);
        if (status == TIKU_RICH_OK) break;
    }
    CHECK(nth > 15 && nth < 100);
    f.fail = 0;
    for (size_t i = 0; i < 4000; i += 17) {
        CHECK(tiku_richlayout_create_reusing(snapshot, &options, old, &next) ==
              TIKU_RICH_OK);
        status = tiku_richlayout_step(next, i);
        CHECK(status == TIKU_RICH_OK || status == TIKU_RICH_PENDING);
        tiku_richlayout_release(next);
        CHECK(f.live == live && tiku_richdoc_memory(doc) == base);
    }
    options.lines = 2;
    CHECK(tiku_richlayout_create_reusing(snapshot, &options, old, &next) ==
          TIKU_RICH_OK);
    CHECK(tiku_richlayout_step(next, 100000) == TIKU_RICH_LIMIT);
    CHECK(!tiku_richlayout_lines(next));
    tiku_richlayout_release(next);
    CHECK(tiku_richdoc_memory(doc) == base);
    tiku_richlayout_release(old);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    CHECK(!f.live);
}

/** @brief Retry without cache when retained geometry exceeds the owner budget. */
static void
cache_pressure(void)
{
    faults_t f = {0};
    tiku_richdoc_t *doc = document(&f);
    tiku_richview_t *view;
    tiku_richlayout_options_t options = {.width = 30,
        .provider = {.shape = shape}};
    tiku_rich_status_t status;
    size_t steps = 0;

    f.peak = f.bytes;
    CHECK(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    do {
        status = tiku_richview_step(view, 1);
    } while (status == TIKU_RICH_PENDING);
    CHECK(status == TIKU_RICH_OK);
    doc->memory->limit = f.peak + 4096;
    CHECK(tiku_richview_reflow(view) == TIKU_RICH_OK);
    do {
        status = tiku_richview_step(view, 1);
        CHECK(++steps < 20000);
        CHECK(tiku_richdoc_memory(doc) <= doc->memory->limit);
    } while (status == TIKU_RICH_PENDING);
    CHECK(status == TIKU_RICH_OK);
    CHECK(!tiku_richlayout_reused(tiku_richview_layout(view)));
    doc->memory->limit = (size_t)512 << 20;
    for (size_t i = 1; i < 15; i++) {
        CHECK(tiku_richview_reflow(view) == TIKU_RICH_OK);
        f.fail = f.calls + i;
        steps = 0;
        do {
            status = tiku_richview_step(view, 11);
            CHECK(++steps < 20000);
        } while (status == TIKU_RICH_PENDING);
        CHECK(status == TIKU_RICH_OK);
        f.fail = 0;
    }
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
    CHECK(!f.live);
}

/** @brief Run allocation, cancellation and pathological text guards. */
int
main(void)
{
    allocation_failures();
    cancellation();
    view_failures();
    oversized_cluster();
    cache_failures();
    cache_pressure();
    printf("PASS layout faults (%u injected failures, 621 cancellations)\n",
           injected);
    return 0;
}
