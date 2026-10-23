/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_richdoc_fragments.c - styled transfer, ownership and paste history.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL fragments:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Convert a separator-inclusive offset to a snapshot position. */
static tiku_rich_pos_t
position(const tiku_richsnapshot_t *s, size_t byte, unsigned affinity)
{
    tiku_rich_paragraph_t p;
    size_t i;

    for (i = 0; i < tiku_richsnapshot_paragraphs(s); i++) {
        CHECK(tiku_richsnapshot_paragraph(s, i, &p) == TIKU_RICH_OK);
        if (byte <= p.length)
            return (tiku_rich_pos_t){tiku_richsnapshot_body(s), p.id, byte,
                affinity ? TIKU_RICH_AFTER : TIKU_RICH_BEFORE};
        byte -= p.length + 1;
    }
    CHECK(0);
    return (tiku_rich_pos_t){0};
}

/** @brief Capture the current document state. */
static tiku_richsnapshot_t *
snapshot(tiku_richdoc_t *doc)
{
    tiku_richsnapshot_t *s;

    CHECK(tiku_richdoc_snapshot(doc, &s) == TIKU_RICH_OK);
    return s;
}

/** @brief Compare complete text, including paragraph separators. */
static void
expect_text(const tiku_richsnapshot_t *s, const char *expected)
{
    char text[256];
    size_t length;

    CHECK(tiku_richsnapshot_text(s, text, sizeof text, &length) ==
          TIKU_RICH_OK);
    CHECK(length == strlen(expected) && !strcmp(text, expected));
}

/** @brief Capture a checked range of styled content. */
static tiku_richfragment_t *
capture(const tiku_richsnapshot_t *s, size_t first, size_t last)
{
    tiku_richfragment_t *f;

    CHECK(tiku_richfragment_create(s, position(s, first, 0),
        position(s, last, 1), &f) == TIKU_RICH_OK);
    return f;
}

/** @brief Import three paragraphs and a child-before-parent style table. */
static tiku_richdoc_t *
fixture(int destination)
{
    tiku_rich_style_source_t styles[4] = {
        {.name = "Body", .parent = 2,
         .paragraph = {.present = TIKU_RICH_ALIGNMENT,
                       .align = TIKU_RICH_CENTER}},
        {.name = "Base", .chars = {.present = TIKU_RICH_SIZE, .size = 12}},
        {.name = "Emphasis", .kind = TIKU_RICH_CHARACTER_STYLE,
         .chars = {.present = TIKU_RICH_ITALIC, .italic = 1}},
        {.name = "Unused"}
    };
    const tiku_rich_run_source_t run = {
        .length = 4, .style = 3, .link = "https://example.org/copied"
    };
    tiku_rich_paragraph_source_t p[3] = {
        {.text = "abcd", .length = 4, .style = 1,
         .runs = &run, .run_count = 1,
         .list = {TIKU_RICH_LIST_DECIMAL, 1, 7}},
        {.text = "EFGH", .length = 4},
        {.text = "ijkl", .length = 4, .style = 1}
    };
    tiku_rich_source_t source = {.styles = styles, .style_count = 4,
        .paragraphs = p, .paragraph_count = 3};
    tiku_richdoc_t *doc;

    if (destination) {
        styles[1].chars.size = 24;
        styles[0].paragraph.align = TIKU_RICH_RIGHT;
        p[0].list = (tiku_rich_list_t){TIKU_RICH_LIST_BULLET, 0, 0};
        p[2].chars = (tiku_rich_chars_t){.present = TIKU_RICH_BOLD, .bold = 1};
    }
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    return doc;
}

