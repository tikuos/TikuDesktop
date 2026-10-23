/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_links.c - hyperlink ownership, boundaries and edit atomicity.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richdoc links:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Apply one edit through the public transaction entry point. */
static tiku_rich_status_t
apply(tiku_richdoc_t *doc, const tiku_rich_edit_t *edit)
{
    const tiku_rich_transaction_t tx = {doc->generation, edit, 1};

    return tiku_richdoc_apply(doc, &tx);
}

/** @brief Import four linked bytes between two unlinked characters. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_richdoc_t *doc;
    char target[] = "https://example.org/notes";
    const tiku_rich_run_source_t runs[] = {
        {.length = 1}, {.length = 4, .link = target}, {.length = 1}
    };
    const tiku_rich_paragraph_source_t p = {
        .text = "abcdef", .length = 6, .runs = runs, .run_count = 3
    };
    const tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    memset(target, 'x', sizeof target);
    return doc;
}

/** @brief Get a borrowed target at one byte of the live document. */
static const char *
target_at(tiku_richdoc_t *doc, size_t paragraph, size_t byte)
{
    const tiku_rich_paragraph_t *p = &doc->root->paragraphs[paragraph]->value;
    size_t i, at = 0;

    CHECK(byte < p->length);
    for (i = 0; i < p->run_count; i++) {
        at += p->runs[i].length;
        if (byte < at)
            return p->runs[i].link;
    }
    CHECK(0);
    return NULL;
}

/** @brief Scheme policy, malformed targets, maximum length and UTF-8 checks. */
static void
validation(void)
{
    const char *accepted[] = {NULL, "", "https://example.org", "HTTP://host/x",
        "mailto:a@example.org", "#section", "https://host/a%20b",
        "https://host/\xc3\xa9"};
    const char *malformed[] = {"#", "mailto:", "https://", "http:///x",
        "https://?x", "https://#x", "https://host/a b", "https://host/\\x",
        "https://host/\n", "mailto:x%0d%0aHeader:y", "https://host/%00",
        "https://host/%5c", "https://host/%7f", "https://host/%xy",
        "https://host/%", "https://host/%a", "https://host/\xc0\x80"};
    const char *unsupported[] = {"javascript:alert(1)", "data:text/plain,a",
        "file:///etc/passwd", "../file", "ftp://host", "h", "http", "https:"};
    char large[2050];
    size_t i;
    tiku_richdoc_t *doc = fixture();
    tiku_rich_edit_t op = {.kind = TIKU_RICH_LINK_SET,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .start = 1, .end = 5, .link = "javascript:alert(1)"};
    size_t used = tiku_richdoc_memory(doc);

    for (i = 0; i < sizeof accepted / sizeof *accepted; i++)
        CHECK(tiku_rich_link_validate(accepted[i]) == TIKU_RICH_OK);
    for (i = 0; i < sizeof malformed / sizeof *malformed; i++)
        CHECK(tiku_rich_link_validate(malformed[i]) == TIKU_RICH_INVALID_LINK);
    for (i = 0; i < sizeof unsupported / sizeof *unsupported; i++)
        CHECK(tiku_rich_link_validate(unsupported[i]) == TIKU_RICH_UNSUPPORTED);
    memset(large, 'a', sizeof large);
    large[0] = '#';
    large[2048] = 0;
    CHECK(tiku_rich_link_validate(large) == TIKU_RICH_OK);
    large[2048] = 'a';
    large[2049] = 0;
    CHECK(tiku_rich_link_validate(large) == TIKU_RICH_LIMIT);
    CHECK(apply(doc, &op) == TIKU_RICH_UNSUPPORTED);
    CHECK(tiku_richdoc_memory(doc) == used && !tiku_richdoc_dirty(doc));
    op.link = "http:///";
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_LINK);
    op.link = "#ok";
    op.end = 99;
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_RANGE);
    op.end = op.start;
    CHECK(apply(doc, &op) == TIKU_RICH_NO_CHANGE);
    tiku_richdoc_release(doc);
}

