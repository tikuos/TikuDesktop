/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richpages.c - page boundaries, keep rules, snapshots and paper history.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richpages.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Deterministic one-byte glyphs with twelve-point line metrics. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
      size_t length, const uint8_t *boundaries, tiku_rich_shape_t *out)
{
    (void)ctx; (void)chars; (void)length; (void)boundaries;
    memset(out, 0, sizeof *out);
    out->bytes = 1;
    out->advance = 6;
    out->ascent = 9;
    out->descent = 3;
    out->glyph_count = 1;
    out->glyphs[0] = (tiku_rich_glyph_t){1, (unsigned char)*text, 0, 0};
    out->caret_count = 2;
    out->carets[0] = (tiku_rich_shape_caret_t){0, 0};
    out->carets[1] = (tiku_rich_shape_caret_t){1, 6};
    return TIKU_RICH_OK;
}

/** @brief Create a document from supplied paragraphs and compact paper. */
static tiku_richdoc_t *
document(tiku_rich_paragraph_source_t *p, size_t count)
{
    tiku_richdoc_t *doc;
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = count,
        .page = {72, 72, 18, 18, 18, 18}};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Paginate a snapshot to completion using the requested work quantum. */
static tiku_richpages_t *
paginate(tiku_richdoc_t *doc, size_t budget, size_t limit,
         tiku_rich_status_t expected)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_options_t options = {.provider = {.shape = shape}};
    tiku_richpages_t *pages;
    tiku_rich_status_t status;
    unsigned calls = 0;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richpages_create(snapshot, &options, limit, &pages) ==
           TIKU_RICH_OK);
    tiku_richsnapshot_release(snapshot);
    assert(!tiku_richpages_count(pages));
    do {
        status = tiku_richpages_step(pages, budget);
        assert(++calls < 100000);
    } while (status == TIKU_RICH_PENDING);
    assert(status == expected);
    return pages;
}

/** @brief Require every source line exactly once and wholly inside its page. */
static void
coverage(tiku_richpages_t *pages)
{
    tiku_rich_page_setup_t setup;
    size_t next = 0;
    const tiku_richlayout_t *layout = tiku_richpages_layout(pages);

    assert(tiku_richpages_setup(pages, &setup) == TIKU_RICH_OK);
    for (size_t i = 0; i < tiku_richpages_count(pages); i++) {
        tiku_rich_page_t page;

        assert(tiku_richpages_page(pages, i, &page) == TIKU_RICH_OK);
        assert(page.first_line == next && page.line_count);
        for (size_t n = 0; n < page.line_count; n++) {
            tiku_rich_line_t line;

            assert(tiku_richlayout_line(layout, next++, &line) == TIKU_RICH_OK);
            assert(line.box.y >= page.source_top);
            assert(line.box.y + line.box.height - page.source_top <=
                   setup.height - setup.top - setup.bottom);
        }
    }
    assert(next == tiku_richlayout_lines(layout));
}

/** @brief Check line pagination, paragraph keeps and impossible page limits. */
static void
boundaries(void)
{
    tiku_rich_paragraph_source_t p[] = {
        {.text = "A\nB", .length = 3},
        {.text = "C\nD", .length = 3,
         .paragraph = {.present = TIKU_RICH_KEEP, .keep_lines = 1}},
        {.text = "E", .length = 1}
    };
    tiku_richdoc_t *doc = document(p, 3);
    tiku_richpages_t *slow = paginate(doc, 1, 0, TIKU_RICH_OK);
    tiku_richpages_t *fast = paginate(doc, 65536, 0, TIKU_RICH_OK);
    tiku_rich_page_t a, b;

    assert(tiku_richpages_count(slow) == 2);
    coverage(slow);
    for (size_t i = 0; i < 2; i++) {
        assert(tiku_richpages_page(slow, i, &a) == TIKU_RICH_OK);
        assert(tiku_richpages_page(fast, i, &b) == TIKU_RICH_OK);
        assert(a.first_line == b.first_line && a.line_count == b.line_count &&
               a.source_top == b.source_top);
    }
    tiku_richpages_page(slow, 0, &a);
    assert(a.line_count == 2);
    tiku_richpages_release(slow);
    tiku_richpages_release(fast);
    slow = paginate(doc, 1, 1, TIKU_RICH_LIMIT);
    assert(!tiku_richpages_count(slow));
    tiku_richpages_release(slow);
    tiku_richdoc_release(doc);
    p[0].paragraph = (tiku_rich_para_t){
        .present = TIKU_RICH_KEEP, .keep_lines = 1, .keep_next = 1};
    doc = document(p, 3);
    slow = paginate(doc, 1, 0, TIKU_RICH_OK);
    assert(tiku_richpages_warnings(slow) & TIKU_RICH_PAGE_KEEP_BROKEN);
    coverage(slow);
    tiku_richpages_release(slow);
    tiku_richdoc_release(doc);
}

