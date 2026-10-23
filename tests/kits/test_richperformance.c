/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richperformance.c - native-font layout, paint and allocation measurements.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richfont.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/** @brief Requested model/layout bytes, including simultaneous growth buffers. */
typedef struct { size_t live, peak, count; } heap_t;
/** @brief Aligned test accounting outside the measured payload. */
typedef union { max_align_t align; size_t bytes; } header_t;
/** @brief Monotonic duration and largest 4096-unit reflow call. */
typedef struct { double total, maximum; size_t calls; } timing_t;

/** @brief Record live allocator payload bytes without counting test prefixes. */
static void *
allocate(void *context, size_t bytes)
{
    heap_t *heap = context;
    header_t *header = malloc(sizeof *header + bytes);

    assert(header);
    header->bytes = bytes;
    heap->live += bytes;
    if (heap->live > heap->peak) heap->peak = heap->live;
    heap->count++;
    return header + 1;
}

/** @brief Balance measured storage and free its private test prefix. */
static void
release(void *context, void *pointer)
{
    heap_t *heap = context;
    header_t *header = (header_t *)pointer - 1;

    assert(heap->live >= header->bytes && heap->count);
    heap->live -= header->bytes;
    heap->count--;
    free(header);
}

/** @brief Read monotonic milliseconds for measurements, not pass thresholds. */
static double
now(void)
{
    struct timespec time;

    assert(!clock_gettime(CLOCK_MONOTONIC, &time));
    return time.tv_sec * 1000.0 + time.tv_nsec / 1000000.0;
}

/** @brief Find a readable test face without a display server. */
static const char *
font_path(void)
{
    const char *path = getenv("TIKU_TEST_FONT");
    static const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf"
    };

    if (path && *path) return path;
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++)
        if (!access(paths[i], R_OK)) return paths[i];
    fputs("Set TIKU_TEST_FONT to a readable TrueType face.\n", stderr);
    exit(1);
}

/** @brief Import realistic short paragraphs with spacing, alignment and lists. */
static tiku_richdoc_t *
document(size_t count, heap_t *heap)
{
    static const char text[] =
        "A paragraph with words, spaces, and enough content to wrap.";
    tiku_rich_limits_t limits = {.allocator = {heap, allocate, release}};
    tiku_rich_paragraph_source_t *p = calloc(count, sizeof *p);
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = count};
    tiku_richdoc_t *doc;

    assert(p);
    for (size_t i = 0; i < count; i++) {
        p[i] = (tiku_rich_paragraph_source_t){.text = text, .length = strlen(text),
            .chars = {.present = TIKU_RICH_FONT, .font = "Fixture"},
            .paragraph = {.present = TIKU_RICH_SPACING | TIKU_RICH_ALIGNMENT,
                .after = 6, .align = i % 11 ? TIKU_RICH_LEFT : TIKU_RICH_CENTER}};
        if (!(i % 7)) p[i].list.kind = TIKU_RICH_LIST_DECIMAL;
    }
    assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    free(p);
    return doc;
}

/** @brief Finish a view while recording actual owner-loop reflow call times. */
static timing_t
finish(tiku_richview_t *view)
{
    timing_t result = {0};
    tiku_rich_status_t status;
    double started = now();

    do {
        double call = now();

        status = tiku_richview_step(view, 4096);
        result.maximum = fmax(result.maximum, now() - call);
        assert(++result.calls < 100000);
    } while (status == TIKU_RICH_PENDING);
    assert(status == TIKU_RICH_OK);
    result.total = now() - started;
    return result;
}

/** @brief Independently lay out the current source without cached geometry. */
static tiku_richlayout_t *
cold_layout(tiku_richdoc_t *doc, const tiku_richlayout_options_t *options)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_t *layout;
    tiku_rich_status_t status;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richlayout_create(snapshot, options, &layout) == TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    do { status = tiku_richlayout_step(layout, 4096); }
    while (status == TIKU_RICH_PENDING);
    assert(status == TIKU_RICH_OK);
    return layout;
}

