/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richcells_rich.c - rich cell placement, ownership and atomic refusal.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Apply one checked edit to the current document. */
static void
edit(tiku_richdoc_t *doc, tiku_rich_edit_t value)
{
    tiku_rich_transaction_t tx = {0, &value, 1};

    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
}

/** @brief Build a three-by-four table with independently labelled cells. */
static tiku_richdoc_t *
fixture(const tiku_rich_limits_t *limits, int source)
{
    tiku_richdoc_t *doc;
    tiku_rich_style_spec_t style = {.name = "Body",
        .chars = {.present = TIKU_RICH_SIZE, .size = source ? 18 : 10}};

    assert(tiku_richdoc_create(limits, &doc) == TIKU_RICH_OK);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_STYLE_DEFINE,
        .definition = &style});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .table = {.rows = 3, .columns = 4, .width = source ? 120 : 240}});
    for (size_t i = 0; i < 12; i++) {
        char text[32];
        size_t length = (size_t)snprintf(text, sizeof text, "%s%zu",
                                         source ? "source" : "old", i);

        edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = doc->root->paragraphs[i + 1]->value.id,
            .text = text, .length = length});
    }
    tiku_richdoc_clear_history(doc);
    return doc;
}

/** @brief Return the fixture's rectangular selection. */
static tiku_rich_cells_t
rectangle(tiku_richdoc_t *doc, unsigned r, unsigned c,
          unsigned rows, unsigned columns)
{
    return (tiku_rich_cells_t){doc->root->blocks->nodes[1].id,
                               r, c, rows, columns};
}

