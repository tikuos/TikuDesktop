/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richcmd.c - catalog, command availability and atomic range dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richcmd.h"
#include "tiku_richdoc_private.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL richcmd:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Resolve a paragraph-local position in the live document. */
static tiku_rich_pos_t
position(tiku_richdoc_t *doc, size_t index, size_t byte)
{
    return (tiku_rich_pos_t){doc->root->body,
        doc->root->paragraphs[index]->value.id, byte, TIKU_RICH_AFTER};
}

/** @brief Load mixed-format paragraphs and named paragraph/character styles. */
static tiku_richdoc_t *
fixture(void)
{
    tiku_richdoc_t *doc;
    const tiku_rich_style_source_t styles[] = {
        {.name = "Body", .chars = {.present = TIKU_RICH_SIZE, .size = 12}},
        {.name = "Emphasis", .kind = TIKU_RICH_CHARACTER_STYLE,
         .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}},
        {.name = "Unused"}
    };
    const tiku_rich_run_source_t run = {.length = 3, .style = 2,
        .chars = {.present = TIKU_RICH_BOLD, .bold = 1}, .link = "#heading"};
    const tiku_rich_paragraph_source_t paragraphs[] = {
        {.text = "one", .length = 3, .style = 1, .runs = &run, .run_count = 1},
        {.text = "two", .length = 3, .style = 1},
        {.text = "last", .length = 4}
    };
    const tiku_rich_source_t source = {.styles = styles, .style_count = 3,
        .paragraphs = paragraphs, .paragraph_count = 3};

    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Query a known command and check its enabled/status consistency. */
static tiku_richcmd_state_t
query(tiku_richdoc_t *doc, const tiku_richcmd_context_t *context,
      tiku_richcmd_kind_t kind)
{
    tiku_richcmd_state_t state;

    CHECK(tiku_richcmd_query(doc, context, kind, &state) == TIKU_RICH_OK);
    CHECK(state.generation == doc->generation);
    CHECK(state.enabled == (state.status == TIKU_RICH_OK));
    CHECK(state.enabled ? !state.reason : state.reason && *state.reason);
    return state;
}

/** @brief Execute a command without parameters. */
static tiku_rich_status_t
execute(tiku_richdoc_t *doc, const tiku_richcmd_context_t *context,
        tiku_richcmd_kind_t kind)
{
    const tiku_richcmd_t command = {.kind = kind};

    return tiku_richcmd_execute(doc, context, &command);
}

/** @brief Read text independently of paragraph IDs. */
static void
expect_text(tiku_richdoc_t *doc, const char *expected)
{
    tiku_richsnapshot_t *s;
    char text[128];
    size_t length;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_text(s, text, sizeof text, &length) ==
          TIKU_RICH_OK);
    CHECK(length == strlen(expected) && !strcmp(text, expected));
    tiku_richsnapshot_release(s);
}

/** @brief Metadata is complete and unique; unsupported queries clear output. */
static void
catalog(void)
{
    const tiku_richcmd_info_t *info, *other;
    tiku_richdoc_t *doc = fixture();
    tiku_richcmd_context_t context = {0};
    tiku_richcmd_state_t state, zero = {0};
    tiku_richcmd_t command = {0};
    size_t i, j;

    CHECK(tiku_richcmd_count() == 36);
    for (i = 0; i < tiku_richcmd_count(); i++) {
        info = tiku_richcmd_at(i);
        CHECK(info && info->kind && info->key && *info->key);
        CHECK(info->label && *info->label);
        CHECK(tiku_richcmd_info(info->kind) == info);
        CHECK(info->parameter <= TIKU_RICHCMD_COLOR_PARAMETER);
        CHECK(info->scope <= TIKU_RICHCMD_PARAGRAPH_SCOPE);
        for (j = 0; j < i; j++) {
            other = tiku_richcmd_at(j);
            CHECK(info->kind != other->kind && strcmp(info->key, other->key));
        }
    }
    CHECK(!tiku_richcmd_at(SIZE_MAX) && !tiku_richcmd_at(36));
    CHECK(!tiku_richcmd_info(0) && !tiku_richcmd_info((tiku_richcmd_kind_t)-1));
    memset(&state, 0xff, sizeof state);
    CHECK(tiku_richcmd_query(doc, &context, 0, &state) == TIKU_RICH_UNSUPPORTED);
    CHECK(!memcmp(&state, &zero, sizeof state));
    CHECK(tiku_richcmd_query(NULL, &context, TIKU_RICHCMD_UNDO, &state) ==
          TIKU_RICH_INVALID);
    CHECK(!memcmp(&state, &zero, sizeof state));
    CHECK(tiku_richcmd_query(doc, NULL, TIKU_RICHCMD_UNDO, &state) ==
          TIKU_RICH_INVALID);
    CHECK(tiku_richcmd_query(doc, &context, TIKU_RICHCMD_UNDO, NULL) ==
          TIKU_RICH_INVALID);
    CHECK(tiku_richcmd_execute(doc, &context, &command) == TIKU_RICH_UNSUPPORTED);
    CHECK(tiku_richcmd_execute(doc, &context, NULL) == TIKU_RICH_INVALID);
    tiku_richdoc_release(doc);
}

