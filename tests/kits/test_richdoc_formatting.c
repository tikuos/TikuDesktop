/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_formatting.c - editable styles, inheritance and undo contracts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richdoc formatting:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Apply one command through the public transaction interface. */
static tiku_rich_status_t
apply(tiku_richdoc_t *doc, const tiku_rich_edit_t *edit)
{
    const tiku_rich_transaction_t tx = {doc->generation, edit, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Load two paragraphs, inherited paragraph styles and a run style. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_richdoc_t *doc;
    const tiku_rich_style_source_t styles[] = {
        {.name = "Body", .chars = {.present = TIKU_RICH_SIZE, .size = 12},
         .paragraph = {.present = TIKU_RICH_ALIGNMENT,
                       .align = TIKU_RICH_LEFT}},
        {.name = "Heading", .parent = 1,
         .chars = {.present = TIKU_RICH_BOLD, .bold = 1}},
        {.name = "Emphasis", .kind = TIKU_RICH_CHARACTER_STYLE,
         .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}}
    };
    const tiku_rich_paragraph_source_t paragraphs[] = {
        {.text = "A\xc3\xa9" "bc", .length = 5, .style = 2}, {.style = 1}
    };
    const tiku_rich_source_t source = {.styles = styles, .style_count = 3,
        .paragraphs = paragraphs, .paragraph_count = 2};

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Read resolved values through an immutable snapshot. */
static void
resolve(tiku_richdoc_t *doc, size_t paragraph, size_t run,
          tiku_rich_chars_t *chars, tiku_rich_para_t *props)
{
    tiku_richsnapshot_t *s;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_resolve(s, paragraph, run, chars, props) ==
          TIKU_RICH_OK);
    tiku_richsnapshot_release(s);
}

/** @brief Paragraph overrides, clearing, empty paragraphs and stable anchors. */
static void
paragraphs(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_edit_t op = {.kind = TIKU_RICH_PARAGRAPH_FORMAT,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .chars = {.present = TIKU_RICH_SIZE, .size = 20},
        .props = {.present = TIKU_RICH_ALIGNMENT | TIKU_RICH_INDENT |
                            TIKU_RICH_SPACING | TIKU_RICH_KEEP,
                  .align = TIKU_RICH_CENTER, .left = 12, .first = -6,
                  .before = 8, .keep_next = 1}};
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    tiku_richanchor_t *anchor;
    tiku_rich_pos_t pos = {1, op.paragraph, 3, TIKU_RICH_AFTER}, actual;
    tiku_richsnapshot_t *old;
    size_t before;
    uint64_t generation;

    CHECK(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    CHECK(tiku_richanchor_create(doc, pos, &anchor) == TIKU_RICH_OK);
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    resolve(doc, 0, 0, &chars, &props);
    CHECK(chars.size == 20 && chars.bold && props.align == TIKU_RICH_CENTER);
    CHECK(props.left == 12 && props.first == -6 && props.before == 8);
    CHECK(props.keep_next && !props.keep_lines);
    CHECK(tiku_richanchor_get(anchor, &actual) == TIKU_RICH_OK);
    CHECK(actual.paragraph == pos.paragraph && actual.byte == pos.byte);
    CHECK(tiku_richsnapshot_resolve(old, 0, 0, &chars, &props) == TIKU_RICH_OK);
    CHECK(chars.size == 12 && props.align == TIKU_RICH_LEFT);
    before = tiku_richdoc_memory(doc);
    generation = doc->generation;
    CHECK(apply(doc, &op) == TIKU_RICH_NO_CHANGE);
    CHECK(doc->generation == generation && tiku_richdoc_memory(doc) == before);
    op.chars.present = 0;
    op.props.present = 0;
    op.clear_chars = TIKU_RICH_SIZE;
    op.clear_props = 15;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    resolve(doc, 0, 0, &chars, &props);
    CHECK(chars.size == 12 && props.present == TIKU_RICH_ALIGNMENT);
    CHECK(props.align == TIKU_RICH_LEFT);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    resolve(doc, 0, 0, &chars, &props);
    CHECK(chars.size == 20 && props.left == 12);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    op.paragraph = doc->root->paragraphs[1]->value.id;
    op.chars = (tiku_rich_chars_t){.present = TIKU_RICH_SIZE, .size = 18};
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    resolve(doc, 1, 0, &chars, &props);
    CHECK(chars.size == 18);
    op.props = (tiku_rich_para_t){.present = TIKU_RICH_SPACING, .after = NAN};
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    op.props.present = 0;
    op.clear_props = 64;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    op.start = 1;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_RANGE);
    op = (tiku_rich_edit_t){.kind = TIKU_RICH_PARAGRAPH_STYLE_SET,
        .paragraph = pos.paragraph, .style = doc->root->styles[0].value.id};
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    resolve(doc, 0, 0, &chars, &props);
    CHECK(!(chars.present & TIKU_RICH_BOLD));
    op.style = doc->root->styles[2].value.id;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    op.style = 0;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    resolve(doc, 0, 0, &chars, &props);
    CHECK(!chars.present && !props.present);
    tiku_richanchor_release(anchor);
    tiku_richsnapshot_release(old);
    tiku_richdoc_release(doc);
}

