/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richcells_nested.c - nested clipboard trees, depth and atomic refusals.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Apply one checked edit. */
static void
edit(tiku_richdoc_t *doc, tiku_rich_edit_t change)
{
    tiku_rich_transaction_t tx = {0, &change, 1};

    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
}

/** @brief Create a table chain with text, nested shading and a linked image. */
static tiku_richdoc_t *
fixture(const tiku_rich_limits_t *limits, unsigned depth)
{
    tiku_richdoc_t *doc;
    uint32_t pixel = 0x80123456;
    tiku_rich_image_t image = {1, 1, &pixel, 8, 9, "Nested camera"};
    tiku_rich_style_spec_t style = {.name = "Nested",
        .chars = {.present = TIKU_RICH_SIZE, .size = 17}};
    uint64_t paragraph;

    assert(tiku_richdoc_create(limits, &doc) == TIKU_RICH_OK);
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_STYLE_DEFINE,
        .definition = &style});
    paragraph = doc->root->paragraphs[0]->value.id;
    for (unsigned i = 0; i < depth; i++) {
        size_t table;

        edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_TABLE_INSERT,
            .paragraph = paragraph, .table = {.rows = 1, .columns = 2,
            .padding = 2, .border = 1, .header_rows = 1,
            .weights = {1, 2}}});
        table = tiku_rd_block_find(doc->root, paragraph) + 1;
        paragraph = doc->root->blocks->nodes[table + 2].id;
        edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_REPLACE,
            .paragraph = paragraph, .text = "inner", .length = 5});
        edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_CELLS_SET,
            .cells = {doc->root->blocks->nodes[table].id, 0, 0, 1, 2},
            .background = 0xffaabbcc});
    }
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_PARAGRAPH_STYLE_SET,
        .paragraph = paragraph, .style = doc->root->styles[0].value.id});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_IMAGE_INSERT,
        .paragraph = paragraph, .image = &image});
    edit(doc, (tiku_rich_edit_t){.kind = TIKU_RICH_LINK_SET,
        .paragraph = paragraph, .end = 8, .link = "https://example.org/nested"});
    tiku_richdoc_clear_history(doc);
    return doc;
}

/** @brief Select the first direct cell of a fixture table. */
static tiku_rich_cells_t
selection(tiku_richdoc_t *doc, size_t table)
{
    return (tiku_rich_cells_t){doc->root->blocks->nodes[table].id, 0, 0, 1, 1};
}

/** @brief Validate imported-tree invariants independently of the copier. */
static void
valid(tiku_richdoc_t *doc)
{
    tiku_rd_root_t *root = doc->root;
    tiku_rich_source_t source = {.paragraph_count = root->count,
        .block_count = root->blocks->count};
    tiku_rich_block_source_t *blocks = calloc(source.block_count, sizeof *blocks);
    size_t paragraph = 0;

    assert(blocks);
    source.blocks = blocks;
    for (size_t i = 0; i < source.block_count; i++) {
        const tiku_rich_block_t *node = &root->blocks->nodes[i];

        blocks[i].kind = node->kind;
        blocks[i].table = node->table;
        blocks[i].background = node->background;
        if (node->parent != root->body) {
            size_t parent = tiku_rd_block_find(root, node->parent);

            assert(parent < i);
            blocks[i].parent = parent + 1;
        }
        if (node->kind == TIKU_RICH_BLOCK_PARAGRAPH) {
            assert(node->id == root->paragraphs[paragraph]->value.id);
            blocks[i].paragraph = paragraph++;
        }
        for (size_t j = 0; j < i; j++)
            assert(node->id != root->blocks->nodes[j].id);
    }
    assert(tiku_rd_blocks_validate(&source) == TIKU_RICH_OK);
    free(blocks);
}