/** @brief Toggles share selection state and undo once across paragraphs. */
static void
toggles(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richcmd_context_t c = {.first = position(doc, 0, 0),
                               .last = position(doc, 1, 3)};
    tiku_richcmd_state_t state;
    tiku_richsnapshot_t *before;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    tiku_richcmd_kind_t kinds[] = {TIKU_RICHCMD_BOLD, TIKU_RICHCMD_ITALIC,
                                  TIKU_RICHCMD_UNDERLINE, TIKU_RICHCMD_STRIKE};
    size_t i;

    CHECK(tiku_richdoc_snapshot(doc, &before) == TIKU_RICH_OK);
    state = query(doc, &c, TIKU_RICHCMD_BOLD);
    CHECK(state.enabled && state.check == TIKU_RICHCMD_CHECK_MIXED);
    state = query(doc, &c, TIKU_RICHCMD_STRIKE);
    CHECK(state.check == TIKU_RICHCMD_CHECK_UNSPECIFIED);
    for (i = 0; i < 4; i++) {
        CHECK(execute(doc, &c, kinds[i]) == TIKU_RICH_OK);
        CHECK(query(doc, &c, kinds[i]).check == TIKU_RICHCMD_CHECK_ON);
        CHECK(execute(doc, &c, kinds[i]) == TIKU_RICH_OK);
        CHECK(query(doc, &c, kinds[i]).check == TIKU_RICHCMD_CHECK_OFF);
        CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
        CHECK(query(doc, &c, kinds[i]).check == TIKU_RICHCMD_CHECK_ON);
        CHECK(execute(doc, &c, TIKU_RICHCMD_REDO) == TIKU_RICH_OK);
        CHECK(query(doc, &c, kinds[i]).check == TIKU_RICHCMD_CHECK_OFF);
        CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
        CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
        CHECK(doc->root == before->root && !tiku_richdoc_dirty(doc));
        tiku_richdoc_clear_history(doc);
    }
    c.last = position(doc, 0, 3);
    CHECK(execute(doc, &c, TIKU_RICHCMD_CLEAR_CHARACTER_OVERRIDES) ==
          TIKU_RICH_OK);
    CHECK(tiku_rd_resolve(doc->root, 0, 0, &chars, &props) == TIKU_RICH_OK);
    CHECK(!(chars.present & TIKU_RICH_BOLD) && chars.italic && chars.size == 12);
    CHECK(doc->root->paragraphs[0]->value.runs[0].style);
    CHECK(!strcmp(doc->root->paragraphs[0]->value.runs[0].link, "#heading"));
    tiku_richsnapshot_release(before);
    tiku_richdoc_release(doc);
}