/** @brief Named run assignment and selective override removal on UTF-8 spans. */
static void
characters(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_edit_t op = {.kind = TIKU_RICH_CHARACTER_STYLE_SET,
        .paragraph = doc->root->paragraphs[0]->value.id, .start = 1, .end = 3,
        .style = doc->root->styles[2].value.id};
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    const tiku_rich_paragraph_t *p;

    doc->limits.runs = 2;
    CHECK(apply(doc, &op) == TIKU_RICH_LIMIT);
    doc->limits.runs = 100;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    p = &doc->root->paragraphs[0]->value;
    CHECK(p->run_count == 3 && p->runs[1].length == 2);
    CHECK(p->runs[1].style == op.style);
    resolve(doc, 0, 1, &chars, &props);
    CHECK(chars.italic && chars.bold && chars.size == 12);
    CHECK(apply(doc, &op) == TIKU_RICH_NO_CHANGE);
    op.start = 2;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_RANGE);
    op.start = 1;
    op.style = doc->root->styles[0].value.id;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    op.kind = TIKU_RICH_FORMAT;
    op.chars = (tiku_rich_chars_t){.present = TIKU_RICH_ITALIC, .italic = 0};
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    resolve(doc, 0, 1, &chars, &props);
    CHECK((chars.present & TIKU_RICH_ITALIC) && !chars.italic);
    op.chars.present = 0;
    op.clear_chars = TIKU_RICH_ITALIC;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    resolve(doc, 0, 1, &chars, &props);
    CHECK(chars.italic);
    op.kind = TIKU_RICH_CHARACTER_STYLE_SET;
    op.style = 0;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[0]->value.run_count == 1);
    resolve(doc, 0, 0, &chars, &props);
    CHECK(!(chars.present & TIKU_RICH_ITALIC));
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    resolve(doc, 0, 1, &chars, &props);
    CHECK(chars.italic);
    op.kind = TIKU_RICH_FORMAT;
    op.clear_chars = 512;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    tiku_richdoc_release(doc);
}

/** @brief Style updates affect descendants, preserve direct values and undo. */
static void
definitions(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_style_spec_t spec = {.name = "Body renamed",
        .chars = {.present = TIKU_RICH_SIZE, .size = 30}};
    tiku_rich_edit_t op = {.kind = TIKU_RICH_STYLE_DEFINE,
        .style = doc->root->styles[0].value.id, .definition = &spec};
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    tiku_richsnapshot_t *old;
    uint64_t id = op.style, next;
    tiku_rd_root_t *root;

    CHECK(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    CHECK(apply(doc, &op) == TIKU_RICH_OK && tiku_richdoc_dirty(doc));
    CHECK(doc->root->styles[0].value.id == id);
    CHECK(doc->root->paragraphs[0] == old->root->paragraphs[0]);
    resolve(doc, 0, 0, &chars, &props);
    CHECK(chars.size == 30 && chars.bold && !props.present);
    CHECK(!strcmp(old->root->styles[0].value.name, "Body"));
    CHECK(apply(doc, &op) == TIKU_RICH_NO_CHANGE);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK && !tiku_richdoc_dirty(doc));
    resolve(doc, 0, 0, &chars, &props);
    CHECK(chars.size == 12);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    root = doc->root;
    spec.parent = doc->root->styles[1].value.id;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE && doc->root == root);
    spec.parent = id;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    spec.parent = UINT64_MAX;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    spec.parent = doc->root->styles[2].value.id;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    spec.parent = 0;
    spec.kind = TIKU_RICH_CHARACTER_STYLE;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    spec.kind = TIKU_RICH_PARAGRAPH_STYLE;
    strcpy(spec.name, "Heading");
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    strcpy(spec.name, "Fresh");
    op.style = 0;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(doc->root->nstyles == 4);
    next = doc->root->styles[3].value.id;
    CHECK(next != id && !strcmp(doc->root->styles[3].value.name, "Fresh"));
    strcpy(spec.name, "input overwritten");
    CHECK(!strcmp(doc->root->styles[3].value.name, "Fresh"));
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(doc->root->styles[3].value.id > next);
    op.kind = TIKU_RICH_STYLE_REMOVE;
    op.style = id;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    op.style = doc->root->styles[2].value.id;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(doc->root->nstyles == 3);
    CHECK(!strcmp(doc->root->styles[2].value.name, "input overwritten"));
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(!strcmp(doc->root->styles[2].value.name, "Emphasis"));
    tiku_richsnapshot_release(old);
    tiku_richdoc_release(doc);
}

