/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richcells_paste.c - bounded TSV, atomic replacement and undo ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Apply a primitive to the current generation. */
static tiku_rich_status_t
apply(tiku_richdoc_t *doc, tiku_rich_edit_t edit)
{
    tiku_rich_transaction_t tx = {doc->generation, &edit, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Create twelve labelled cells and a leading body paragraph. */
static tiku_richdoc_t *
fixture(const tiku_rich_limits_t *limits)
{
    tiku_richdoc_t *doc;

    assert(tiku_richdoc_create(limits, &doc) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .table = {.rows = 3, .columns = 4, .width = 240}}) == TIKU_RICH_OK);
    for (unsigned i = 0; i < 12; i++)
        assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = doc->root->paragraphs[i + 1]->value.id,
            .text = "old", .length = 3}) == TIKU_RICH_OK);
    tiku_richdoc_clear_history(doc);
    return doc;
}

/** @brief Compare the complete exported rectangle against independent bytes. */
static void
expect(tiku_richdoc_t *doc, const tiku_rich_cells_t *cells, const char *text)
{
    tiku_richsnapshot_t *snapshot;
    char buffer[4096];
    size_t length;

    assert(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_cells_text(snapshot, cells, buffer, sizeof buffer,
                                       &length) == TIKU_RICH_OK);
    assert(length == strlen(text) && !strcmp(buffer, text));
    tiku_richsnapshot_release(snapshot);
}

/** @brief Count publications; each published root contains the complete edit. */
static void
changed(void *ctx, tiku_richdoc_t *doc, const tiku_rich_change_t *change)
{
    unsigned *count = ctx;

    (void)doc; (void)change;
    (*count)++;
}

/** @brief Try every fitting subrectangle and preserve nonmembers and old roots. */
static void
matrix(void)
{
    tiku_richdoc_t *doc = fixture(NULL);
    uint64_t table = doc->root->blocks->nodes[1].id;
    tiku_richsnapshot_t *old;
    tiku_richwatch_t *watch;
    unsigned notifications = 0, cases = 0;
    char text[2048];

    assert(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    assert(tiku_richwatch_create(doc, changed, &notifications, &watch) == TIKU_RICH_OK);
    for (unsigned r = 0; r < 3; r++) for (unsigned c = 0; c < 4; c++)
        for (unsigned rows = 1; rows <= 3 - r; rows++)
            for (unsigned columns = 1; columns <= 4 - c; columns++) {
                tiku_rich_cells_t selection = {table,r,c,1,1}, out;
                size_t used = 0;
                uint64_t generation = doc->generation;

                for (unsigned i = 0; i < rows; i++)
                    for (unsigned j = 0; j < columns; j++)
                        used += (size_t)snprintf(text + used, sizeof text - used,
                            "%s\"%u:%u\"", j ? "\t" : i ? "\n" : "", i,j);
                notifications = 0;
                assert(tiku_richdoc_cells_paste_text(doc, generation, &selection,
                    text, used, &out) == TIKU_RICH_OK);
                assert(notifications == 1 && doc->generation == generation + 1);
                assert(out.row == r && out.column == c && out.rows == rows && out.columns == columns);
                expect(doc, &out, text);
                for (unsigned i = 0; i < 12; i++) {
                    char wanted[16] = "old";

                    if (i / 4 >= r && i / 4 < r + rows && i % 4 >= c && i % 4 < c + columns)
                        snprintf(wanted, sizeof wanted, "%u:%u", i / 4 - r, i % 4 - c);
                    assert(!strcmp(doc->root->paragraphs[i + 1]->value.text, wanted));
                    assert(!strcmp(old->root->paragraphs[i + 1]->value.text, "old"));
                }
                assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
                assert(doc->root == old->root);
                cases++;
            }
    assert(cases == 60);
    tiku_richwatch_destroy(watch);
    tiku_richsnapshot_release(old);
    tiku_richdoc_release(doc);
    puts("Cell paste: 60 rectangular placement oracles PASS");
}

/** @brief Preserve paragraph defaults and exact undo positions across multiline paste. */
static void
multiline(void)
{
    const char text[] = "\"a\"\"b\tX\r\nY\"\t\"\"\r\n\"\xce\xbb\"\t\"end\"\r\n";
    const char normalized[] = "\"a\"\"b\tX\nY\"\t\"\"\n\"\xce\xbb\"\t\"end\"";
    tiku_richdoc_t *doc = fixture(NULL);
    tiku_rich_cells_t cells = {doc->root->blocks->nodes[1].id, 0,0,2,2}, out;
    uint64_t first = doc->root->paragraphs[1]->value.id;
    tiku_richanchor_t *anchor;
    tiku_rich_pos_t position = {doc->root->body, first, 2, TIKU_RICH_AFTER}, read;
    tiku_richsnapshot_t *saved;

    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_PARAGRAPH_FORMAT,
        .paragraph = first, .props = {.present = TIKU_RICH_ALIGNMENT,
        .align = TIKU_RICH_CENTER}}) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_CELLS_SET,
        .cells = cells, .background = 0xff123456}) == TIKU_RICH_OK);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_SPLIT,
        .paragraph = first, .start = 1, .end = 1}) == TIKU_RICH_OK);
    position.byte = 1;
    assert(tiku_richanchor_create(doc, position, &anchor) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(doc, &saved) == TIKU_RICH_OK);
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, text, sizeof text - 1, &out) == TIKU_RICH_OK);
    expect(doc, &out, normalized);
    assert(doc->root->paragraphs[1]->value.id == first);
    assert(doc->root->paragraphs[1]->value.paragraph.align == TIKU_RICH_CENTER);
    assert(doc->root->paragraphs[2]->value.paragraph.align == TIKU_RICH_CENTER);
    assert(doc->root->blocks->nodes[2].background == 0xff123456);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK && doc->root == saved->root);
    assert(tiku_richanchor_get(anchor, &read) == TIKU_RICH_OK);
    assert(read.paragraph == position.paragraph && read.byte == position.byte);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    expect(doc, &cells, normalized);
    tiku_richsnapshot_release(saved);
    tiku_richanchor_release(anchor);
    tiku_richdoc_release(doc);
}

