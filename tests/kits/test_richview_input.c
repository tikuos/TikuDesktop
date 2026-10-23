/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richview_input.c - host input, grapheme deletion and commit refusal.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview.h"
#include "tiku_text_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richview input:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Count live model allocations and fail a selected request. */
typedef struct { size_t calls, fail_at, live; } heap_t;

/** @brief Allocate or fail at the chosen model allocation. */
static void *
allocate(void *ctx, size_t bytes)
{
    heap_t *heap = ctx;
    void *p;

    if (++heap->calls == heap->fail_at) return NULL;
    p = malloc(bytes);
    if (p) heap->live++;
    return p;
}

/** @brief Release a model allocation and decrement its live count. */
static void
release(void *ctx, void *p)
{
    heap_t *heap = ctx;

    CHECK(heap->live);
    heap->live--;
    free(p);
}

/** @brief Give each complete grapheme one fixed-advance glyph. */
static tiku_rich_status_t
shape(void *ctx, const tiku_rich_chars_t *chars, const char *text,
    size_t length, const uint8_t *boundaries, tiku_rich_shape_t *out)
{
    size_t end = 1;

    (void)ctx; (void)chars; (void)text;
    while (end < length && !boundaries[end]) end++;
    *out = (tiku_rich_shape_t){.bytes = end, .glyph_count = 1,
        .caret_count = 2, .advance = 6, .ascent = 9, .descent = 3,
        .glyphs = {{1, 1, 0, 0}}, .carets = {{0, 0}, {end, 6}}};
    return TIKU_RICH_OK;
}

/** @brief Finish pending reflow without opening a host window. */
static void
finish(tiku_richview_t *view)
{
    tiku_rich_status_t status;
    size_t steps = 0;

    do {
        status = tiku_richview_step(view, 100);
        CHECK(++steps < 10000);
    } while (status == TIKU_RICH_PENDING);
    CHECK(status == TIKU_RICH_OK);
}

/** @brief Create two paragraphs with one combining grapheme. */
static tiku_richview_t *
fixture(tiku_richdoc_t **doc, heap_t *heap)
{
    tiku_rich_limits_t limits = {.allocator = {heap, allocate, release}};
    tiku_rich_paragraph_source_t p[] = {
        {.text = "a\xcc\x81" "b", .length = 4}, {.text = "tail", .length = 4}};
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 2};
    tiku_richlayout_options_t options = {.width = 36,
        .provider = {.shape = shape}};
    tiku_richview_t *view;

    CHECK(tiku_richdoc_create(heap ? &limits : NULL, doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(*doc, &source, 0) == TIKU_RICH_OK);
    CHECK(tiku_richview_create(*doc, &options, &view) == TIKU_RICH_OK);
    finish(view);
    return view;
}

/** @brief Read the complete document text through its immutable snapshot. */
static void
text_is(tiku_richdoc_t *doc, const char *wanted)
{
    tiku_richsnapshot_t *snapshot;
    char text[2048];
    size_t length;

    CHECK(tiku_richdoc_snapshot(doc, &snapshot) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_text(snapshot, text, sizeof text, &length) ==
          TIKU_RICH_OK);
    CHECK(length == strlen(wanted) && !strcmp(text, wanted));
    tiku_richsnapshot_release(snapshot);
}

/** @brief Return a collapsed caret, verifying both selection endpoints. */
static tiku_rich_pos_t
caret(tiku_richview_t *view)
{
    tiku_rich_pos_t a, c;

    CHECK(tiku_richsession_selection(tiku_richview_session(view), &a, &c) ==
          TIKU_RICH_OK);
    CHECK(a.paragraph == c.paragraph && a.byte == c.byte);
    return c;
}

/** @brief Dispatch a recognized key and verify its operation status. */
static void
key(tiku_richview_t *view, unsigned key, unsigned modifiers,
    tiku_rich_status_t wanted)
{
    tiku_event_t event = {.type = TIKU_EVENT_KEY_DOWN, .key = key,
        .modifiers = modifiers};
    int handled;

    if (key >= 32 && key < 127) event.text[0] = (char)key;
    CHECK(tiku_richview_event(view, &event, &handled) == wanted);
    CHECK(handled);
}

