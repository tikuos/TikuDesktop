/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_extensions.c - an application's lexer: its state carried
 * from line to line, its runs checked, its failures and its long lines;
 * the JSON lexer; keys the host binds and commands run by name; and the
 * changes a host polls a view for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview_private.h"

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

/** @brief A view of a new document holding @p text, 200 by 60 pixels;
 *         the view holds the only reference. */
static tiku_codeview_t *
view_of(const char *text)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;
    tiku_rect_t box = { 0, 0, 200, 60 };

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, text, strlen(text)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK) {
        printf("  [FAIL] setup     view\n");
        exit(1);
    }
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
    return v;
}

/** @brief Whether the view's document holds exactly @p text. */
static int
holds(tiku_codeview_t *v, const char *text)
{
    tiku_edit_range_t all = { 0, 0 };
    char buf[128];
    size_t n = 0;

    all.end = tiku_editdoc_length(v->doc);
    return tiku_editdoc_read(v->doc, all, buf, sizeof buf, &n) ==
               TIKU_EDIT_OK &&
           n == strlen(text) && memcmp(buf, text, n) == 0;
}

/** @brief What the test lexer is told, and how it misbehaves: bad 1 runs
 *         past the line, 2 empty, 3 short, 4 in an ink there is not, 5 a
 *         failure, 6 short of the line's end, 7 negative. */
typedef struct {
    tiku_editdoc_t *doc;        /* tried for a change while it runs     */
    uint32_t last;              /* the state it was last given          */
    size_t length;              /* and the line's bytes                 */
    int calls;                  /* lines it was asked for               */
    int bad;                    /* the rule it breaks, 0 none           */
    int mutate;                 /* try to change the document           */
    tiku_ink_t ink;             /* the ink of its one run               */
} provider_t;

/** @brief A lexer leaving each line in its state plus its length, and
 *         giving the line one run, or a bad one as it is told. */
static tiku_edit_status_t
provider(void *ctx, const char *text, size_t n, uint32_t in, uint32_t *out,
         tiku_syntax_run_fn emit, void *emit_ctx)
{
    provider_t *p = ctx;
    int length;

    p->last = in;
    p->length = n;
    p->calls++;
    check(text[n] == '\0', "STY-008", "a line comes with a NUL after it");
    if (p->mutate)
        check(tiku_editdoc_load(p->doc, "bad", 3) == TIKU_EDIT_BUSY,
              "STY-009", "the document refuses changes while a lexer "
              "runs");
    *out = in + (uint32_t)n;
    if (p->bad == 5)
        return TIKU_EDIT_NO_MEMORY;
    if (emit == NULL || n == 0)
        return TIKU_EDIT_OK;
    switch (p->bad) {
    case 1:
        length = (int)n + 1;
        break;
    case 2:
        length = 0;
        break;
    case 3:
        length = 1;
        break;
    case 6:
        length = (int)n - 1;
        break;
    case 7:
        length = -1;
        break;
    default:
        length = (int)n;
        break;
    }
    emit(emit_ctx, p->bad == 4 ? (tiku_ink_t)999 : p->ink, length);
    return TIKU_EDIT_OK;
}

/** @brief An allocator with no room at all. */
static void *
no_alloc(size_t n)
{
    (void)n;
    return NULL;
}

/** @brief The same for growing. */
static void *
no_realloc(void *p, size_t n)
{
    (void)p;
    (void)n;
    return NULL;
}

static const tiku_ed_allocator_t failing = { no_alloc, no_realloc, free };

/** @brief A lexer's state is carried from line to line through edits,
 *         and every line starts afresh without it. */