/** @brief Check quoting, row endings and empty fields against literal results. */
static void
grammar(void)
{
    static const struct { const char *input, *quoted; unsigned rows, columns; } cases[] = {
        {"a\tb\r\nc\td\r\n", "\"a\"\t\"b\"\n\"c\"\t\"d\"", 2,2},
        {"a\tb\rc\td\r", "\"a\"\t\"b\"\n\"c\"\t\"d\"", 2,2},
        {"\t", "\"\"\t\"\"", 1,2},
        {"a\t\nb\t", "\"a\"\t\"\"\n\"b\"\t\"\"", 2,2},
        {"\"a\r\nb\rc\n\"\t\"say \"\"hi\"\"\"",
         "\"a\nb\nc\n\"\t\"say \"\"hi\"\"\"", 1,2},
        {"raw\"quote\t\"quoted\"", "\"raw\"\"quote\"\t\"quoted\"", 1,2},
        {"\"\t\n\"\t\"\xc3\x9f\"", "\"\t\n\"\t\"\xc3\x9f\"", 1,2},
        {"a\n\n", "\"a\"\n\"\"", 2,1}
    };
    tiku_richdoc_t *doc = fixture(NULL);
    tiku_rich_cells_t cells = {doc->root->blocks->nodes[1].id, 0,0,1,1}, out;

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, cases[i].input,
            strlen(cases[i].input), &out) == TIKU_RICH_OK);
        assert(out.rows == cases[i].rows && out.columns == cases[i].columns);
        expect(doc, &out, cases[i].quoted);
        assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    }
    tiku_richdoc_release(doc);
}

