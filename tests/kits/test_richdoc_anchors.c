/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_anchors.c - tracked positions through edits and undo/redo.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL anchors:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Create a document with one plain-text paragraph. */
static tiku_richdoc_t *
document(const char *text)
{
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_source_t p = {.text = text, .length = strlen(text)};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Construct a live position from a paragraph order and byte offset. */
static tiku_rich_pos_t
position(tiku_richdoc_t *doc, size_t index, size_t byte, int after)
{
    return (tiku_rich_pos_t){doc->root->body,
        doc->root->paragraphs[index]->value.id, byte,
        after ? TIKU_RICH_AFTER : TIKU_RICH_BEFORE};
}

/** @brief Register a checked tracked position. */
static tiku_richanchor_t *
anchor(tiku_richdoc_t *doc, size_t index, size_t byte, int after)
{
    tiku_richanchor_t *result;

    CHECK(tiku_richanchor_create(doc, position(doc, index, byte, after),
                                 &result) == TIKU_RICH_OK);
    return result;
}

/** @brief Compare a tracked position with a current document location. */
static void
expect(tiku_richanchor_t *a, size_t index, size_t byte)
{
    tiku_rich_pos_t p;

    CHECK(tiku_richanchor_get(a, &p) == TIKU_RICH_OK);
    CHECK(p.paragraph == a->doc->root->paragraphs[index]->value.id);
    CHECK(p.body == a->doc->root->body && p.byte == byte);
}

/** @brief Apply one checked edit. */
static void
apply(tiku_richdoc_t *doc, tiku_rich_edit_t edit)
{
    tiku_rich_transaction_t tx = {doc->generation, &edit, 1};

    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
}

/** @brief Affinity separates insertion sides and undo restores deleted points. */
static void
insertion_and_deletion(void)
{
    tiku_richdoc_t *doc = document("abcdef");
    tiku_richanchor_t *before = anchor(doc, 0, 2, 0);
    tiku_richanchor_t *after = anchor(doc, 0, 2, 1);
    tiku_richanchor_t *inside = anchor(doc, 0, 3, 1);
    tiku_rich_edit_t op = {.paragraph = before->position.paragraph,
        .start = 2, .end = 2, .text = "XYZ", .length = 3};

    apply(doc, op);
    expect(before, 0, 2);
    expect(after, 0, 5);
    expect(inside, 0, 6);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(after, 0, 2);
    expect(inside, 0, 3);
    op.end = 5;
    op.text = "!";
    op.length = 1;
    apply(doc, op);
    expect(before, 0, 2);
    expect(after, 0, 3);
    expect(inside, 0, 3);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(inside, 0, 3);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    expect(inside, 0, 3);
    CHECK(tiku_richanchor_set(inside, position(doc, 0, 4, 1)) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(inside, 0, 6);
    tiku_richanchor_release(before);
    tiku_richanchor_release(after);
    tiku_richanchor_release(inside);
    tiku_richdoc_release(doc);
}

/** @brief Structural mapping retains exact boundary ownership across history. */
static void
structure_and_lifetime(void)
{
    tiku_richdoc_t *doc = document("abcdef");
    tiku_richanchor_t *before = anchor(doc, 0, 3, 0);
    tiku_richanchor_t *after = anchor(doc, 0, 3, 1), *late;
    tiku_rich_edit_t op = {.kind = TIKU_RICH_SPLIT,
        .paragraph = before->position.paragraph, .start = 3, .end = 3};
    const tiku_rich_source_t empty = {0};

    apply(doc, op);
    expect(before, 0, 3);
    expect(after, 1, 0);
    late = anchor(doc, 1, 1, 1);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(before, 0, 3);
    expect(after, 0, 3);
    expect(late, 0, 4);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    expect(late, 1, 1);
    op.kind = TIKU_RICH_JOIN;
    op.start = op.end = 0;
    apply(doc, op);
    expect(after, 0, 3);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(after, 1, 0);
    tiku_richanchor_release(after);
    after = anchor(doc, 0, 1, 0);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    expect(after, 0, 1);
    CHECK(tiku_richdoc_import(doc, &empty, 0) == TIKU_RICH_OK);
    expect(before, 0, 0);
    expect(after, 0, 0);
    expect(late, 0, 0);
    CHECK(!tiku_richdoc_can_undo(doc));
    tiku_richdoc_release(doc);
    expect(late, 0, 0);
    tiku_richanchor_release(before);
    tiku_richanchor_release(after);
    tiku_richanchor_release(late);
}

/** @brief Anchor bounds, UTF-8 validation and revision overflow are checked. */
static void
limits(void)
{
    tiku_richdoc_t *doc = document("\xc3\xa9");
    tiku_richanchor_t *a, *failed = (void *)(uintptr_t)1;
    tiku_rich_pos_t out;
    uint64_t id;

    doc->limits.anchors = 1;
    CHECK(tiku_richanchor_create(doc, position(doc, 0, 1, 0), &failed) ==
          TIKU_RICH_INVALID_RANGE && !failed);
    a = anchor(doc, 0, 0, 0);
    id = a->id;
    CHECK(tiku_richanchor_create(doc, position(doc, 0, 0, 0), &failed) ==
          TIKU_RICH_LIMIT && !failed);
    CHECK(tiku_richanchor_set(a, position(doc, 0, 1, 0)) ==
          TIKU_RICH_INVALID_RANGE);
    expect(a, 0, 0);
    a->revision = UINT64_MAX;
    CHECK(tiku_richanchor_set(a, position(doc, 0, 2, 1)) == TIKU_RICH_LIMIT);
    expect(a, 0, 0);
    tiku_richanchor_release(a);
    a = anchor(doc, 0, 2, 1);
    CHECK(a->id > id);
    tiku_richanchor_release(a);
    doc->next_anchor = 0;
    CHECK(tiku_richanchor_create(doc, position(doc, 0, 0, 0), &failed) ==
          TIKU_RICH_LIMIT && !failed);
    CHECK(tiku_richanchor_get(NULL, &out) == TIKU_RICH_INVALID);
    CHECK(!out.body && !out.paragraph && !out.byte);
    tiku_richdoc_release(doc);
}

/** @brief Run position tracking, history and ownership checks. */
int
main(void)
{
    insertion_and_deletion();
    structure_and_lifetime();
    limits();
    puts("PASS: rich document anchors, affinity, undo mapping and lifetime");
    return 0;
}