/** @brief Host/global permission, stale state and selection refusals agree. */
static void
availability(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richcmd_context_t c = {.first = position(doc, 0, 1),
                               .last = position(doc, 0, 2)};
    tiku_richcmd_state_t state;
    tiku_rd_root_t *root = doc->root;
    size_t i, used = tiku_richdoc_memory(doc);

    CHECK(query(doc, &c, TIKU_RICHCMD_UNDO).status == TIKU_RICH_NO_CHANGE);
    CHECK(query(doc, &c, TIKU_RICHCMD_REDO).status == TIKU_RICH_NO_CHANGE);
    c.read_only = 1;
    for (i = 0; i < tiku_richcmd_count(); i++) {
        state = query(doc, &c, tiku_richcmd_at(i)->kind);
        CHECK(state.status == TIKU_RICH_READ_ONLY);
        CHECK(execute(doc, &c, tiku_richcmd_at(i)->kind) == TIKU_RICH_READ_ONLY);
    }
    CHECK(query(doc, &c, TIKU_RICHCMD_BOLD).check == TIKU_RICHCMD_CHECK_ON);
    c.read_only = 0;
    tiku_richdoc_set_read_only(doc, 1);
    CHECK(query(doc, &c, TIKU_RICHCMD_BOLD).status == TIKU_RICH_READ_ONLY);
    tiku_richdoc_set_read_only(doc, 0);
    c.generation = doc->generation + 1;
    for (i = 0; i < tiku_richcmd_count(); i++) {
        state = query(doc, &c, tiku_richcmd_at(i)->kind);
        CHECK(state.status == TIKU_RICH_STALE);
        CHECK(state.check == TIKU_RICHCMD_CHECK_NONE);
        CHECK(execute(doc, &c, tiku_richcmd_at(i)->kind) == TIKU_RICH_STALE);
    }
    c.generation = 0;
    c.last = c.first;
    state = query(doc, &c, TIKU_RICHCMD_BOLD);
    CHECK(state.status == TIKU_RICH_NO_CHANGE);
    CHECK(state.check == TIKU_RICHCMD_CHECK_ON);
    CHECK(query(doc, &c, TIKU_RICHCMD_DELETE).status == TIKU_RICH_NO_CHANGE);
    CHECK(query(doc, &c, TIKU_RICHCMD_ALIGN_CENTER).enabled);
    c.first = position(doc, 0, 3);
    c.last = position(doc, 1, 0);
    CHECK(query(doc, &c, TIKU_RICHCMD_BOLD).status == TIKU_RICH_NO_CHANGE);
    CHECK(query(doc, &c, TIKU_RICHCMD_DELETE).enabled);
    c.first = position(doc, 2, 0);
    CHECK(query(doc, &c, TIKU_RICHCMD_BOLD).status == TIKU_RICH_INVALID_RANGE);
    CHECK(execute(doc, &c, TIKU_RICHCMD_ALIGN_CENTER) == TIKU_RICH_INVALID_RANGE);
    CHECK(query(doc, &c, TIKU_RICHCMD_STYLE_DEFINE).enabled);
    CHECK(doc->root == root && used == tiku_richdoc_memory(doc));
    doc->generation = UINT64_MAX;
    CHECK(query(doc, &c, TIKU_RICHCMD_STYLE_DEFINE).status == TIKU_RICH_LIMIT);
    tiku_richdoc_release(doc);
}