/** @brief Refuse malformed clipboard bytes without changing state or accounting. */
static void
refusals(void)
{
    static const struct { const char *text; tiku_rich_status_t status; } cases[] = {
        {"a\tb\nc", TIKU_RICH_INVALID_RANGE},
        {"a\nb\tc", TIKU_RICH_INVALID_RANGE},
        {"\"unterminated", TIKU_RICH_INVALID_TEXT},
        {"\"closed\"junk", TIKU_RICH_INVALID_TEXT},
        {"\"a\" \t\"b\"", TIKU_RICH_INVALID_TEXT},
        {"\xc0\x80", TIKU_RICH_INVALID_TEXT},
        {"\xc3\t\xa9", TIKU_RICH_INVALID_TEXT},
        {"\xef\xbf\xbc", TIKU_RICH_UNSUPPORTED},
        {"a\001b", TIKU_RICH_INVALID_TEXT}
    };
    tiku_richdoc_t *doc = fixture(NULL);
    tiku_rich_cells_t cells = {doc->root->blocks->nodes[1].id, 0,0,1,1}, out;
    size_t memory = tiku_richdoc_memory(doc);
    uint64_t generation = doc->generation, next = doc->next_id;
    char huge[258];

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, cases[i].text,
            strlen(cases[i].text), &out) == cases[i].status);
        assert(!out.table && doc->generation == generation && doc->next_id == next);
        assert(tiku_richdoc_memory(doc) == memory);
    }
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "a\0b", 3, &out) == TIKU_RICH_INVALID_TEXT);
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, NULL, 1, &out) == TIKU_RICH_INVALID);
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "", SIZE_MAX, &out) == TIKU_RICH_LIMIT);
    assert(tiku_richdoc_cells_paste_text(doc, generation - 1, &cells, "x", 1, &out) == TIKU_RICH_STALE);
    cells.row = 2; cells.column = 3;
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "a\tb", 3, &out) == TIKU_RICH_INVALID_RANGE);
    cells.row = cells.column = 0; cells.rows = 2;
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "x", 1, &out) == TIKU_RICH_INVALID_RANGE);
    cells.rows = 1;
    memset(huge, '\t', 64);
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, huge, 64, &out) == TIKU_RICH_LIMIT);
    memset(huge, '\n', 257);
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, huge, 257, &out) == TIKU_RICH_LIMIT);
    assert(doc->generation == generation && tiku_richdoc_memory(doc) == memory);
    tiku_richdoc_set_read_only(doc, 1);
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "x", 1, &out) == TIKU_RICH_READ_ONLY);
    tiku_richdoc_set_read_only(doc, 0);
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, NULL, 0, &cells) == TIKU_RICH_OK);
    expect(doc, &cells, "\"\"");
    generation = doc->generation;
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "\"\"", 2, &out) == TIKU_RICH_NO_CHANGE);
    assert(doc->generation == generation && out.table == cells.table);
    tiku_richdoc_release(doc);
}

/** @brief Remove explicitly replaced media, refuse nested tables and bound edits. */
static void
limits(void)
{
    tiku_richdoc_t *doc = fixture(NULL);
    tiku_rich_cells_t cells = {doc->root->blocks->nodes[1].id, 0,0,1,1}, out;
    uint64_t paragraph = doc->root->paragraphs[1]->value.id, generation;
    uint32_t pixel = 0xff123456;
    tiku_rich_image_t image = {1,1,&pixel,10,10,"pixel"};
    char text[1028];

    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_INSERT,
        .paragraph = paragraph, .image = &image}) == TIKU_RICH_OK);
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "plain", 5, &out) == TIKU_RICH_OK);
    expect(doc, &cells, "\"plain\"");
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(doc->root->paragraphs[1]->value.runs[0].image);
    assert(apply(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
        .paragraph = paragraph, .table = {.rows = 1, .columns = 1}}) == TIKU_RICH_OK);
    generation = doc->generation;
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "plain", 5, &out) == TIKU_RICH_UNSUPPORTED);
    assert(doc->generation == generation);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    text[0] = '"';
    memset(text + 1, '\n', 1024);
    text[1025] = '"';
    generation = doc->generation;
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, text, 1026, &out) == TIKU_RICH_LIMIT);
    assert(doc->generation == generation);
    text[1024] = '"';
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, text, 1025, &out) == TIKU_RICH_OK);
    assert(doc->generation == generation + 1);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    generation = doc->generation;
    doc->next_id = UINT64_MAX;
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "\"a\nb\nc\"", 7, &out) == TIKU_RICH_LIMIT);
    assert(doc->generation == generation && doc->next_id == UINT64_MAX);
    tiku_richdoc_release(doc);
}

/** @brief Fill one cell using bytes reclaimed from another in the same paste. */
static void
capacity(void)
{
    tiku_richdoc_t *doc = fixture(NULL);
    tiku_rich_cells_t cells = {doc->root->blocks->nodes[1].id, 0,0,1,2};
    uint64_t generation = doc->generation;

    doc->limits.text_bytes = doc->root->bytes;
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "123456\t", 7, NULL) == TIKU_RICH_OK);
    expect(doc, &cells, "\"123456\"\t\"\"");
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    generation = doc->generation;
    assert(tiku_richdoc_cells_paste_text(doc, 0, &cells, "1234567\t", 8, NULL) == TIKU_RICH_LIMIT);
    assert(doc->generation == generation);
    expect(doc, &cells, "\"old\"\t\"old\"");
    tiku_richdoc_release(doc);
}

/** @brief Supply deterministic metrics for a view without a desktop font kit. */
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