/** @brief Resolve the character at a local paragraph offset. */
static tiku_rich_chars_t
appearance(const tiku_richsnapshot_t *s, size_t para, size_t byte)
{
    tiku_rich_paragraph_t p;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    size_t i, at = 0;

    CHECK(tiku_richsnapshot_paragraph(s, para, &p) == TIKU_RICH_OK);
    for (i = 0; i < p.run_count; i++) {
        at += p.runs[i].length;
        if (byte < at) {
            CHECK(tiku_richsnapshot_resolve(s, para, i, &chars, &props) ==
                  TIKU_RICH_OK);
            return chars;
        }
    }
    CHECK(0);
    return (tiku_rich_chars_t){0};
}

/** @brief Captures own just selected content and cannot mark a document saved. */
static void
capture_and_lifetime(void)
{
    tiku_richdoc_t *doc = fixture(0);
    tiku_richsnapshot_t *s = snapshot(doc), *part;
    tiku_richfragment_t *f = capture(s, 1, 3), *empty, *separator;
    tiku_rich_paragraph_t p;
    tiku_rich_pos_t bad;
    tiku_richfragment_t *invalid = (void *)(uintptr_t)1;

    CHECK(tiku_richfragment_snapshot(f, &part) == TIKU_RICH_OK);
    expect_text(part, "bc");
    CHECK(tiku_richsnapshot_styles(part) == 3);
    CHECK(!tiku_richsnapshot_state(part));
    CHECK(tiku_richdoc_mark_saved(doc, part) == TIKU_RICH_INVALID);
    CHECK(atomic_load(&s->root->refs) == 2);
    CHECK(f->root->paragraphs[0] != s->root->paragraphs[0]);
    CHECK(tiku_richsnapshot_paragraph(part, 0, &p) == TIKU_RICH_OK);
    CHECK(p.runs[0].link == s->root->paragraphs[0]->value.runs[0].link);
    empty = capture(s, 2, 2);
    CHECK(empty->empty && !empty->root->nstyles);
    separator = capture(s, 4, 5);
    CHECK(!separator->empty && separator->root->count == 2);
    CHECK(!separator->root->bytes);
    bad = position(s, 0, 0);
    bad.body++;
    CHECK(tiku_richfragment_create(s, bad, bad, &invalid) ==
          TIKU_RICH_INVALID_RANGE && !invalid);
    CHECK(tiku_richfragment_create(s, position(s, 3, 0), position(s, 1, 0),
          &invalid) == TIKU_RICH_INVALID_RANGE && !invalid);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
    tiku_richfragment_release(f);
    expect_text(part, "bc");
    CHECK(!strcmp(p.runs[0].link, "https://example.org/copied"));
    tiku_richsnapshot_release(part);
    tiku_richfragment_release(empty);
    tiku_richfragment_release(separator);
}