/** @brief Paragraph endpoints, list restarts and property clearing are typed. */
static void
paragraphs(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richcmd_context_t c = {.first = position(doc, 0, 1),
                               .last = position(doc, 2, 0)};
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_LIST_SET,
        .value.list = {TIKU_RICH_LIST_DECIMAL, 1, 7}};
    tiku_richsnapshot_t *s;
    tiku_rich_list_t list;
    uint64_t ordinal;
    tiku_richcmd_kind_t alignments[] = {TIKU_RICHCMD_ALIGN_LEFT,
        TIKU_RICHCMD_ALIGN_CENTER, TIKU_RICHCMD_ALIGN_RIGHT,
        TIKU_RICHCMD_ALIGN_JUSTIFY};
    size_t i;

    for (i = 0; i < 4; i++) {
        CHECK(execute(doc, &c, alignments[i]) == TIKU_RICH_OK);
        CHECK(query(doc, &c, alignments[i]).check == TIKU_RICHCMD_CHECK_ON);
        CHECK(doc->root->paragraphs[0]->value.paragraph.align ==
              (tiku_rich_align_t)i);
        CHECK(doc->root->paragraphs[1]->value.paragraph.align ==
              (tiku_rich_align_t)i);
        CHECK(!doc->root->paragraphs[2]->value.paragraph.present);
        CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    }
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[0]->value.list.restart == 7);
    CHECK(!doc->root->paragraphs[1]->value.list.restart);
    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richsnapshot_list(s, 1, &list, &ordinal) == TIKU_RICH_OK);
    CHECK(list.level == 1 && ordinal == 8);
    CHECK(tiku_richsnapshot_list(s, 2, &list, &ordinal) == TIKU_RICH_OK);
    CHECK(list.kind == TIKU_RICH_LIST_NONE && !ordinal);
    tiku_richsnapshot_release(s);
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_NO_CHANGE);
    command = (tiku_richcmd_t){.kind = TIKU_RICHCMD_PARAGRAPH_FORMAT,
        .value.paragraph = {.values = {.present = TIKU_RICH_SPACING,
                                        .before = 8},
                            .chars = {.present = TIKU_RICH_SIZE, .size = 20}}};
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[1]->value.paragraph.before == 8);
    CHECK(doc->root->paragraphs[1]->value.chars.size == 20);
    command.value.paragraph.values.present = 0;
    command.value.paragraph.chars.present = 0;
    command.value.paragraph.clear_props = TIKU_RICH_SPACING;
    command.value.paragraph.clear_chars = TIKU_RICH_SIZE;
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    CHECK(!doc->root->paragraphs[1]->value.paragraph.present);
    CHECK(!doc->root->paragraphs[1]->value.chars.present);
    c.last = c.first;
    CHECK(execute(doc, &c, TIKU_RICHCMD_ALIGN_CENTER) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[0]->value.paragraph.align == TIKU_RICH_CENTER);
    c.last = position(doc, 1, 3);
    CHECK(query(doc, &c, TIKU_RICHCMD_ALIGN_CENTER).check ==
          TIKU_RICHCMD_CHECK_MIXED);
    tiku_richdoc_release(doc);
}

/** @brief Styles, links and selective character overrides use their payloads. */
static void
styles_and_links(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richcmd_context_t c = {.first = position(doc, 0, 0),
                               .last = position(doc, 1, 3)};
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_LINK_SET,
                            .value.link = "https://example.org/doc"};
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    uint64_t style;

    CHECK(query(doc, &c, TIKU_RICHCMD_LINK_REMOVE).check ==
          TIKU_RICHCMD_CHECK_MIXED);
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    CHECK(query(doc, &c, TIKU_RICHCMD_LINK_SET).check == TIKU_RICHCMD_CHECK_ON);
    CHECK(!strcmp(doc->root->paragraphs[1]->value.runs[0].link,
                  command.value.link));
    CHECK(execute(doc, &c, TIKU_RICHCMD_LINK_REMOVE) == TIKU_RICH_OK);
    CHECK(query(doc, &c, TIKU_RICHCMD_LINK_REMOVE).status == TIKU_RICH_NO_CHANGE);
    command = (tiku_richcmd_t){.kind = TIKU_RICHCMD_CHARACTER_STYLE,
                             .value.style = doc->root->styles[1].value.id};
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[1]->value.runs[0].style == command.value.style);
    command = (tiku_richcmd_t){.kind = TIKU_RICHCMD_CHARACTER_FORMAT,
        .value.character = {.values = {.present = TIKU_RICH_SIZE, .size = 18},
                            .clear = TIKU_RICH_BOLD}};
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    CHECK(tiku_rd_resolve(doc->root, 0, 0, &chars, &props) == TIKU_RICH_OK);
    CHECK(!chars.bold && chars.italic && chars.size == 18);
    command = (tiku_richcmd_t){.kind = TIKU_RICHCMD_STYLE_DEFINE,
        .value.definition = {.value = {.name = "Title",
            .chars = {.present = TIKU_RICH_SIZE, .size = 24}}}};
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    CHECK(doc->root->nstyles == 4);
    style = doc->root->styles[3].value.id;
    command.value.definition.style = style;
    command.value.definition.value.chars.size = 26;
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    CHECK(doc->root->styles[3].value.id == style);
    command = (tiku_richcmd_t){.kind = TIKU_RICHCMD_PARAGRAPH_STYLE,
                             .value.style = style};
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[1]->value.style == style);
    command.kind = TIKU_RICHCMD_STYLE_REMOVE;
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_INVALID_STYLE);
    CHECK(doc->root->nstyles == 4);
    CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    CHECK(doc->root->nstyles == 3);
    CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    CHECK(doc->root->styles[3].value.chars.size == 26);
    tiku_richdoc_release(doc);
}

