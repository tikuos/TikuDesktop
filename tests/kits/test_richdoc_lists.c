/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_lists.c - nested numbering, restart, structure and history.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richdoc lists:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Read one decimal ordinal through the public snapshot API. */
static uint64_t
number(tiku_richdoc_t *doc, size_t index)
{
    tiku_richsnapshot_t *s;
    tiku_rich_list_t list;
    uint64_t result;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_list(s, index, &list, &result) == TIKU_RICH_OK);
    tiku_richsnapshot_release(s);
    return result;
}

/** @brief Apply one typed edit as one history group. */
static tiku_rich_status_t
apply(tiku_richdoc_t *doc, const tiku_rich_edit_t *edit)
{
    const tiku_rich_transaction_t tx = {doc->generation, edit, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Restart, nested levels, type transitions, gaps and large ordinals. */
static void
numbering(void)
{
    tiku_rich_paragraph_source_t p[12] = {0};
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 12};
    const uint64_t expected[] = {4, 1, 2, 5, 0, 1, 6, 0, 1,
                                 UINT32_MAX, (uint64_t)UINT32_MAX + 1, 1};
    tiku_richdoc_t *doc;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_LIST_SET};
    size_t i;

    for (i = 0; i < 12; i++)
        p[i].list.kind = TIKU_RICH_LIST_DECIMAL;
    p[0].list.restart = 4;
    p[1].list.level = p[2].list.level = 1;
    p[4].list = (tiku_rich_list_t){TIKU_RICH_LIST_BULLET, 1, 0};
    p[5].list.level = 1;
    p[7].list.kind = TIKU_RICH_LIST_NONE;
    p[9].list.restart = UINT32_MAX;
    p[11].list.level = 8;
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    for (i = 0; i < 12; i++)
        CHECK(number(doc, i) == expected[i]);
    edit.paragraph = doc->root->paragraphs[0]->value.id;
    edit.list = (tiku_rich_list_t){TIKU_RICH_LIST_DECIMAL, 0, 10};
    CHECK(apply(doc, &edit) == TIKU_RICH_OK);
    CHECK(number(doc, 0) == 10 && number(doc, 3) == 11);
    CHECK(number(doc, 1) == 1 && number(doc, 6) == 12);
    CHECK(number(doc, 8) == 1);
    CHECK(apply(doc, &edit) == TIKU_RICH_NO_CHANGE);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(number(doc, 0) == 4 && !tiku_richdoc_dirty(doc));
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    CHECK(number(doc, 3) == 11);
    tiku_richdoc_release(doc);
}

/** @brief Split continues numbering; join keeps the leading membership. */
static void
structure(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_source_t p = {.text = "abcd", .length = 4,
        .list = {TIKU_RICH_LIST_DECIMAL, 0, 7}};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_rich_edit_t op = {.kind = TIKU_RICH_SPLIT, .start = 2, .end = 2};
    tiku_richsnapshot_t *old;
    tiku_rich_list_t list;
    uint64_t ordinal;
    tiku_rich_pos_t pos;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    op.paragraph = doc->root->paragraphs[0]->value.id;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(number(doc, 0) == 7 && number(doc, 1) == 8);
    CHECK(!doc->root->paragraphs[1]->value.list.restart);
    op.kind = TIKU_RICH_JOIN;
    op.start = op.end = 0;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(number(doc, 0) == 7);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(number(doc, 1) == 8);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    pos = (tiku_rich_pos_t){1, op.paragraph, 2, TIKU_RICH_AFTER};
    CHECK(tiku_richdoc_replace_range(doc, 0, pos, pos, "x\ny\nz", 5) ==
          TIKU_RICH_OK);
    CHECK(number(doc, 0) == 7 && number(doc, 1) == 8 && number(doc, 2) == 9);
    op = (tiku_rich_edit_t){.kind = TIKU_RICH_LIST_SET,
        .paragraph = doc->root->paragraphs[1]->value.id};
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(number(doc, 2) == 1);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK && number(doc, 2) == 9);
    CHECK(tiku_richsnapshot_list(old, 0, &list, &ordinal) == TIKU_RICH_OK);
    CHECK(list.restart == 7 && ordinal == 7);
    tiku_richdoc_release(doc);
    CHECK(tiku_richsnapshot_list(old, 0, &list, &ordinal) == TIKU_RICH_OK);
    tiku_richsnapshot_release(old);
}

