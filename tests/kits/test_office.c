/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_office.c - Word, OpenDocument and PowerPoint files translated to
 * PDF, held to what the PDF kit reads back: the pages each lays out to
 * and the words on the first, page sizes and turns, links, the outline
 * and the title; a PDF exported beside them.  Hostile files -- cut
 * short, not XML, nested past reason, sizes out of all measure, styles
 * based on each other -- are refused or laid out, never a crash or a
 * hang.  Documents come from tests/fixtures/office.inc; faces from the
 * machine, and without any the translating rows are skipped.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "tiku_fontset.h"
#include "tiku_office.h"
#include "tiku_pdf.h"

#include "../fixtures/office.inc"

static int fails;
static char scratch[] = "/tmp/test_office.XXXXXX";

/** @brief One row of the report, PASS or FAIL; a FAIL is counted. */
static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-7s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief Base64 decoded, to free(). */
static unsigned char *
unbase64(const char *b64, size_t *n)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char *p = malloc(strlen(b64) + 1);
    unsigned v = 0;
    int bits = 0;

    for (*n = 0; p != NULL && *b64 && *b64 != '='; b64++) {
        v = v << 6 | (unsigned)(strchr(abc, *b64) - abc);
        if ((bits += 6) >= 8) {
            bits -= 8;
            p[(*n)++] = (unsigned char)(v >> bits);
        }
    }
    return p;
}

/** @brief Fixture @p k written into the scratch folder; its path into
 *         @p path.  @return 0, or -1. */
static int
place(int k, char *path, size_t max)
{
    size_t n;
    unsigned char *b = unbase64(fixtures[k].data, &n);
    FILE *f;
    int ok;

    snprintf(path, max, "%s/%s%s", scratch, fixtures[k].name,
             fixtures[k].ext);
    f = b != NULL ? fopen(path, "wb") : NULL;
    ok = f != NULL && fwrite(b, 1, n, f) == n;
    if (f != NULL) {
        ok &= fclose(f) == 0;
    }
    free(b);
    return ok ? 0 : -1;
}

/** @brief The fixture called @p name, or -1. */
static int
named(const char *name)
{
    int k;

    for (k = 0; k < (int)(sizeof fixtures / sizeof fixtures[0]); k++) {
        if (strcmp(fixtures[k].name, name) == 0) {
            return k;
        }
    }
    return -1;
}

/** @brief Fixture @p name translated and read back as a PDF, or NULL. */
static tiku_pdf_t *
translated(const char *name)
{
    char path[512], why[256];
    size_t len = 0;
    unsigned char *b;
    tiku_pdf_t *doc = NULL;
    int k = named(name);

    if (k < 0 || place(k, path, sizeof path) != 0) {
        return NULL;
    }
    b = tiku_office_pdf(path, &len, why, sizeof why);
    if (b == NULL || tiku_pdf_decode(b, len, &doc) != TIKU_PDF_OK) {
        doc = NULL;
    }
    free(b);
    return doc;
}

/** @brief Whether every line of @p words is in @p text. */
static int
has_words(const char *text, const char *words, char *missing, size_t max)
{
    const char *w = words;

    missing[0] = '\0';
    while (w != NULL && *w) {
        const char *end = strchr(w, '\n');
        size_t n = end ? (size_t)(end - w) : strlen(w);
        char one[128];

        snprintf(one, sizeof one, "%.*s", (int)n, w);
        if (text == NULL || strstr(text, one) == NULL) {
            snprintf(missing, max, "%s", one);
            return 0;
        }
        w = end ? end + 1 : NULL;
    }
    return 1;
}

/* ---------------------------------------------------------------- kinds */

/** @brief Each file known by what it holds, never by its name. */
static void
kind_tests(void)
{
    static const struct { const char *name; tiku_office_kind_t kind; } k[] = {
        {"docx_features", TIKU_OFFICE_DOCX}, {"odt_features", TIKU_OFFICE_ODT},
        {"pptx_features", TIKU_OFFICE_PPTX}, {"not_office", TIKU_OFFICE_NONE},
        {"truncated", TIKU_OFFICE_NONE}};
    char path[512];
    size_t i;
    int ok = 1;

    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        int f = named(k[i].name);

        ok &= f >= 0 && place(f, path, sizeof path) == 0 &&
              tiku_office_kind(path) == k[i].kind;
    }
    check(ok && tiku_office_kind("/no/such/file.docx") == TIKU_OFFICE_NONE,
          "OF-001", "Word, OpenDocument and slides known by their parts; "
          "a ZIP of nothing, a file cut short, no file at all: none");
}