/** @brief Capture an immutable root and assert success. */
static tiku_richsnapshot_t *
snapshot(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *s;

    assert(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    return s;
}

/** @brief Fetch a cell's first paragraph, independent of preceding splits. */
static const tiku_rich_paragraph_t *
paragraph(tiku_richdoc_t *doc, unsigned ordinal)
{
    uint64_t id = tiku_rd_grid_paragraph(doc->root,
        doc->root->blocks->nodes[1].id, ordinal);

    return &doc->root->paragraphs[tiku_rd_find(doc->root, id)]->value;
}

/** @brief Count edit publications after checking the complete model. */
static void
changed(void *ctx, tiku_richdoc_t *doc, const tiku_rich_change_t *change)
{
    unsigned *count = ctx;

    (void)doc; (void)change;
    (*count)++;
}

/** @brief Exercise every fitting rectangle with independent text oracles. */
static void
matrix(void)
{
    tiku_richdoc_t *src = fixture(NULL, 1), *dst = fixture(NULL, 0);
    tiku_richsnapshot_t *s = snapshot(src), *old = snapshot(dst);
    tiku_richwatch_t *watch;
    unsigned notifications = 0, cases = 0;

    assert(tiku_richwatch_create(dst, changed, &notifications, &watch) == TIKU_RICH_OK);
    for (unsigned r = 0; r < 3; r++) for (unsigned c = 0; c < 4; c++)
        for (unsigned rows = 1; rows <= 3 - r; rows++)
            for (unsigned columns = 1; columns <= 4 - c; columns++) {
                tiku_rich_cells_t from = rectangle(src, 0, 0, rows, columns);
                tiku_rich_cells_t to = rectangle(dst, r, c, 1, 1), out;
                uint64_t generation = dst->generation;

                notifications = 0;
                assert(tiku_richdoc_cells_paste(dst, generation, &to, s, &from, &out) == TIKU_RICH_OK);
                assert(notifications == 1 && dst->generation == generation + 1);
                assert(out.row == r && out.column == c && out.rows == rows && out.columns == columns);
                assert(dst->root->blocks->nodes[1].table.width == 240);
                for (unsigned i = 0; i < 12; i++) {
                    char expected[32];
                    int member = i / 4 >= r && i / 4 < r + rows &&
                                 i % 4 >= c && i % 4 < c + columns;

                    snprintf(expected, sizeof expected, "%s%u", member ? "source" : "old",
                        member ? (i / 4 - r) * 4 + i % 4 - c : i);
                    assert(!strcmp(paragraph(dst, i)->text, expected));
                    assert(paragraph(dst, i)->id == old->root->paragraphs[i + 1]->value.id);
                }
                assert(tiku_richdoc_undo(dst) == TIKU_RICH_OK && dst->root == old->root);
                assert(tiku_richdoc_redo(dst) == TIKU_RICH_OK);
                assert(tiku_richdoc_undo(dst) == TIKU_RICH_OK && dst->root == old->root);
                cases++;
            }
    assert(cases == 60);
    tiku_richwatch_destroy(watch);
    tiku_richsnapshot_release(old);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(src);
    tiku_richdoc_release(dst);
    puts("Rich cell paste: 60 rectangular placement oracles PASS");
}

/** @brief Add styles, links, pictures, multiple paragraphs and an empty cell. */
static void
decorate(tiku_richdoc_t *doc)
{
    uint64_t first = paragraph(doc, 0)->id, empty = paragraph(doc, 1)->id;
    uint32_t pixel = 0xff123456;
    tiku_rich_image_t image = {1, 1, &pixel, 10, 10, "sample"};

    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_PARAGRAPH_STYLE_SET,
        .paragraph = first, .style = doc->root->styles[0].value.id});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_FORMAT,
        .paragraph = first, .end = 7,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_LINK_SET,
        .paragraph = first, .end = 7, .link = "https://example.org/cell"});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_LIST_SET,
        .paragraph = first, .list = {TIKU_RICH_LIST_DECIMAL, 1, 7}});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_SPLIT,
        .paragraph = first, .start = 3, .end = 3});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
        .paragraph = empty, .end = 7});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_PARAGRAPH_STYLE_SET,
        .paragraph = empty, .style = doc->root->styles[0].value.id});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_PARAGRAPH_FORMAT,
        .paragraph = empty, .props = {.present = TIKU_RICH_ALIGNMENT,
                                      .align = TIKU_RICH_CENTER}});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_INSERT,
        .paragraph = paragraph(doc, 5)->id, .image = &image});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_CELLS_SET,
        .cells = rectangle(doc, 0, 0, 2, 2), .background = 0xffaabbcc});
    tiku_richdoc_clear_history(doc);
}