/** @brief Links stay distinct from character appearance and merge by value. */
static void
formatting(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_edit_t op = {.kind = TIKU_RICH_FORMAT,
        .paragraph = doc->root->paragraphs[0]->value.id, .start = 2, .end = 4,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}};
    tiku_richsnapshot_t *old;
    const char *link = target_at(doc, 0, 1);
    char target[] = "#new-section";

    CHECK(!strcmp(link, "https://example.org/notes"));
    CHECK(tiku_richdoc_snapshot(doc, &old) == TIKU_RICH_OK);
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[0]->value.run_count == 5);
    CHECK(target_at(doc, 0, 1) == link && target_at(doc, 0, 2) == link);
    op.chars.present = 0;
    op.clear_chars = TIKU_RICH_BOLD;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[0]->value.run_count == 3);
    CHECK(target_at(doc, 0, 2) == link);
    op.kind = TIKU_RICH_LINK_SET;
    op.link = target;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    memset(target, 'x', sizeof target);
    CHECK(!strcmp(target_at(doc, 0, 2), "#new-section"));
    CHECK(!strcmp(target_at(doc, 0, 1), link));
    op.link = "https://example.org/notes";
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[0]->value.run_count == 3);
    CHECK(apply(doc, &op) == TIKU_RICH_NO_CHANGE);
    op.start = 1;
    op.end = 5;
    op.link = "";
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[0]->value.run_count == 1);
    CHECK(!target_at(doc, 0, 2));
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(!strcmp(target_at(doc, 0, 2), link));
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    tiku_richdoc_clear_history(doc);
    tiku_richdoc_release(doc);
    CHECK(!strcmp(old->root->paragraphs[0]->value.runs[1].link, link));
    tiku_richsnapshot_release(old);
}

/** @brief Insertion does not extend edges; linked selections retain targets. */
static void
typing(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_edit_t op = {.kind = TIKU_RICH_REPLACE,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .text = "X", .length = 1};
    size_t i;

    for (i = 0; i <= 6; i++) {
        op.start = op.end = i;
        CHECK(apply(doc, &op) == TIKU_RICH_OK);
        CHECK(!!target_at(doc, 0, i) == (i > 1 && i < 5));
        CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    }
    op.start = 1;
    op.end = 5;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(!strcmp(target_at(doc, 0, 1), "https://example.org/notes"));
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    op.start = 0;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(!target_at(doc, 0, 0));
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    op = (tiku_rich_edit_t){.kind = TIKU_RICH_FORMAT,
        .paragraph = op.paragraph, .start = 2, .end = 4,
        .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}};
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    op.kind = TIKU_RICH_REPLACE;
    op.start = op.end = 2;
    op.text = "X";
    op.length = 1;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(target_at(doc, 0, 2));
    tiku_richdoc_release(doc);
}

/** @brief Import policy, UTF-8 boundaries and whole-linked replacements. */
static void
unicode_and_gaps(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_run_source_t run = {.length = 3, .link = "file:///tmp/x"};
    tiku_rich_paragraph_source_t p = {.text = "A\xc3\xa9", .length = 3,
        .runs = &run, .run_count = 1};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_rich_edit_t op = {.kind = TIKU_RICH_LINK_SET,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .start = 2, .end = 4};
    size_t used = tiku_richdoc_memory(doc);

    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_UNSUPPORTED);
    CHECK(tiku_richdoc_memory(doc) == used);
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(target_at(doc, 0, 1) && target_at(doc, 0, 4));
    op.kind = TIKU_RICH_REPLACE;
    op.start = 1;
    op.end = 5;
    op.text = "X";
    op.length = 1;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(!target_at(doc, 0, 1));
    run.link = "#unicode";
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    op = (tiku_rich_edit_t){.kind = TIKU_RICH_LINK_SET,
        .paragraph = doc->root->paragraphs[0]->value.id,
        .start = 1, .end = 2, .link = "#new"};
    CHECK(apply(doc, &op) == TIKU_RICH_INVALID_RANGE);
    op.end = 3;
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(apply(doc, &op) == TIKU_RICH_READ_ONLY);
    tiku_richdoc_set_read_only(doc, 0);
    op.kind = TIKU_RICH_REPLACE;
    op.start = 0;
    op.text = "Z";
    op.length = 1;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(!strcmp(target_at(doc, 0, 0), "#unicode"));
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(!strcmp(doc->root->paragraphs[0]->value.text, "A\xc3\xa9"));
    tiku_richdoc_release(doc);
}