/* ---------------------------------------------------------- translation */

/** @brief Fixture @p k translated: refused when it must be, else the
 *         pages it lays out to and the words on the first. */
static void
fixture_test(int k)
{
    char path[512], why[256] = "", row[16], what[256], missing[128] = "";
    size_t len = 0;
    unsigned char *b = NULL;
    tiku_pdf_t *doc = NULL;
    char *text = NULL;
    clock_t t0 = clock();
    double secs;

    snprintf(row, sizeof row, "OF-%03d", 10 + k);
    if (place(k, path, sizeof path) == 0) {
        b = tiku_office_pdf(path, &len, why, sizeof why);
    }
    secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    if (b != NULL && tiku_pdf_decode(b, len, &doc) != TIKU_PDF_OK) {
        doc = NULL;
    }
    text = doc != NULL ? tiku_pdf_page_text(doc, 0) : NULL;
    if (fixtures[k].pages == 0) {
        snprintf(what, sizeof what, "%s: refused, saying why (%s)",
                 fixtures[k].name, why);
        check(b == NULL && why[0] != '\0' &&
              (strcmp(fixtures[k].name, "ole_locked") != 0 ||
               strstr(why, "before Office 2007") != NULL), row, what);
    } else if (fixtures[k].pages < 0) {
        snprintf(what, sizeof what, "%s: laid out or refused, not a crash, "
                 "in %.2f s", fixtures[k].name, secs);
        check((b == NULL ? why[0] != '\0' : doc != NULL) && secs < 10, row,
              what);
    } else {
        int ok = doc != NULL && tiku_pdf_pages(doc) == fixtures[k].pages &&
                 has_words(text, fixtures[k].words, missing, sizeof missing);

        snprintf(what, sizeof what, "%s: %d pages (%d), its words on the "
                 "first%s%s", fixtures[k].name, doc ? tiku_pdf_pages(doc) : 0,
                 fixtures[k].pages, missing[0] ? "; missing: " : "", missing);
        check(ok, row, what);
    }
    free(text);
    tiku_pdf_close(doc);
    free(b);
}

/* -------------------------------------------------------------- details */

/** @brief Whether page @p p of @p doc is @p w by @p h, give or take. */
static int
sized(tiku_pdf_t *doc, int p, double w, double h)
{
    double pw = 0, ph = 0;
    int rot = 0;

    return doc != NULL && tiku_pdf_page_size(doc, p, &pw, &ph, &rot) == 0 &&
           fabs(pw - w) < 1 && fabs(ph - h) < 1 && rot == 0;
}

/** @brief Whether the outline of @p doc has an entry holding @p title. */
static int
outlined(tiku_pdf_t *doc, const char *title)
{
    tiku_pdf_outline_t *o = NULL;
    int n = doc != NULL ? tiku_pdf_outline(doc, &o) : 0, i, found = 0;

    for (i = 0; i < n; i++) {
        found |= o[i].title != NULL && strstr(o[i].title, title) != NULL;
    }
    tiku_pdf_outline_free(o, n);
    return found;
}

