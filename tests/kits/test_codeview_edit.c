/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_edit.c - changing a document through a code view:
 * typing, Return and Tab, Backspace and Delete, text-input commits, undo
 * and redo, a read-only document, line breaks kept whole, and the
 * clipboard a host lends, which Cut and Paste use whole or not at all.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview.h"
#include "tiku_editdoc_private.h"
#include "tiku_text_input.h"

static int fails;

/** @brief Count a failure and say which contract row it belongs to. */
static void
check(int cond, const char *row, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-9s %s\n", row, what);
        fails++;
    }
}

/* The host's clipboard, and what it was asked. */
static char clip[256];
static size_t clip_len;
static int clip_full;           /* nonzero: refuse what is offered     */
static int clip_sets;           /* how many times it was offered text   */
static int clip_empty;          /* nonzero: hold nothing                */
static int invalidated;         /* invalidations heard                  */

static int
clip_set(void *ctx, const char *text, size_t length)
{
    (void)ctx;
    clip_sets++;
    if (clip_full || length >= sizeof clip || text[length] != '\0')
        return 1;
    memcpy(clip, text, length + 1);
    clip_len = length;
    return 0;
}

static const char *
clip_text(void *ctx, size_t *length)
{
    (void)ctx;
    if (clip_empty)
        return NULL;
    *length = clip_len;
    return clip;
}

static void
invalidate(void *ctx, tiku_codeview_t *view)
{
    (void)ctx;
    (void)view;
    invalidated++;
}

static const tiku_codeview_host_t host = { NULL, clip_set, clip_text,
                                           invalidate, NULL };

/** @brief A view of a new document holding @p s, with the test's host. */
static tiku_codeview_t *
view_of(const char *s)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;
    tiku_rect_t box = { 0, 0, 400, 200 };

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
        exit(1);
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
    tiku_codeview_set_host(v, &host);
    return v;
}

/** @brief Whether the document holds exactly @p s. */
static int
holds(tiku_codeview_t *v, const char *s)
{
    char buf[512];
    size_t n = 0;
    tiku_edit_range_t all = { 0, 0 };

    all.end = tiku_editdoc_length(tiku_codeview_doc(v));
    return tiku_editdoc_read(tiku_codeview_doc(v), all, buf, sizeof buf,
                             &n) == TIKU_EDIT_OK &&
           n == strlen(s) && memcmp(buf, s, n) == 0;
}

/** @brief Whether the selection is @p anchor to @p caret. */
static int
is(tiku_codeview_t *v, tiku_edit_pos_t anchor, tiku_edit_pos_t caret)
{
    tiku_edit_pos_t a = -1, c = -1;

    return tiku_codeview_selection(v, &a, &c) == TIKU_EDIT_OK &&
           a == anchor && c == caret;
}

/** @brief Press key @p k with @p mods, bringing @p text. */
static int
press(tiku_codeview_t *v, unsigned k, unsigned mods, const char *text)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_KEY_DOWN;
    e.key = k;
    e.modifiers = mods;
    if (text != NULL)
        memcpy(e.text, text, strnlen(text, sizeof e.text - 1));
    return tiku_codeview_event(v, &e);
}

/** @brief Type each character of the ASCII @p s. */
static void
type(tiku_codeview_t *v, const char *s)
{
    char one[2] = { 0, 0 };

    for (; *s; s++) {
        one[0] = *s;
        (void)press(v, (unsigned char)*s, 0, one);
    }
}

/** @brief Deliver @p s as one text-input commit; the view's answer to
 *         its last event. */
static int
commit(tiku_codeview_t *v, const char *s)
{
    tiku_text_input_t in;
    tiku_event_t e;
    int used = 0;

    if (!tiku_text_input_set(&in, s, strlen(s)))
        return -1;
    while (tiku_text_input_next(&in, &e))
        used = tiku_codeview_event(v, &e);
    return used;
}

/** @brief Typing: key text at the caret or over the selection, joined
 *         into one undo step; text from a key with none of its own;
 *         nothing typed with Command or for a control character. */