/** @brief Splits/joins and failed groups retain links with correct ownership. */
static void
structure(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_edit_t op = {.kind = TIKU_RICH_SPLIT,
        .paragraph = doc->root->paragraphs[0]->value.id, .start = 3, .end = 3};
    tiku_rich_edit_t edits[2];
    tiku_rich_transaction_t tx = {.edits = edits, .count = 2};
    tiku_rd_root_t *root;
    size_t used;

    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(target_at(doc, 0, 1) == target_at(doc, 1, 0));
    op.kind = TIKU_RICH_JOIN;
    op.start = op.end = 0;
    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[0]->value.run_count == 3);
    CHECK(!strcmp(target_at(doc, 0, 4), "https://example.org/notes"));
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    root = doc->root;
    used = tiku_richdoc_memory(doc);
    edits[0] = (tiku_rich_edit_t){.kind = TIKU_RICH_LINK_SET,
        .paragraph = op.paragraph, .start = 1, .end = 5, .link = "#changed"};
    edits[1] = (tiku_rich_edit_t){.kind = TIKU_RICH_FORMAT,
        .paragraph = op.paragraph, .end = 999};
    CHECK(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_INVALID_RANGE);
    CHECK(doc->root == root && tiku_richdoc_memory(doc) == used);
    CHECK(tiku_richdoc_can_redo(doc));
    CHECK(!strcmp(target_at(doc, 0, 1), "https://example.org/notes"));
    tiku_richdoc_release(doc);
}

/** @brief Shared targets are excluded from the extra history reservation. */
static void
history_budget(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_rich_edit_t op = {.kind = TIKU_RICH_FORMAT,
        .paragraph = doc->root->paragraphs[0]->value.id, .start = 2, .end = 4,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}};
    const tiku_rd_paragraph_t *p;
    const tiku_rd_history_t *entry;
    size_t expected, used;
    tiku_rd_root_t *root;

    CHECK(apply(doc, &op) == TIKU_RICH_OK);
    p = doc->root->paragraphs[0];
    entry = doc->undo;
    CHECK(p->value.run_count == 5);
    expected = tiku_rd_size(doc->root) + tiku_rd_size(doc->root->paragraphs) +
        tiku_rd_size(p) + tiku_rd_size(p->value.text) +
        tiku_rd_size(p->value.runs) +
        tiku_rd_size(entry) +
        tiku_rd_size(entry->maps) + tiku_rd_size(entry->bindings);
    CHECK(tiku_richdoc_history_memory(doc) == expected);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    doc->limits.history_bytes = expected - 1;
    root = doc->root;
    used = tiku_richdoc_memory(doc);
    CHECK(apply(doc, &op) == TIKU_RICH_LIMIT);
    CHECK(doc->root == root && tiku_richdoc_memory(doc) == used);
    CHECK(tiku_richdoc_can_redo(doc));
    tiku_richdoc_release(doc);
}

/** @brief Read and release a retained snapshot on a worker. */
static void *
read_links(void *ctx)
{
    tiku_richsnapshot_t *snapshot = ctx;
    tiku_rich_paragraph_t p;
    size_t i;

    for (i = 0; i < 10000; i++) {
        CHECK(tiku_richsnapshot_paragraph(snapshot, 0, &p) == TIKU_RICH_OK);
        CHECK(!strcmp(p.runs[1].link, "https://example.org/notes"));
    }
    tiku_richsnapshot_release(snapshot);
    return NULL;
}

/** @brief Immutable linked snapshots survive concurrent formatting and close. */
static void
workers(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richsnapshot_t *snapshot;
    tiku_rich_edit_t op = {.kind = TIKU_RICH_LINK_SET,
        .paragraph = doc->root->paragraphs[0]->value.id, .start = 1, .end = 5};
    pthread_t threads[4];
    size_t i;

    for (i = 0; i < 4; i++) {
        CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
        CHECK(!pthread_create(&threads[i], NULL, read_links, snapshot));
    }
    for (i = 0; i < 100; i++) {
        op.link = i % 2 ? "#second" : "#first";
        CHECK(apply(doc, &op) == TIKU_RICH_OK);
        tiku_richdoc_clear_history(doc);
    }
    tiku_richdoc_release(doc);
    for (i = 0; i < 4; i++)
        CHECK(!pthread_join(threads[i], NULL));
}

/** @brief Run target validation, ownership, editing and worker checks. */
int
main(void)
{
    validation();
    formatting();
    typing();
    unicode_and_gaps();
    structure();
    history_budget();
    workers();
    puts("PASS: rich document hyperlinks, boundaries, history and workers");
    return 0;
}