/** @brief Preserve descendants and resources across insertion and replacement. */
static void
transfer(void)
{
    tiku_richdoc_t *src = fixture(NULL, 3), *dst = fixture(NULL, 1);
    tiku_richsnapshot_t *source, *before, *after;
    tiku_rich_cells_t from = selection(src, 1), to = selection(dst, 1), out;
    tiku_rich_pos_t at = {dst->root->body, dst->root->paragraphs[0]->value.id, 0, 0};
    size_t source_end = tiku_rd_subtree_end(src->root, 2);
    const tiku_rich_image_t *copied_image = NULL, *source_image = NULL;
    int picture = 0;

    assert(tiku_richdoc_snapshot(src, &source) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(dst, &before) == TIKU_RICH_OK);
    assert(tiku_richdoc_cells_paste(dst, 0, &to, source, &from, &out) == TIKU_RICH_OK);
    valid(dst);
    assert(tiku_rd_subtree_end(dst->root, 2) == source_end);
    for (size_t i = 2; i < source_end; i++) {
        const tiku_rich_block_t *a = &src->root->blocks->nodes[i];
        const tiku_rich_block_t *b = &dst->root->blocks->nodes[i];

        assert(a->kind == b->kind && a->background == b->background);
        assert(a->table.header_rows == b->table.header_rows);
        assert(a->table.weights[1] == b->table.weights[1]);
        if (i > 2) {
            size_t parent = tiku_rd_block_find(src->root, a->parent);

            assert(b->parent == dst->root->blocks->nodes[parent].id);
        }
    }
    for (size_t i = 0; i < dst->root->count; i++) {
        const tiku_rich_paragraph_t *p = &dst->root->paragraphs[i]->value;

        if (!p->run_count || !p->runs[0].image) continue;
        assert(p->runs[0].image->pixels[0] == 0x80123456);
        assert(!strcmp(p->runs[0].image->alt, "Nested camera"));
        assert(!strcmp(p->runs[0].link, "https://example.org/nested"));
        assert(p->style);
        copied_image = p->runs[0].image;
        picture++;
    }
    assert(picture == 1);
    for (size_t i = 0; i < src->root->count; i++) {
        const tiku_rich_paragraph_t *p = &src->root->paragraphs[i]->value;

        if (p->run_count && p->runs[0].image) source_image = p->runs[0].image;
    }
    assert(source_image && copied_image != source_image);
    assert(tiku_richdoc_snapshot(dst, &after) == TIKU_RICH_OK);
    assert(tiku_richdoc_undo(dst) == TIKU_RICH_OK && dst->root == before->root);
    assert(tiku_richdoc_redo(dst) == TIKU_RICH_OK && dst->root == after->root);
    assert(tiku_richdoc_cells_insert(dst, 0, at, source, &from, &out) == TIKU_RICH_OK);
    size_t table = tiku_rd_block_find(dst->root, out.table);
    assert(dst->root->blocks->nodes[table].table.header_rows == 1);
    assert(dst->root->blocks->nodes[table].table.padding == 2);
    assert(dst->root->blocks->nodes[table].table.weights[0] == 1);
    valid(dst);
    assert(tiku_richdoc_undo(dst) == TIKU_RICH_OK && dst->root == after->root);
    tiku_richsnapshot_release(source);
    tiku_richdoc_release(src);
    assert(copied_image->pixels[0] == 0x80123456);
    assert(!strcmp(copied_image->alt, "Nested camera"));
    valid(dst);
    tiku_richsnapshot_release(before);
    tiku_richsnapshot_release(after);
    tiku_richdoc_release(dst);
}

/** @brief Reject combined depth and ID exhaustion without changing history. */
static void
limits(void)
{
    tiku_richdoc_t *src = fixture(NULL, 8), *dst = fixture(NULL, 2);
    tiku_richsnapshot_t *source, *before;
    tiku_rich_cells_t from = selection(src, 1), to = selection(dst, 4), out;
    uint64_t generation = dst->generation, next = dst->next_id;
    size_t bytes;

    assert(tiku_richdoc_snapshot(src, &source) == TIKU_RICH_OK);
    assert(tiku_richdoc_snapshot(dst, &before) == TIKU_RICH_OK);
    bytes = tiku_richdoc_memory(dst);
    assert(tiku_richdoc_cells_paste(dst, 0, &to, source, &from, &out) == TIKU_RICH_LIMIT);
    assert(!out.table && dst->root == before->root && dst->generation == generation);
    assert(dst->next_id == next && tiku_richdoc_memory(dst) == bytes && !dst->undo);
    to = selection(dst, 1);
    dst->next_id = UINT64_MAX;
    assert(tiku_richdoc_cells_paste(dst, 0, &to, source, &from, &out) == TIKU_RICH_LIMIT);
    assert(dst->root == before->root && dst->next_id == UINT64_MAX);
    dst->next_id = next;
    assert(tiku_richdoc_cells_paste(dst, 0, &to, source, &from, &out) == TIKU_RICH_OK);
    valid(dst);
    tiku_richsnapshot_release(source);
    tiku_richsnapshot_release(before);
    tiku_richdoc_release(src);
    tiku_richdoc_release(dst);
}

/** @brief Run nested hierarchy and boundary checks. */
int
main(void)
{
    transfer();
    limits();
    puts("Nested cell clipboard: hierarchy, resources, insertion, depth and history PASS");
    return 0;
}