/** @brief Deletion preserves graphemes and undo restores the original caret. */
static void
deletion(void)
{
    tiku_richdoc_t *doc;
    tiku_richview_t *view = fixture(&doc, NULL);
    tiku_rich_pos_t before;

    key(view, TIKU_KEY_DELETE, 0, TIKU_RICH_OK);
    text_is(doc, "b\ntail");
    key(view, 'z', TIKU_MOD_CMD, TIKU_RICH_OK);
    CHECK(caret(view).byte == 0);
    key(view, TIKU_KEY_RIGHT, 0, TIKU_RICH_PENDING);
    finish(view);
    key(view, TIKU_KEY_RIGHT, 0, TIKU_RICH_OK);
    CHECK(caret(view).byte == 3);
    key(view, TIKU_KEY_BACKSPACE, 0, TIKU_RICH_OK);
    text_is(doc, "b\ntail");
    key(view, 'z', TIKU_MOD_CMD, TIKU_RICH_OK);
    CHECK(caret(view).byte == 3);
    finish(view);
    key(view, TIKU_KEY_END, TIKU_MOD_CMD, TIKU_RICH_OK);
    key(view, TIKU_KEY_HOME, 0, TIKU_RICH_OK);
    before = caret(view);
    CHECK(!before.byte);
    key(view, TIKU_KEY_BACKSPACE, 0, TIKU_RICH_OK);
    text_is(doc, "a\xcc\x81" "btail");
    key(view, 'z', TIKU_MOD_CMD, TIKU_RICH_OK);
    CHECK(caret(view).paragraph == before.paragraph && !caret(view).byte);
    text_is(doc, "a\xcc\x81" "b\ntail");
    CHECK(tiku_richsession_set_read_only(tiku_richview_session(view), 1) ==
          TIKU_RICH_OK);
    key(view, TIKU_KEY_BACKSPACE, 0, TIKU_RICH_READ_ONLY);
    key(view, 'x', 0, TIKU_RICH_READ_ONLY);
    CHECK(tiku_richview_destroy(view) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Commit text only after its final event and reject stale transfers. */
static void
commits(void)
{
    tiku_richdoc_t *doc;
    tiku_richview_t *view = fixture(&doc, NULL);
    tiku_text_input_t sender = {0};
    tiku_event_t event;
    int handled;
    char large[901];
    uint64_t generation;
    tiku_rich_pos_t pos;

    key(view, 'a', TIKU_MOD_CMD, TIKU_RICH_OK);
    memset(large, 'q', sizeof large - 1);
    large[sizeof large - 1] = 0;
    generation = tiku_richdoc_generation(doc);
    CHECK(tiku_text_input_set(&sender, large, strlen(large)));
    while (tiku_text_input_next(&sender, &event)) {
        if (event.button & TIKU_TEXT_END) {
            CHECK(tiku_richview_event(view, &event, &handled) == TIKU_RICH_OK);
        } else {
            CHECK(tiku_richview_event(view, &event, &handled) ==
                  TIKU_RICH_PENDING);
            CHECK(tiku_richdoc_generation(doc) == generation);
        }
        CHECK(handled);
    }
    text_is(doc, large);
    key(view, 'z', TIKU_MOD_CMD, TIKU_RICH_OK);
    text_is(doc, "a\xcc\x81" "b\ntail");
    finish(view);
    key(view, TIKU_KEY_HOME, TIKU_MOD_CMD, TIKU_RICH_OK);
    CHECK(tiku_text_input_set(&sender, "abcdefgh", 8));
    CHECK(tiku_text_input_next(&sender, &event));
    CHECK(tiku_richview_event(view, &event, &handled) == TIKU_RICH_PENDING);
    pos = caret(view);
    CHECK(tiku_richsession_select(tiku_richview_session(view), 0, pos, pos) ==
          TIKU_RICH_OK);
    CHECK(tiku_text_input_next(&sender, &event));
    CHECK(tiku_richview_event(view, &event, &handled) == TIKU_RICH_STALE);
    text_is(doc, "a\xcc\x81" "b\ntail");
    CHECK(tiku_text_input_set(&sender, "abcdefgh", 8));
    CHECK(tiku_text_input_next(&sender, &event));
    CHECK(tiku_richview_event(view, &event, &handled) == TIKU_RICH_PENDING);
    event.type = TIKU_EVENT_DEACTIVATED;
    CHECK(tiku_richview_event(view, &event, &handled) == TIKU_RICH_OK);
    CHECK(tiku_text_input_next(&sender, &event));
    CHECK(tiku_richview_event(view, &event, &handled) ==
          TIKU_RICH_INVALID_TEXT);
    CHECK(tiku_richview_destroy(view) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Route formatting, typing, pointer and host-owned actions. */
static void
events(void)
{
    tiku_richdoc_t *doc;
    tiku_richview_t *view = fixture(&doc, NULL);
    tiku_richcmd_state_t state;
    tiku_rich_pos_t a, c;
    tiku_event_t event = {.type = TIKU_EVENT_KEY_DOWN, .key = 's',
        .modifiers = TIKU_MOD_CMD};
    int handled;

    CHECK(tiku_richview_event(view, &event, &handled) == TIKU_RICH_NO_CHANGE);
    CHECK(!handled);
    key(view, 'b', TIKU_MOD_CMD, TIKU_RICH_OK);
    CHECK(tiku_richsession_query(tiku_richview_session(view),
        TIKU_RICHCMD_BOLD, &state) == TIKU_RICH_OK);
    CHECK(state.check == TIKU_RICHCMD_CHECK_ON);
    key(view, 'x', 0, TIKU_RICH_OK);
    key(view, 'y', 0, TIKU_RICH_OK);
    text_is(doc, "xya\xcc\x81" "b\ntail");
    key(view, 'z', TIKU_MOD_CMD, TIKU_RICH_OK);
    text_is(doc, "a\xcc\x81" "b\ntail");
    finish(view);
    event = (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN,
        .button = 1, .x = 0, .y = 1};
    CHECK(tiku_richview_event(view, &event, &handled) == TIKU_RICH_OK);
    event.type = TIKU_EVENT_POINTER_MOVE;
    event.x = 12;
    CHECK(tiku_richview_event(view, &event, &handled) == TIKU_RICH_OK);
    CHECK(tiku_richsession_selection(tiku_richview_session(view), &a, &c) ==
          TIKU_RICH_OK && a.byte == 0 && c.byte == 4);
    event.type = TIKU_EVENT_POINTER_UP;
    CHECK(tiku_richview_event(view, &event, &handled) == TIKU_RICH_OK);
    key(view, TIKU_KEY_DELETE, 0, TIKU_RICH_OK);
    text_is(doc, "\ntail");
    finish(view);
    CHECK(tiku_richview_viewport(view, 36, 12, 1) == TIKU_RICH_OK);
    event = (tiku_event_t){.type = TIKU_EVENT_WHEEL, .dy = 1};
    CHECK(tiku_richview_event(view, &event, &handled) == TIKU_RICH_OK);
    CHECK(tiku_richview_scroll_y(view) > 0);
    CHECK(tiku_richview_destroy(view) == TIKU_RICH_OK);
    tiku_richdoc_release(doc);
}

/** @brief Allocation refusal leaves the original caret and bytes unchanged. */
static void
delete_faults(void)
{
    heap_t heap = {0};
    tiku_richdoc_t *doc;
    tiku_richview_t *view;
    size_t total = 0, i = 0, base;
    uint64_t generation;

    do {
        view = fixture(&doc, &heap);
        key(view, TIKU_KEY_RIGHT, 0, TIKU_RICH_OK);
        base = heap.live;
        generation = tiku_richdoc_generation(doc);
        heap.calls = 0;
        heap.fail_at = i;
        if (!i) {
            CHECK(tiku_richview_delete(view, 1) == TIKU_RICH_OK);
            total = heap.calls;
            CHECK(total);
        } else {
            CHECK(tiku_richview_delete(view, 1) == TIKU_RICH_NO_MEMORY);
            CHECK(heap.live == base &&
                  tiku_richdoc_generation(doc) == generation);
            CHECK(caret(view).byte == 3);
            heap.fail_at = 0;
            text_is(doc, "a\xcc\x81" "b\ntail");
        }
        heap.fail_at = 0;
        CHECK(tiku_richview_destroy(view) == TIKU_RICH_OK);
        tiku_richdoc_release(doc);
        CHECK(!heap.live);
    } while (++i <= total);
    printf("Rich view input: %zu deletion allocation refusals PASS\n", total);
}

/** @brief Export directional selections and preserve state on word failure. */
static void
selected_text(void)
{
    heap_t heap = {0};
    tiku_richdoc_t *doc;
    tiku_richview_t *view = fixture(&doc, &heap);
    tiku_richsession_t *session = tiku_richview_session(view);
    tiku_rich_pos_t a, b, first, last;
    char buffer[32] = "unchanged";
    size_t length;

    CHECK(tiku_richview_select_all(view) == TIKU_RICH_OK);
    CHECK(tiku_richsession_selection(session, &a, &b) == TIKU_RICH_OK);
    CHECK(tiku_richsession_select(session, 0, b, a) == TIKU_RICH_OK);
    CHECK(tiku_richsession_range(session, &first, &last) == TIKU_RICH_OK);
    CHECK(first.paragraph == a.paragraph && last.paragraph == b.paragraph);
    CHECK(tiku_richsession_text(session, NULL, 0, &length) == TIKU_RICH_OK);
    CHECK(length == 9);
    CHECK(tiku_richsession_text(session, buffer, length, &length) ==
          TIKU_RICH_LIMIT && !strcmp(buffer, "unchanged"));
    CHECK(tiku_richsession_text(session, buffer, sizeof buffer, &length) ==
          TIKU_RICH_OK && !strcmp(buffer, "a\xcc\x81" "b\ntail"));
    heap.calls = 0;
    heap.fail_at = 1;
    CHECK(tiku_richview_select_at(view, 2, 2, 0) == TIKU_RICH_NO_MEMORY);
    heap.fail_at = 0;
    CHECK(tiku_richsession_selection(session, &first, &last) == TIKU_RICH_OK);
    CHECK(first.paragraph == b.paragraph && last.paragraph == a.paragraph);
    CHECK(tiku_richview_select_at(view, 2, 2, 0) == TIKU_RICH_OK);
    CHECK(tiku_richsession_text(session, buffer, sizeof buffer, &length) ==
          TIKU_RICH_OK && !strcmp(buffer, "a\xcc\x81" "b"));
    CHECK(tiku_richview_select_at(view, 7, 13, 1) == TIKU_RICH_OK);
    CHECK(tiku_richsession_text(session, buffer, sizeof buffer, &length) ==
          TIKU_RICH_OK && !strcmp(buffer, "tail"));
    CHECK(tiku_richview_viewport(view, 6, 40, 1) == TIKU_RICH_OK);
    CHECK(tiku_richview_select_at(view, 1, 1, 1) == TIKU_RICH_PENDING);
    finish(view);
    CHECK(tiku_richview_select_at(view, 1, 1, 1) == TIKU_RICH_OK);
    CHECK(tiku_richsession_text(session, buffer, sizeof buffer, &length) ==
          TIKU_RICH_OK && !strcmp(buffer, "a\xcc\x81"));
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
    CHECK(!heap.live);
}

/** @brief Exercise shared host input without a window system or board. */
int
main(void)
{
    deletion();
    commits();
    events();
    delete_faults();
    selected_text();
    puts("Rich view input: shortcuts, graphemes, paragraph joins, exact undo, "
         "pointer selection and complete/stale commits PASS");
    return 0;
}
