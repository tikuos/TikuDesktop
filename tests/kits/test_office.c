/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_office.c - Word, OpenDocument and PowerPoint files translated to
 * PDF, held to what the PDF kit reads back: the pages each lays out to
 * and the words on the first, page sizes and turns, links, the outline
 * and the title, charts drawn from their values, Windows metafiles as
 * their records say, equations set in two dimensions; a PDF exported
 * beside them.  Hostile files -- cut short, not XML, nested past reason,
 * sizes out of all measure -- are refused or laid out, never a crash or
 * a hang.  Documents come from tests/fixtures/office.inc; faces from the
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
 *         pages it lays out to and the words on the first.  Its row is
 *         OF-010 on; the twenty-first on, OF-060 on. */
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

    snprintf(row, sizeof row, "OF-%03d", k < 20 ? 10 + k : 40 + k);
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

/* ---------------------------------------------------------------- charts */

/** @brief The colour at fraction @p fx, @p fy across page @p p of @p doc,
 *         drawn small; 0 when it cannot be drawn. */
static uint32_t
colour_at(tiku_pdf_t *doc, int p, double fx, double fy)
{
    tiku_pdf_pixmap_t pm;
    uint32_t c;

    if (doc == NULL || tiku_pdf_render(doc, p, 0.5, 0, &pm) != 0) {
        return 0;
    }
    c = pm.px[(int)(fy * (pm.h - 1)) * pm.w + (int)(fx * (pm.w - 1))];
    tiku_pdf_pixmap_free(&pm);
    return c & 0xffffffu;
}

/** @brief How many pixels of page @p p of @p doc, drawn small, are
 *         colour @p rgb. */
static int
pixels_of(tiku_pdf_t *doc, int p, uint32_t rgb)
{
    tiku_pdf_pixmap_t pm;
    int i, n = 0;

    if (doc == NULL || tiku_pdf_render(doc, p, 0.5, 0, &pm) != 0) {
        return 0;
    }
    for (i = 0; i < pm.w * pm.h; i++) {
        n += (pm.px[i] & 0xffffffu) == rgb;
    }
    tiku_pdf_pixmap_free(&pm);
    return n;
}

/** @brief Charts drawn from the values their parts keep, DrawingML's
 *         and OpenDocument's; a kind not drawn here left as a grey box
 *         of its place. */
static void
chart_tests(void)
{
    tiku_pdf_t *c = translated("pptx_charts");
    char *pie = c != NULL ? tiku_pdf_page_text(c, 1) : NULL;
    char *left = c != NULL ? tiku_pdf_page_text(c, 2) : NULL;

    check(pie != NULL && strstr(pie, "Where the time goes") != NULL &&
          strstr(pie, "Reading") != NULL && strstr(pie, "50%") != NULL,
          "OF-036", "a pie drawn from its cached values: its title, its "
          "slices' names in the legend and their shares");
    check(c != NULL && (left == NULL ||
                        strstr(left, "Not drawn here") == NULL) &&
          colour_at(c, 2, 0.5, 0.6) == 0xeeeeeeu, "OF-037",
          "a chart of a kind not drawn here: a grey box of its place");
    free(pie);
    free(left);
    tiku_pdf_close(c);
    c = translated("odt_charts");
    check(pixels_of(c, 0, 0x004586u) > 100 && pixels_of(c, 0, 0xff420eu)
          > 100 && pixels_of(c, 0, 0x579d1cu) > 100 &&
          pixels_of(c, 0, 0xffd320u) > 100, "OF-038",
          "OpenDocument charts drawn from the tables their objects keep: "
          "each series' columns, each slice, in its own colour");
    tiku_pdf_close(c);
}

/* ------------------------------------------------------------ metafiles */

/** @brief Windows metafiles drawn as their records say: an EMF picture
 *         and an object's WMF picture in Word, embedded objects' kept
 *         pictures on slides, and broken ones as far as they go. */
static void
metafile_tests(void)
{
    tiku_pdf_t *w = translated("docx_metafiles");
    tiku_pdf_t *p = translated("pptx_ole");
    tiku_pdf_t *b = translated("docx_bad_metafiles");
    char *text = w != NULL ? tiku_pdf_page_text(w, 0) : NULL;

    check(text != NULL && strstr(text, "Metafile text") != NULL &&
          pixels_of(w, 0, 0xff0000u) > 100 &&
          pixels_of(w, 0, 0x00c000u) > 20 &&
          pixels_of(w, 0, 0xe4e4e4u) < 20, "OF-043",
          "an EMF picture drawn: its box red, its bitmap green, its text "
          "words on the page; no grey box in its place");
    check(text != NULL && strstr(text, "Placeable WMF") != NULL &&
          pixels_of(w, 0, 0x0000ffu) > 100, "OF-044",
          "an embedded object's WMF picture drawn: its triangle blue, its "
          "text words");
    check(pixels_of(p, 0, 0xff0000u) > 100 &&
          pixels_of(p, 0, 0xeeeeeeu) < 20, "OF-045",
          "an object on a slide shown by the picture its frame's fallback "
          "keeps");
    check(pixels_of(p, 1, 0x0000ffu) > 100, "OF-046",
          "and one of Office 2007, by the picture of its VML drawing");
    check(b != NULL && pixels_of(b, 0, 0xff0000u) > 10 &&
          pixels_of(b, 0, 0xe4e4e4u) > 10, "OF-047",
          "broken metafiles drawn as far as they go; one of EMF+ alone a "
          "grey box");
    free(text);
    tiku_pdf_close(w);
    tiku_pdf_close(p);
    tiku_pdf_close(b);
}