/** @brief Preserve rich ownership, empty defaults and forward/inverse anchors. */
static void
rich(void)
{
    tiku_richdoc_t *src = fixture(NULL, 1), *dst = fixture(NULL, 0);
    tiku_rich_cells_t from = rectangle(src, 0, 0, 2, 2);
    tiku_rich_cells_t to = rectangle(dst, 1, 1, 2, 2);
    tiku_richsnapshot_t *s, *old = snapshot(dst);
    tiku_richanchor_t *anchors[24], *late;
    tiku_rich_pos_t saved[24], p;
    const tiku_rich_paragraph_t *a;

    decorate(src);
    s = snapshot(src);
    for (unsigned i = 0; i < 24; i++) {
        saved[i] = (tiku_rich_pos_t){dst->root->body,
            paragraph(dst, i / 2)->id, 1, i % 2 ? TIKU_RICH_AFTER : TIKU_RICH_BEFORE};
        assert(tiku_richanchor_create(dst, saved[i], &anchors[i]) == TIKU_RICH_OK);
    }
    assert(tiku_richdoc_cells_paste(dst, 0, &to, s, &from, &to) == TIKU_RICH_OK);
    assert(dst->root->nstyles == 2);
    assert(dst->root->styles[0].value.chars.size == 10);
    assert(dst->root->styles[1].value.chars.size == 18);
    assert(!strcmp(dst->root->styles[1].name, "Body (paste 1)"));
    for (unsigned i = 0; i < 24; i++) {
        unsigned cell = i / 2;
        int member = cell == 5 || cell == 6 || cell == 9 || cell == 10;
        size_t index = tiku_rd_find(dst->root, paragraph(dst, cell)->id);
        const tiku_rich_paragraph_t *expected = &dst->root->paragraphs[index]->value;

        if (member && i % 2 && cell == 5) expected = &dst->root->paragraphs[index + 1]->value;
        assert(tiku_richanchor_get(anchors[i], &p) == TIKU_RICH_OK);
        assert(p.paragraph == expected->id);
        assert(p.byte == (member ? (i % 2 ? expected->length : 0) : 1));
    }
    a = paragraph(dst, 5);
    assert(!strcmp(a->text, "sou") && a->runs[0].chars.bold);
    assert(!strcmp(a->runs[0].link, "https://example.org/cell"));
    assert(a->runs[0].link != paragraph(src, 0)->runs[0].link);
    assert(a->list.kind == TIKU_RICH_LIST_DECIMAL && a->list.restart == 7);
    a = paragraph(dst, 6);
    assert(!a->length && a->style && a->paragraph.align == TIKU_RICH_CENTER);
    assert(paragraph(dst, 10)->runs[0].image->pixels[0] == 0xff123456);
    assert(paragraph(dst, 10)->runs[0].image != paragraph(src, 5)->runs[0].image);
    tiku_richsnapshot_t *after = snapshot(dst);
    tiku_rich_block_t cell;
    assert(tiku_richsnapshot_cell_at(after, &to, 3, &cell) == TIKU_RICH_OK);
    assert(cell.background == 0xffaabbcc);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(src);
    p = (tiku_rich_pos_t){dst->root->body, paragraph(dst, 11)->id, 2, TIKU_RICH_AFTER};
    assert(tiku_richanchor_create(dst, p, &late) == TIKU_RICH_OK);
    assert(tiku_richdoc_undo(dst) == TIKU_RICH_OK && dst->root == old->root);
    for (unsigned i = 0; i < 24; i++) {
        assert(tiku_richanchor_get(anchors[i], &p) == TIKU_RICH_OK);
        assert(p.paragraph == saved[i].paragraph && p.byte == saved[i].byte);
    }
    assert(tiku_richanchor_get(late, &p) == TIKU_RICH_OK);
    assert(p.paragraph == paragraph(dst, 11)->id && p.byte == 2);
    assert(tiku_richdoc_redo(dst) == TIKU_RICH_OK && dst->root == after->root);
    assert(tiku_richanchor_get(late, &p) == TIKU_RICH_OK && p.byte == 2);
    assert(paragraph(dst, 10)->runs[0].image->pixels[0] == 0xff123456);
    for (unsigned i = 0; i < 24; i++) tiku_richanchor_release(anchors[i]);
    tiku_richanchor_release(late);
    tiku_richsnapshot_release(old);
    tiku_richsnapshot_release(after);
    tiku_richdoc_release(dst);
}