/** @brief Paper changes are validated, undoable, snapshot-isolated and dirty. */
static void
paper_history(void)
{
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *old, *newer;
    tiku_rich_page_setup_t before, after, invalid;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_PAGE_SETUP,
                             .page = {612, 792, 36, 36, 36, 36}};
    tiku_rich_transaction_t tx = {0, &edit, 1};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_page(old, &before) == TIKU_RICH_OK);
    assert(before.width > 595 && before.width < 596 && before.left == 54);
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    assert(tiku_richdoc_dirty(doc));
    assert(tiku_richdoc_snapshot(doc, &newer) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_page(newer, &after) == TIKU_RICH_OK);
    assert(after.width == 612 && after.left == 36);
    assert(tiku_richsnapshot_page(old, &after) == TIKU_RICH_OK);
    assert(after.width == before.width);
    invalid = edit.page;
    invalid.top = NAN;
    after = before;
    assert(tiku_rich_page_setup(&invalid, &after) == TIKU_RICH_INVALID);
    assert(after.width == before.width);
    edit.page.left = 600;
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_INVALID);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(!tiku_richdoc_dirty(doc));
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    tiku_richsnapshot_release(old);
    tiku_richsnapshot_release(newer);
    tiku_richdoc_release(doc);
}

/** @brief Pagination releases retained states after its document is destroyed. */
static void
lifetime(void)
{
    tiku_richdoc_t *doc = document(NULL, 0);
    tiku_richpages_t *pages = paginate(doc, 1, 0, TIKU_RICH_OK);
    tiku_richsnapshot_t *source;
    tiku_rich_page_t page;

    tiku_richdoc_release(doc);
    assert(tiku_richpages_count(pages) == 1);
    assert(tiku_richpages_page(pages, 0, &page) == TIKU_RICH_OK);
    assert(page.first_line == 0 && page.line_count == 1);
    assert(tiku_richlayout_snapshot(tiku_richpages_layout(pages), &source) ==
           TIKU_RICH_OK);
    tiku_richpages_release(pages);
    assert(tiku_richsnapshot_paragraphs(source) == 1);
    tiku_richsnapshot_release(source);
}