/** @brief Boundary paragraphs retain their properties; text retains appearance. */
static void
boundaries_and_styles(void)
{
    tiku_richdoc_t *src = fixture(0), *dst = fixture(1);
    tiku_richsnapshot_t *s = snapshot(src), *before = snapshot(dst), *after;
    tiku_richfragment_t *f = capture(s, 1, 12);
    tiku_rich_paragraph_t p;
    tiku_rich_chars_t chars, expected;
    tiku_rich_style_t style;
    size_t count, i;

    CHECK(tiku_richdoc_paste_fragment(dst, 0, position(before, 2, 0),
        position(before, 12, 1), f) == TIKU_RICH_OK);
    after = snapshot(dst);
    expect_text(after, "abbcd\nEFGH\nijkl");
    CHECK(tiku_richsnapshot_paragraphs(after) == 3);
    CHECK(tiku_richsnapshot_paragraph(after, 0, &p) == TIKU_RICH_OK);
    CHECK(p.id == before->root->paragraphs[0]->value.id);
    CHECK(p.style == before->root->paragraphs[0]->value.style);
    CHECK(p.list.kind == TIKU_RICH_LIST_BULLET);
    CHECK(p.run_count >= 2);
    CHECK(!strcmp(p.runs[1].link, "https://example.org/copied"));
    CHECK(p.runs[1].link != f->root->paragraphs[0]->value.runs[0].link);
    chars = appearance(after, 0, 0);
    expected = appearance(before, 0, 0);
    CHECK(tiku_rd_chars_equal(&chars, &expected));
    chars = appearance(after, 0, 2);
    expected = appearance(s, 0, 1);
    CHECK(tiku_rd_chars_equal(&chars, &expected));
    chars = appearance(after, 2, 0);
    expected = appearance(s, 2, 0);
    CHECK(tiku_rd_chars_equal(&chars, &expected));
    CHECK(!(chars.present & TIKU_RICH_BOLD));
    chars = appearance(after, 2, 2);
    expected = appearance(before, 2, 2);
    CHECK(tiku_rd_chars_equal(&chars, &expected) && chars.bold);
    CHECK(tiku_richsnapshot_paragraph(after, 2, &p) == TIKU_RICH_OK);
    CHECK(p.id != before->root->paragraphs[2]->value.id);
    CHECK(p.style == before->root->paragraphs[2]->value.style);
    count = tiku_richsnapshot_styles(after);
    CHECK(count == 6);
    for (i = 0; i < 4; i++) {
        CHECK(tiku_richsnapshot_style(after, i, &style) == TIKU_RICH_OK);
        CHECK(!strcmp(style.name, before->root->styles[i].name));
        CHECK(tiku_rd_chars_equal(&style.chars,
              &before->root->styles[i].value.chars));
    }
    CHECK(!strcmp(after->root->styles[4].name, "Base (paste 1)"));
    CHECK(!strcmp(after->root->styles[5].name, "Body (paste 1)"));
    CHECK(after->root->styles[5].value.parent == after->root->styles[4].value.id);
    CHECK(tiku_richdoc_paste_fragment(dst, 0, position(after, 0, 0),
        position(after, 0, 1), f) == TIKU_RICH_OK);
    CHECK(dst->root->nstyles == count);
    tiku_richsnapshot_release(after);
    CHECK(tiku_richdoc_undo(dst) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_undo(dst) == TIKU_RICH_OK);
    CHECK(!tiku_richdoc_dirty(dst) && dst->root == before->root);
    CHECK(tiku_richdoc_redo(dst) == TIKU_RICH_OK);
    after = snapshot(dst);
    expect_text(after, "abbcd\nEFGH\nijkl");
    tiku_richfragment_release(f);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(src);
    CHECK(!strcmp(after->root->paragraphs[0]->value.runs[1].link,
                  "https://example.org/copied"));
    tiku_richsnapshot_release(after);
    tiku_richsnapshot_release(before);
    tiku_richdoc_release(dst);
}