static void
typing(void)
{
    tiku_codeview_t *v = view_of("ab");
    tiku_editdoc_t *doc = tiku_codeview_doc(v);

    tiku_codeview_select(v, 1, 1);
    type(v, "xy");
    check(holds(v, "axyb") && is(v, 3, 3), "EDT-001",
          "typed characters go in at the caret, which follows them");
    check(press(v, 'z', TIKU_MOD_CMD, "z") == 1 && holds(v, "ab") &&
          is(v, 1, 1), "EDT-001",
          "a run of typing undoes as one step, the caret back where it was");
    check(press(v, 0, 0, "\xc3\xa9") == 1 && press(v, 'k', 0, NULL) == 1 &&
          holds(v, "a\xc3\xa9kb"), "EDT-001",
          "a key's UTF-8 text is typed, and a printable key code with no "
          "text types itself");
    check(press(v, 'e', TIKU_MOD_OPTION, "\xc3\xa9") == 1 &&
          holds(v, "a\xc3\xa9k\xc3\xa9" "b"), "EDT-001",
          "Option does not stop a key typing what it brings");
    check(press(v, 'b', TIKU_MOD_CMD, "b") == 0 &&
          press(v, 0, 0, "\x1b") == 0 && press(v, 0, 0, "\x7f") == 0 &&
          holds(v, "a\xc3\xa9k\xc3\xa9" "b"), "EDT-001",
          "Command, a control character or DEL types nothing");
    tiku_codeview_select(v, 0, 3);
    type(v, "Q");
    check(holds(v, "Qk\xc3\xa9" "b") && is(v, 1, 1) &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          holds(v, "a\xc3\xa9k\xc3\xa9" "b") && is(v, 0, 3), "EDT-001",
          "typing over a selection replaces it as one step, and undo "
          "brings the selection back");
    tiku_codeview_destroy(v);
}

/** @brief Return types the view's line break, Tab a tab, and Shift
 *         with Tab takes it off again. */
static void
returns_and_tabs(void)
{
    tiku_codeview_t *v = view_of("ab");

    tiku_codeview_select(v, 1, 1);
    press(v, TIKU_KEY_RETURN, 0, NULL);
    check(holds(v, "a\nb") && is(v, 2, 2), "EDT-002",
          "Return breaks the line with a newline");
    check(tiku_codeview_set_newline(v, "\r\n") == TIKU_EDIT_OK, "EDT-002",
          "CRLF may be set");
    press(v, TIKU_KEY_RETURN, 0, NULL);
    check(holds(v, "a\n\r\nb") && is(v, 4, 4), "EDT-002",
          "and then Return types it");
    check(tiku_codeview_set_newline(v, "x") == TIKU_EDIT_INVALID_TEXT &&
          tiku_codeview_set_newline(v, "\n\n") == TIKU_EDIT_INVALID_TEXT &&
          tiku_codeview_set_newline(v, "\r") == TIKU_EDIT_OK, "EDT-002",
          "only LF, CRLF or CR may be set");
    press(v, TIKU_KEY_TAB, 0, NULL);
    check(holds(v, "a\n\r\n\tb") && press(v, TIKU_KEY_TAB, TIKU_MOD_SHIFT,
                                          NULL) == 1 &&
          holds(v, "a\n\r\nb"), "EDT-002",
          "Tab types a tab; Shift with Tab takes it off again");
    tiku_codeview_destroy(v);
}

/** @brief Backspace and Delete: the selection, or one character, a CRLF
 *         as one; nothing at the ends; presses join. */
