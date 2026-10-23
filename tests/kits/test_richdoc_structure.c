/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_structure.c - paragraph identities, split/join and appearance.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL structure:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Apply a single operation on one paragraph by order. */
static tiku_rich_status_t
edit(tiku_richdoc_t *doc, size_t index, tiku_rich_edit_kind_t kind, size_t at)
{
    tiku_rich_edit_t op = {.kind = kind, .start = at, .end = at,
        .paragraph = doc->root->paragraphs[index]->value.id};
    tiku_rich_transaction_t tx = {doc->generation, &op, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Compare the live document with a plain-text oracle. */
static void
expect(tiku_richdoc_t *doc, const char *expected)
{
    tiku_richsnapshot_t *s;
    char text[1024];
    size_t length;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_text(s, text, sizeof text, &length) ==
          TIKU_RICH_OK);
    CHECK(!strcmp(text, expected) && length == strlen(expected));
    tiku_richsnapshot_release(s);
}

/** @brief Splits retain leading IDs, use fresh trailing IDs and copy styles. */
static void
split_and_join(void)
{
    tiku_rich_paragraph_source_t p = {.text = "A\xc3\xa9Z", .length = 4,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *old;
    uint64_t first, second, third;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    first = doc->root->paragraphs[0]->value.id;
    CHECK(edit(doc, 0, TIKU_RICH_SPLIT, 2) == TIKU_RICH_INVALID_RANGE);
    CHECK(edit(doc, 0, TIKU_RICH_SPLIT, 1) == TIKU_RICH_OK);
    expect(doc, "A\n\xc3\xa9Z");
    second = doc->root->paragraphs[1]->value.id;
    CHECK(first == doc->root->paragraphs[0]->value.id && first != second);
    CHECK(doc->root->paragraphs[1]->value.chars.bold);
    CHECK(old->root->count == 1 && old->root->paragraphs[0]->value.length == 4);
    CHECK(edit(doc, 0, TIKU_RICH_SPLIT, 1) == TIKU_RICH_OK);
    third = doc->root->paragraphs[1]->value.id;
    CHECK(third > second);
    expect(doc, "A\n\n\xc3\xa9Z");
    CHECK(edit(doc, 1, TIKU_RICH_JOIN, 0) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[1]->value.id == third);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[2]->value.id == second);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "A\xc3\xa9Z");
    CHECK(!tiku_richdoc_dirty(doc));
    CHECK(edit(doc, 0, TIKU_RICH_SPLIT, 0) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[1]->value.id > third);
    expect(doc, "\nA\xc3\xa9Z");
    CHECK(edit(doc, 0, TIKU_RICH_JOIN, 0) == TIKU_RICH_OK);
    expect(doc, "A\xc3\xa9Z");
    CHECK(edit(doc, 0, TIKU_RICH_JOIN, 0) == TIKU_RICH_INVALID_RANGE);
    tiku_richdoc_release(doc);
    CHECK(!strcmp(old->root->paragraphs[0]->value.text, "A\xc3\xa9Z"));
    tiku_richsnapshot_release(old);
}