/** @brief Full paragraphs keep source styles; empty fragments delete ranges. */
static void
full_and_empty(void)
{
    tiku_richdoc_t *src = fixture(0), *dst;
    tiku_richsnapshot_t *s = snapshot(src), *before, *after;
    tiku_richfragment_t *f = capture(s, 0, 14), *empty = capture(s, 0, 0);
    tiku_rich_paragraph_t p;
    tiku_rich_pos_t start, end;
    uint64_t generation;

    CHECK(tiku_richdoc_create(NULL, &dst) == TIKU_RICH_OK);
    before = snapshot(dst);
    start = position(before, 0, 0);
    CHECK(tiku_richdoc_paste_fragment(dst, 0, start, start, f) == TIKU_RICH_OK);
    after = snapshot(dst);
    expect_text(after, "abcd\nEFGH\nijkl");
    CHECK(tiku_richsnapshot_styles(after) == 3);
    CHECK(tiku_richsnapshot_paragraph(after, 0, &p) == TIKU_RICH_OK);
    CHECK(p.style && p.runs[0].style && !p.runs[0].chars.present);
    CHECK(p.list.kind == TIKU_RICH_LIST_DECIMAL && p.list.restart == 7);
    CHECK(!p.paragraph.present && !p.chars.present);
    generation = tiku_richdoc_generation(dst);
    end = position(after, 14, 1);
    CHECK(tiku_richdoc_paste_fragment(dst, generation - 1, start, end, f) ==
          TIKU_RICH_STALE);
    tiku_richdoc_set_read_only(dst, 1);
    CHECK(tiku_richdoc_paste_fragment(dst, 0, start, end, f) ==
          TIKU_RICH_READ_ONLY);
    tiku_richdoc_set_read_only(dst, 0);
    CHECK(tiku_richdoc_paste_fragment(dst, 0, end, start, f) ==
          TIKU_RICH_INVALID_RANGE);
    CHECK(tiku_richdoc_paste_fragment(dst, 0, start, end, NULL) ==
          TIKU_RICH_INVALID);
    CHECK(tiku_richdoc_paste_fragment(dst, 0, start, start, empty) ==
          TIKU_RICH_NO_CHANGE);
    CHECK(tiku_richdoc_generation(dst) == generation);
    CHECK(tiku_richdoc_paste_fragment(dst, 0, start, end, empty) ==
          TIKU_RICH_OK);
    CHECK(!dst->root->bytes && dst->root->count == 1);
    CHECK(tiku_richdoc_undo(dst) == TIKU_RICH_OK && dst->root == after->root);
    tiku_richsnapshot_release(before);
    tiku_richsnapshot_release(after);
    tiku_richsnapshot_release(s);
    tiku_richfragment_release(f);
    tiku_richfragment_release(empty);
    tiku_richdoc_release(src);
    tiku_richdoc_release(dst);
}

/** @brief Conflicting long Unicode style names truncate at scalar boundaries. */
static void
unicode_names(void)
{
    char name[64];
    tiku_rich_style_source_t style = {0};
    tiku_rich_paragraph_source_t p = {.text = "a\xc3\xa9z",
                                     .length = 4, .style = 1};
    tiku_rich_source_t source = {.styles = &style, .style_count = 1,
        .paragraphs = &p, .paragraph_count = 1};
    tiku_richdoc_t *src, *dst;
    tiku_richsnapshot_t *s, *before, *after;
    tiku_richfragment_t *f, *bad;
    tiku_rich_pos_t at;
    size_t i;

    for (i = 0; i < 31; i++) {
        name[2 * i] = '\xc3';
        name[2 * i + 1] = '\xa9';
    }
    name[62] = 0;
    memcpy(style.name, name, 63);
    CHECK(tiku_richdoc_create(NULL, &src) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_create(NULL, &dst) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(src, &source, 0) == TIKU_RICH_OK);
    style.chars = (tiku_rich_chars_t){.present = TIKU_RICH_BOLD, .bold = 1};
    CHECK(tiku_richdoc_import(dst, &source, 0) == TIKU_RICH_OK);
    s = snapshot(src);
    before = snapshot(dst);
    CHECK(tiku_richfragment_create(s, position(s, 2, 0), position(s, 3, 0),
          &bad) == TIKU_RICH_INVALID_RANGE && !bad);
    f = capture(s, 1, 3);
    at = position(before, 0, 0);
    CHECK(tiku_richdoc_paste_fragment(dst, 0, at, at, f) == TIKU_RICH_OK);
    after = snapshot(dst);
    expect_text(after, "\xc3\xa9" "a\xc3\xa9z");
    CHECK(dst->root->nstyles == 2);
    CHECK(tiku_rd_name(dst->root->styles[1].name));
    CHECK(strlen(dst->root->styles[1].name) == 62);
    CHECK(strstr(dst->root->styles[1].name, " (paste 1)"));
    tiku_richsnapshot_release(after);
    tiku_richsnapshot_release(before);
    tiku_richsnapshot_release(s);
    tiku_richfragment_release(f);
    tiku_richdoc_release(src);
    tiku_richdoc_release(dst);
}

/** @brief Generate repeatable independent flat-offset choices. */
static unsigned
random_step(unsigned *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}

