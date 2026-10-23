/*
 * TikuDesktop Document Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richsession_observer.c - directional endpoint events and callback guards.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief Allocation counter and switch for no-allocation navigation checks. */
typedef struct { size_t calls, live; int refuse; } memory_t;
/** @brief Count storage without accepting allocations in the refusal phase. */
static void *
allocate(void *context, size_t bytes)
{
    memory_t *memory = context;
    void *p;

    memory->calls++;
    if (memory->refuse) return NULL;
    p = malloc(bytes);
    assert(p);
    memory->live++;
    return p;
}
/** @brief Release storage counted by the test allocator. */
static void
release(void *context, void *p)
{
    memory_t *memory = context;

    assert(memory->live);
    memory->live--;
    free(p);
}

/** @brief Captured event and delivery count for one independent session. */
typedef struct {
    tiku_richdoc_t *doc;
    size_t calls;
    tiku_rich_selection_change_t last;
} observer_t;

/** @brief Check coherent query state and refusal of reentrant mutations. */
static void
changed(void *context, tiku_richsession_t *session,
        const tiku_rich_selection_change_t *change)
{
    observer_t *observer = context;
    tiku_rich_pos_t a, c;
    tiku_richcmd_state_t state;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_DELETE};

    observer->calls++;
    observer->last = *change;
    assert(tiku_richsession_selection(session, &a, &c) == TIKU_RICH_OK);
    assert(a.paragraph == change->anchor.paragraph && a.byte == change->anchor.byte);
    assert(c.paragraph == change->caret.paragraph && c.byte == change->caret.byte);
    assert(change->generation == tiku_richdoc_generation(observer->doc));
    assert(tiku_richsession_query(session, TIKU_RICHCMD_BOLD, &state) == TIKU_RICH_OK);
    assert(tiku_richsession_select(session, 0, a, c) == TIKU_RICH_BUSY);
    assert(tiku_richsession_observe_selection(session, NULL, NULL) == TIKU_RICH_BUSY);
    assert(tiku_richsession_destroy(session) == TIKU_RICH_BUSY);
    assert(tiku_richsession_execute(session, 0, &command) == TIKU_RICH_BUSY);
    assert(tiku_richdoc_undo(observer->doc) == TIKU_RICH_BUSY);
}

/** @brief Apply an external edit using the normal document transaction path. */
static void
edit(tiku_richdoc_t *doc, uint64_t paragraph, const char *text, size_t length)
{
    tiku_rich_edit_t value = {.kind = TIKU_RICH_REPLACE,
        .paragraph = paragraph, .text = text, .length = length};
    tiku_rich_transaction_t tx = {0, &value, 1};

    assert(tiku_richdoc_apply(doc, &tx) == TIKU_RICH_OK);
}

/** @brief Exercise navigation, typing, foreign edits, history and import. */
int
main(void)
{
    memory_t memory = {0};
    tiku_rich_limits_t limits = {.allocator = {&memory, allocate, release}};
    tiku_richdoc_t *doc;
    tiku_richsession_t *first, *second;
    tiku_rich_pos_t a, c;
    observer_t one = {0}, two = {0};
    uint64_t generation;
    size_t calls;

    assert(tiku_richdoc_create(&limits, &doc) == TIKU_RICH_OK);
    edit(doc, doc->root->paragraphs[0]->value.id, "abcdef", 6);
    assert(tiku_richsession_create(doc, &first) == TIKU_RICH_OK);
    assert(tiku_richsession_create(doc, &second) == TIKU_RICH_OK);
    one.doc = two.doc = doc;
    calls = memory.calls;
    memory.refuse = 1;
    assert(tiku_richsession_observe_selection(NULL, changed, &one) == TIKU_RICH_INVALID);
    assert(tiku_richsession_observe_selection(first, changed, &one) == TIKU_RICH_OK);
    assert(tiku_richsession_observe_selection(second, changed, &two) == TIKU_RICH_OK);
    assert(!one.calls && !two.calls);
    assert(tiku_richsession_selection(first, &a, &c) == TIKU_RICH_OK);
    generation = doc->generation;
    a.byte = 1; c.byte = 4;
    assert(tiku_richsession_select(first, 0, a, c) == TIKU_RICH_OK);
    assert(one.calls == 1 && !two.calls && doc->generation == generation);
    assert(!one.last.previous_anchor.byte && !one.last.previous_caret.byte);
    assert(one.last.anchor.byte == 1 && one.last.caret.byte == 4);
    assert(tiku_richsession_select(first, 0, a, c) == TIKU_RICH_OK && one.calls == 1);
    assert(tiku_richsession_select(first, 0, c, a) == TIKU_RICH_OK && one.calls == 2);
    assert(one.last.anchor.byte == 4 && one.last.caret.byte == 1);
    assert(tiku_richsession_select(first, generation - 1, a, c) == TIKU_RICH_STALE);
    c.byte = 7;
    assert(tiku_richsession_select(first, 0, a, c) == TIKU_RICH_INVALID_RANGE);
    assert(one.calls == 2 && memory.calls == calls);
    memory.refuse = 0;
    assert(tiku_richsession_type(first, 0, "Q", 1) == TIKU_RICH_OK);
    assert(one.calls == 3 && !two.calls);
    assert(one.last.anchor.byte == 2 && one.last.caret.byte == 2);
    assert(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    assert(one.calls == 4 && one.last.anchor.byte == 4 && one.last.caret.byte == 1);
    assert(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
    assert(one.calls == 5 && one.last.caret.byte == 2);
    c.byte = a.byte = 4;
    assert(tiku_richsession_select(second, 0, a, c) == TIKU_RICH_OK);
    assert(two.calls == 1);
    edit(doc, a.paragraph, "X", 1);
    assert(one.calls == 6 && one.last.caret.byte == 3);
    assert(two.calls == 2 && two.last.caret.byte == 5);
    tiku_rich_paragraph_source_t p = {.text = "new", .length = 3};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    assert(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    assert(one.calls == 7 && two.calls == 3 && !one.last.caret.byte);
    assert(one.last.caret.paragraph != a.paragraph);
    assert(tiku_richsession_selection(first, &a, &c) == TIKU_RICH_OK);
    c.affinity = c.affinity == TIKU_RICH_AFTER ? TIKU_RICH_BEFORE : TIKU_RICH_AFTER;
    assert(tiku_richsession_select(first, 0, a, c) == TIKU_RICH_OK && one.calls == 8);
    assert(tiku_richsession_observe_selection(first, NULL, NULL) == TIKU_RICH_OK);
    a.byte = c.byte = 1;
    assert(tiku_richsession_select(first, 0, a, c) == TIKU_RICH_OK && one.calls == 8);
    assert(tiku_richsession_destroy(first) == TIKU_RICH_OK);
    assert(tiku_richsession_destroy(second) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
    assert(!memory.live);
    puts("Selection observers: navigation, multi-view, typing, history, import and guards PASS");
    return 0;
}
