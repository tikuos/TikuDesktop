/* Documentation Markdown conversion and bounded malformed input.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "tiku_markdown.h"
#include "tiku_doc.h"

static char html[32768];
static tiku_doc_t doc;

static void convert(const char *source)
{
    assert(tiku_markdown_html(html, sizeof html, source));
    tiku_doc_init(&doc); tiku_doc_read(&doc, html);
}

static void formatting(void)
{
    convert("# Intro\r\n\r\nHello **bold** and *soft*, `a < b`, café.  \r\nNext\\\nline\n\n"
            "Title\n=====\n\n## Repeat\n## Repeat\n###### Small\n\n"
            "> Quote\n> continued\n\n---\n\n- first\n  - child\n- last\n\n"
            "1. one\n2. two\n");
    assert(strstr(html, "<b>bold</b>") && strstr(html, "<em>soft</em>"));
    assert(strstr(html, "<code>a &lt; b</code>") && strstr(html, "<br>"));
    assert(strstr(html, "id=\"repeat-1\"") && strstr(html, "<h3 id=\"small\">"));
    assert(strstr(html, "<ul><li>first<ul><li>child</li></ul></li><li>last"));
    assert(strstr(html, "<blockquote><p>Quote continued</p></blockquote>"));
    assert(strstr(html, "<ol><li>one</li><li>two</li></ol>"));
    assert(!strcmp(tiku_doc_title(&doc), "Intro"));
    assert(doc.nanchor == 5 && !(doc.full & TIKU_DOC_FULL_DEPTH));
    convert("snake_case_value and \\*literal\\* and **unclosed\n");
    assert(strstr(html, "snake_case_value and *literal* and **unclosed"));
    convert("## Same\n## Same-1\n## Same\n");
    assert(strstr(html, "id=\"same-2\""));
}

static void code_and_links(void)
{
    convert("```basic\r\n  PRINT \"<&amp;>\"\r\n\tEND\r\n```\r\n\n"
            "~~~\n<script>literal</script>\n~~~\n\n"
            "[**Next**](folder/topic.md#here) ![a \"b\"](picture.png)\n"
            "[parens](part(one).md) [title](index.html \"Contents\")\n"
            "[space](<a b.md>) [escaped](part\\(one\\).md)\n");
    assert(strstr(html, "<pre>  PRINT &quot;&lt;&amp;amp;&gt;&quot;\n\tEND\n</pre>"));
    assert(strstr(html, "&lt;script&gt;literal&lt;/script&gt;"));
    assert(!strcmp(tiku_doc_link_href(&doc, 0), "folder/topic.md#here"));
    assert(!strcmp(tiku_doc_link_href(&doc, 1), "part(one).md"));
    assert(!strcmp(tiku_doc_link_href(&doc, 2), "index.html"));
    assert(!strcmp(tiku_doc_link_href(&doc, 3), "a b.md"));
    assert(!strcmp(tiku_doc_link_href(&doc, 4), "part(one).md"));
    assert(doc.nimage == 1);
    convert("<script>visible</script> &amp; [broken](abc\n");
    assert(strstr(html, "&lt;script&gt;visible&lt;/script&gt; &amp;amp; [broken](abc"));
    convert("````\n```\nno closing fence");
    assert(!strcmp(html, "<pre>```\nno closing fence</pre>"));
}

static void tables_and_limits(void)
{
    convert("| Name | Value |\n| :--- | ---: |\n| **clock** | 64 |\n| a\\|b | x |\n");
    assert(strstr(html, "<th>Name</th><th>Value</th>"));
    int cells = 0;
    for (int i = 0; i < doc.nblock; i++) cells += doc.block[i].kind == TIKU_DOC_BLOCK_CELL;
    assert(strstr(html, "<td>a|b</td>") && cells == 6);
    char small[16]; memset(small, 42, sizeof small);
    assert(!tiku_markdown_html(small, 8, "**too much output**"));
    assert(!small[0] && small[8] == 42);
    assert(tiku_markdown_html(small, 1, "") && !small[0]);
    assert(!tiku_markdown_html(small, 1, "x") && !small[0]);
    assert(!tiku_markdown_html(NULL, 0, "x"));
    convert("x"); size_t n = strlen(html);
    assert(tiku_markdown_html(small, n+1, "x"));
    assert(!tiku_markdown_html(small, n, "x"));
}

/* Deterministic malformed spans exercise delimiter handling under sanitizers. */
static void malformed(void)
{
    static const char chars[] = "abc #*_~`[]()!<>\\\n\r|:-\"\t&";
    uint32_t state = 187;
    char source[300], output[8192];
    for (int trial = 0; trial < 6000; trial++) {
        size_t n = (size_t)trial % sizeof source;
        for (size_t i = 0; i < n; i++) {
            state = state * 1664525u + 1013904223u;
            source[i] = chars[state % (sizeof chars-1)];
        }
        source[n] = 0;
        assert(tiku_markdown_html(output, sizeof output, source));
        char tiny[33]; memset(tiny, 42, sizeof tiny);
        int ok = tiku_markdown_html(tiny, 32, source);
        assert(tiny[32] == 42);
        if (!ok) assert(!tiny[0]);
    }
}

/** @brief Seconds on the monotonic clock, for the work-budget bound. */
static double seconds(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/**
 * @brief A long run of one delimiter is answered, not ground through.
 *
 * The run was counted again at every position inside it, so 400 KB of
 * stars took 41 seconds with the budget untouched -- and the reader
 * converts twice, once to size and once to write.  The bound is loose
 * on purpose: the answer takes about 30 ms, and anything under two
 * seconds means the count is charged.
 */
static void pathological(void)
{
    size_t n = 400000;
    char *run = malloc(n + 1);
    double t0, spent;

    assert(run != NULL);
    for (size_t k = 0; k < n; k++) {
        run[k] = '*';
    }
    run[n - 1] = 'x';           /* the run never finds its closing pair */
    run[n] = '\0';
    t0 = seconds();
    (void)tiku_markdown_html_size(run);
    spent = seconds() - t0;
    assert(spent < 2.0);
    memset(run, '`', n);
    run[n - 1] = 'x';
    run[n] = '\0';
    t0 = seconds();
    (void)tiku_markdown_html_size(run);
    spent = seconds() - t0;
    assert(spent < 2.0);
    for (size_t k = 0; k + 4 <= n; k += 4) {
        memcpy(run + k, "# a\n", 4);   /* every heading wants the same id */
    }
    run[n] = '\0';
    t0 = seconds();
    (void)tiku_markdown_html_size(run);
    spent = seconds() - t0;
    assert(spent < 2.0);
    free(run);
}

int main(void)
{
    formatting(); code_and_links(); tables_and_limits(); malformed();
    pathological();
    puts("PASS: Markdown formatting, links, tables, literal code and bounded malformed input");
    return 0;
}