static void
deleting(void)
{
    tiku_codeview_t *v = view_of("a\xc3\xa9" "b\r\ny");
    tiku_editdoc_t *doc = tiku_codeview_doc(v);

    tiku_codeview_select(v, 3, 3);
    press(v, TIKU_KEY_BACKSPACE, 0, NULL);
    check(holds(v, "ab\r\ny") && is(v, 1, 1), "EDT-003",
          "Backspace removes the whole character before the caret");
    tiku_codeview_select(v, 2, 2);
    press(v, TIKU_KEY_DELETE, 0, NULL);
    check(holds(v, "aby") && is(v, 2, 2), "EDT-003",
          "Delete removes a CRLF as one");
    tiku_codeview_select(v, 0, 0);
    check(press(v, TIKU_KEY_BACKSPACE, 0, NULL) == 1 &&
          tiku_codeview_status(v) == TIKU_EDIT_OK &&
          tiku_codeview_delete(v, 0) == TIKU_EDIT_NOTHING && holds(v, "aby"),
          "EDT-003", "Backspace at the start changes nothing");
    tiku_codeview_select(v, 3, 3);
    check(tiku_codeview_delete(v, 1) == TIKU_EDIT_NOTHING, "EDT-003",
          "and Delete at the end");
    tiku_codeview_select(v, 0, 2);
    press(v, TIKU_KEY_DELETE, 0, NULL);
    check(holds(v, "y") && is(v, 0, 0), "EDT-003",
          "with a selection, both remove it");
    tiku_editdoc_load(doc, "abcdef", 6);
    tiku_codeview_select(v, 6, 6);
    press(v, TIKU_KEY_BACKSPACE, 0, NULL);
    press(v, TIKU_KEY_BACKSPACE, 0, NULL);
    press(v, TIKU_KEY_BACKSPACE, 0, NULL);
    check(holds(v, "abc") && tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          holds(v, "abcdef") && !tiku_editdoc_can_undo(doc), "EDT-003",
          "Backspace presses in a row undo as one step");
    tiku_codeview_destroy(v);
}

/** @brief Text-input commits: whole, as one step; a refused one changes
 *         nothing and says so; a key in the middle drops a partial one. */
static void
commits(void)
{
    tiku_codeview_t *v = view_of("<>");
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_event_t e;

    tiku_codeview_select(v, 1, 1);
    check(commit(v, "h\xc3\xa9llo w\xc3\xb6rld\n\tand on") == 1 &&
          holds(v, "<h\xc3\xa9llo w\xc3\xb6rld\n\tand on>") &&
          tiku_codeview_status(v) == TIKU_EDIT_OK, "EDT-004",
          "a commit arriving in pieces goes in whole");
    check(tiku_editdoc_undo(doc) == TIKU_EDIT_OK && holds(v, "<>") &&
          is(v, 1, 1), "EDT-004", "as one step");
    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_TEXT_INPUT;
    e.button = TIKU_TEXT_BEGIN | TIKU_TEXT_END;
    e.dy = 1;
    e.text[0] = (char)0xff;
    check(tiku_codeview_event(v, &e) == 1 &&
          tiku_codeview_status(v) == TIKU_EDIT_INVALID_TEXT && holds(v, "<>"),
          "EDT-004", "a refused commit changes nothing and says why");
    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_TEXT_INPUT;
    e.button = TIKU_TEXT_BEGIN;
    e.dy = 8;
    strcpy(e.text, "abcd");
    tiku_codeview_event(v, &e);
    press(v, TIKU_KEY_RIGHT, 0, NULL);
    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_TEXT_INPUT;
    e.button = TIKU_TEXT_END;
    e.dx = 4;
    e.dy = 8;
    strcpy(e.text, "efgh");
    tiku_codeview_event(v, &e);
    check(holds(v, "<>") && tiku_codeview_status(v) == TIKU_EDIT_INVALID_TEXT,
          "EDT-004", "a key in the middle of a commit drops it");
    tiku_codeview_destroy(v);
}

/** @brief A read-only document refuses every edit and says so; the caret
 *         still moves. */
static void
read_only(void)
{
    tiku_codeview_t *v = view_of("ab");
    tiku_editdoc_t *doc = tiku_codeview_doc(v);

    strcpy(clip, "zz");
    clip_len = 2;
    tiku_editdoc_set_read_only(doc, 1);
    check(press(v, 'x', 0, "x") == 1 &&
          tiku_codeview_status(v) == TIKU_EDIT_READ_ONLY && holds(v, "ab"),
          "EDT-005", "typing into a read-only document is refused");
    tiku_codeview_select(v, 0, 1);
    check(press(v, 'v', TIKU_MOD_CMD, NULL) == 1 &&
          tiku_codeview_status(v) == TIKU_EDIT_READ_ONLY &&
          press(v, TIKU_KEY_DELETE, 0, NULL) == 1 &&
          tiku_codeview_status(v) == TIKU_EDIT_READ_ONLY && holds(v, "ab"),
          "EDT-005", "and so are pasting and deleting");
    check(press(v, TIKU_KEY_RIGHT, 0, NULL) == 1 && is(v, 1, 1) &&
          tiku_codeview_status(v) == TIKU_EDIT_OK, "EDT-005",
          "while the caret still moves");
    tiku_codeview_destroy(v);
}