/** @brief Reject bad shapes and state, and use a frozen overlapping source. */
static void
guards(void)
{
    tiku_richdoc_t *doc = fixture(NULL, 1);
    tiku_rich_cells_t from = rectangle(doc, 0, 0, 2, 2);
    tiku_rich_cells_t to = rectangle(doc, 1, 1, 1, 1), out;
    tiku_richsnapshot_t *s = snapshot(doc);
    uint64_t generation = doc->generation;
    size_t memory = tiku_richdoc_memory(doc);

    assert(tiku_richdoc_cells_paste(doc, generation - 1, &to, s, &from, &out) == TIKU_RICH_STALE);
    tiku_richdoc_set_read_only(doc, 1);
    assert(tiku_richdoc_cells_paste(doc, 0, &to, s, &from, &out) == TIKU_RICH_READ_ONLY);
    tiku_richdoc_set_read_only(doc, 0);
    doc->notifying = 1;
    assert(tiku_richdoc_cells_paste(doc, 0, &to, s, &from, &out) == TIKU_RICH_BUSY);
    doc->notifying = 0;
    to.rows = 2;
    assert(tiku_richdoc_cells_paste(doc, 0, &to, s, &from, &out) == TIKU_RICH_INVALID_RANGE);
    to = rectangle(doc, 2, 3, 1, 1);
    assert(tiku_richdoc_cells_paste(doc, 0, &to, s, &from, &out) == TIKU_RICH_INVALID_RANGE);
    assert(!out.table && doc->generation == generation && tiku_richdoc_memory(doc) == memory);
    to = rectangle(doc, 1, 1, 1, 1);
    assert(tiku_richdoc_cells_paste(doc, 0, &to, s, &from, &out) == TIKU_RICH_OK);
    assert(!strcmp(paragraph(doc, 5)->text, "source0"));
    assert(!strcmp(paragraph(doc, 10)->text, "source5"));
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = paragraph(doc, 5)->id, .table = {.rows = 1, .columns = 1}});
    generation = doc->generation;
    tiku_richsnapshot_t *nested = snapshot(doc);
    assert(tiku_richdoc_cells_paste(doc, 0, &to, s, &from, &out) == TIKU_RICH_OK);
    assert(doc->root->count == s->root->count);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK && doc->root == nested->root);
    from = to;
    assert(tiku_richdoc_cells_paste(doc, 0, &to, nested, &from, &out) == TIKU_RICH_OK);
    assert(doc->root->count == nested->root->count);
    assert(doc->root->blocks->count == nested->root->blocks->count);
    assert(doc->generation == generation + 3);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK && doc->root == nested->root);
    tiku_richsnapshot_release(nested);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Check final content budgets, retained redo and identity exhaustion. */
static void
limits(void)
{
    tiku_richdoc_t *src = fixture(NULL, 1), *dst = fixture(NULL, 0);
    tiku_rich_cells_t from = rectangle(src, 0, 0, 1, 2);
    tiku_rich_cells_t to = rectangle(dst, 0, 0, 1, 2), out;
    tiku_richsnapshot_t *s, *old = snapshot(dst);
    uint64_t first = paragraph(src, 0)->id, second = paragraph(src, 1)->id;

    edit(src, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
        .paragraph = first, .end = 7});
    edit(src, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
        .paragraph = second, .end = 7, .text = "12345678", .length = 8});
    s = snapshot(src);
    dst->limits.text_bytes = dst->root->bytes;
    assert(tiku_richdoc_cells_paste(dst, 0, &to, s, &from, &out) == TIKU_RICH_OK);
    assert(!paragraph(dst, 0)->length && paragraph(dst, 1)->length == 8);
    assert(tiku_richdoc_undo(dst) == TIKU_RICH_OK && dst->root == old->root);
    tiku_richsnapshot_release(s);
    edit(src, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
        .paragraph = second, .text = "X", .length = 1});
    s = snapshot(src);
    uint64_t generation = dst->generation, next = dst->next_id;
    tiku_rd_history_t *redo = dst->redo;
    assert(tiku_richdoc_cells_paste(dst, 0, &to, s, &from, &out) == TIKU_RICH_LIMIT);
    assert(dst->root == old->root && dst->generation == generation && dst->redo == redo);
    dst->limits.text_bytes += 100;
    dst->limits.history_bytes = 1;
    assert(tiku_richdoc_cells_paste(dst, 0, &to, s, &from, &out) == TIKU_RICH_LIMIT);
    assert(dst->root == old->root && dst->next_id == next && dst->redo == redo);
    dst->limits.history_bytes = (size_t)64 << 20;
    tiku_richsnapshot_release(s);
    edit(src, (tiku_rich_edit_t){.kind = TIKU_RICH_SPLIT,
        .paragraph = second, .start = 1, .end = 1});
    s = snapshot(src);
    dst->limits.paragraphs = dst->root->count;
    assert(tiku_richdoc_cells_paste(dst, 0, &to, s, &from, &out) == TIKU_RICH_LIMIT);
    dst->limits.paragraphs += 100;
    dst->next_id = 0;
    assert(tiku_richdoc_cells_paste(dst, 0, &to, s, &from, &out) == TIKU_RICH_LIMIT);
    assert(dst->generation == generation && dst->root == old->root && dst->redo == redo);
    tiku_richsnapshot_release(old);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(src);
    tiku_richdoc_release(dst);
}