/** @brief Check a tracked anchor against a flat offset in the captured state. */
static void
expect_anchor(tiku_richanchor_t *anchor, const tiku_richsnapshot_t *s,
              size_t offset, unsigned affinity)
{
    tiku_rich_pos_t got, wanted = position(s, offset, affinity);

    CHECK(tiku_richanchor_get(anchor, &got) == TIKU_RICH_OK);
    CHECK(got.paragraph == wanted.paragraph && got.byte == wanted.byte);
    CHECK(got.body == wanted.body && got.affinity == wanted.affinity);
}

/** @brief Compare character appearance using independent flat-text slices. */
static void
expect_appearance(const tiku_richsnapshot_t *before,
    const tiku_richsnapshot_t *after, size_t a, size_t b, size_t c, size_t d)
{
    const char *original = "abcd\nEFGH\nijkl";
    size_t i, old, count = 14 - (d - c) + b - a;
    tiku_rich_pos_t first, last;
    tiku_rich_chars_t wanted, got;

    for (i = 0; i < count; i++) {
        old = i < c ? i : i < c + b - a ? a + i - c : d + i - c - (b - a);
        if (original[old] == '\n')
            continue;
        first = position(before, old, 0);
        last = position(after, i, 0);
        wanted = appearance(before, tiku_rd_find(before->root, first.paragraph),
                            first.byte);
        got = appearance(after, tiku_rd_find(after->root, last.paragraph),
                         last.byte);
        CHECK(tiku_rd_chars_equal(&wanted, &got));
    }
}

/** @brief Independent text and anchor oracle for one generated styled paste. */
static void
generated_step(tiku_richdoc_t *doc, const tiku_richsnapshot_t *s,
               unsigned *seed)
{
    const char *text = "abcd\nEFGH\nijkl";
    size_t a = random_step(seed) % 15, b = a + random_step(seed) % (15 - a);
    size_t c = random_step(seed) % 15, d = c + random_step(seed) % (15 - c);
    size_t i, offsets[8], mapped[8];
    tiku_richfragment_t *f = capture(s, a, b);
    tiku_richanchor_t *anchors[8];
    tiku_richsnapshot_t *after;
    tiku_rich_status_t status;
    char expected[64];

    memcpy(expected, text, c);
    memcpy(expected + c, text + a, b - a);
    strcpy(expected + c + b - a, text + d);
    for (i = 0; i < 8; i++) {
        offsets[i] = random_step(seed) % 15;
        CHECK(tiku_richanchor_create(doc, position(s, offsets[i], i & 1),
                                    &anchors[i]) == TIKU_RICH_OK);
        if (offsets[i] < c)
            mapped[i] = offsets[i];
        else if (offsets[i] > d || (c != d && offsets[i] == d))
            mapped[i] = offsets[i] - d + c + b - a;
        else
            mapped[i] = c + ((i & 1) ? b - a : 0);
    }
    status = tiku_richdoc_paste_fragment(doc, 0, position(s, c, 0),
                                        position(s, d, 1), f);
    CHECK(status == TIKU_RICH_OK || (status == TIKU_RICH_NO_CHANGE &&
                                   a == b && c == d));
    after = snapshot(doc);
    expect_text(after, expected);
    expect_appearance(s, after, a, b, c, d);
    for (i = 0; i < 8; i++)
        expect_anchor(anchors[i], after, mapped[i], i & 1);
    if (status == TIKU_RICH_OK) {
        CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
        CHECK(doc->root == s->root && !tiku_richdoc_dirty(doc));
        for (i = 0; i < 8; i++)
            expect_anchor(anchors[i], s, offsets[i], i & 1);
        CHECK(tiku_richdoc_redo(doc) == TIKU_RICH_OK);
        for (i = 0; i < 8; i++)
            expect_anchor(anchors[i], after, mapped[i], i & 1);
        CHECK(tiku_richdoc_undo(doc) == TIKU_RICH_OK);
    }
    for (i = 0; i < 8; i++)
        tiku_richanchor_release(anchors[i]);
    tiku_richsnapshot_release(after);
    tiku_richfragment_release(f);
    tiku_richdoc_clear_history(doc);
}