/** @brief Undo and redo by key; with nothing to step, the status says. */
static void
history(void)
{
    tiku_codeview_t *v = view_of("");

    type(v, "one");
    press(v, TIKU_KEY_LEFT, 0, NULL);
    type(v, "X");
    check(press(v, 'z', TIKU_MOD_CMD, NULL) == 1 && holds(v, "one") &&
          press(v, 'Z', TIKU_MOD_CMD | TIKU_MOD_SHIFT, NULL) == 1 &&
          holds(v, "onXe") && press(v, 'z', TIKU_MOD_CMD, NULL) == 1 &&
          press(v, 'y', TIKU_MOD_CMD, NULL) == 1 && holds(v, "onXe"),
          "EDT-006", "Command Z undoes, Shift Command Z and Command Y redo, "
          "and moving the caret ends a typing step");
    press(v, 'y', TIKU_MOD_CMD, NULL);
    check(tiku_codeview_status(v) == TIKU_EDIT_NOTHING, "EDT-006",
          "with nothing to redo the status says so");
    tiku_codeview_destroy(v);
}

/** @brief A caret never lands between a CR and its LF. */
static void
line_breaks(void)
{
    tiku_codeview_t *v = view_of("a\nb");

    tiku_codeview_select(v, 1, 1);
    check(tiku_codeview_insert(v, "\r", 1, 0) == TIKU_EDIT_OK &&
          holds(v, "a\r\nb") && is(v, 1, 1), "EDT-007",
          "a CR typed before an LF joins it, and the caret stands before "
          "the CR");
    tiku_codeview_destroy(v);
    v = view_of("\rX\n");
    tiku_codeview_select(v, 2, 2);
    press(v, TIKU_KEY_BACKSPACE, 0, NULL);
    check(holds(v, "\r\n") && is(v, 0, 0), "EDT-007",
          "and so does one a deletion joins");
    tiku_codeview_destroy(v);
}

/** @brief Copy puts the selection's bytes on the clipboard; Cut deletes
 *         only what the clipboard took; Paste is whole or nothing. */