/** @brief Provide fixed metrics for view construction without a desktop. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
    size_t size, const uint8_t *bounds, tiku_rich_shape_t *out)
{
    (void)ctx; (void)chars; (void)text; (void)size; (void)bounds;
    *out = (tiku_rich_shape_t){.bytes = 1, .advance = 6,
        .ascent = 8, .descent = 2, .caret_count = 2,
        .carets = {{0,0}, {1,6}}};
    return TIKU_RICH_OK;
}

/** @brief Enforce view restrictions and reselect the expanded result. */
static void
view(void)
{
    tiku_richdoc_t *doc = fixture(NULL, 1);
    tiku_richsnapshot_t *s = snapshot(doc);
    tiku_rich_cells_t from = rectangle(doc, 0, 0, 2, 2);
    tiku_rich_cells_t to = rectangle(doc, 1, 1, 1, 1), selected;
    tiku_richlayout_options_t options = {.width = 240, .provider = {.shape = shape}};
    tiku_richview_t *v;

    assert(tiku_richview_create(doc, &options, &v) == TIKU_RICH_OK);
    assert(tiku_richview_cells_paste(v, s, &from) == TIKU_RICH_NOT_FOUND);
    assert(tiku_richview_select_cells(v, 0, &to) == TIKU_RICH_OK);
    tiku_richsession_set_read_only(v->session, 1);
    assert(tiku_richview_cells_paste(v, s, &from) == TIKU_RICH_READ_ONLY);
    tiku_richsession_set_read_only(v->session, 0);
    doc->initiator = v->session;
    assert(tiku_richview_cells_paste(v, s, &from) == TIKU_RICH_BUSY);
    doc->initiator = NULL;
    assert(tiku_richview_cells_paste(v, s, &from) == TIKU_RICH_OK);
    assert(tiku_richview_cells(v, &selected) == TIKU_RICH_OK);
    assert(selected.row == 1 && selected.column == 1 && selected.rows == 2 && selected.columns == 2);
    assert(!strcmp(paragraph(doc, 10)->text, "source5"));
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(!strcmp(paragraph(doc, 10)->text, "source10"));
    tiku_richview_destroy(v);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Allocation counter shared by both source and destination. */
typedef struct { size_t call, fail, live; } faults_t;
/** @brief Refuse exactly the chosen allocation. */
static void *
allocate(void *ctx, size_t bytes)
{
    faults_t *f = ctx;
    void *p;

    if (++f->call == f->fail) return NULL;
    p = malloc(bytes);
    assert(p);
    f->live++;
    return p;
}
/** @brief Release counted test storage. */
static void
release(void *ctx, void *p)
{
    faults_t *f = ctx;

    assert(f->live);
    f->live--;
    free(p);
}

/** @brief Refuse each allocation without publishing content, IDs or history. */
static void
failures(void)
{
    faults_t f = {0};
    tiku_rich_limits_t limits = {.allocator = {&f, allocate, release}};
    tiku_richdoc_t *src = fixture(&limits, 1), *dst = fixture(&limits, 0);
    tiku_rich_cells_t from = rectangle(src, 0, 0, 2, 2);
    tiku_rich_cells_t to = rectangle(dst, 0, 0, 1, 1), out;
    tiku_richsnapshot_t *s;
    uint64_t generation, next;
    size_t live, source_memory, dest_memory, nth;

    decorate(src);
    edit(src, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = paragraph(src, 0)->id, .table = {.rows = 1, .columns = 2}});
    edit(dst, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = paragraph(dst, 0)->id, .table = {.rows = 2, .columns = 1}});
    tiku_richdoc_clear_history(src);
    tiku_richdoc_clear_history(dst);
    generation = dst->generation;
    next = dst->next_id;
    s = snapshot(src);
    live = f.live;
    source_memory = tiku_richdoc_memory(src);
    dest_memory = tiku_richdoc_memory(dst);
    for (nth = 1; nth < 1000; nth++) {
        f.fail = f.call + nth;
        tiku_rich_status_t status = tiku_richdoc_cells_paste(dst, 0, &to, s, &from, &out);

        if (status == TIKU_RICH_OK) break;
        assert(status == TIKU_RICH_NO_MEMORY && !out.table);
        assert(f.live == live && dst->generation == generation && dst->next_id == next);
        assert(tiku_richdoc_memory(src) == source_memory && tiku_richdoc_memory(dst) == dest_memory);
        assert(!dst->undo && !dst->redo);
        assert(!strcmp(paragraph(dst, 0)->text, "old0"));
    }
    assert(nth > 50 && nth < 1000);
    f.fail = 0;
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(src);
    tiku_richdoc_release(dst);
    assert(!f.live);
    printf("Rich cell paste: %zu allocation refusals PASS\n", nth - 1);
}