/** @brief Descendant depth, duplicate categories, ID and style count limits. */
static void
style_limits(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_style_source_t styles[65] = {0};
    tiku_rich_source_t source = {.styles = styles, .style_count = 65};
    tiku_rich_style_spec_t spec = {.name = "S0"};
    tiku_rich_edit_t op = {.kind = TIKU_RICH_STYLE_DEFINE, .definition = &spec};
    size_t i;

    for (i = 0; i < 65; i++) {
        snprintf(styles[i].name, sizeof styles[i].name, "S%zu", i);
        styles[i].parent = i < 64 ? i : 0;
    }
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    op.style = doc->root->styles[0].value.id;
    spec.parent = doc->root->styles[64].value.id;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    op.style = 0;
    spec.parent = 0;
    spec.kind = TIKU_RICH_CHARACTER_STYLE;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    strcpy(spec.name, "Overflow");
    doc->limits.styles = doc->root->nstyles;
    CHECK(apply(doc, &op) == TIKU_RICH_LIMIT);
    doc->limits.styles++;
    doc->next_id = UINT64_MAX;
    CHECK(apply(doc, &op) == TIKU_RICH_OK && !doc->next_id);
    CHECK(doc->root->styles[doc->root->nstyles - 1].value.id == UINT64_MAX);
    strcpy(spec.name, "Exhausted");
    doc->limits.styles++;
    CHECK(apply(doc, &op) == TIKU_RICH_LIMIT);
    tiku_richdoc_release(doc);
}

/** @brief Mixed text/style commands share failure atomicity and one undo. */
static void
transactions(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_edit_t edits[4] = {
        {.kind = TIKU_RICH_PARAGRAPH_STYLE_SET,
         .paragraph = doc->root->paragraphs[0]->value.id},
        {.kind = TIKU_RICH_STYLE_REMOVE,
         .style = doc->root->styles[1].value.id},
        {.kind = TIKU_RICH_REPLACE,
         .paragraph = doc->root->paragraphs[0]->value.id,
         .text = "X", .length = 1},
        {.kind = TIKU_RICH_CHARACTER_STYLE_SET,
         .paragraph = doc->root->paragraphs[0]->value.id,
         .start = 1, .end = 3, .style = doc->root->styles[2].value.id}
    };
    tiku_rich_transaction_t tx = {doc->generation, edits, 4};
    tiku_rd_root_t *root = doc->root;
    size_t used = tiku_richdoc_memory(doc);

    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_INVALID_RANGE);
    CHECK(doc->root == root && !tiku_richdoc_dirty(doc));
    CHECK(tiku_richdoc_memory(doc) == used && !tiku_richdoc_can_undo(doc));
    edits[3].end = 4;
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
    CHECK(doc->root->nstyles == 2);
    CHECK(!strcmp(doc->root->styles[1].value.name, "Emphasis"));
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_STALE);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK && doc->root == root);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(apply(doc, &edits[0]) == TIKU_RICH_READ_ONLY);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_READ_ONLY);
    tiku_richdoc_release(doc);
}

/** @brief Clearing run reset bits restores an inherited property. */
static void
reset_inheritance(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_run_source_t run = {.length = 1, .reset = TIKU_RICH_SIZE};
    tiku_rich_paragraph_source_t p = {.text = "x", .length = 1,
        .runs = &run, .run_count = 1,
        .chars = {.present = TIKU_RICH_SIZE, .size = 12}};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    tiku_rich_edit_t op = {.kind = TIKU_RICH_FORMAT, .end = 1,
                           .clear_chars = TIKU_RICH_SIZE};

    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    op.paragraph = doc->root->paragraphs[0]->value.id;
    resolve(doc, 0, 0, &chars, &props);
    CHECK(!(chars.present & TIKU_RICH_SIZE));
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    resolve(doc, 0, 0, &chars, &props);
    CHECK(chars.size == 12);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    resolve(doc, 0, 0, &chars, &props);
    CHECK(!(chars.present & TIKU_RICH_SIZE));
    tiku_richdoc_release(doc);
}

/** @brief Run paragraph, run, named style, transaction and boundary checks. */
int
main(void)
{
    paragraphs();
    characters();
    definitions();
    style_limits();
    transactions();
    reset_inheritance();
    puts("PASS: rich document formatting and editable named styles");
    return 0;
}