static void
clipboard(void)
{
    tiku_codeview_t *v = view_of("x\xc3\xa9\tb\r\ncz");
    tiku_editdoc_t *doc = tiku_codeview_doc(v), *small = NULL;
    tiku_editdoc_limits_t limits = { 12, 0 };
    tiku_codeview_t *bare = NULL;

    tiku_codeview_select(v, 1, 8);
    check(tiku_codeview_copy(v) == TIKU_EDIT_OK && clip_len == 7 &&
          memcmp(clip, "\xc3\xa9\tb\r\nc", 7) == 0, "CLP-001",
          "Copy puts the selection's bytes on the clipboard, line break "
          "and all");
    clip_sets = 0;
    tiku_codeview_select(v, 3, 3);
    check(tiku_codeview_copy(v) == TIKU_EDIT_NOTHING && clip_sets == 0,
          "CLP-001", "with nothing selected nothing is offered");
    tiku_editdoc_create(NULL, &small);
    tiku_codeview_create(small, &bare);
    tiku_editdoc_release(small);
    tiku_editdoc_load(small, "abc", 3);
    tiku_codeview_select(bare, 0, 3);
    check(tiku_codeview_copy(bare) == TIKU_EDIT_INVALID &&
          tiku_codeview_paste(bare) == TIKU_EDIT_INVALID, "CLP-001",
          "a view with no clipboard neither copies nor pastes");
    tiku_codeview_destroy(bare);

    tiku_codeview_select(v, 0, 1);
    clip_full = 1;
    check(tiku_codeview_cut(v) == TIKU_EDIT_LIMIT &&
          holds(v, "x\xc3\xa9\tb\r\ncz"), "CLP-002",
          "Cut deletes nothing when the clipboard refuses the text");
    clip_full = 0;
    check(tiku_codeview_cut(v) == TIKU_EDIT_OK && clip_len == 1 &&
          clip[0] == 'x' && holds(v, "\xc3\xa9\tb\r\ncz") &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          holds(v, "x\xc3\xa9\tb\r\ncz"), "CLP-002",
          "and deletes it, as one step, once the clipboard took it");

    strcpy(clip, "1\r\n\t2");
    clip_len = 5;
    tiku_codeview_select(v, 8, 9);
    check(tiku_codeview_paste(v) == TIKU_EDIT_OK &&
          holds(v, "x\xc3\xa9\tb\r\nc1\r\n\t2") && is(v, 13, 13) &&
          tiku_editdoc_undo(doc) == TIKU_EDIT_OK &&
          holds(v, "x\xc3\xa9\tb\r\ncz"), "CLP-003",
          "Paste replaces the selection with the clipboard's text as one "
          "step");
    strcpy(clip, "ok\xff");
    clip_len = 3;
    check(tiku_codeview_paste(v) == TIKU_EDIT_INVALID_TEXT &&
          holds(v, "x\xc3\xa9\tb\r\ncz"), "CLP-003",
          "a clipboard holding malformed UTF-8 pastes nothing");
    clip_empty = 1;
    check(tiku_codeview_paste(v) == TIKU_EDIT_NOTHING, "CLP-003",
          "and an empty one pastes nothing");
    clip_empty = 0;
    tiku_editdoc_create(&limits, &small);
    tiku_codeview_create(small, &bare);
    tiku_editdoc_release(small);
    tiku_codeview_set_host(bare, &host);
    tiku_editdoc_load(small, "1234567890", 10);
    strcpy(clip, "abc");
    clip_len = 3;
    check(tiku_codeview_paste(bare) == TIKU_EDIT_LIMIT &&
          holds(bare, "1234567890"), "CLP-003",
          "and none of a paste is kept when the whole would pass the "
          "document's limit");
    tiku_codeview_destroy(bare);

    tiku_codeview_select(v, 0, 1);
    check(press(v, 'c', TIKU_MOD_CMD, NULL) == 1 && clip_len == 1 &&
          press(v, 'a', TIKU_MOD_CMD, NULL) == 1 && is(v, 0, 9) &&
          press(v, 'x', TIKU_MOD_CMD, NULL) == 1 && holds(v, "") &&
          press(v, 'v', TIKU_MOD_CMD, NULL) == 1 &&
          holds(v, "x\xc3\xa9\tb\r\ncz"), "CLP-004",
          "Command C, A, X and V copy, select all, cut and paste");
    tiku_codeview_destroy(v);
}

/* An allocator that fails while told to. */
static int refuse;

static void *
t_alloc(size_t n)
{
    return refuse ? NULL : malloc(n);
}

static void *
t_realloc(void *p, size_t n)
{
    return refuse ? NULL : realloc(p, n);
}

/** @brief Copy with no memory leaves the clipboard alone; the host hears
 *         of every change, made by any view. */
static void
host_calls(void)
{
    tiku_ed_allocator_t failing = { t_alloc, t_realloc, free };
    tiku_codeview_t *v = view_of("abc"), *other = NULL;
    tiku_editdoc_t *doc = tiku_codeview_doc(v);

    clip_sets = 0;
    tiku_codeview_select(v, 0, 3);
    tiku_ed_test_allocator(&failing);
    refuse = 1;
    check(tiku_codeview_copy(v) == TIKU_EDIT_NO_MEMORY && clip_sets == 0,
          "CLP-005", "a copy that cannot allocate offers nothing");
    refuse = 0;
    tiku_ed_test_allocator(NULL);
    tiku_codeview_create(doc, &other);
    invalidated = 0;
    tiku_codeview_select(other, 3, 3);
    tiku_codeview_insert(other, "!", 1, 0);
    tiku_editdoc_load(doc, "new", 3);
    check(invalidated == 2, "HST-001",
          "a view's host hears of each change to its document, another "
          "view's and a load included");
    tiku_codeview_destroy(other);
    tiku_codeview_destroy(v);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    typing();
    returns_and_tabs();
    deleting();
    commits();
    read_only();
    history();
    line_breaks();
    clipboard();
    host_calls();
    printf("%s\n", fails ? "FAIL: code view editing" :
           "PASS: code view editing -- typing, Return and Tab, deleting, "
           "commits, read-only, history, line breaks, clipboard, host");
    return fails != 0;
}