/** @brief Insert rich grids at Unicode caret boundaries and restore history. */
static void
insertion(void)
{
    const size_t positions[] = {0, 1, 3, 4};
    tiku_richdoc_t *src = fixture(NULL, 1);
    tiku_rich_cells_t from = rectangle(src, 0, 0, 2, 2);
    tiku_richsnapshot_t *s;

    decorate(src);
    s = snapshot(src);
    for (size_t n = 0; n < 4; n++) {
        tiku_richdoc_t *doc = fixture(NULL, 0);
        uint64_t id = doc->root->paragraphs[0]->value.id;
        tiku_rich_pos_t at = {doc->root->body, id, positions[n], TIKU_RICH_BEFORE}, got;
        tiku_rich_cells_t out;
        tiku_richanchor_t *before, *after;
        tiku_richsnapshot_t *old;
        unsigned notifications = 0;
        tiku_richwatch_t *watch;

        edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = id, .text = "a\xc3\xa9z", .length = 4,
            .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}});
        tiku_richdoc_clear_history(doc);
        old = snapshot(doc);
        assert(tiku_richanchor_create(doc, at, &before) == TIKU_RICH_OK);
        at.affinity = TIKU_RICH_AFTER;
        assert(tiku_richanchor_create(doc, at, &after) == TIKU_RICH_OK);
        assert(tiku_richwatch_create(doc, changed, &notifications, &watch) == TIKU_RICH_OK);
        assert(tiku_richdoc_cells_insert(doc, 0, at, s, &from, &out) == TIKU_RICH_OK);
        assert(notifications == 1 && out.rows == 2 && out.columns == 2);
        assert(doc->root->count == old->root->count + 6);
        assert(doc->root->paragraphs[0]->value.id == id);
        assert(doc->root->paragraphs[0]->value.length == positions[n]);
        assert(doc->root->paragraphs[6]->value.length == 4 - positions[n]);
        assert(!memcmp(doc->root->paragraphs[6]->value.text,
                       "a\xc3\xa9z" + positions[n], 4 - positions[n]));
        if (positions[n]) assert(doc->root->paragraphs[0]->value.runs[0].chars.italic);
        if (positions[n] != 4) assert(doc->root->paragraphs[6]->value.runs[0].chars.italic);
        assert(doc->root->paragraphs[5]->value.runs[0].image->pixels[0] == 0xff123456);
        assert(tiku_richanchor_get(before, &got) == TIKU_RICH_OK);
        assert(got.paragraph == id && got.byte == positions[n]);
        assert(tiku_richanchor_get(after, &got) == TIKU_RICH_OK);
        assert(got.paragraph == doc->root->paragraphs[6]->value.id && !got.byte);
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK && doc->root == old->root);
        assert(tiku_richanchor_get(after, &got) == TIKU_RICH_OK);
        assert(got.paragraph == id && got.byte == positions[n]);
        assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
        assert(doc->root->paragraphs[5]->value.runs[0].image->pixels[0] == 0xff123456);
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        at.byte = 2;
        assert(tiku_richdoc_cells_insert(doc, 0, at, s, &from, &out) == TIKU_RICH_INVALID_RANGE);
        at = (tiku_rich_pos_t){doc->root->body, paragraph(doc, 0)->id, 0, TIKU_RICH_AFTER};
        assert(tiku_richdoc_cells_insert(doc, 0, at, s, &from, &out) == TIKU_RICH_OK);
        assert(out.table && doc->root->count == old->root->count + 6);
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        assert(doc->root == old->root && doc->redo);
        tiku_richwatch_destroy(watch);
        tiku_richanchor_release(before);
        tiku_richanchor_release(after);
        tiku_richsnapshot_release(old);
        tiku_richdoc_release(doc);
    }
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(src);
}