/** @brief Breaks override keeps, survive history, and stay on the split left. */
static void
explicit_breaks(void)
{
    tiku_rich_style_source_t style = {.name = "Chapter",
        .paragraph = {.present = TIKU_RICH_PAGE_BREAK, .break_before = 1}};
    tiku_rich_paragraph_source_t p[] = {
        {.text = "A", .length = 1, .style = 1,
         .paragraph = {.present = TIKU_RICH_KEEP, .keep_next = 1}},
        {.text = "BC", .length = 2, .style = 1},
        {.text = "D", .length = 1, .paragraph = {
            .present = TIKU_RICH_PAGE_BREAK, .break_before = 1}}
    };
    tiku_rich_source_t source = {.styles = &style, .style_count = 1,
        .paragraphs = p, .paragraph_count = 3};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_richpages_t *pages;
    tiku_rich_paragraph_t paragraph;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_SPLIT, .start = 1, .end = 1};
    tiku_rich_transaction_t tx = {0, &edit, 1};

    assert(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    pages = paginate(doc, 1, 0, TIKU_RICH_OK);
    assert(tiku_richpages_count(pages) == 3);
    coverage(pages);
    tiku_richpages_release(pages);
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_paragraph(snapshot, 1, &paragraph) == TIKU_RICH_OK);
    edit.paragraph = paragraph.id;
    tiku_richsnapshot_release(snapshot);
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    pages = paginate(doc, 1, 0, TIKU_RICH_OK);
    assert(tiku_richpages_count(pages) == 3);
    coverage(pages);
    tiku_richpages_release(pages);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    edit.kind = TIKU_RICH_PARAGRAPH_FORMAT;
    edit.start = edit.end = 0;
    edit.props.present = TIKU_RICH_PAGE_BREAK;
    edit.props.break_before = 2;
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_INVALID_STYLE);
    edit.props.break_before = 0;
    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    pages = paginate(doc, 1, 0, TIKU_RICH_OK);
    assert(tiku_richpages_count(pages) == 2);
    tiku_richpages_release(pages);
    tiku_richdoc_release(doc);
}

/** @brief Allocation refusal and live-block accounting for retained jobs. */
typedef struct { size_t call, fail, live; } fault_t;

/** @brief Refuse one chosen allocation, otherwise count the returned block. */
static void *
allocate(void *context, size_t size)
{
    fault_t *f = context;
    void *p;

    if (++f->call == f->fail) return NULL;
    p = malloc(size);
    assert(p);
    f->live++;
    return p;
}

/** @brief Release one block allocated through the test allocator. */
static void
release(void *context, void *pointer)
{
    fault_t *f = context;

    assert(f->live);
    f->live--;
    free(pointer);
}

/** @brief Refuse each pagination allocation with no leaked snapshot or page. */
static void
allocation_failures(void)
{
    fault_t f = {0};
    tiku_rich_limits_t limits = {.allocator = {&f, allocate, release}};
    tiku_richlayout_options_t options = {.provider = {.shape = shape}};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *snapshot;
    tiku_richpages_t *pages;
    tiku_rich_status_t status;
    size_t live, memory, nth;

    assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    {
        tiku_rich_paragraph_source_t p = {
            .text = "Memory allocation during pagination", .length = 35};
        tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};

        assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    }
    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    live = f.live;
    memory = tiku_richdoc_memory(doc);
    for (nth = 1; nth < 80; nth++) {
        f.fail = f.call + nth;
        status = tiku_richpages_create(snapshot, &options, 0, &pages);
        while (status == TIKU_RICH_OK || status == TIKU_RICH_PENDING) {
            status = tiku_richpages_step(pages, 3);
            if (status == TIKU_RICH_OK) break;
        }
        assert(status == TIKU_RICH_OK || status == TIKU_RICH_NO_MEMORY);
        if (status != TIKU_RICH_OK) assert(!tiku_richpages_count(pages));
        tiku_richpages_release(pages);
        assert(f.live == live && tiku_richdoc_memory(doc) == memory);
        if (status == TIKU_RICH_OK) break;
    }
    assert(nth > 8 && nth < 80);
    f.fail = 0;
    options.defaults.present = TIKU_RICH_SIZE;
    options.defaults.size = 4096;
    options.leading = 1000;
    assert(tiku_richpages_create(snapshot, &options, 0, &pages) == TIKU_RICH_OK);
    do { status = tiku_richpages_step(pages, 3); }
    while (status == TIKU_RICH_PENDING);
    assert(status == TIKU_RICH_LIMIT && !tiku_richpages_count(pages));
    tiku_richpages_release(pages);
    tiku_richsnapshot_release(snapshot);
    tiku_richdoc_release(doc);
    assert(!f.live);
}

/** @brief Run deterministic pagination and document-property contracts. */
int
main(void)
{
    boundaries();
    paper_history();
    lifetime();
    explicit_breaks();
    allocation_failures();
    puts("Rich pages: bounded/full equivalence, line coverage, keeps, limits, "
         "paper validation, undo and retained lifetime PASS");
    return 0;
}