/** @brief Invalid imports/commands refuse publication and clear query outputs. */
static void
validation(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_edit_t op = {.kind = TIKU_RICH_LIST_SET,
        .list = {TIKU_RICH_LIST_DECIMAL, 9, 0}};
    tiku_richsnapshot_t *s;
    tiku_rich_list_t list;
    uint64_t ordinal;
    tiku_rich_paragraph_source_t p = {.list = {TIKU_RICH_LIST_BULLET, 0, 1}};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    size_t used;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    used = tiku_richdoc_memory(doc);
    op.paragraph = doc->root->paragraphs[0]->value.id;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    CHECK(tiku_richdoc_memory(doc) == used && doc->generation == 1);
    op.list = p.list;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_INVALID_STYLE);
    op.list.kind = (tiku_rich_list_kind_t)99;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_STYLE);
    op.list.kind = TIKU_RICH_LIST_NONE;
    op.list.level = 999;
    CHECK(apply(doc, &op) == TIKU_RICH_NO_CHANGE);
    op.list = (tiku_rich_list_t){TIKU_RICH_LIST_BULLET, 0, 0};
    op.start = 1;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_RANGE);
    op.start = 0;
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(apply(doc, &op) == TIKU_RICH_READ_ONLY);
    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    ordinal = 999;
    list = p.list;
    CHECK(tiku_richsnapshot_list(s, 1, &list, &ordinal) ==
          TIKU_RICH_INVALID_RANGE);
    CHECK(list.kind == TIKU_RICH_LIST_NONE && !list.restart && !ordinal);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Independent backward recurrence for a decimal item's number. */
static uint64_t
reference(const tiku_rich_paragraph_source_t *p, size_t index)
{
    size_t previous = index;

    if (p[index].list.kind != TIKU_RICH_LIST_DECIMAL)
        return 0;
    if (p[index].list.restart)
        return p[index].list.restart;
    while (previous) {
        previous--;
        if (p[previous].list.kind == TIKU_RICH_LIST_NONE ||
            p[previous].list.level < p[index].list.level)
            break;
        if (p[previous].list.level == p[index].list.level) {
            if (p[previous].list.kind != p[index].list.kind)
                break;
            return reference(p, previous) + 1;
        }
    }
    return 1;
}

/** @brief Deterministic generator independent of kit state. */
static uint32_t
random_value(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

/** @brief Compare generated memberships and edits against backward recurrence. */
static void
generated(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_source_t p[32] = {0};
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 32};
    tiku_rich_edit_t op = {.kind = TIKU_RICH_LIST_SET};
    uint32_t state = 827612;
    size_t trial, i, index;
    tiku_rich_status_t status;
    tiku_rich_list_t before;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    for (trial = 0; trial < 1000; trial++) {
        for (i = 0; i < 32; i++) {
            p[i].list.kind = (tiku_rich_list_kind_t)
                ((random_value(&state) >> 16) % 3);
            p[i].list.level = random_value(&state) % 4;
            p[i].list.restart = p[i].list.kind == TIKU_RICH_LIST_DECIMAL &&
                random_value(&state) % 3 == 0 ? random_value(&state) % 50 : 0;
        }
        CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
        for (i = 0; i < 32; i++)
            CHECK(number(doc, i) == reference(p, i));
        index = random_value(&state) % 32;
        before = p[index].list;
        op.paragraph = doc->root->paragraphs[index]->value.id;
        op.list = (tiku_rich_list_t){TIKU_RICH_LIST_DECIMAL,
            random_value(&state) % 4, 1 + random_value(&state) % 30};
        status = apply(doc, &op);
        CHECK(status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE);
        p[index].list = op.list;
        for (i = 0; i < 32; i++)
            CHECK(number(doc, i) == reference(p, i));
        if (status == TIKU_RICH_OK) {
            CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
            p[index].list = before;
            for (i = 0; i < 32; i++)
                CHECK(number(doc, i) == reference(p, i));
        }
    }
    tiku_richdoc_release(doc);
}

/** @brief Run list import, editing, structural and numbering checks. */
int
main(void)
{
    numbering();
    structure();
    validation();
    generated();
    puts("PASS: rich document lists, 1000 generated numbering/edit cases");
    return 0;
}
