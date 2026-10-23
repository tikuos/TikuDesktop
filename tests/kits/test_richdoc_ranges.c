/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_ranges.c - cross-paragraph paste, formatting and position oracle.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL ranges:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Resolve a flat byte offset including one byte per paragraph break. */
static tiku_rich_pos_t
position(tiku_richdoc_t *doc, size_t byte, unsigned affinity)
{
    size_t i;
    const tiku_rich_paragraph_t *p;

    for (i = 0; i < doc->root->count; i++) {
        p = &doc->root->paragraphs[i]->value;
        if (byte <= p->length)
            return (tiku_rich_pos_t){doc->root->body, p->id, byte,
                affinity ? TIKU_RICH_AFTER : TIKU_RICH_BEFORE};
        byte -= p->length + 1;
    }
    CHECK(0);
    return (tiku_rich_pos_t){0};
}

/** @brief Check snapshot text against an independently maintained string. */
static void
expect_text(tiku_richdoc_t *doc, const char *expected)
{
    tiku_richsnapshot_t *s;
    char text[8192];
    size_t length;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_text(s, text, sizeof text, &length) ==
          TIKU_RICH_OK);
    CHECK(!strcmp(text, expected) && length == strlen(expected));
    tiku_richsnapshot_release(s);
}

/** @brief Apply plain-text paste with checked endpoints. */
static void
paste(tiku_richdoc_t *doc, size_t start, size_t end, const char *text)
{
    CHECK(tiku_richdoc_replace_range(doc, doc->generation,
        position(doc, start, 0), position(doc, end, 1), text, strlen(text)) ==
        TIKU_RICH_OK);
}

/** @brief Line endings become structure, while surviving text retains style. */
static void
paste_and_format(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_chars_t bold = {.present = TIKU_RICH_BOLD, .bold = 1};
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    tiku_rich_pos_t first, last;
    uint64_t generation, leading;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    leading = doc->root->paragraphs[0]->value.id;
    paste(doc, 0, 0, "one\r\ntwo\rthree\n");
    expect_text(doc, "one\ntwo\nthree\n");
    CHECK(doc->root->count == 4);
    first = position(doc, 1, 0);
    last = position(doc, 10, 1);
    CHECK(tiku_richdoc_format_range(doc, doc->generation, first, last,
                                     &bold) == TIKU_RICH_OK);
    CHECK(tiku_rd_resolve(doc->root, 1, 0, &chars, &props) == TIKU_RICH_OK);
    CHECK(chars.bold);
    generation = doc->generation;
    CHECK(tiku_richdoc_format_range(doc, generation, first, first, &bold) ==
          TIKU_RICH_NO_CHANGE && doc->generation == generation);
    CHECK(tiku_richdoc_replace_range(doc, generation - 1, first, last,
                                     "x", 1) == TIKU_RICH_STALE);
    CHECK(tiku_richdoc_replace_range(doc, generation, last, first,
                                     "x", 1) == TIKU_RICH_INVALID_RANGE);
    paste(doc, 2, 9, "A\nB");
    expect_text(doc, "onA\nBhree\n");
    CHECK(doc->root->paragraphs[0]->value.id == leading);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect_text(doc, "one\ntwo\nthree\n");
    CHECK(tiku_rd_resolve(doc->root, 1, 0, &chars, &props) == TIKU_RICH_OK);
    CHECK(chars.bold);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(tiku_rd_resolve(doc->root, 1, 0, &chars, &props) == TIKU_RICH_OK);
    CHECK(!chars.bold);
    paste(doc, 0, 14, "");
    CHECK(doc->root->count == 1 && doc->root->paragraphs[0]->value.id == leading);
    expect_text(doc, "");
    tiku_richdoc_release(doc);
}