static void
states(void)
{
    tiku_codeview_t *v = view_of("abc\nx\nyz");
    provider_t p;
    tiku_codeview_lexer_t lexer = { &p, 0x12345678, 1, provider };
    const tiku_span_t *runs;
    size_t n = 0;

    memset(&p, 0, sizeof p);
    p.doc = v->doc;
    p.ink = TIKU_INK_WORD;
    p.mutate = 1;
    check(tiku_codeview_set_lexer(v, &lexer) == TIKU_EDIT_OK, "STY-008",
          "a lexer is set");
    runs = tiku_cv_style_line(v, 2, 8, &n);
    check(runs != NULL && n == 1 && runs[0].ink == TIKU_INK_WORD,
          "STY-008", "a line is coloured by it");
    check(p.calls == 3 && p.last == 0x1234567c && p.length == 2, "STY-008",
          "the lines above are read for the state the line starts in");
    (void)tiku_codeview_select(v, 0, 0);
    check(tiku_codeview_insert(v, "q\nq", 3, 0) == TIKU_EDIT_OK &&
          tiku_cv_style_line(v, 3, 11, &n) != NULL &&
          p.last == 0x1234567e, "STY-008",
          "the state is read again past an edit, all 32 bits of it");
    lexer.stateful = 0;
    check(tiku_codeview_set_lexer(v, &lexer) == TIKU_EDIT_OK, "STY-008",
          "a lexer that keeps no state is set");
    p.calls = 0;
    check(tiku_cv_style_line(v, 3, 11, &n) != NULL && p.calls == 1 &&
          p.last == lexer.initial_state, "STY-008",
          "each line starts in the first state, the lines above unread");
    tiku_codeview_destroy(v);
}

/** @brief Runs that break the lexer's rules, and a lexer's failure, leave
 *         the line plain and say why. */
static void
bad_runs(void)
{
    tiku_codeview_t *v = view_of("\xc3\xa9!");
    provider_t p;
    tiku_codeview_lexer_t lexer = { &p, 0, 0, provider };
    int plain = 1;
    size_t n = 0;

    memset(&p, 0, sizeof p);
    p.ink = TIKU_INK_WORD;
    check(tiku_codeview_set_lexer(v, &lexer) == TIKU_EDIT_OK, "STY-009",
          "a lexer is set");
    for (p.bad = 1; p.bad <= 7; p.bad++) {
        n = 99;
        plain &= tiku_cv_style_line(v, 0, 3, &n) == NULL && n == 0 &&
                 tiku_codeview_lexer_status(v) ==
                     (p.bad == 5 ? TIKU_EDIT_NO_MEMORY
                                 : TIKU_EDIT_INVALID_RANGE) &&
                 holds(v, "\xc3\xa9!");
    }
    check(plain, "STY-009", "a run past the line, empty, short, in an ink "
          "there is not, splitting a character or negative, and a "
          "failure, each leave the line plain and the text as it was");
    p.bad = 0;
    check(tiku_cv_style_line(v, 0, 3, &n) != NULL &&
          tiku_codeview_lexer_status(v) == TIKU_EDIT_OK, "STY-009",
          "good runs colour the line again");
    lexer.line = NULL;
    check(tiku_codeview_set_lexer(v, &lexer) == TIKU_EDIT_INVALID &&
          tiku_cv_style_line(v, 0, 3, &n) != NULL, "STY-009",
          "a lexer with no line function is refused, the one set staying");
    check(tiku_codeview_set_lexer(v, NULL) == TIKU_EDIT_OK &&
          tiku_cv_style_line(v, 0, 3, &n) == NULL, "STY-009",
          "NULL colours by the language again");
    tiku_codeview_destroy(v);
}

/** @brief A long line is read whole once, kept until the lexer changes,
 *         and every allocation a line needs can fail cleanly. */