/** @brief Repeat capture, self-paste and exact undo with a flat-string oracle. */
static void
generated(void)
{
    tiku_richdoc_t *doc = fixture(0);
    tiku_richsnapshot_t *s = snapshot(doc);
    unsigned seed = 0x51aab39u, i;

    for (i = 0; i < 1000; i++)
        generated_step(doc, s, &seed);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
}

/** @brief Rich paste accepts paragraph counts beyond the primitive-edit cap. */
static void
many_paragraphs(void)
{
    tiku_rich_paragraph_source_t *p = calloc(1100, sizeof *p);
    tiku_rich_source_t source = {.paragraphs = p, .paragraph_count = 1100};
    tiku_richdoc_t *src, *dst;
    tiku_richsnapshot_t *s, *before;
    tiku_richfragment_t *f;
    tiku_rich_pos_t at;

    CHECK(p && tiku_richdoc_create(NULL, &src) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_create(NULL, &dst) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(src, &source, 0) == TIKU_RICH_OK);
    free(p);
    s = snapshot(src);
    before = snapshot(dst);
    f = capture(s, 0, 1099);
    at = position(before, 0, 0);
    CHECK(tiku_richdoc_paste_fragment(dst, 0, at, at, f) == TIKU_RICH_OK);
    CHECK(dst->root->count == 1100 && !dst->root->bytes);
    CHECK(tiku_richdoc_undo(dst) == TIKU_RICH_OK && !tiku_richdoc_can_undo(dst));
    CHECK(dst->root == before->root);
    CHECK(tiku_richdoc_redo(dst) == TIKU_RICH_OK && dst->root->count == 1100);
    tiku_richsnapshot_release(before);
    tiku_richsnapshot_release(s);
    tiku_richfragment_release(f);
    tiku_richdoc_release(src);
    tiku_richdoc_release(dst);
}

/** @brief Small captures allocate only intersecting runs and selected text. */
static void
small_capture(void)
{
    tiku_rich_run_source_t *runs = calloc(1000, sizeof *runs);
    char text[1001];
    tiku_rich_paragraph_source_t p = {.text = text, .length = 1000,
                                     .runs = runs, .run_count = 1000};
    tiku_rich_source_t source = {.paragraphs = &p, .paragraph_count = 1};
    tiku_richdoc_t *doc;
    tiku_richsnapshot_t *s;
    tiku_richfragment_t *f;
    size_t i, before;

    CHECK(runs);
    memset(text, 'x', 1000);
    text[1000] = 0;
    for (i = 0; i < 1000; i++) {
        runs[i].length = 1;
        runs[i].chars.present = TIKU_RICH_BOLD;
        runs[i].chars.bold = i & 1;
    }
    CHECK(tiku_richdoc_create(NULL, &doc) == TIKU_RICH_OK);
    CHECK(tiku_richdoc_import(doc, &source, 0) == TIKU_RICH_OK);
    free(runs);
    s = snapshot(doc);
    before = tiku_richdoc_memory(doc);
    doc->memory->limit = before + 4096;
    f = capture(s, 500, 501);
    CHECK(f->root->runs == 1 && f->root->bytes == 1);
    CHECK(tiku_richdoc_memory(doc) < before + 4096);
    tiku_richsnapshot_release(s);
    tiku_richdoc_release(doc);
    CHECK(atomic_load(&f->memory->used) < 4096);
    tiku_richfragment_release(f);
}

/** @brief Check styled capture, cross-document paste, Unicode and history. */
int
main(void)
{
    capture_and_lifetime();
    boundaries_and_styles();
    full_and_empty();
    unicode_names();
    generated();
    many_paragraphs();
    small_capture();
    puts("PASS: rich document fragments, styles and 1000 paste/anchor cases");
    return 0;
}