/** @brief End-of-selection anchors remain after the entire multiline paste. */
static void
selection_edges(void)
{
    tiku_richdoc_t *doc;
    tiku_richanchor_t *before, *after, *end, *late;
    tiku_rich_pos_t p;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    paste(doc, 0, 0, "abcdef");
    CHECK(tiku_richanchor_create(doc, position(doc, 1, 0), &before) ==
          TIKU_RICH_OK);
    CHECK(tiku_richanchor_create(doc, position(doc, 1, 1), &after) ==
          TIKU_RICH_OK);
    CHECK(tiku_richanchor_create(doc, position(doc, 5, 0), &end) ==
          TIKU_RICH_OK);
    paste(doc, 1, 5, "X\nY\nZ");
    expect_text(doc, "aX\nY\nZf");
    CHECK(before->position.paragraph == doc->root->paragraphs[0]->value.id);
    CHECK(before->position.byte == 1);
    CHECK(after->position.paragraph == doc->root->paragraphs[2]->value.id);
    CHECK(after->position.byte == 1);
    CHECK(end->position.paragraph == doc->root->paragraphs[2]->value.id);
    CHECK(end->position.byte == 1 && end->position.affinity == TIKU_RICH_BEFORE);
    CHECK(tiku_richanchor_create(doc, position(doc, 7, 1), &late) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    CHECK(end->position.byte == 5 && before->position.byte == 1);
    CHECK(after->position.byte == 1 && late->position.byte == 6);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    p = position(doc, 7, 1);
    CHECK(late->position.paragraph == p.paragraph && late->position.byte == p.byte);
    tiku_richanchor_release(before);
    tiku_richanchor_release(after);
    tiku_richanchor_release(end);
    tiku_richanchor_release(late);
    tiku_richdoc_release(doc);
}

/** @brief Deterministic random generator independent of document storage. */
static unsigned
random_step(unsigned *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

/** @brief Map an independent flat offset through one string replacement. */
static size_t
expected_offset(size_t point, size_t start, size_t end, size_t added,
                  unsigned affinity)
{
    if (point < start)
        return point;
    if (point > end || (start != end && point == end))
        return point - end + start + added;
    return start + (affinity ? added : 0);
}

/** @brief Check an anchor against a flat oracle and retain its affinity. */
static void
expect_anchor(tiku_richanchor_t *anchor, size_t flat, unsigned affinity)
{
    tiku_rich_pos_t wanted = position(anchor->doc, flat, affinity), got;

    CHECK(tiku_richanchor_get(anchor, &got) == TIKU_RICH_OK);
    CHECK(got.paragraph == wanted.paragraph && got.byte == wanted.byte);
    CHECK(got.affinity == wanted.affinity && got.body == wanted.body);
}

/** @brief Independent mutable text and selection state for generated edits. */
typedef struct {
    char text[8192], before[8192];
    size_t start, end, old_length, added;
    size_t points[8], mapped[8];
    tiku_richanchor_t *anchors[8];
    unsigned seed;
} oracle_t;

/** @brief Apply one generated selection replacement and verify its history. */
static void
generated_step(tiku_richdoc_t *doc, oracle_t *o)
{
    const char *input[] = {"", "x", "ab", "\n", "\r\nx\ry\n"};
    const char *normalized[] = {"", "x", "ab", "\n", "\nx\ny\n"};
    size_t i, pick = random_step(&o->seed) % 5;
    tiku_rich_status_t status;

    o->old_length = strlen(o->text);
    memcpy(o->before, o->text, o->old_length + 1);
    o->start = random_step(&o->seed) % (o->old_length + 1);
    o->end = o->start + random_step(&o->seed) % (o->old_length - o->start + 1);
    o->added = strlen(normalized[pick]);
    for (i = 0; i < 8; i++) {
        o->points[i] = random_step(&o->seed) % (o->old_length + 1);
        CHECK(tiku_richanchor_set(o->anchors[i],
            position(doc, o->points[i], i & 1)) == TIKU_RICH_OK);
        o->mapped[i] = expected_offset(o->points[i], o->start, o->end,
                                       o->added, i & 1);
    }
    CHECK(o->old_length + o->added < sizeof o->text);
    memmove(o->text + o->start + o->added, o->text + o->end,
            o->old_length - o->end + 1);
    memcpy(o->text + o->start, normalized[pick], o->added);
    status = tiku_richdoc_replace_range(doc, doc->generation,
        position(doc, o->start, 0), position(doc, o->end, 1),
        input[pick], strlen(input[pick]));
    CHECK(status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE);
    expect_text(doc, o->text);
    if (status == TIKU_RICH_NO_CHANGE) {
        CHECK(!strcmp(o->before, o->text));
        return;
    }
    for (i = 0; i < 8; i++)
        expect_anchor(o->anchors[i], o->mapped[i], i & 1);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect_text(doc, o->before);
    for (i = 0; i < 8; i++)
        expect_anchor(o->anchors[i], o->points[i], i & 1);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    expect_text(doc, o->text);
    for (i = 0; i < 8; i++)
        expect_anchor(o->anchors[i], o->mapped[i], i & 1);
}

/** @brief Compare generated multiline edits and eight independent selections. */
static void
generated_ranges(void)
{
    tiku_richdoc_t *doc;
    oracle_t oracle = {.seed = 451};
    size_t i;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    for (i = 0; i < 8; i++)
        CHECK(tiku_richanchor_create(doc, position(doc, 0, i & 1),
                                     &oracle.anchors[i]) == TIKU_RICH_OK);
    for (i = 0; i < 2000; i++)
        generated_step(doc, &oracle);
    for (i = 0; i < 8; i++)
        tiku_richanchor_release(oracle.anchors[i]);
    tiku_richdoc_release(doc);
}

/** @brief Late structural and operation-count refusals preserve live state. */
static void
range_limits(void)
{
    tiku_richdoc_t *doc;
    tiku_richanchor_t *anchor;
    tiku_rich_pos_t p;
    tiku_rd_root_t *root;
    size_t bytes;
    uint64_t next, generation;
    char text[512];

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    paste(doc, 0, 0, "\xc3\xa9");
    p = position(doc, 0, 0);
    CHECK(tiku_richanchor_create(doc, p, &anchor) == TIKU_RICH_OK);
    root = doc->root;
    next = doc->next_id;
    generation = doc->generation;
    bytes = tiku_richdoc_memory(doc);
    memset(text, '\n', sizeof text);
    CHECK(tiku_richdoc_replace_range(doc, generation, p, p, text, sizeof text)
          == TIKU_RICH_LIMIT);
    CHECK(tiku_richdoc_replace_range(doc, generation, p, p, "\n\xff", 2)
          == TIKU_RICH_INVALID_TEXT);
    CHECK(tiku_richdoc_replace_range(doc, generation, p, p, NULL, 1)
          == TIKU_RICH_INVALID);
    CHECK(tiku_richdoc_replace_range(doc, generation, p, p, "x", SIZE_MAX)
          == TIKU_RICH_LIMIT);
    p.byte = 1;
    CHECK(tiku_richdoc_replace_range(doc, generation, p, p, "x", 1)
          == TIKU_RICH_INVALID_RANGE);
    p.byte = 0;
    doc->limits.paragraphs = 2;
    CHECK(tiku_richdoc_replace_range(doc, generation, p, p, "a\nb\nc", 5)
          == TIKU_RICH_LIMIT);
    doc->limits.history_bytes = 1;
    CHECK(tiku_richdoc_replace_range(doc, generation, p, p, "a\nb", 3)
          == TIKU_RICH_LIMIT);
    CHECK(doc->root == root && doc->next_id == next);
    CHECK(doc->generation == generation && tiku_richdoc_memory(doc) == bytes);
    CHECK(anchor->position.paragraph == p.paragraph && !anchor->position.byte);
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(tiku_richdoc_replace_range(doc, generation, p, p, "x", 1)
          == TIKU_RICH_READ_ONLY);
    CHECK(tiku_richanchor_set(anchor, position(doc, 2, 1)) == TIKU_RICH_OK);
    CHECK(!tiku_richdoc_can_undo(doc));
    tiku_richanchor_release(anchor);
    tiku_richdoc_release(doc);
}

/** @brief Run cross-paragraph replacement, formatting and position tests. */
int
main(void)
{
    paste_and_format();
    selection_edges();
    generated_ranges();
    range_limits();
    puts("PASS: rich document ranges, 2000 multiline edits and anchor history");
    return 0;
}