static void
long_lines(void)
{
    static char text[6001];
    tiku_codeview_t *v;
    provider_t p;
    tiku_codeview_lexer_t lexer = { &p, 0, 0, provider };
    const tiku_span_t *runs;
    size_t n = 0;

    memset(text, 'a', 6000);
    text[6000] = '\0';
    v = view_of(text);
    memset(&p, 0, sizeof p);
    p.ink = TIKU_INK_WORD;
    check(tiku_codeview_set_lexer(v, &lexer) == TIKU_EDIT_OK &&
          tiku_cv_style_line(v, 0, 8, &n) != NULL && p.length == 6000,
          "STY-008", "a lexer is given a long line whole");
    check(tiku_cv_style_line(v, 0, 9, &n) != NULL && p.calls == 1,
          "STY-008", "and its runs are kept");
    p.ink = TIKU_INK_STR;
    tiku_codeview_lexer_changed(v);
    runs = tiku_cv_style_line(v, 0, 9, &n);
    check(runs != NULL && runs[0].ink == TIKU_INK_STR && p.calls == 2,
          "STY-008", "until the lexer is said to have changed");
    tiku_codeview_destroy(v);

    v = view_of("abc");
    check(tiku_codeview_set_lexer(v, &lexer) == TIKU_EDIT_OK, "STY-009",
          "a lexer is set");
    tiku_ed_test_allocator(&failing);
    check(tiku_cv_style_line(v, 0, 3, &n) == NULL &&
          tiku_codeview_lexer_status(v) == TIKU_EDIT_NO_MEMORY, "STY-009",
          "no room for the line leaves it plain");
    tiku_ed_test_allocator(NULL);
    check(tiku_cv_style_line(v, 0, 3, &n) != NULL, "STY-009",
          "and it is coloured once there is");
    tiku_ed_free(v->runs);
    v->runs = NULL;
    v->runs_cap = 0;
    tiku_ed_test_allocator(&failing);
    check(tiku_cv_style_line(v, 0, 3, &n) == NULL &&
          tiku_codeview_lexer_status(v) == TIKU_EDIT_NO_MEMORY, "STY-009",
          "no room for the runs leaves it plain");
    tiku_ed_test_allocator(NULL);
    lexer.stateful = 1;
    check(tiku_codeview_set_lexer(v, &lexer) == TIKU_EDIT_OK, "STY-009",
          "a lexer that keeps state is set");
    tiku_ed_test_allocator(&failing);
    check(tiku_cv_style_line(v, 0, 3, &n) == NULL &&
          tiku_codeview_lexer_status(v) == TIKU_EDIT_NO_MEMORY, "STY-009",
          "and no room for the states leaves the line plain");
    tiku_ed_test_allocator(NULL);
    tiku_codeview_destroy(v);
}

/** @brief The JSON lexer colours a line's strings, numbers and keywords
 *         and covers every byte of it. */
static void
json(void)
{
    tiku_codeview_t *v = view_of("{\"caf\xc3\xa9\":true,\"quote\":"
                                 "\"a\\\"b\",\"n\":-1.2e+3,\"z\":null}");
    const unsigned want = (1u << TIKU_INK_STR) | (1u << TIKU_INK_NUM) |
                          (1u << TIKU_INK_WORD);
    const tiku_span_t *runs;
    size_t n = 0, i, length = 0;
    unsigned inks = 0;

    check(tiku_codeview_set_lexer(v, tiku_codeview_json_lexer()) ==
          TIKU_EDIT_OK, "STY-010", "the JSON lexer is set");
    runs = tiku_cv_style_line(v, 0, 0, &n);
    check(runs != NULL && tiku_codeview_lexer_status(v) == TIKU_EDIT_OK,
          "STY-010", "a line of JSON is coloured");
    for (i = 0; runs != NULL && i < n; i++) {
        length += (size_t)runs[i].len;
        inks |= 1u << runs[i].ink;
    }
    check(length == (size_t)tiku_editdoc_length(v->doc), "STY-010",
          "its runs cover the line");
    check((inks & want) == want, "STY-010",
          "in the string, number and keyword inks");
    tiku_codeview_destroy(v);
}

/** @brief A key press @p key with @p mods to @p v; whether it was taken. */
static int
key(tiku_codeview_t *v, unsigned key, unsigned mods)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_KEY_DOWN;
    e.key = key;
    e.modifiers = mods;
    return tiku_codeview_event(v, &e);
}

/** @brief A binding of @p key with @p mods to @p command. */
static tiku_codeview_binding_t
bound(unsigned key, unsigned mods, tiku_codeview_command_t command)
{
    tiku_codeview_binding_t b;

    memset(&b, 0, sizeof b);
    b.key = key;
    b.modifiers = mods;
    b.command = command;
    return b;
}

/** @brief Keys the host binds run their commands, any case, and are left
 *         to the host or taken doing nothing as bound. */
