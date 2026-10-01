/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_style.c - a code view coloured as BASIC or C: the runs
 * the syntax kit gives, painted in the theme's inks; a C remark carried
 * across lines; the kept line states right after random edits and read
 * again only as far as a change reaches; a line of hundreds of runs; a
 * long line coloured only as far as it is drawn; and a line that cannot
 * be coloured drawn plain rather than half done.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_codeview_private.h"
#include "tiku_editdoc_private.h"

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

#define W 420
#define H 120
#define PAPER TIKU_RGB(255, 0, 255)

static const tiku_rect_t box = { 0, 0, 420, 120 };

/** @brief A view of a new document holding @p s; the view holds the only
 *         reference. */
static tiku_codeview_t *
view_of(const char *s, tiku_syntax_lang_t lang)
{
    tiku_editdoc_t *doc = NULL;
    tiku_codeview_t *v = NULL;

    if (tiku_editdoc_create(NULL, &doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(doc, s, strlen(s)) != TIKU_EDIT_OK ||
        tiku_codeview_create(doc, &v) != TIKU_EDIT_OK)
        exit(1);
    tiku_editdoc_release(doc);
    tiku_codeview_set_bounds(v, box);
    tiku_codeview_set_focus(v, 0);
    tiku_codeview_set_language(v, lang);
    return v;
}

/** @brief A surface of W by H, all PAPER. */
static tiku_surface_t *
surface(void)
{
    tiku_surface_t *s = tiku_surface_new(W, H, PAPER);

    if (s == NULL)
        exit(1);
    return s;
}

static int
same(const tiku_surface_t *a, const tiku_surface_t *b)
{
    return memcmp(a->px, b->px, (size_t)a->w * a->h * sizeof *a->px) == 0;
}

/** @brief Draw the lines of @p text as the syntax kit tells them apart,
 *         each run in its ink, the C state carried from line to line. */
static void
reference(tiku_surface_t *s, const char *text, tiku_syntax_lang_t lang)
{
    const tiku_font_t *f = tiku_font_mono(0);
    tiku_syntax_state_t state = TIKU_SYNTAX_OPEN;
    int y = 3, step = f->height + 2;
    char line[256];

    tiku_fill(s, box, TIKU_C_DOC);
    tiku_clip_set(s, (tiku_rect_t){ 5, 0, box.w - 5, box.h });
    while (*text && y < box.h) {
        size_t n = strcspn(text, "\n");
        tiku_span_t spans[256];
        int count, i, x = 5;
        size_t at = 0;

        memcpy(line, text, n);
        line[n] = '\0';
        count = tiku_syntax_spans_on(lang, line, state, &state, spans, 256);
        for (i = 0; i < count; i++) {
            char piece[256];

            memcpy(piece, line + at, (size_t)spans[i].len);
            piece[spans[i].len] = '\0';
            tiku_text(s, f, x, y + f->ascent, piece, tiku_ink(spans[i].ink));
            x += tiku_text_width(f, piece);
            at += (size_t)spans[i].len;
        }
        if (at < n)
            tiku_text(s, f, x, y + f->ascent, line + at, TIKU_C_TEXT);
        text += n + (text[n] == '\n');
        y += step;
    }
    tiku_clip_reset(s);
}

/** @brief BASIC and C lines are painted in the runs' inks, C remarks
 *         carried from one line into the next. */
static void
colours(void)
{
    static const char basic[] = "10 PRINT \"hi\"\n20 GOTO 10 ' loop\n"
                                "A$ = STR$(1.5)";
    static const char c[] = "int a = 0x1F; /* open\nstill a remark\n"
                            "*/ char *s = \"x\"; // tail\n#include <x.h>";
    tiku_codeview_t *v = view_of(basic, TIKU_SYNTAX_BASIC);
    tiku_surface_t *got = surface(), *want = surface();

    tiku_codeview_paint(v, got);
    reference(want, basic, TIKU_SYNTAX_BASIC);
    check(same(got, want), "STY-001",
          "BASIC is painted in the inks the syntax kit gives its runs");
    tiku_codeview_destroy(v);
    v = view_of(c, TIKU_SYNTAX_C);
    tiku_codeview_paint(v, got);
    reference(want, c, TIKU_SYNTAX_C);
    check(same(got, want), "STY-001",
          "and C, a remark opened on one line colouring the next");
    tiku_codeview_set_language(v, (tiku_syntax_lang_t)77);
    tiku_codeview_paint(v, got);
    reference(want, c, TIKU_SYNTAX_NONE);
    check(tiku_codeview_language(v) == TIKU_SYNTAX_NONE && same(got, want),
          "STY-001", "a language that is not one is no colouring at all");
    tiku_codeview_destroy(v);
    tiku_surface_free(got);
    tiku_surface_free(want);
}

/** @brief The runs of @p line coloured from scratch: every line above it
 *         read from the top. */
static size_t
scratch_runs(tiku_codeview_t *v, int64_t line, tiku_span_t *out, size_t max)
{
    tiku_editdoc_t *doc = tiku_codeview_doc(v);
    tiku_syntax_state_t state = TIKU_SYNTAX_OPEN;
    char buf[512];
    int64_t i;

    for (i = 0; i <= line; i++) {
        tiku_edit_range_t r;
        size_t n = 0;

        r.start = tiku_editdoc_line_start(doc, i);
        r.end = tiku_editdoc_line_end(doc, i);
        if (tiku_editdoc_read(doc, r, buf, sizeof buf, &n) != TIKU_EDIT_OK)
            return (size_t)-1;
        if (i == line)
            return (size_t)tiku_syntax_spans_on(TIKU_SYNTAX_C, buf, state,
                                                &state, out, (int)max);
        (void)tiku_syntax_spans_on(TIKU_SYNTAX_C, buf, state, &state, out,
                                   (int)max);
    }
    return 0;
}

/** @brief Replace [s, e) with @p text. */
static void
put(tiku_editdoc_t *doc, int64_t s, int64_t e, const char *text)
{
    tiku_edit_replace_t r = { { s, e }, text, strlen(text) };
    tiku_edit_transaction_t t;

    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    (void)tiku_editdoc_apply(doc, &t);
}

/** @brief After every one of hundreds of random edits, the runs the view
 *         colours a line with are the runs read from the top. */
static void
model(void)
{
    static const char *const pieces[] = {
        "/*", "*/", "x", "\n", "// c", "\"s\"", "\n\n", "a /* b */ c",
        "", "1", "\r\n", "'*/'"
    };
    char text[2000];
    tiku_codeview_t *v;
    tiku_editdoc_t *doc;
    unsigned seed = 99;
    int i, k, agree = 1;

    text[0] = '\0';
    for (i = 0; i < 40; i++)
        strcat(text, i % 7 == 3 ? "/* a\n" : i % 7 == 5 ? "b */ c\n" :
                                  "int x = 1;\n");
    v = view_of(text, TIKU_SYNTAX_C);
    doc = tiku_codeview_doc(v);
    for (i = 0; i < 400 && agree; i++) {
        int64_t len = tiku_editdoc_length(doc), s, e, lines;

        seed = seed * 1103515245u + 12345u;
        s = (int64_t)(seed >> 8) % (len + 1);
        seed = seed * 1103515245u + 12345u;
        e = s + (int64_t)(seed >> 8) % 6;
        if (e > len)
            e = len;
        while (!tiku_editdoc_is_caret(doc, s))
            s--;
        while (!tiku_editdoc_is_caret(doc, e))
            e--;
        if (e < s)
            e = s;
        seed = seed * 1103515245u + 12345u;
        if ((seed >> 9) % 9 == 0)
            (void)tiku_editdoc_undo(doc);
        else
            put(doc, s, e, pieces[(seed >> 12) % 12]);
        lines = tiku_editdoc_lines(doc);
        seed = seed * 1103515245u + 12345u;
        for (k = 0; k < lines + 3; k++) {
            tiku_span_t want[256];
            size_t n;
            int64_t line;

            if (k < 3) {
                seed = seed * 1103515245u + 12345u;
                line = (int64_t)(seed >> 8) % lines;
            } else {
                line = k - 3;
            }
            const tiku_span_t *got;
            size_t got_n = 0;

            n = scratch_runs(v, line, want, 256);
            got = tiku_cv_style_line(v, line,
                                     tiku_editdoc_line_end(doc, line),
                                     &got_n);
            if (got == NULL || got_n != n ||
                (n > 0 && memcmp(got, want, n * sizeof want[0]) != 0)) {
                agree = 0;
                printf("         edit %d line %lld\n", i, (long long)line);
            }
        }
    }
    check(agree, "STY-002", "after each of 400 random edits and undos, "
          "three lines asked for at random and then every line are "
          "coloured as if read from the top");
    tiku_codeview_destroy(v);
}

/** @brief A change that leaves the states below it as they were stops
 *         the reading there; one that opens a remark reads on. */
static void
convergence(void)
{
    char *text = malloc(10000 * 12 + 1);
    tiku_codeview_t *v;
    tiku_editdoc_t *doc;
    const tiku_span_t *runs;
    size_t n;
    int i;

    if (text == NULL)
        exit(1);
    text[0] = '\0';
    for (i = 0; i < 10000; i++)
        strcat(text + (size_t)i * 11, "int x = 1;\n");
    v = view_of(text, TIKU_SYNTAX_C);
    doc = tiku_codeview_doc(v);
    check(tiku_cv_style_line(v, 9999, INT64_MAX, &n) != NULL &&
          v->sty_valid == 9999, "STY-003",
          "the first time, every line above is read once");
    put(doc, tiku_editdoc_line_start(doc, 5000),
        tiku_editdoc_line_start(doc, 5000), "x = 2;\n");
    check(v->sty_valid == 5000 &&
          tiku_cv_style_line(v, 5002, INT64_MAX, &n) != NULL &&
          v->sty_valid == 10000, "STY-003",
          "after a change that leaves the states as they were, the line "
          "past it starts as kept and every kept state below is right "
          "again");
    put(doc, tiku_editdoc_line_start(doc, 100),
        tiku_editdoc_line_start(doc, 100), "/*\n");
    runs = tiku_cv_style_line(v, 102, INT64_MAX, &n);
    check(runs != NULL && v->sty_valid == 102 && n == 1 &&
          runs[0].ink == TIKU_INK_REMARK, "STY-003",
          "a remark left open reads on, and colours the lines below it");
    tiku_codeview_destroy(v);
    free(text);
}

/** @brief A line of more than 128 runs is coloured to its end. */
static void
many_runs(void)
{
    char line[2000];
    tiku_codeview_t *v;
    const tiku_span_t *runs;
    size_t n;
    int i;

    line[0] = '\0';
    for (i = 0; i < 300; i++)
        strcat(line, "A 1 ");
    v = view_of(line, TIKU_SYNTAX_BASIC);
    runs = tiku_cv_style_line(v, 0, INT64_MAX, &n);
    check(runs != NULL && n > 128 && runs[n - 2].ink == TIKU_INK_NUM,
          "STY-004",
          "a line of hundreds of runs keeps all of them to its end");
    tiku_codeview_destroy(v);
}

/** @brief Each byte's ink from @p n runs, over @p len bytes; bytes past
 *         the runs are left as they were. */
static void
inks(const tiku_span_t *runs, size_t n, tiku_ink_t *out, size_t len)
{
    size_t i, at = 0;
    int k;

    for (i = 0; i < n; i++) {
        for (k = 0; k < runs[i].len && at < len; k++)
            out[at++] = runs[i].ink;
    }
}

/** @brief A long line coloured only as far as is drawn has, up to there,
 *         the colours of the whole line, wherever the cut falls and
 *         whether its runs were kept or read again. */
static void
long_cuts(void)
{
    static const char unit[] = "int a = 0x1F; s = \"q /* no */\"; "
                               "/* rem */ b /= 2; ";
    static const int64_t uptos[] = {
        0, 100, 4097, 9000, 2000, 15000, 14990, 1, 7777
    };
    size_t len = 0, i, n, k;
    char *line = malloc(300 * sizeof unit + 16);
    tiku_ink_t *whole, *part;
    tiku_span_t *table;
    tiku_codeview_t *v;
    int agree = 1;

    if (line == NULL)
        exit(1);
    for (i = 0; i < 300; i++) {
        memcpy(line + len, unit, sizeof unit - 1);
        len += sizeof unit - 1;
    }
    memcpy(line + len, " // end", 8);
    len += 7;
    whole = malloc(len * sizeof *whole);
    part = malloc(len * sizeof *part);
    table = malloc(len * sizeof *table);
    if (whole == NULL || part == NULL || table == NULL)
        exit(1);
    for (i = 0; i < len; i++)
        whole[i] = TIKU_INK_PLAIN;
    n = (size_t)tiku_syntax_spans_on(TIKU_SYNTAX_C, line, TIKU_SYNTAX_OPEN,
                                     NULL, table, (int)len);
    inks(table, n, whole, len);
    v = view_of(line, TIKU_SYNTAX_C);
    for (k = 0; k < sizeof uptos / sizeof uptos[0] + 400; k++) {
        int64_t upto = k < 400 ? (int64_t)k : uptos[k - 400];
        const tiku_span_t *runs = tiku_cv_style_line(v, 0, upto, &n);

        for (i = 0; i < len; i++)
            part[i] = (tiku_ink_t)-1;
        if (runs == NULL) {
            agree = 0;
            continue;
        }
        inks(runs, n, part, len);
        for (i = 0; i <= (size_t)upto && i < len; i++) {
            if (part[i] != whole[i])
                agree = 0;
        }
    }
    check(agree, "STY-007", "a long line coloured up to where it is drawn "
          "has, up to there, the whole line's colours");
    {
        tiku_editdoc_t *doc = tiku_codeview_doc(v);
        tiku_edit_replace_t r = { { 10, 10 }, "/*", 2 };
        tiku_edit_transaction_t t;
        const tiku_span_t *runs;
        char *again = malloc(len + 3);

        memset(&t, 0, sizeof t);
        t.edits = &r;
        t.count = 1;
        if (again == NULL || tiku_editdoc_apply(doc, &t) != TIKU_EDIT_OK)
            exit(1);
        memcpy(again, line, 10);
        memcpy(again + 10, "/*", 2);
        memcpy(again + 12, line + 10, len - 10 + 1);
        for (i = 0; i < len; i++)
            whole[i] = TIKU_INK_PLAIN;
        n = (size_t)tiku_syntax_spans_on(TIKU_SYNTAX_C, again,
                                         TIKU_SYNTAX_OPEN, NULL, table,
                                         (int)len);
        inks(table, n, whole, len);
        runs = tiku_cv_style_line(v, 0, 5000, &n);
        for (i = 0; i < len; i++)
            part[i] = (tiku_ink_t)-1;
        agree = runs != NULL;
        if (runs != NULL)
            inks(runs, n, part, len);
        for (i = 0; i <= 5000 && agree; i++)
            agree = part[i] == whole[i];
        check(agree, "STY-007", "and after the line changes, the colours "
              "are read again from the changed text");
        free(again);
    }
    tiku_codeview_destroy(v);
    free(line);
    free(whole);
    free(part);
    free(table);
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

/** @brief With no room to colour a line it is drawn plain, whole; once
 *         there is room it is coloured again. */
static void
plain_when_short(void)
{
    static const char text[] = "10 PRINT \"hi\"";
    tiku_ed_allocator_t failing = { t_alloc, t_realloc, free };
    tiku_codeview_t *v = view_of(text, TIKU_SYNTAX_BASIC);
    tiku_surface_t *got = surface(), *want = surface();

    tiku_cv_style_free(v);
    tiku_ed_test_allocator(&failing);
    refuse = 1;
    tiku_codeview_paint(v, got);
    refuse = 0;
    tiku_ed_test_allocator(NULL);
    reference(want, text, TIKU_SYNTAX_NONE);
    check(same(got, want), "STY-005",
          "a line that cannot be coloured is drawn whole in the document's "
          "ink");
    tiku_codeview_paint(v, got);
    reference(want, text, TIKU_SYNTAX_BASIC);
    check(same(got, want), "STY-005", "and coloured once it can be");
    tiku_codeview_destroy(v);
    tiku_surface_free(got);
    tiku_surface_free(want);
}

/** @brief The selection's ink overrides the colours; a whitespace mark
 *         takes the colour of the run it is in, faint. */
static void
selection_and_marks(void)
{
    tiku_codeview_t *v = view_of("PRINT \"a b\"", TIKU_SYNTAX_BASIC);
    tiku_surface_t *got = surface();
    const tiku_font_t *f = tiku_font_mono(0);
    int c = tiku_font_mono_cell(0), mid = 3 + f->height / 2, x, y, plain = 1;

    tiku_codeview_set_options(v, TIKU_CODEVIEW_INVISIBLES);
    tiku_codeview_paint(v, got);
    check(tiku_peek(got, 5 + 8 * c + c / 2, mid) ==
          tiku_dim(TIKU_C_CODE_STR, TIKU_C_DOC), "STY-006",
          "a space inside a string is marked in the string's ink, faint");
    tiku_codeview_select(v, 0, 11);
    tiku_codeview_set_options(v, 0);
    tiku_codeview_paint(v, got);
    for (y = 3; y < 3 + f->height; y++) {
        for (x = 5; x < 5 + 11 * c; x++) {
            tiku_rgb_t p = tiku_peek(got, x, y);

            if (p == TIKU_C_CODE_WORD || p == TIKU_C_CODE_STR)
                plain = 0;
        }
    }
    check(plain, "STY-006", "selected text is drawn in the selection's ink, "
          "whatever it means");
    tiku_surface_free(got);
    tiku_codeview_destroy(v);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    colours();
    model();
    convergence();
    many_runs();
    long_cuts();
    plain_when_short();
    selection_and_marks();
    printf("%s\n", fails ? "FAIL: code view colouring" :
           "PASS: code view colouring -- BASIC and C inks, carried remarks, "
           "kept states, convergence, long lines, plain fallback");
    return fails != 0;
}