/** @brief Refuse each insertion allocation, preserving view and model state. */
static void
insertion_failures(void)
{
    faults_t f = {0};
    tiku_rich_limits_t limits = {.allocator = {&f, allocate, release}};
    tiku_richdoc_t *src = fixture(&limits, 1), *doc;
    tiku_rich_cells_t from = rectangle(src, 0, 0, 2, 2), result;
    tiku_richsnapshot_t *s;
    tiku_richview_t *v;
    tiku_richlayout_options_t options = {.width = 240, .provider = {.shape = shape}};
    size_t live, memory, nth;
    uint64_t generation, next;

    decorate(src);
    edit(src, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = paragraph(src, 0)->id, .table = {.rows = 1, .columns = 2}});
    tiku_richdoc_clear_history(src);
    s = snapshot(src);
    assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    assert(tiku_richview_create(doc, &options, &v) == TIKU_RICH_OK);
    tiku_richsession_set_read_only(v->session, 1);
    assert(tiku_richview_cells_insert(v, s, &from) == TIKU_RICH_READ_ONLY);
    tiku_richsession_set_read_only(v->session, 0);
    v->session->caret->revision = UINT64_MAX;
    assert(tiku_richview_cells_insert(v, s, &from) == TIKU_RICH_LIMIT);
    v->session->caret->revision = 0;
    live = f.live; memory = tiku_richdoc_memory(doc);
    generation = doc->generation; next = doc->next_id;
    for (nth = 1; nth < 1000; nth++) {
        f.fail = f.call + nth;
        tiku_rich_status_t status = tiku_richview_cells_insert(v, s, &from);

        if (status == TIKU_RICH_OK) break;
        assert(status == TIKU_RICH_NO_MEMORY);
        assert(doc->generation == generation && doc->next_id == next);
        assert(f.live == live && tiku_richdoc_memory(doc) == memory);
        assert(!doc->undo && !doc->redo && !doc->root->blocks);
    }
    f.fail = 0;
    assert(nth > 50 && nth < 1000);
    assert(tiku_richview_cells(v, &result) == TIKU_RICH_OK);
    assert(result.rows == 2 && result.columns == 2);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK && !doc->root->blocks);
    tiku_richview_destroy(v);
    tiku_richdoc_release(doc);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(src);
    assert(!f.live);
    printf("Rich table insertion: %zu allocation refusals PASS\n", nth - 1);
}

/** @brief Run rich rectangular transfer, lifetime and refusal checks. */
int
main(void)
{
    matrix();
    rich();
    guards();
    limits();
    view();
    failures();
    insertion();
    insertion_failures();
    puts("Rich cell paste: styles, links, images, empty defaults, anchors and self-paste PASS");
    return 0;
}
