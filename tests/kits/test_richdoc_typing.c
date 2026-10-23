/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_typing.c - grouped typing, history boundaries and view anchors.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL rich typing:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Read the live text without using its history representation. */
static void
expect(tiku_richdoc_t *doc, const char *text)
{
    tiku_richsnapshot_t snapshot = {.root = doc->root};
    char buffer[4096];
    size_t length;

    CHECK(tiku_richsnapshot_text(&snapshot, buffer, sizeof buffer, &length) ==
          TIKU_RICH_OK);
    CHECK(length == strlen(text) && !strcmp(text, buffer));
}

/** @brief Adjacent inserts share a group, preserving pre-group anchors. */
static void
adjacent(void)
{
    tiku_richdoc_t *doc;
    tiku_richsession_t *a, *b;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_pos_t first, last;
    size_t i;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &a) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &b) == TIKU_RICH_OK);
    for (i = 0; i < 1000; i++) {
        CHECK(tiku_richsession_type(a, 0, "x", 1) == TIKU_RICH_OK);
        CHECK(doc->undo && !doc->undo->next && !doc->redo);
        CHECK(a->caret->position.byte == i + 1);
        CHECK(b->caret->position.byte == i + 1);
        CHECK(doc->history_bytes == doc->undo->planned_bytes);
    }
    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richsession_selection(a, &first, &last) == TIKU_RICH_OK);
    first.byte = last.byte = 17;
    CHECK(tiku_richsession_select(a, 0, first, last) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "");
    CHECK(a->caret->position.byte == 0 && b->caret->position.byte == 0);
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    CHECK(a->caret->position.byte == 1000 && b->caret->position.byte == 1000);
    CHECK(tiku_richsnapshot_paragraphs(snapshot) == 1);
    tiku_richsnapshot_release(snapshot);
    CHECK(tiku_richsession_destroy(a) == TIKU_RICH_OK);
    CHECK(tiku_richsession_destroy(b) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Formatting, save, navigation, other inputs and focus split groups. */
static void
boundaries(void)
{
    tiku_richdoc_t *doc;
    tiku_richsession_t *session, *other;
    tiku_richsnapshot_t *saved;
    tiku_richcmd_t bold = {.kind = TIKU_RICHCMD_BOLD};
    tiku_richcmd_t paste = {.kind = TIKU_RICHCMD_REPLACE_TEXT,
        .value.text = {"P", 1}};
    tiku_rich_pos_t at;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &other) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "a", 1) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_snapshot(doc, &saved) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_mark_saved(doc, saved) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "b", 1) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "a");
    CHECK(!tiku_richdoc_dirty(doc));
    CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_execute(session, 0, &bold) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "c", 1) == TIKU_RICH_OK);
    CHECK(tiku_richsession_break_undo(session) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "d", 1) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "abc");
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "ab");
    CHECK(tiku_richsession_type(session, 0, "e", 1) == TIKU_RICH_OK);
    at = other->caret->position;
    CHECK(tiku_richsession_select(other, 0, at, at) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "f", 1) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "abe");
    CHECK(tiku_richsession_execute(session, 0, &paste) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "g", 1) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "abeP");
    CHECK(tiku_richsession_type(session, 0, "\nh", 2) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "i", 1) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "abeP\nh");
    CHECK(tiku_richsession_type(other, 0, "j", 1) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "k", 1) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "abeP\nhj");
    tiku_richsnapshot_release(saved);
    CHECK(tiku_richsession_destroy(other) == TIKU_RICH_OK);
    CHECK(tiku_richsession_destroy(session) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Capture formatting and directional selections before replacement. */
static void
replacement(void)
{
    tiku_richdoc_t *doc;
    tiku_richsession_t *session;
    tiku_rich_pos_t first, last;

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richsession_create(doc, &session) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "abcd", 4) == TIKU_RICH_OK);
    first = last = session->caret->position;
    first.byte = 3;
    last.byte = 1;
    CHECK(tiku_richsession_select(session, 0, first, last) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "X", 1) == TIKU_RICH_OK);
    CHECK(tiku_richsession_type(session, 0, "Y", 1) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "aXd");
    CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    expect(doc, "abcd");
    CHECK(session->anchor->position.byte == 3);
    CHECK(session->caret->position.byte == 1);
    CHECK(tiku_richsession_destroy(session) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Exercise grouped insertion and explicit undo boundaries. */
int
main(void)
{
    adjacent();
    boundaries();
    replacement();
    puts("PASS: grouped typing, 1000 inserts, view mapping and save boundaries");
    return 0;
}