/** @brief Count allocations and refuse an individual reservation. */
typedef struct { size_t call, fail, live; } faults_t;
/** @brief Allocate test storage unless this call is selected for refusal. */
static void *
allocate(void *ctx, size_t size)
{
    faults_t *f = ctx;
    void *p;

    if (++f->call == f->fail) return NULL;
    p = malloc(size);
    assert(p);
    f->live++;
    return p;
}
/** @brief Release counted storage. */
static void
release(void *ctx, void *p)
{
    faults_t *f = ctx;

    assert(f->live);
    f->live--;
    free(p);
}

/** @brief Refuse each allocation while preserving view, model, IDs and history. */
static void
failures(void)
{
    faults_t f = {0};
    tiku_rich_limits_t limits = {.allocator = {&f, allocate, release}};
    tiku_richdoc_t *doc = fixture(&limits);
    tiku_richlayout_options_t options = {.width = 240,
        .provider = {.shape = shape}};
    tiku_richview_t *view;
    tiku_rich_cells_t cells = {doc->root->blocks->nodes[1].id, 0,0,1,1}, read;
    const char text[] = "\"one\ntwo\"\tthree\nfour\tfive";
    uint64_t generation = doc->generation, next = doc->next_id;
    size_t memory, live, nth;

    assert(tiku_richview_create(doc, &options, &view) == TIKU_RICH_OK);
    assert(tiku_richview_select_cells(view, 0, &cells) == TIKU_RICH_OK);
    tiku_richsession_set_read_only(view->session, 1);
    assert(tiku_richview_cells_paste_text(view, text, sizeof text - 1) == TIKU_RICH_READ_ONLY);
    tiku_richsession_set_read_only(view->session, 0);
    doc->initiator = view->session;
    assert(tiku_richview_cells_paste_text(view, text, sizeof text - 1) == TIKU_RICH_BUSY);
    doc->initiator = NULL;
    memory = tiku_richdoc_memory(doc); live = f.live;
    for (nth = 1; nth < 200; nth++) {
        tiku_rich_status_t status;

        f.fail = f.call + nth;
        status = tiku_richview_cells_paste_text(view, text, sizeof text - 1);
        if (status == TIKU_RICH_OK) break;
        assert(status == TIKU_RICH_NO_MEMORY);
        assert(doc->generation == generation && doc->next_id == next);
        assert(tiku_richdoc_memory(doc) == memory && f.live == live);
        assert(!doc->undo && !doc->redo);
        assert(tiku_richview_cells(view, &read) == TIKU_RICH_OK);
        assert(read.rows == 1 && read.columns == 1 && read.table == cells.table);
    }
    assert(nth > 1 && nth < 200);
    f.fail = 0;
    assert(tiku_richview_cells(view, &read) == TIKU_RICH_OK);
    assert(read.rows == 2 && read.columns == 2);
    expect(doc, &read, "\"one\ntwo\"\t\"three\"\n\"four\"\t\"five\"");
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(tiku_richview_select_cells(view, 0, &cells) == TIKU_RICH_OK);
    tiku_richcmd_t cmd = {.kind = TIKU_RICHCMD_REPLACE_TEXT,
        .value.text = {"left\tright", 10}};
    tiku_richcmd_state_t state;
    assert(tiku_richview_query(view, cmd.kind, &state) == TIKU_RICH_OK && state.enabled);
    assert(tiku_richview_execute(view, doc->generation - 1, &cmd) == TIKU_RICH_STALE);
    assert(tiku_richview_execute(view, doc->generation, &cmd) == TIKU_RICH_OK);
    assert(tiku_richview_cells(view, &read) == TIKU_RICH_OK && read.columns == 2);
    expect(doc, &read, "\"left\"\t\"right\"");
    read.rows = 2;
    assert(tiku_richdoc_cells_clear(doc, 0, &read) == TIKU_RICH_OK);
    assert(tiku_richview_select_cells(view, 0, &cells) == TIKU_RICH_OK);
    generation = doc->generation;
    assert(tiku_richview_cells_paste_text(view, "\t\n\t", 3) == TIKU_RICH_OK);
    assert(doc->generation == generation);
    assert(tiku_richview_cells(view, &read) == TIKU_RICH_OK && read.rows == 2 && read.columns == 2);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
    assert(!f.live);
    printf("Cell paste: %zu allocation refusals PASS\n", nth - 1);
}

/** @brief Run rectangular paste, grammar, lifetime and refusal checks. */
int
main(void)
{
    matrix();
    multiline();
    grammar();
    refusals();
    limits();
    capacity();
    failures();
    puts("Cell paste: quoted TSV, multiline, media, undo and guards PASS");
    return 0;
}