/** @brief Compare every line and one scrolled frame against uncached layout. */
static void
oracle(tiku_richfont_t *fonts, tiku_richview_t *view,
    tiku_richlayout_t *cold, tiku_surface_t *a, tiku_surface_t *b)
{
    const tiku_richlayout_t *warm = tiku_richview_layout(view);
    tiku_rich_line_t x, y;
    double scroll = tiku_richview_scroll_y(view);

    assert(tiku_richlayout_lines(warm) == tiku_richlayout_lines(cold));
    assert(tiku_richlayout_clusters(warm) == tiku_richlayout_clusters(cold));
    assert(tiku_richlayout_warnings(warm) == tiku_richlayout_warnings(cold));
    assert(fabs(tiku_richlayout_height(warm) - tiku_richlayout_height(cold)) < 1e-7);
    for (size_t i = 0; i < tiku_richlayout_lines(warm); i++) {
        assert(tiku_richlayout_line(warm, i, &x) == TIKU_RICH_OK);
        assert(tiku_richlayout_line(cold, i, &y) == TIKU_RICH_OK);
        assert(x.paragraph == y.paragraph && x.first == y.first && x.last == y.last);
        assert(x.cluster == y.cluster && x.count == y.count);
        assert(!strcmp(x.marker, y.marker));
        assert(fabs(x.box.x - y.box.x) < 1e-7 && fabs(x.box.y - y.box.y) < 1e-7);
        assert(fabs(x.box.width - y.box.width) < 1e-7);
        assert(fabs(x.box.height - y.box.height) < 1e-7);
        assert(fabs(x.baseline - y.baseline) < 1e-7);
    }
    tiku_fill(b, (tiku_rect_t){0, 0, b->w, b->h}, 0xffffff);
    assert(tiku_richfont_draw(fonts, cold, b, 0, -scroll, 1,
        0, tiku_richlayout_lines(cold)) == TIKU_RICH_OK);
    assert(!memcmp(a->px, b->px, (size_t)a->w * a->h * sizeof *a->px));
}

/** @brief Measure a viewport paint and return milliseconds per frame. */
static double
paint(tiku_richfont_t *fonts, tiku_richview_t *view, tiku_surface_t *surface)
{
    double started = now();

    for (int i = 0; i < 5; i++) {
        tiku_fill(surface, (tiku_rect_t){0, 0, surface->w, surface->h}, 0xffffff);
        assert(tiku_richfont_draw_view(fonts, view, surface,
            (tiku_rect_t){0, 0, surface->w, surface->h},
            0x604a82c0, 0, 0) == TIKU_RICH_OK);
    }
    return (now() - started) / 5;
}

/** @brief Publish one insertion at a chosen paragraph without UI input. */
static void
edit(tiku_richdoc_t *doc, size_t index)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_paragraph_t p;
    tiku_rich_edit_t change = {.kind = TIKU_RICH_REPLACE, .text = "W", .length = 1};
    tiku_rich_transaction_t tx = {0, &change, 1};

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(snapshot, index, &p) == TIKU_RICH_OK);
    change.paragraph = p.id;
    tiku_richsnapshot_release(snapshot);
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
}

/** @brief Print one completed layout sample, excluding the independent oracle. */
static void
sample(size_t count, const char *operation, heap_t *heap,
    tiku_richfont_t *fonts, tiku_richview_t *view, tiku_surface_t *surface)
{
    timing_t timing = finish(view);
    const tiku_richlayout_t *layout = tiku_richview_layout(view);
    double frame;

    assert(!tiku_richlayout_warnings(layout));
    tiku_richview_scroll(view, tiku_richlayout_height(layout) * 0.5);
    frame = paint(fonts, view, surface);
    printf("%zu,%s,%zu,%.3f,%.3f,%zu,%.3f,%zu,%zu\n", count, operation,
        tiku_richlayout_lines(layout), timing.total, timing.maximum, timing.calls,
        frame, tiku_richlayout_reused(layout), heap->peak);
    fflush(stdout);
}