/** @brief Joining unlike paragraph defaults preserves resolved character data. */
static void
join_appearance(void)
{
    tiku_rich_style_source_t styles[2] = {
        {.name = "Heading", .chars = {
            .present = TIKU_RICH_BOLD | TIKU_RICH_SIZE | TIKU_RICH_FONT,
            .bold = 1, .size = 24, .font = "Serif"}},
        {.name = "Emphasis", .kind = TIKU_RICH_CHARACTER_STYLE,
         .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}}
    };
    tiku_rich_run_source_t run = {.length = 4, .style = 2};
    tiku_rich_paragraph_source_t p[2] = {
        {.text = "head", .length = 4, .style = 1,
         .paragraph = {.present = TIKU_RICH_ALIGNMENT,
                       .align = TIKU_RICH_CENTER}},
        {.text = "body", .length = 4, .runs = &run, .run_count = 1}
    };
    tiku_rich_source_t source = {.styles = styles, .style_count = 2,
        .paragraphs = p, .paragraph_count = 2};
    tiku_richdoc_t *doc;
    tiku_rich_chars_t before, after;
    tiku_rich_para_t props;
    uint64_t character_style;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    character_style = doc->root->paragraphs[1]->value.runs[0].style;
    CHECK(tiku_rd_resolve(doc->root, 1, 0, &before, &props) == TIKU_RICH_OK);
    CHECK(edit(doc, 0, TIKU_RICH_JOIN, 0) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[0]->value.run_count == 2);
    CHECK(tiku_rd_resolve(doc->root, 0, 1, &after, &props) == TIKU_RICH_OK);
    CHECK(tiku_rd_chars_equal(&before, &after));
    CHECK(!(after.present & TIKU_RICH_SIZE) && !after.bold && !after.font[0]);
    CHECK(after.italic && props.align == TIKU_RICH_CENTER);
    CHECK(doc->root->paragraphs[0]->value.runs[1].style == character_style);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(!doc->root->paragraphs[1]->value.runs[0].reset);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    expect(doc, "headbody");
    p[1].style = 1;
    p[1].chars = (tiku_rich_chars_t){.present = TIKU_RICH_SIZE |
        TIKU_RICH_BOLD | TIKU_RICH_FONT, .size = 12, .font = "Sans", .bold = 0};
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_rd_resolve(doc->root, 1, 0, &before, &props) == TIKU_RICH_OK);
    CHECK(edit(doc, 0, TIKU_RICH_JOIN, 0) == TIKU_RICH_OK);
    CHECK(tiku_rd_resolve(doc->root, 0, 1, &after, &props) == TIKU_RICH_OK);
    CHECK(tiku_rd_chars_equal(&before, &after));
    CHECK(after.size == 12 && !after.bold && !strcmp(after.font, "Sans"));
    run.reset = 1u << 30;
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    tiku_richdoc_release(doc);
}

/** @brief Structural limits and late failure preserve ID and history state. */
static void
limits_and_rollback(void)
{
    tiku_rich_limits_t limits = {.paragraphs = 2, .runs = 1};
    tiku_rich_paragraph_source_t p = {.text = "abcd", .length = 4};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richdoc_t *doc;
    tiku_rich_edit_t ops[2] = {0};
    tiku_rich_transaction_t tx = {.edits = ops, .count = 2};
    uint64_t next;
    size_t memory;

    CHECK(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    next = doc->next_id;
    memory = tiku_richdoc_memory(doc);
    CHECK(edit(doc, 0, TIKU_RICH_SPLIT, 2) == TIKU_RICH_LIMIT);
    CHECK(doc->next_id == next && tiku_richdoc_memory(doc) == memory);
    ops[0] = (tiku_rich_edit_t){.kind = TIKU_RICH_SPLIT,
        .paragraph = doc->root->paragraphs[0]->value.id, .start = 4, .end = 4};
    ops[1] = (tiku_rich_edit_t){.paragraph = UINT64_MAX};
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_INVALID_RANGE);
    CHECK(doc->next_id == next && tiku_richdoc_memory(doc) == memory);
    CHECK(edit(doc, 0, TIKU_RICH_SPLIT, 4) == TIKU_RICH_OK);
    CHECK(edit(doc, 1, TIKU_RICH_SPLIT, 0) == TIKU_RICH_LIMIT);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    doc->next_id = UINT64_MAX;
    CHECK(edit(doc, 0, TIKU_RICH_SPLIT, 4) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[1]->value.id == UINT64_MAX && !doc->next_id);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(edit(doc, 0, TIKU_RICH_SPLIT, 0) == TIKU_RICH_LIMIT);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    CHECK(edit(doc, 0, TIKU_RICH_JOIN, 0) == TIKU_RICH_OK);
    expect(doc, "abcd");
    tiku_richdoc_release(doc);
}

/** @brief Run structural text, style, identity and budget tests. */
int
main(void)
{
    split_and_join();
    join_appearance();
    limits_and_rollback();
    puts("PASS: rich document split/join, styles, IDs and atomic refusal");
    return 0;
}