static void
bindings(void)
{
    tiku_codeview_t *v = view_of("abc");
    tiku_codeview_binding_t b[4], dup[2];

    b[0] = bound('j', TIKU_MOD_CMD, TIKU_CODEVIEW_CMD_SELECT_ALL);
    b[1] = bound('z', TIKU_MOD_CMD, TIKU_CODEVIEW_CMD_PASS);
    b[2] = bound('x', 0, TIKU_CODEVIEW_CMD_IGNORE);
    b[3] = bound(TIKU_KEY_RIGHT, 0, TIKU_CODEVIEW_CMD_MOVE);
    b[3].move = TIKU_CODEVIEW_CHAR_RIGHT;
    b[3].extend = 1;
    check(tiku_codeview_set_bindings(v, b, 4) == TIKU_EDIT_OK, "CMD-001",
          "keys are bound");
    b[0].command = TIKU_CODEVIEW_CMD_IGNORE;
    check(key(v, 'J', TIKU_MOD_CMD | TIKU_MOD_CAPS) &&
          tiku_codeview_has_selection(v), "CMD-001", "a bound key runs its "
          "command in either case, Caps Lock aside, from a copy");
    check(!key(v, 'j', TIKU_MOD_CMD | TIKU_MOD_SHIFT), "CMD-001",
          "other keys held are another binding");
    check(!key(v, 'z', TIKU_MOD_CMD), "CMD-001",
          "a key bound to PASS is left to the host");
    check(key(v, 'x', 0) && holds(v, "abc"), "CMD-001",
          "a key bound to IGNORE is taken, doing nothing");
    (void)tiku_codeview_select(v, 0, 0);
    check(key(v, TIKU_KEY_RIGHT, 0) && tiku_codeview_has_selection(v),
          "CMD-001", "a move binds with where it goes and whether it "
          "extends");
    dup[0] = bound('a', 0, TIKU_CODEVIEW_CMD_IGNORE);
    dup[1] = bound('A', TIKU_MOD_CAPS, TIKU_CODEVIEW_CMD_IGNORE);
    check(tiku_codeview_set_bindings(v, dup, 2) == TIKU_EDIT_INVALID,
          "CMD-001", "a key bound twice is refused");
    tiku_ed_test_allocator(&failing);
    check(tiku_codeview_set_bindings(v, b, 4) == TIKU_EDIT_NO_MEMORY,
          "CMD-001", "a table that cannot be copied is refused");
    tiku_ed_test_allocator(NULL);
    check(key(v, 'j', TIKU_MOD_CMD), "CMD-001",
          "and the keys bound before stay");
    check(tiku_codeview_insert(v, "Q", 1, 0) == TIKU_EDIT_OK &&
          holds(v, "Q") &&
          tiku_codeview_command_enabled(v, TIKU_CODEVIEW_CMD_UNDO),
          "CMD-002", "Undo is enabled after an edit");
    tiku_editdoc_set_read_only(v->doc, 1);
    check(!tiku_codeview_command_enabled(v, TIKU_CODEVIEW_CMD_UNDO),
          "CMD-002", "and not while the document refuses changes");
    check(tiku_codeview_execute(v, TIKU_CODEVIEW_CMD_NEWLINE, 0, 0) ==
              TIKU_EDIT_READ_ONLY &&
          tiku_codeview_status(v) == TIKU_EDIT_READ_ONLY && holds(v, "Q"),
          "CMD-002", "a command run by name keeps its refusal as the "
          "view's status");
    tiku_editdoc_set_read_only(v->doc, 0);
    check(tiku_codeview_execute(v, TIKU_CODEVIEW_CMD_UNDO, 0, 0) ==
          TIKU_EDIT_OK && holds(v, "abc"), "CMD-002", "Undo runs by name");
    check(tiku_codeview_set_bindings(v, NULL, 0) == TIKU_EDIT_OK &&
          key(v, 'z', TIKU_MOD_CMD), "CMD-001",
          "with none bound the view's own keys come back");
    tiku_codeview_destroy(v);
}

/** @brief Committed text is never a bound key, and a list of words takes
 *         its keys before the host's bindings. */
static void
precedence(void)
{
    tiku_codeview_t *v = view_of("abc");
    tiku_codeview_binding_t b[1];
    const char *items[] = { "hello" };
    tiku_codeview_list_t list;
    tiku_event_t text;

    b[0] = bound('x', 0, TIKU_CODEVIEW_CMD_IGNORE);
    check(tiku_codeview_set_bindings(v, b, 1) == TIKU_EDIT_OK, "CMD-001",
          "a key is bound");
    (void)tiku_codeview_select(v, 0, 0);
    memset(&text, 0, sizeof text);
    text.type = TIKU_EVENT_TEXT_INPUT;
    text.button = TIKU_TEXT_BEGIN | TIKU_TEXT_END;
    text.dy = 1;
    strcpy(text.text, "x");
    check(tiku_codeview_event(v, &text) && holds(v, "xabc"), "CMD-001",
          "the bound character committed as text is typed");
    b[0] = bound(TIKU_KEY_RETURN, 0, TIKU_CODEVIEW_CMD_IGNORE);
    memset(&list, 0, sizeof list);
    list.range.end = 1;
    list.items = items;
    list.count = 1;
    list.inserts = 1;
    check(tiku_codeview_set_bindings(v, b, 1) == TIKU_EDIT_OK &&
          tiku_codeview_list_show(v, &list) == TIKU_EDIT_OK, "CMD-001",
          "Return is bound and a list shown");
    check(key(v, TIKU_KEY_RETURN, 0) && holds(v, "helloabc"), "CMD-001",
          "the list takes Return before the binding");
    tiku_codeview_destroy(v);
}