/* ------------------------------------------------------------ equations */

/* The colours the equation fixtures give the parts the tests look for:
 * gen_office.py's RED ... MAGENTA, FLOATING and SIGMA. */
enum { C_RED, C_BLUE, C_GREEN, C_ORANGE, C_PURPLE, C_TEAL, C_BROWN,
       C_AZURE, C_OLIVE, C_MAGENTA, C_FLOAT, C_SIGMA, C_COUNT };

/** @brief Where on page @p p of @p doc, drawn large, the pixels of each
 *         colour the fixtures' parts take lie: their mean place into
 *         @p x and @p y, their count into @p n. */
static void
places(tiku_pdf_t *doc, int p, double *x, double *y, int *n)
{
    static const uint32_t rgb[C_COUNT] = {
        0xff0000u, 0x0000ffu, 0x00a000u, 0xff8000u, 0x8000ffu, 0x00a0a0u,
        0xa05000u, 0x0080ffu, 0xa0a000u, 0xc000c0u, 0x804080u,
        0x600060u};
    tiku_pdf_pixmap_t pm;
    int i, k, d;

    memset(n, 0, C_COUNT * sizeof *n);
    memset(x, 0, C_COUNT * sizeof *x);
    memset(y, 0, C_COUNT * sizeof *y);
    if (doc == NULL || tiku_pdf_render(doc, p, 2, 0, &pm) != 0) {
        return;
    }
    for (i = 0; i < pm.w * pm.h; i++) {
        for (k = 0; k < C_COUNT; k++) {
            for (d = 0; d < 24 && abs((int)(pm.px[i] >> d & 255) -
                                      (int)(rgb[k] >> d & 255)) < 24;
                 d += 8) {}
            if (d == 24) {          /* each channel near the part's */
                x[k] += i % pm.w;
                y[k] += i / pm.w;
                n[k]++;
            }
        }
    }
    for (k = 0; k < C_COUNT; k++) {
        x[k] /= n[k] > 0 ? n[k] : 1;
        y[k] /= n[k] > 0 ? n[k] : 1;
    }
    tiku_pdf_pixmap_free(&pm);
}

/** @brief Whether page 0 of @p doc sets the fixtures' equations in two
 *         dimensions: a numerator over its denominator, a sum's limits
 *         centred over and under it, its body after it; with @p all, a
 *         radicand, a matrix's corners, a power over its base. */
static int
two_dimensions(tiku_pdf_t *doc, int all)
{
    double x[C_COUNT], y[C_COUNT];
    int n[C_COUNT], frac, sum;

    places(doc, 0, x, y, n);
    frac = n[C_RED] > 20 && n[C_BLUE] > 20 && y[C_RED] < y[C_BLUE] - 6 &&
           fabs(x[C_RED] - x[C_BLUE]) < 12;
    sum = n[C_PURPLE] > 5 && n[C_SIGMA] > 20 && n[C_ORANGE] > 5 &&
          n[C_GREEN] > 5 && y[C_PURPLE] < y[C_SIGMA] &&
          y[C_SIGMA] < y[C_ORANGE] && fabs(x[C_PURPLE] - x[C_SIGMA]) < 12 &&
          fabs(x[C_ORANGE] - x[C_SIGMA]) < 12 &&
          x[C_GREEN] > x[C_SIGMA] + 10;
    return frac && sum && (!all || (n[C_TEAL] > 20 && n[C_BROWN] > 5 &&
                                    n[C_AZURE] > 5 && n[C_OLIVE] > 5 &&
                                    n[C_MAGENTA] > 3 &&
                                    x[C_BROWN] < x[C_AZURE] - 10 &&
                                    y[C_BROWN] < y[C_AZURE] - 6 &&
                                    y[C_MAGENTA] < y[C_OLIVE] - 3 &&
                                    x[C_MAGENTA] > x[C_OLIVE]));
}

/** @brief Equations set as TeX sets them, Word's, OpenDocument's and
 *         PowerPoint's alike: each part where its structure puts it. */
static void
math_tests(void)
{
    tiku_pdf_t *w = translated("docx_math");
    tiku_pdf_t *o = translated("odt_math");
    tiku_pdf_t *p = translated("pptx_math");
    double x[C_COUNT], y[C_COUNT];
    int n[C_COUNT];

    check(two_dimensions(w, 1), "OF-048", "Word's equations in two "
          "dimensions: a numerator over its denominator, limits over and "
          "under a sum, a matrix's corners, a power over its base");
    places(o, 0, x, y, n);
    check(two_dimensions(o, 1) && n[C_FLOAT] > 20, "OF-049",
          "OpenDocument's formula objects set the same from their MathML; "
          "a floating one centred in its frame");
    check(two_dimensions(p, 0) && pixels_of(p, 0, 0xeeeeeeu) < 20,
          "OF-050", "PowerPoint's equations set in their text box, not "
          "the picture kept for older readers");
    tiku_pdf_close(w);
    tiku_pdf_close(o);
    tiku_pdf_close(p);
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
        chart_tests();
        metafile_tests();
        math_tests();
        export_tests();
    } else {
        printf("  [SKIP] no faces on this machine to set text in\n");
    }
    tidy();
    printf("%s\n", fails ? "FAIL" : "PASS: documents known, translated, "
           "laid out, linked, outlined and exported");
    return fails ? 1 : 0;
}