/** @brief Exercise beginning/middle/end edits, undo and changed-width reflow. */
static void
workload(tiku_richfont_t *fonts, size_t count)
{
    heap_t heap = {0};
    tiku_richdoc_t *doc = document(count, &heap);
    tiku_richlayout_options_t options = {.width = 300,
        .provider = tiku_richfont_provider(fonts)};
    tiku_richview_t *view;
    tiku_surface_t *frame = tiku_surface_new(300, 400, 0xffffff);
    tiku_surface_t *reference = tiku_surface_new(300, 400, 0xffffff);
    static const char *names[] = {"edit-start", "edit-middle", "edit-end"};
    size_t positions[] = {0, count / 2, count - 1};

    assert(frame && reference);
    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    sample(count, "initial", &heap, fonts, view, frame);
    for (size_t i = 0; i < 3; i++) {
        tiku_richlayout_t *cold;

        heap.peak = heap.live;
        edit(doc, positions[i]);
        sample(count, names[i], &heap, fonts, view, frame);
        assert(tiku_richlayout_reused(tiku_richview_layout(view)) >=
               (count - 1) * 99 / 100);
        assert(tiku_richlayout_reused(tiku_richview_layout(view)) <= count - 1);
        cold = cold_layout(doc, &options);
        oracle(fonts, view, cold, frame, reference);
        tiku_richlayout_release(cold);
        heap.peak = heap.live;
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        sample(count, "undo", &heap, fonts, view, frame);
        assert(!tiku_richdoc_dirty(doc));
        assert(tiku_richlayout_reused(tiku_richview_layout(view)) >=
               (count - 1) * 99 / 100);
        assert(tiku_richlayout_reused(tiku_richview_layout(view)) <= count - 1);
    }
    heap.peak = heap.live;
    tiku_surface_free(frame);
    frame = tiku_surface_new(240, 400, 0xffffff);
    assert(frame);
    assert(tiku_richview_viewport(view, 240, 400, 1) == TIKU_RICH_OK);
    sample(count, "resize", &heap, fonts, view, frame);
    assert(!tiku_richlayout_reused(tiku_richview_layout(view)));
    tiku_surface_free(frame); tiku_surface_free(reference);
    tiku_richview_destroy(view); tiku_richdoc_release(doc);
    assert(!heap.live && !heap.count);
}

/** @brief Measure 200 separate edits and allocation-free whole-history traversal. */
static void
history_workload(size_t count)
{
    heap_t heap = {0};
    tiku_richdoc_t *doc = document(count, &heap);
    double started, total = 0, maximum = 0, elapsed;
    size_t reserve, allocations, retained;

    for (size_t i = 0; i < 200; i++) {
        started = now();
        edit(doc, (i * 31) % count);
        elapsed = now() - started;
        total += elapsed;
        maximum = fmax(maximum, elapsed);
    }
    reserve = tiku_richdoc_history_memory(doc);
    allocations = heap.count;
    started = now();
    for (size_t i = 0; i < 200; i++) {
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        assert(tiku_richdoc_history_memory(doc) == reserve);
        assert(tiku_richdoc_history_retained_memory(doc) <= reserve);
    }
    assert(!tiku_richdoc_dirty(doc));
    for (size_t i = 0; i < 200; i++)
        assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    elapsed = now() - started;
    assert(heap.count == allocations);
    retained = tiku_richdoc_history_retained_memory(doc);
    printf("# history paragraphs=%zu edits=200 edit_total_ms=%.3f "
           "edit_max_ms=%.3f traversal_ms=%.3f reserve_bytes=%zu "
           "retained_bytes=%zu peak_bytes=%zu\n", count, total, maximum,
           elapsed, reserve, retained, heap.peak);
    retained = heap.live - retained;
    tiku_richdoc_clear_history(doc);
    assert(heap.live == retained);
    tiku_richdoc_release(doc);
    assert(!heap.live && !heap.count);
}

/** @brief Run reproducible native-font workloads without machine-specific gates. */
int
main(void)
{
    const char *path = font_path();
    tiku_richfont_source_t source = {path, 0, "Fixture", 0, 0};
    tiku_richfont_t *fonts;
    struct utsname machine;

    assert(!uname(&machine));
    printf("# %s %s %s; compiler %s; font %s\n",
        machine.sysname, machine.release, machine.machine, __VERSION__, path);
    puts("# ASCII, 12pt, 300pt width, 400px viewport; resize uses 240pt width");
    puts("# peak_bytes counts model/layout allocator payload, not fonts, raster or test buffers");
    puts("paragraphs,operation,lines,reflow_ms,max_step_ms,step_calls,paint_ms,reused,peak_bytes");
    assert(tiku_richfont_open(&source, 1, &fonts) == TIKU_RICH_OK);
    workload(fonts, 250); workload(fonts, 1000); workload(fonts, 2000);
    tiku_richfont_release(fonts);
    history_workload(250); history_workload(1000); history_workload(2000);
    puts("# PASS: cold-layout geometry/pixels, cache counts, undo and allocator cleanup");
    return 0;
}