/** @brief Text replacement, paragraph breaks and styled paste share history. */
static void
text_and_fragments(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richcmd_context_t c = {.generation = doc->generation,
        .first = position(doc, 0, 1), .last = position(doc, 1, 2)};
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_REPLACE_TEXT,
                             .value.text = {"X\r\nY", 4}};
    tiku_richsnapshot_t *s;
    tiku_richfragment_t *f;
    tiku_richanchor_t *anchor;
    tiku_rich_pos_t got;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    CHECK(tiku_richfragment_create(s, c.first, c.last, &f) == TIKU_RICH_OK);
    CHECK(tiku_richanchor_create(doc, c.first, &anchor) == TIKU_RICH_OK);
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    expect_text(doc, "oX\nYo\nlast");
    CHECK(tiku_richanchor_get(anchor, &got) == TIKU_RICH_OK);
    CHECK(got.paragraph == doc->root->paragraphs[1]->value.id && got.byte == 1);
    CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_STALE);
    c.generation = 0;
    CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    CHECK(doc->root == s->root && !tiku_richdoc_dirty(doc));
    CHECK(tiku_richanchor_get(anchor, &got) == TIKU_RICH_OK);
    CHECK(got.paragraph == c.first.paragraph && got.byte == c.first.byte);
    CHECK(execute(doc, &c, TIKU_RICHCMD_PARAGRAPH_BREAK) == TIKU_RICH_OK);
    expect_text(doc, "o\no\nlast");
    CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    CHECK(execute(doc, &c, TIKU_RICHCMD_DELETE) == TIKU_RICH_OK);
    expect_text(doc, "oo\nlast");
    CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    command = (tiku_richcmd_t){.kind = TIKU_RICHCMD_PASTE_FRAGMENT,
                             .value.fragment = f};
    c.first = c.last = position(doc, 2, 4);
    CHECK(tiku_richcmd_execute(doc, &c, &command) == TIKU_RICH_OK);
    expect_text(doc, "one\ntwo\nlastne\ntw");
    CHECK(doc->root->paragraphs[2]->value.runs[1].chars.bold);
    CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    CHECK(doc->root == s->root);
    tiku_richanchor_release(anchor);
    tiku_richfragment_release(f);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Invalid payloads and operation-count overflow preserve live state. */
static void
refusals(void)
{
    tiku_richdoc_t *doc = fixture();
    tiku_richcmd_context_t c = {.first = position(doc, 0, 0),
                               .last = position(doc, 1, 3)};
    tiku_richcmd_t commands[] = {
        {.kind = TIKU_RICHCMD_CHARACTER_FORMAT,
         .value.character.values = {.present = TIKU_RICH_SIZE, .size = NAN}},
        {.kind = TIKU_RICHCMD_PARAGRAPH_FORMAT,
         .value.paragraph.clear_props = 32u},
        {.kind = TIKU_RICHCMD_CHARACTER_STYLE, .value.style = UINT64_MAX},
        {.kind = TIKU_RICHCMD_PARAGRAPH_STYLE,
         .value.style = doc->root->styles[1].value.id},
        {.kind = TIKU_RICHCMD_LIST_SET,
         .value.list = {TIKU_RICH_LIST_BULLET, 0, 7}},
        {.kind = TIKU_RICHCMD_LINK_SET, .value.link = "javascript:alert(1)"},
        {.kind = TIKU_RICHCMD_REPLACE_TEXT, .value.text = {NULL, 1}},
        {.kind = TIKU_RICHCMD_PASTE_FRAGMENT},
        {.kind = TIKU_RICHCMD_STYLE_DEFINE},
        {.kind = TIKU_RICHCMD_STYLE_REMOVE}
    };
    tiku_rd_root_t *root = doc->root;
    size_t used = tiku_richdoc_memory(doc), i;
    uint64_t generation = doc->generation, next_id = doc->next_id;

    for (i = 0; i < sizeof commands / sizeof commands[0]; i++) {
        CHECK(tiku_richcmd_execute(doc, &c, &commands[i]) != TIKU_RICH_OK);
        CHECK(doc->root == root && doc->generation == generation);
        CHECK(doc->next_id == next_id && tiku_richdoc_memory(doc) == used);
        CHECK(!doc->undo && !doc->redo && !tiku_richdoc_dirty(doc));
    }
    tiku_richdoc_release(doc);
}