/** @brief Each view reports what changed since it was last asked, once,
 *         allocating nothing. */
static void
changes(void)
{
    tiku_codeview_t *v = view_of("a\nb\nc\nd\ne\nf\ng\nh\n"), *other = NULL;
    tiku_edit_selection_t selections[] = { { 0, 1 }, { 4, 4 } };
    const unsigned edited = TIKU_CODEVIEW_CHANGE_DOCUMENT |
                            TIKU_CODEVIEW_CHANGE_SELECTION;
    const unsigned refused = TIKU_CODEVIEW_CHANGE_DOCUMENT |
                             TIKU_CODEVIEW_CHANGE_COMMANDS;
    unsigned f;

    check(tiku_codeview_create(v->doc, &other) == TIKU_EDIT_OK &&
          tiku_codeview_changes(v) == TIKU_CODEVIEW_CHANGE_ALL &&
          tiku_codeview_changes(other) == TIKU_CODEVIEW_CHANGE_ALL &&
          !tiku_codeview_changes(v), "CMD-003",
          "every flag the first time, none with nothing changed");
    (void)tiku_codeview_select(v, 0, 1);
    check((tiku_codeview_changes(v) & TIKU_CODEVIEW_CHANGE_SELECTION) &&
          !tiku_codeview_changes(other), "CMD-003",
          "a selection is the view's own");
    (void)tiku_codeview_select_many(v, selections, 2, 0);
    f = tiku_codeview_changes(v);
    check((f & TIKU_CODEVIEW_CHANGE_SELECTION) &&
          (f & TIKU_CODEVIEW_CHANGE_COMMANDS), "CMD-003",
          "a second selection enables Keep Main");
    (void)tiku_codeview_keep_main(v);
    check(tiku_codeview_insert(v, "x", 1, 0) == TIKU_EDIT_OK &&
          (tiku_codeview_changes(v) & TIKU_CODEVIEW_CHANGE_DOCUMENT) &&
          (tiku_codeview_changes(other) & edited) == edited, "CMD-003",
          "an edit is told to every view of the document");
    tiku_codeview_scroll_to(v, 4, 0);
    check(tiku_codeview_changes(v) & TIKU_CODEVIEW_CHANGE_SCROLL, "CMD-003",
          "and a scroll to the view scrolled");
    tiku_editdoc_saved(v->doc, tiku_editdoc_state(v->doc));
    check(tiku_codeview_changes(v) & TIKU_CODEVIEW_CHANGE_DOCUMENT,
          "CMD-003", "saving changes the document's state");
    tiku_editdoc_set_read_only(v->doc, 1);
    tiku_ed_test_allocator(&failing);
    f = tiku_codeview_changes(v);
    check((f & refused) == refused && !tiku_codeview_changes(v), "CMD-003",
          "with no room to allocate, read-only is told once");
    tiku_ed_test_allocator(NULL);
    check(tiku_codeview_execute(v, TIKU_CODEVIEW_CMD_NEWLINE, 0, 0) ==
              TIKU_EDIT_READ_ONLY &&
          tiku_codeview_changes(v) == TIKU_CODEVIEW_CHANGE_STATUS &&
          !tiku_codeview_changes(v), "CMD-003",
          "a command's refusal is a change of status");
    check(!tiku_codeview_changes(NULL), "CMD-003", "NULL has none");
    tiku_codeview_destroy(other);
    tiku_codeview_destroy(v);
}

int
main(void)
{
    states();
    bad_runs();
    long_lines();
    json();
    bindings();
    precedence();
    changes();
    printf("%s\n", fails ? "FAIL: code view extensions" :
           "PASS: code view extensions -- lexer states, bad runs, long "
           "lines, JSON, bound keys, commands by name, polled changes");
    return fails != 0;
}