/** @brief Page sizes, a turned page, links, the outline and the title. */
static void
detail_tests(void)
{
    tiku_pdf_t *w = translated("docx_features");
    tiku_pdf_t *o = translated("odt_features");
    tiku_pdf_t *s = translated("pptx_features");
    tiku_pdf_link_t *links = NULL;
    char *title = o != NULL ? tiku_pdf_title(o) : NULL;
    int n = w != NULL ? tiku_pdf_links(w, 0, &links) : 0, i, web = 0;

    check(sized(w, 0, 612, 792) && sized(o, 0, 595.3, 841.9) &&
          sized(o, 1, 841.9, 595.3) && sized(s, 0, 960, 540) &&
          sized(s, 2, 960, 540), "OF-030", "Letter, A4, A4 turned by its "
          "master page, and slides 16:9, as each file sets them");
    for (i = 0; i < n; i++) {
        web |= links[i].to.uri != NULL &&
               strcmp(links[i].to.uri, "https://example.com/") == 0;
    }
    tiku_pdf_links_free(links, n);
    check(web, "OF-031", "a hyperlink kept as a link to its address");
    check(outlined(w, "Word features") &&
          outlined(o, "OpenDocument features") &&
          outlined(s, "Second slide"), "OF-032",
          "headings and slide titles in the outline");
    check(title != NULL && strcmp(title, "ODT features") == 0, "OF-033",
          "the document's own title kept as the PDF's");
    free(title);
    title = s != NULL ? tiku_pdf_page_text(s, 2) : NULL;
    check(title != NULL && strstr(title, "Plan") != NULL &&
          strstr(title, "Build") != NULL, "OF-034",
          "a diagram drawn from the drawing PowerPoint saved with it");
    free(title);
    title = w != NULL ? tiku_pdf_page_text(w, 1) : NULL;
    check(title != NULL && strstr(title, "Plan") != NULL &&
          strstr(title, "Build") != NULL, "OF-035",
          "and Word's, its words set as a slide's are");
    free(title);
    tiku_pdf_close(w);
    tiku_pdf_close(o);
    tiku_pdf_close(s);
}

/* --------------------------------------------------------------- export */

/** @brief Whether @p path reads back as a PDF of @p pages pages. */
static int
pdf_of(const char *path, int pages)
{
    tiku_pdf_t *doc = NULL;
    int ok = tiku_pdf_open(path, &doc) == TIKU_PDF_OK &&
             tiku_pdf_pages(doc) == pages;

    tiku_pdf_close(doc);
    return ok;
}

/** @brief A PDF written beside the document: readable, replacing what was
 *         there, and nothing left behind where it cannot go. */
static void
export_tests(void)
{
    char src[512], out[512], bad[512], why[256] = "";
    struct stat st;
    FILE *f;
    int k = named("odt_features");

    snprintf(out, sizeof out, "%s/exported.pdf", scratch);
    snprintf(bad, sizeof bad, "%s/no/such/folder/x.pdf", scratch);
    if (k < 0 || place(k, src, sizeof src) != 0) {
        check(0, "OF-040", "the document to export could not be placed");
        return;
    }
    check(tiku_office_export(src, out, why, sizeof why) == 0 &&
          pdf_of(out, 2) && stat(out, &st) == 0 &&
          (st.st_mode & 0777) == 0644, "OF-040",
          "exported: a PDF of its two pages, readable by all");
    f = fopen(out, "w");
    if (f != NULL) {
        fputs("not a PDF", f);
        fclose(f);
    }
    check(tiku_office_export(src, out, why, sizeof why) == 0 &&
          pdf_of(out, 2), "OF-041", "exported over a file: replaced whole");
    why[0] = '\0';
    check(tiku_office_export(src, bad, why, sizeof why) != 0 &&
          why[0] != '\0', "OF-042", "exported where it cannot be written: "
          "refused, saying why");
    unlink(out);
}

/** @brief The scratch folder's files and the folder given back. */
static void
tidy(void)
{
    char path[512];
    int k;

    for (k = 0; k < (int)(sizeof fixtures / sizeof fixtures[0]); k++) {
        snprintf(path, sizeof path, "%s/%s%s", scratch, fixtures[k].name,
                 fixtures[k].ext);
        unlink(path);
    }
    rmdir(scratch);
}

/** @brief Every group; the translating ones with faces to set text in. */
int
main(void)
{
    int k;

    printf("Office: documents translated to PDF, read back by the PDF kit\n");
    if (mkdtemp(scratch) == NULL) {
        printf("  [FAIL] no scratch folder\nFAIL\n");
        return 1;
    }
    kind_tests();
    if (tiku_fontset_count() > 0) {
        for (k = 0; k < (int)(sizeof fixtures / sizeof fixtures[0]); k++) {
            fixture_test(k);
        }
        detail_tests();
        export_tests();
    } else {
        printf("  [SKIP] no faces on this machine to set text in\n");
    }
    tidy();
    printf("%s\n", fails ? "FAIL" : "PASS: documents known, translated, "
           "laid out, linked, outlined and exported");
    return fails ? 1 : 0;
}