/** @brief Large selections count affected operations, excluding a zero end. */
static void
operation_limit(void)
{
    tiku_richdoc_t *doc;
    tiku_rich_paragraph_source_t *p = calloc(1025, sizeof *p);
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 1025};
    tiku_richcmd_context_t c;
    size_t i, before;

    CHECK(p);
    for (i = 0; i < 1025; i++) {
        p[i].text = "x";
        p[i].length = 1;
    }
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    free(p);
    c = (tiku_richcmd_context_t){.first = position(doc, 0, 0),
                                .last = position(doc, 1024, 1)};
    before = tiku_richdoc_memory(doc);
    CHECK(query(doc, &c, TIKU_RICHCMD_BOLD).status == TIKU_RICH_LIMIT);
    CHECK(query(doc, &c, TIKU_RICHCMD_DELETE).status == TIKU_RICH_LIMIT);
    CHECK(query(doc, &c, TIKU_RICHCMD_REPLACE_TEXT).status == TIKU_RICH_LIMIT);
    CHECK(query(doc, &c, TIKU_RICHCMD_PASTE_FRAGMENT).enabled);
    CHECK(execute(doc, &c, TIKU_RICHCMD_ALIGN_LEFT) == TIKU_RICH_LIMIT);
    CHECK(before == tiku_richdoc_memory(doc) && !doc->undo);
    c.last.byte = 0;
    CHECK(query(doc, &c, TIKU_RICHCMD_BOLD).enabled);
    CHECK(execute(doc, &c, TIKU_RICHCMD_ALIGN_CENTER) == TIKU_RICH_OK);
    CHECK(doc->root->paragraphs[1023]->value.paragraph.align == TIKU_RICH_CENTER);
    CHECK(!doc->root->paragraphs[1024]->value.paragraph.present);
    CHECK(execute(doc, &c, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
    CHECK(!doc->undo && !tiku_richdoc_dirty(doc));
    c.last = position(doc, 1022, 1);
    CHECK(query(doc, &c, TIKU_RICHCMD_DELETE).enabled);
    CHECK(query(doc, &c, TIKU_RICHCMD_PARAGRAPH_BREAK).status == TIKU_RICH_LIMIT);
    c.last = position(doc, 1021, 1);
    CHECK(query(doc, &c, TIKU_RICHCMD_PARAGRAPH_BREAK).enabled);
    tiku_richdoc_release(doc);
}

/** @brief Deterministic selections independent of document storage. */
static unsigned
random_step(unsigned *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}

/** @brief Convert the fixture's fixed flat-text offset to a live position. */
static tiku_rich_pos_t
flat_position(tiku_richdoc_t *doc, size_t byte)
{
    if (byte <= 3)
        return position(doc, 0, byte);
    if (byte <= 7)
        return position(doc, 1, byte - 4);
    return position(doc, 2, byte - 8);
}

/** @brief Per-byte bold oracle: -1 means absent, 0 false and 1 true. */
typedef struct {
    int values[12];
    unsigned seed;
} oracle_t;

/** @brief Compare stored appearance to the independent per-character oracle. */
static void
expect_oracle(tiku_richdoc_t *doc, const oracle_t *oracle)
{
    size_t i, j, k, flat = 0;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    const tiku_rich_paragraph_t *p;

    for (i = 0; i < doc->root->count; i++) {
        p = &doc->root->paragraphs[i]->value;
        for (j = 0; j < p->run_count; j++) {
            CHECK(tiku_rd_resolve(doc->root, i, j, &chars, &props) ==
                  TIKU_RICH_OK);
            for (k = 0; k < p->runs[j].length; k++, flat++) {
                CHECK(flat < 12);
                CHECK(!!(chars.present & TIKU_RICH_BOLD) ==
                      (oracle->values[flat] >= 0));
                CHECK(chars.bold == (oracle->values[flat] == 1));
            }
        }
        flat++;
    }
    expect_text(doc, "one\ntwo\nlast");
}

/** @brief Apply one generated command and compare its state and exact undo. */
static void
generated_step(tiku_richdoc_t *doc, oracle_t *oracle)
{
    size_t first = random_step(&oracle->seed) % 13;
    size_t last = first + random_step(&oracle->seed) % (13 - first), i;
    unsigned pick = random_step(&oracle->seed) % 4;
    int value = -2, mixed = 0;
    oracle_t before = *oracle;
    tiku_richcmd_context_t context = {.first = flat_position(doc, first),
                                     .last = flat_position(doc, last)};
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_BOLD};
    tiku_richcmd_state_t state = query(doc, &context, command.kind);
    tiku_rich_status_t status;

    for (i = first; i < last; i++) {
        if (i == 3 || i == 7)
            continue;
        if (value == -2)
            value = oracle->values[i];
        else if (value != oracle->values[i])
            mixed = 1;
    }
    if (value == -2) {
        CHECK(!state.enabled && state.status == TIKU_RICH_NO_CHANGE);
        CHECK(tiku_richcmd_execute(doc, &context, &command) ==
              TIKU_RICH_NO_CHANGE);
        return;
    }
    CHECK(state.enabled);
    CHECK(state.check == (mixed ? TIKU_RICHCMD_CHECK_MIXED :
        value == -1 ? TIKU_RICHCMD_CHECK_UNSPECIFIED :
        value ? TIKU_RICHCMD_CHECK_ON : TIKU_RICHCMD_CHECK_OFF));
    value = mixed || value != 1;
    if (pick == 1)
        command.kind = TIKU_RICHCMD_CLEAR_CHARACTER_OVERRIDES;
    else if (pick > 1) {
        command.kind = TIKU_RICHCMD_CHARACTER_FORMAT;
        command.value.character.values.present = TIKU_RICH_BOLD;
        command.value.character.values.bold = pick == 3;
    }
    for (i = first; i < last; i++)
        if (i != 3 && i != 7)
            oracle->values[i] = pick == 0 ? value : pick == 1 ? -1 : pick == 3;
    status = tiku_richcmd_execute(doc, &context, &command);
    CHECK(status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE);
    expect_oracle(doc, oracle);
    if (status == TIKU_RICH_OK) {
        CHECK(execute(doc, &context, TIKU_RICHCMD_UNDO) == TIKU_RICH_OK);
        expect_oracle(doc, &before);
        CHECK(execute(doc, &context, TIKU_RICHCMD_REDO) == TIKU_RICH_OK);
        expect_oracle(doc, oracle);
    }
    tiku_richdoc_clear_history(doc);
}

/** @brief Exercise mixed states and formatting with a per-character oracle. */
static void
generated(void)
{
    tiku_richdoc_t *doc = fixture();
    oracle_t oracle = {.values = {1, 1, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                       .seed = 0xe28a310u};
    size_t i;

    for (i = 0; i < 1000; i++)
        generated_step(doc, &oracle);
    tiku_richdoc_release(doc);
}

/** @brief Run catalog, state, history, range and payload regression checks. */
int
main(void)
{
    catalog();
    toggles();
    availability();
    paragraphs();
    styles_and_links();
    text_and_fragments();
    refusals();
    operation_limit();
    generated();
    puts("PASS: rich document commands, catalog and 1000 formatting/state cases");
    return 0;
}
