/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_doc.c - a help page read and fitted to a width.
 *
 * Most of what is pinned here is the agreement between the two tables a
 * page is: the pieces the painter draws and the pieces the hit test
 * searches are the same pieces, and a point below the last line is on
 * nothing.  The rest is the reading: what a tag this dialect does not
 * know does to the text inside it, and what a cap that fills says.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "tiku_doc.h"
#include "tiku_font.h"

static int fails;
static tiku_doc_t doc;

static void
check(int cond, const char *what)
{
    printf("  [%s] doc    %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief Every piece of the page, in order, as one string. */
static const char *
words(const tiku_doc_t *d)
{
    static char out[4096];
    int at = 0;

    for (int i = 0; i < tiku_doc_piece_count(d); i++) {
        const tiku_doc_piece_t *pc = tiku_doc_piece(d, i);

        if (at + pc->len + 2 >= (int)sizeof out) break;
        memcpy(out + at, tiku_doc_text(d, pc->off), (size_t)pc->len);
        at += pc->len;
        out[at++] = '|';
    }
    out[at] = 0;
    return out;
}

/** @brief Every piece of the page run together, as it would be read. */
static const char *
sentence(const tiku_doc_t *d)
{
    static char out[4096];
    int at = 0;

    for (int i = 0; i < tiku_doc_piece_count(d); i++) {
        const tiku_doc_piece_t *pc = tiku_doc_piece(d, i);

        if (at + pc->len + 1 >= (int)sizeof out) break;
        memcpy(out + at, tiku_doc_text(d, pc->off), (size_t)pc->len);
        at += pc->len;
    }
    out[at] = 0;
    return out;
}

/** @brief How many blocks of the page say it was cut. */
static int
notices(const tiku_doc_t *d)
{
    int n = 0;

    for (int i = 0; i < tiku_doc_block_count(d); i++) {
        if (tiku_doc_block(d, i)->kind == TIKU_DOC_BLOCK_NOTICE) n++;
    }
    return n;
}

static int asked_for_a_source;

static int
image_size(void *ctx, const char *src, int *w, int *h)
{
    (void)ctx;
    asked_for_a_source++;
    if (strcmp(src, "known.png")) return 0;
    *w = 64;
    *h = 32;
    return 1;
}

int
main(void)
{
    tiku_font_set_size(12);

    printf("\nan empty page is a page, and it is empty\n");
    {
        tiku_doc_init(&doc);
        check(tiku_doc_read(&doc, "") == 1, "nothing read is not a failure");
        check(tiku_doc_block_count(&doc) == 0, "and it has no blocks");
        check(tiku_doc_layout(&doc, 400) == 0, "it is no pixels tall");
        check(tiku_doc_title(&doc) == NULL, "and it is not called anything");
        check(tiku_doc_hit(&doc, 0, 0).kind == TIKU_DOC_HIT_NONE,
              "a point on a page with nothing laid out is on nothing");
    }

    printf("\nthe words, with the entities decoded and the space collapsed\n");
    {
        tiku_doc_read(&doc, "<p>  one   two\n\tthree  </p>");
        tiku_doc_layout(&doc, 400);
        check(strcmp(words(&doc), "one two three|") == 0,
              "runs of blank space are one space, and the ends are trimmed");
        tiku_doc_read(&doc, "<p>&amp;&lt;&gt;&quot;&apos;&#65;&#x42;&nbsp;C</p>");
        tiku_doc_layout(&doc, 400);
        check(strcmp(words(&doc), "&<>\"'AB C|") == 0,
              "every entity this dialect knows, and nbsp as a space");
        tiku_doc_read(&doc, "<p>a &notathing; b</p>");
        tiku_doc_layout(&doc, 400);
        check(strstr(words(&doc), "&notathing;") != NULL,
              "and one it does not know reads as itself, ampersand and all");
    }

    printf("\nwhat a tag does, and what it does not\n");
    {
        tiku_doc_read(&doc, "<p>plain <b>bold</b> <code>mono</code> "
                            "<i>said</i></p>");
        check(tiku_doc_block_count(&doc) == 1, "one paragraph");
        check(tiku_doc_run(&doc, 1)->face == TIKU_MEASURE_BOLD,
              "bold is the bold face");
        check(tiku_doc_run(&doc, 2)->face == TIKU_MEASURE_MONO &&
              tiku_doc_run(&doc, 2)->ink == TIKU_DOC_INK_CODE,
              "code is the mono face and the code ink");
        check(tiku_doc_run(&doc, 3)->ink == TIKU_DOC_INK_EMPHASIS,
              "and emphasis is an ink, there being no italic face");
        tiku_doc_read(&doc, "<p>a <span class=x>b</span> "
                            "<table><tr><td>c</td></tr></table></p>");
        tiku_doc_layout(&doc, 400);
        check(strstr(words(&doc), "b") && strstr(words(&doc), "c"),
              "a tag this dialect does not know keeps its words");
        tiku_doc_read(&doc, "<p>a</p><script>var x = 1;</script>"
                            "<style>p{}</style><!-- gone --><p>b</p>");
        tiku_doc_layout(&doc, 400);
        check(!strstr(words(&doc), "var") && !strstr(words(&doc), "gone") &&
              !strstr(words(&doc), "p{}"),
              "script, style and a comment lose their words as well");
        check(strstr(words(&doc), "a") && strstr(words(&doc), "b"),
              "and the page around them is unharmed");
    }

    printf("\nblocks: headings, items, rules and what is quoted\n");
    {
        tiku_doc_read(&doc,
            "<h1>Title</h1><h2 id=\"two\">Second</h2>"
            "<ul><li>first<li>second</ul>"
            "<ol><li>one<li>two</ol>"
            "<pre>  kept  spaces\nand a line</pre>"
            "<blockquote>said</blockquote><hr>");
        check(strcmp(tiku_doc_title(&doc), "Title") == 0,
              "the first heading names the page");
        int items = 0, ordered = 0, pre = 0, quote = 0, rule = 0;
        for (int i = 0; i < tiku_doc_block_count(&doc); i++) {
            const tiku_doc_block_t *b = tiku_doc_block(&doc, i);

            if (b->kind == TIKU_DOC_BLOCK_ITEM) {
                items++;
                if (b->index) ordered++;
            }
            if (b->kind == TIKU_DOC_BLOCK_PRE) pre++;
            if (b->kind == TIKU_DOC_BLOCK_QUOTE) quote++;
            if (b->kind == TIKU_DOC_BLOCK_RULE) rule++;
        }
        check(items == 4 && ordered == 2,
              "four items, and the two in the ordered list are numbered");
        check(pre == 1 && quote == 1 && rule == 1,
              "a pre, a quote and a rule are blocks of their own");
        tiku_doc_layout(&doc, 400);
        check(strstr(words(&doc), "  kept  spaces") != NULL,
              "the space inside pre is the page's own");
        check(tiku_doc_anchor_y(&doc, "two") > 0,
              "a name a link may reach is where its block is");
        check(tiku_doc_anchor_y(&doc, "nosuch") == TIKU_DOC_NONE,
              "and a name the page has not got is nowhere");
    }

    printf("\nlines: a width, and the same words on more of them\n");
    {
        const char *page = "<p>the quick brown fox jumps over the lazy dog "
                           "and keeps running until it is tired</p>";

        tiku_doc_read(&doc, page);
        tiku_doc_layout(&doc, 2000);
        int wide = tiku_doc_line_count(&doc);
        int tall = tiku_doc_height(&doc);

        tiku_doc_layout(&doc, 120);
        check(tiku_doc_line_count(&doc) > wide,
              "a narrower page is more lines");
        check(tiku_doc_height(&doc) > tall, "and a taller one");
        check(tiku_doc_check(&doc), "and the tables still agree");
        int ok = 1;
        for (int i = 0; i < tiku_doc_line_count(&doc); i++) {
            const tiku_doc_line_t *ln = tiku_doc_line(&doc, i);

            if (ln->x + ln->w > 120) ok = 0;
            if (i && ln->y < tiku_doc_line(&doc, i - 1)->y) ok = 0;
        }
        check(ok, "every line is inside the width and below the one before");
        /* Nothing may be dropped on the way: the words are the page's. */
        char joined[2048];
        int at = 0;
        for (int i = 0; i < tiku_doc_piece_count(&doc); i++) {
            const tiku_doc_piece_t *pc = tiku_doc_piece(&doc, i);

            memcpy(joined + at, tiku_doc_text(&doc, pc->off), (size_t)pc->len);
            at += pc->len;
        }
        joined[at] = 0;
        check(strstr(joined, "tired") != NULL && strstr(joined, "quick") != NULL,
              "and the first and last words are both still on it");
    }

    printf("\nan indented block is inside the width, indent and all\n");
    {
        const int width = 160;

        tiku_doc_read(&doc,
            "<ul><li><ul><li>a fairly long item that must wrap several "
            "times inside its own indent</li></ul></li></ul>"
            "<blockquote>a quoted paragraph that also has to wrap inside "
            "the room it was given</blockquote>"
            "<pre>a preformatted line that is longer than its width</pre>");
        tiku_doc_layout(&doc, width);
        int over = 0, indented = 0;
        for (int i = 0; i < tiku_doc_line_count(&doc); i++) {
            const tiku_doc_line_t *ln = tiku_doc_line(&doc, i);

            if (ln->x > 0) indented++;
            if (ln->x + ln->w > width) over++;
        }
        check(indented > 0, "the items, the quote and the pre are indented");
        check(over == 0, "and not one of their lines reaches past the width");
        check(tiku_doc_check(&doc), "with the tables still agreeing");
    }

    printf("\na paragraph of mixed faces wraps by each run's own face\n");
    {
        tiku_doc_read(&doc, "<p>some plain words <code>and_a_mono_word</code> "
                            "and more plain words after it</p>");
        tiku_doc_layout(&doc, 150);
        int mono = 0, plain = 0;
        for (int i = 0; i < tiku_doc_piece_count(&doc); i++) {
            int face = tiku_doc_run(&doc, tiku_doc_piece(&doc, i)->run)->face;

            if (face == TIKU_MEASURE_MONO) mono++;
            if (face == TIKU_MEASURE_PLAIN) plain++;
        }
        check(mono && plain, "both faces are on the page");
        /* Measured in the face it is drawn in, not in the body face. */
        int measured = 0;
        for (int i = 0; i < tiku_doc_piece_count(&doc); i++) {
            const tiku_doc_piece_t *pc = tiku_doc_piece(&doc, i);
            char buf[256];

            if (tiku_doc_run(&doc, pc->run)->face != TIKU_MEASURE_MONO) continue;
            memcpy(buf, tiku_doc_text(&doc, pc->off), (size_t)pc->len);
            buf[pc->len] = 0;
            if (pc->w == tiku_measure_width(TIKU_MEASURE_MONO, buf) &&
                pc->w != tiku_measure_width(TIKU_MEASURE_PLAIN, buf)) measured++;
        }
        check(measured > 0, "and the mono word is as wide as mono makes it");
        check(tiku_doc_check(&doc), "and the tables agree about all of it");
    }

    printf("\nlinks, and where they are\n");
    {
        tiku_doc_read(&doc,
            "<p>go to <a href=\"other.html\">the other page</a> or "
            "<a href=\"#two\">back up</a></p>");
        tiku_doc_layout(&doc, 400);
        check(tiku_doc_link_count(&doc) == 2, "two links");
        check(strcmp(tiku_doc_link_href(&doc, 0), "other.html") == 0 &&
              strcmp(tiku_doc_link_href(&doc, 1), "#two") == 0,
              "each with the address it was written with");
        int first = 0, n = 0;
        check(tiku_doc_link_pieces(&doc, 0, &first, &n) && n >= 1,
              "and pieces of its own to draw a ring around");
        tiku_rect_t r = tiku_doc_piece_rect(&doc, first);
        tiku_doc_hit_t h = tiku_doc_hit(&doc, r.x + r.w / 2, r.y + r.h / 2);
        check(h.kind == TIKU_DOC_HIT_LINK && h.link == 0,
              "a point in the middle of a link is that link");
        check(tiku_doc_link_next(&doc, TIKU_DOC_NONE, 0) == 0 &&
              tiku_doc_link_next(&doc, 0, 0) == 1 &&
              tiku_doc_link_next(&doc, 1, 0) == 0,
              "the walk forward reaches both and comes round");
        check(tiku_doc_link_next(&doc, TIKU_DOC_NONE, 1) == 1 &&
              tiku_doc_link_next(&doc, 0, 1) == 1,
              "and the walk back does too");
        /* What is underlined is the link, not the gap beside it. */
        int clean = 1;
        for (int link = 0; link < tiku_doc_link_count(&doc); link++) {
            int at = 0, count = 0;

            if (!tiku_doc_link_pieces(&doc, link, &at, &count)) continue;
            for (int i = at; i < at + count; i++) {
                const tiku_doc_piece_t *pc = tiku_doc_piece(&doc, i);
                const char *t = tiku_doc_text(&doc, pc->off);

                if (!pc->len) continue;
                if (t[0] == ' ' || t[pc->len - 1] == ' ') clean = 0;
            }
        }
        check(clean, "and a link's own words carry no space at either end");
    }

    printf("\nthe page reads as it was written, and no address is in it\n");
    {
        tiku_doc_read(&doc, "<p>go to <a href=\"other.html\">the other page"
                            "</a> for <b>more</b> of it</p>");
        tiku_doc_layout(&doc, 600);
        check(strstr(sentence(&doc), "other.html") == NULL,
              "an address is not drawn among the words it was written on");
        check(strcmp(sentence(&doc), "go to the other page for more of it") == 0,
              "and the space after a closing tag is still a space");
        tiku_doc_read(&doc, "<p>a <b>b</b> c <code>d</code> e</p>");
        tiku_doc_layout(&doc, 400);
        check(strcmp(sentence(&doc), "a b c d e") == 0,
              "with every inline tag, whatever it was");
    }

    printf("\na page that was not cut says nothing about being cut\n");
    {
        tiku_doc_read(&doc, "<h1>Help</h1><p>just a paragraph</p>");
        tiku_doc_layout(&doc, 400);
        check(notices(&doc) == 0,
              "a heading is how the page is fitted, not something lost");
        char wide[6000];
        int at = snprintf(wide, sizeof wide, "<p>");

        for (int i = 0; i < 400 && at < (int)sizeof wide - 24; i++) {
            at += snprintf(wide + at, sizeof wide - (size_t)at, "word%d ", i);
        }
        snprintf(wide + at, sizeof wide - (size_t)at, "</p>");
        tiku_doc_read(&doc, wide);
        tiku_doc_layout(&doc, 12);
        check(notices(&doc) == 1,
              "a width that fills the line table is a cut, and says so");
        tiku_doc_layout(&doc, 4000);
        check(notices(&doc) == 0,
              "and that cut does not outlive the width that caused it");
        check((tiku_doc_full(&doc) & 0x1ffu) == 0,
              "with nothing left claiming a table filled");
    }

    printf("\na link owns the words that lie against it\n");
    {
        tiku_doc_read(&doc,
            "<p><a href=\"one\">first <a href=\"two\">second</a> tail</p>");
        tiku_doc_layout(&doc, 400);
        int first = 0, n = 0, mine = 1;

        if (tiku_doc_link_pieces(&doc, 0, &first, &n)) {
            for (int i = first; i < first + n; i++) {
                if (tiku_doc_run(&doc, tiku_doc_piece(&doc, i)->run)->link != 0) {
                    mine = 0;
                }
            }
        }
        check(mine, "a page that left an anchor open lends it no other words");
    }

    printf("\na picture with no source is never asked about\n");
    {
        tiku_doc_images(&doc, image_size, NULL);
        asked_for_a_source = 0;
        tiku_doc_read(&doc, "<img alt=\"a map with no source\">");
        tiku_doc_layout(&doc, 400);
        check(asked_for_a_source == 0,
              "nothing is asked to size a picture that names no file");
        tiku_doc_read(&doc, "<img src=\"known.png\" alt=\"a map\">");
        tiku_doc_layout(&doc, 400);
        check(asked_for_a_source == 1, "and one that does is asked once");
        tiku_doc_images(&doc, NULL, NULL);
    }

    printf("\na point on nothing is on nothing\n");
    {
        tiku_doc_read(&doc, "<p>a short line</p>");
        tiku_doc_layout(&doc, 400);
        const tiku_doc_line_t *ln = tiku_doc_line(&doc, 0);

        check(tiku_doc_hit(&doc, ln->x + ln->w + 40,
                           ln->y + ln->h / 2).kind == TIKU_DOC_HIT_NONE,
              "the room left at the end of a line is not the last word");
        check(tiku_doc_hit(&doc, ln->x + 2,
                           tiku_doc_height(&doc) + 20).kind == TIKU_DOC_HIT_NONE,
              "and below the last line is not the line that would be next");
        check(tiku_doc_line_at(&doc, tiku_doc_height(&doc) + 20) ==
              TIKU_DOC_NONE, "which is what the line search says too");
    }

    printf("\na box too narrow still keeps every byte\n");
    {
        const char *page = "<p>wide word here</p>";

        for (int width = 0; width <= 6; width++) {
            char joined[512];
            int at = 0;

            tiku_doc_read(&doc, page);
            tiku_doc_layout(&doc, width);
            for (int i = 0; i < tiku_doc_piece_count(&doc); i++) {
                const tiku_doc_piece_t *pc = tiku_doc_piece(&doc, i);

                if (at + pc->len >= (int)sizeof joined) break;
                memcpy(joined + at, tiku_doc_text(&doc, pc->off),
                       (size_t)pc->len);
                at += pc->len;
            }
            joined[at] = 0;
            /* A space left at the end of a line is not drawn, so the
             * letters are what must all be there, in order. */
            int k = 0;
            for (int c = 0; joined[c]; c++) {
                if (joined[c] != ' ') joined[k++] = joined[c];
            }
            joined[k] = 0;
            if (strcmp(joined, "widewordhere")) {
                printf("         width %d gave \"%s\"\n", width, joined);
                fails++;
            }
            if (!tiku_doc_check(&doc)) {
                printf("         width %d left the tables disagreeing\n", width);
                fails++;
            }
        }
        check(1, "at every width from nothing to six pixels");
        check(tiku_doc_full(&doc) & TIKU_DOC_NARROW,
              "and the page says a word had to be broken");
    }

    printf("\na picture is measured by whoever can decode one\n");
    {
        tiku_doc_read(&doc, "<p>before</p><img src=\"known.png\" alt=\"a map\">"
                            "<p>after</p>");
        tiku_doc_images(&doc, image_size, NULL);
        tiku_doc_layout(&doc, 400);
        int block = TIKU_DOC_NONE;
        for (int i = 0; i < tiku_doc_block_count(&doc); i++) {
            if (tiku_doc_block(&doc, i)->kind == TIKU_DOC_BLOCK_IMAGE) block = i;
        }
        check(block != TIKU_DOC_NONE, "the picture is a block");
        check(tiku_doc_block_rect(&doc, block).h >= 32,
              "as tall as the size that was answered");
        tiku_doc_images(&doc, NULL, NULL);
        tiku_doc_read(&doc, "<img src=\"known.png\" alt=\"a map\">");
        tiku_doc_layout(&doc, 400);
        check(strstr(words(&doc), "a map") != NULL,
              "and with nobody to answer, the words it stands for are drawn");
    }

    printf("\na reader keeps their place across a change of width\n");
    {
        char page[2048];
        int at = 0;

        for (int i = 0; i < 12; i++) {
            at += snprintf(page + at, sizeof page - (size_t)at,
                           "<p>paragraph number %d with a few words in it</p>",
                           i);
        }
        tiku_doc_read(&doc, page);
        tiku_doc_layout(&doc, 600);
        int y = (int)tiku_doc_block(&doc, 7)->y;
        tiku_doc_place_t at_block = tiku_doc_place(&doc, y);

        check(at_block.block == 7, "where the reader is, is a block");
        tiku_doc_layout(&doc, 220);
        check(tiku_doc_place_y(&doc, at_block) ==
              (int)tiku_doc_block(&doc, 7)->y,
              "and the same block after the page was laid out again");
    }

    printf("\nlaying out again at the same width changes nothing\n");
    {
        tiku_doc_read(&doc, "<p>one two three four five six seven</p>");
        int h = tiku_doc_layout(&doc, 200);
        int lines = tiku_doc_line_count(&doc);

        check(tiku_doc_layout(&doc, 200) == h &&
              tiku_doc_line_count(&doc) == lines,
              "the same answer, and the same tables");
        tiku_font_set_size(16);
        check(tiku_doc_layout(&doc, 200) != h ||
              tiku_doc_line_count(&doc) != lines,
              "but a change of text size lays it out afresh");
        tiku_font_set_size(12);
    }

    printf("\nwhat a cap costs is said, and takes room\n");
    {
        char page[8192];
        int at = 0;

        for (int i = 0; i < 400 && at < (int)sizeof page - 32; i++) {
            at += snprintf(page + at, sizeof page - (size_t)at, "<p>x%d</p>", i);
        }
        check(tiku_doc_read(&doc, page) == 0, "a page too long to hold says so");
        const char *what = NULL;
        int cap = 0;
        check(tiku_doc_full_first(&doc, &what, &cap) && what && cap > 0,
              "and names the table that filled, and how big it is");
        check(strcmp(what, "blocks") == 0, "which was the block table");
        tiku_doc_layout(&doc, 400);
        int notice = 0;
        for (int i = 0; i < tiku_doc_block_count(&doc); i++) {
            if (tiku_doc_block(&doc, i)->kind == TIKU_DOC_BLOCK_NOTICE) notice++;
        }
        check(notice == 1, "the cut is a block of its own, not only a flag");
        check(tiku_doc_block_rect(&doc,
                  tiku_doc_block_count(&doc) - 1).h > 0,
              "with room on the page that nothing can paint over");
    }

    printf("\nheadings are the bold face until the ends can measure a size\n");
    {
        tiku_doc_read(&doc, "<h1>Heading</h1><p>words</p>");
        tiku_doc_layout(&doc, 400);
        check(((tiku_doc_full(&doc) & TIKU_DOC_FACE_FLAT) != 0) ==
              (tiku_measure_heading(1) == TIKU_MEASURE_BOLD),
              "a page reports flat headings only when its measuring end requires them");
        int bold = 0, of_heading = 0;
        for (int i = 0; i < tiku_doc_line_count(&doc); i++) {
            const tiku_doc_line_t *ln = tiku_doc_line(&doc, i);

            if (tiku_doc_block(&doc, ln->block)->kind != TIKU_DOC_BLOCK_H1) {
                continue;
            }
            for (int k = ln->piece0; k < ln->piece0 + ln->npiece; k++) {
                of_heading++;
                if (tiku_doc_run(&doc, tiku_doc_piece(&doc, k)->run)->face ==
                    tiku_measure_heading(1)) bold++;
            }
        }
        check(of_heading > 0 && bold == of_heading,
              "and every word of a heading uses the measured heading face");
        tiku_doc_read(&doc, "<p>words only</p>");
        tiku_doc_layout(&doc, 400);
        check(!(tiku_doc_full(&doc) & TIKU_DOC_FACE_FLAT),
              "and a page without one does not");
    }

    printf("\na table is read as cells, and laid out as columns\n");
    {
        static const char *const page =
            "<p>before</p>"
            "<table>"
            "<tr><th>Road</th><th>What it does</th></tr>"
            "<tr><td>Forms</td><td>declares rows bound to its own "
            "variables and publishes them under the namespace</td></tr>"
            "<tr><td>Drawing</td><td>opens a window</td></tr>"
            "</table>"
            "<p>after</p>";
        int cells = 0, headers = 0, bold = 1, rows = 0;
        int first = TIKU_DOC_NONE, second = TIKU_DOC_NONE;

        tiku_doc_read(&doc, page);
        for (int i = 0; i < tiku_doc_block_count(&doc); i++) {
            const tiku_doc_block_t *b = tiku_doc_block(&doc, i);

            if (b->kind != TIKU_DOC_BLOCK_CELL) continue;
            cells++;
            if (b->index + 1 > rows) rows = b->index + 1;
            if (b->flags & TIKU_DOC_BLOCK_HEADER) {
                headers++;
                if (tiku_doc_run(&doc, b->run0)->face != TIKU_MEASURE_BOLD) {
                    bold = 0;
                }
            }
            if (b->index == 1 && b->col == 0) first = i;
            if (b->index == 1 && b->col == 1) second = i;
        }
        check(cells == 6 && rows == 3 && headers == 2,
              "six cells over three rows, two of them the head");
        check(bold, "and a head cell's words are the bold face, which is "
                    "the face they were measured in");

        tiku_doc_layout(&doc, 400);
        check(tiku_doc_check(&doc),
              "the tables agree after a table is laid out");
        {
            const tiku_doc_block_t *a = tiku_doc_block(&doc, first);
            const tiku_doc_block_t *b = tiku_doc_block(&doc, second);
            tiku_rect_t ra = tiku_doc_block_rect(&doc, first);
            tiku_rect_t rb = tiku_doc_block_rect(&doc, second);

            check(first != TIKU_DOC_NONE && second != TIKU_DOC_NONE &&
                  a->y == b->y,
                  "two cells of one row begin at the same height");
            check(ra.x + ra.w <= rb.x,
                  "and the second begins to the right of the first, which "
                  "is what a column is");
            check(a->h == b->h,
                  "both are as tall as the row, so a rule under one is a "
                  "rule under all of them");
        }
        {
            /* The wide cell wraps, and its second line is under its own
             * column rather than under the page. */
            const tiku_doc_block_t *b = tiku_doc_block(&doc, second);
            int wrapped = b->nline > 1;
            int aligned = 1;

            for (int k = b->line0; k < b->line0 + b->nline; k++) {
                if (tiku_doc_line(&doc, k)->x !=
                    tiku_doc_line(&doc, b->line0)->x) aligned = 0;
            }
            check(wrapped && aligned,
                  "a cell too narrow for its words wraps inside its own "
                  "column");
        }
        {
            /* Every word of the table is reachable where it was drawn,
             * the second column included: the cells of a row share a
             * band of the page and are told apart by x alone. */
            int reached = 0, links = 0;

            for (int i = 0; i < tiku_doc_piece_count(&doc); i++) {
                tiku_rect_t r = tiku_doc_piece_rect(&doc, i);
                tiku_doc_hit_t h = tiku_doc_hit(&doc, r.x + r.w / 2,
                                                r.y + r.h / 2);

                if (h.piece == i) reached++;
            }
            check(reached == tiku_doc_piece_count(&doc),
                  "every piece of the page is hit where it was drawn");
            (void)links;
        }
        {
            /* A link in the second column: the one a reader of a table
             * is most likely to press, and the one a hit test that
             * stopped at the first line of a band could never reach. */
            tiku_doc_read(&doc,
                "<table><tr><td>left</td>"
                "<td><a href=\"other.html\">right</a></td></tr></table>");
            tiku_doc_layout(&doc, 300);
            int found = TIKU_DOC_NONE;

            for (int i = 0; i < tiku_doc_piece_count(&doc); i++) {
                tiku_rect_t r = tiku_doc_piece_rect(&doc, i);
                tiku_doc_hit_t h = tiku_doc_hit(&doc, r.x + 1, r.y + 1);

                if (h.kind == TIKU_DOC_HIT_LINK) found = h.link;
            }
            check(found != TIKU_DOC_NONE &&
                  !strcmp(tiku_doc_link_href(&doc, found), "other.html"),
                  "a link in the second column is followed from where it "
                  "is drawn");
        }
    }

    printf("\na table too wide for the columns there are is still read\n");
    {
        char page[512];
        int at = 0;

        at += snprintf(page + at, sizeof page - at, "<table><tr>");
        for (int c = 0; c < TIKU_DOC_COLS_MAX + 3; c++) {
            at += snprintf(page + at, sizeof page - at,
                           "<td>c%d</td>", c);
        }
        snprintf(page + at, sizeof page - at, "</tr></table>");
        tiku_doc_read(&doc, page);
        tiku_doc_layout(&doc, 400);
        check((tiku_doc_full(&doc) & TIKU_DOC_COLS_FOLDED) != 0,
              "a table past the last column says so");
        check(tiku_doc_check(&doc) &&
              strstr(sentence(&doc), "c10") != NULL,
              "and folds the rest into the last column rather than "
              "losing them");
        {
            /* Folded, not stacked on top of one another: two cells in
             * one column are drawn one under the other. */
            int over = 0;

            for (int i = 0; i < tiku_doc_line_count(&doc); i++) {
                for (int k = i + 1; k < tiku_doc_line_count(&doc); k++) {
                    const tiku_doc_line_t *a = tiku_doc_line(&doc, i);
                    const tiku_doc_line_t *b = tiku_doc_line(&doc, k);

                    if (a->x != b->x) continue;
                    if (a->y < b->y + b->h && b->y < a->y + a->h) over++;
                }
            }
            check(over == 0, "and no two of them are drawn over each other");
        }
    }

    printf("\na cell without its table, and a table without its rows\n");
    {
        int held = 1;
        static const char *const loose[] = {
            "<td>a cell nobody opened a table for</td>",
            "<tr><td>one</td><td>two</td></tr>",
            "<table><tr><td>never closed</td>",
            "<table></table>", "<table><tr></tr></table>",
            "<table><tr><td></td><td></td></tr></table>",
            "<table><tr><td colspan=\"3\">wide</td></tr>"
            "<tr><td>a</td><td>b</td><td>c</td></tr></table>",
            "<table><tr><td colspan=\"99\">too wide</td></tr></table>",
            "<table><tr><td colspan=\"x\">not a number</td></tr></table>"
        };

        for (size_t i = 0; i < sizeof loose / sizeof loose[0]; i++) {
            tiku_doc_read(&doc, loose[i]);
            for (int w = 1; w <= 400; w += 133) {
                tiku_doc_layout(&doc, w);
                if (!tiku_doc_check(&doc)) held = 0;
                (void)tiku_doc_hit(&doc, 5, 5);
            }
        }
        check(held, "every one of them leaves the tables agreeing");

        {
            /* A row of empty cells is a row: it takes its height and
             * gets its rule, because a cell with nothing in it is still
             * a place in the grid. */
            int blank, full;

            tiku_doc_read(&doc, "<table><tr><td></td><td></td></tr>"
                          "<tr><td>x</td><td>y</td></tr></table>");
            tiku_doc_layout(&doc, 300);
            blank = tiku_doc_height(&doc);
            tiku_doc_read(&doc, "<table><tr><td>x</td><td>y</td></tr>"
                          "</table>");
            tiku_doc_layout(&doc, 300);
            full = tiku_doc_height(&doc);
            check(blank > full,
                  "a row of empty cells takes the room of a row");
        }

        tiku_doc_read(&doc, "<table><tr><td colspan=\"2\">over both</td>"
                      "</tr><tr><td>a</td><td>b</td></tr></table>");
        tiku_doc_layout(&doc, 300);
        {
            int wide_cell = TIKU_DOC_NONE, right_cell = TIKU_DOC_NONE;
            for (int i = 0; i < doc.nblock; i++) {
                const tiku_doc_block_t *b = &doc.block[i];
                if (b->kind != TIKU_DOC_BLOCK_CELL) continue;
                if (b->index == 0 && b->span == 2) wide_cell = i;
                if (b->index == 1 && b->col == 1) right_cell = i;
            }
            tiku_rect_t wide = tiku_doc_block_rect(&doc, wide_cell);
            tiku_rect_t right = tiku_doc_block_rect(&doc, right_cell);

            check(wide.y < right.y && right.x > 0,
                  "a cell over two columns is a row of its own, and the "
                  "row under it still has two");
        }
    }

    printf("\nthe rule that a page goes down refuses one that does not\n");
    {
        int was;

        tiku_doc_read(&doc, "<p>one two three four five six seven</p>");
        tiku_doc_layout(&doc, 60);
        check(tiku_doc_line_count(&doc) > 1 && tiku_doc_check(&doc),
              "a paragraph over several lines agrees with itself");
        was = (int)doc.line[1].y;
        doc.line[1].y = doc.line[0].y - 1;
        check(!tiku_doc_check(&doc),
              "and a line put back above the one before it, inside the "
              "same block, is refused");
        doc.line[1].y = was;
        check(tiku_doc_check(&doc), "put back, it agrees again");
    }

    printf("\na page written wrong is still a page\n");
    {
        static const char *const nasty[] = {
            "<p>hello <b world", "<a href=\"never>text</a>", "a < b > c",
            "</b></p></ul>text", "text &am", "&;&#;&#x;",
            "&aaaaaaaaaaaaaaaaaaaaaaaaaaaa;", "a\x01\x02b",
            "a\xff\xfe b \xc3", "<b>one<i>two</b>three</i>",
            "<p></p><ul><li></li></ul><pre></pre><h1></h1>", "<img>",
            "<a href=p><img src=known.png alt=pic></a>", "<!", "<!-", "<!--"
        };
        int held = 1;

        for (size_t i = 0; i < sizeof nasty / sizeof nasty[0]; i++) {
            tiku_doc_read(&doc, nasty[i]);
            for (int w = 0; w <= 400; w += 97) {
                tiku_doc_layout(&doc, w);
                if (!tiku_doc_check(&doc)) held = 0;
                (void)tiku_doc_hit(&doc, 5, 5);
                for (int k = 0; k < tiku_doc_piece_count(&doc); k++) {
                    const tiku_doc_piece_t *pc = tiku_doc_piece(&doc, k);

                    if (pc->off < 0 ||
                        pc->off + pc->len > TIKU_DOC_TEXT_MAX) held = 0;
                }
            }
        }
        check(held, "an unterminated tag, a bad entity, a broken nest and "
                    "invalid UTF-8 all leave the tables agreeing");
    }

    printf("\n%s\n", fails ? "FAILURES" : "the page is where it was drawn");
    return fails ? 1 : 0;
}
