/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_pdf.c - PDFs built byte by byte, and the ones on this machine.
 *
 * A one-page file with a classic table; the same with its table lying
 * and its Length wrong, so the reader has to find the objects itself;
 * a file with a cross-reference stream and an object stream, packed
 * with a stored DEFLATE block; every byte filter against known input;
 * fonts read through their encoding and through a ToUnicode map.  Then,
 * when TIKU_PDF_CORPUS names a folder, every PDF in it is opened and
 * its first page's text shown, which is how the vendor documents are
 * tried without being checked in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_pdf.h"
#include "../../kits/translation/tiku_pdf_private.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] PDF-001   %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/* ------------------------------------------------------------ building */

typedef struct {
    char *p;
    long  n, cap;
    long  off[64];
    int   nobj;
} pdfw_t;

static void
w(pdfw_t *b, const char *s, long n)
{
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, (size_t)b->cap);
    }
    memcpy(b->p + b->n, s, (size_t)n);
    b->n += n;
    b->p[b->n] = '\0';
}

static void
ws(pdfw_t *b, const char *s)
{
    w(b, s, (long)strlen(s));
}

/** @brief "N 0 obj" at the current offset, remembered for the table. */
static void
obj(pdfw_t *b, int num, const char *body)
{
    char head[32];

    b->off[num] = b->n;
    if (num >= b->nobj) {
        b->nobj = num + 1;
    }
    snprintf(head, sizeof head, "%d 0 obj\n", num);
    ws(b, head);
    ws(b, body);
    ws(b, "\nendobj\n");
}

/** @brief A stream object with the length it really has. */
static void
stream(pdfw_t *b, int num, const char *dict_inner, const char *data, long n)
{
    char head[128];

    b->off[num] = b->n;
    if (num >= b->nobj) {
        b->nobj = num + 1;
    }
    snprintf(head, sizeof head, "%d 0 obj\n<< %s /Length %ld >>\nstream\n",
             num, dict_inner, n);
    ws(b, head);
    w(b, data, n);
    ws(b, "\nendstream\nendobj\n");
}

/** @brief The table and trailer, with @p lie added to every offset. */
static void
xref(pdfw_t *b, int root, long lie)
{
    char line[64];
    long start = b->n;
    int i;

    snprintf(line, sizeof line, "xref\n0 %d\n", b->nobj);
    ws(b, line);
    ws(b, "0000000000 65535 f \n");
    for (i = 1; i < b->nobj; i++) {
        snprintf(line, sizeof line, "%010ld 00000 n \n", b->off[i] + lie);
        ws(b, line);
    }
    snprintf(line, sizeof line, "trailer\n<< /Size %d /Root %d 0 R >>\n"
             "startxref\n%ld\n%%%%EOF\n", b->nobj, root, start);
    ws(b, line);
}

/** @brief One page saying "Hello, PDF" with Helvetica, WinAnsi. */
static pdfw_t
simple_file(long lie, int wrong_length)
{
    pdfw_t b = {0};
    const char *content = "BT /F1 12 Tf 72 700 Td (Hello, PDF) Tj "
                          "0 -14 Td [(Sec) 20 (ond) -250 (line)] TJ ET";

    ws(&b, "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 "
               "/MediaBox [0 0 300 400] >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /Rotate 90 /Resources << /Font "
               "<< /F1 4 0 R >> >> /Contents 5 0 R >>");
    obj(&b, 4, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
               "/Encoding /WinAnsiEncoding >>");
    if (wrong_length) {
        char head[64];

        b.off[5] = b.n;
        b.nobj = 6;
        snprintf(head, sizeof head, "5 0 obj\n<< /Length 999 >>\nstream\n");
        ws(&b, head);
        ws(&b, content);
        ws(&b, "\nendstream\nendobj\n");
    } else {
        stream(&b, 5, "", content, (long)strlen(content));
    }
    xref(&b, 1, lie);
    return b;
}

/** @brief A zlib stream holding @p n bytes stored, not compressed. */
static long
stored_zlib(const char *in, long n, char *out)
{
    long k = 0;

    out[k++] = 0x78;
    out[k++] = 0x01;
    out[k++] = 0x01;                /* last block, stored */
    out[k++] = (char)(n & 0xff);
    out[k++] = (char)((n >> 8) & 0xff);
    out[k++] = (char)(~n & 0xff);
    out[k++] = (char)((~n >> 8) & 0xff);
    memcpy(out + k, in, (size_t)n);
    k += n;
    out[k++] = 0;                   /* an Adler-32 nobody checks */
    out[k++] = 0;
    out[k++] = 0;
    out[k++] = 1;
    return k;
}

/**
 * @brief A PDF 1.5 file: catalog, pages and page packed in an object
 *        stream, located by a cross-reference stream.
 */
static pdfw_t
packed_file(void)
{
    pdfw_t b = {0};
    const char *objs = "<< /Type /Catalog /Pages 2 0 R >> "
                       "<< /Type /Pages /Kids [3 0 R] /Count 1 >> "
                       "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                       "/Contents 5 0 R /Resources << /Font << /F1 << /Type "
                       "/Font /Subtype /TrueType /BaseFont /Arial /FirstChar "
                       "65 /Widths [667 667] /Encoding << /Differences [65 "
                       "/alpha /beta] >> >> >> >> >>";
    const char *content = "BT /F1 10 Tf 10 50 Td (AB) Tj ET";
    char header[64], packed[1024], z[1200], xr[256], dict[256];
    long n, zn, xrn = 0, i, xoff;
    long o1 = 0, o2, o3;

    o2 = (long)strlen("<< /Type /Catalog /Pages 2 0 R >> ");
    o3 = o2 + (long)strlen("<< /Type /Pages /Kids [3 0 R] /Count 1 >> ");
    snprintf(header, sizeof header, "1 %ld 2 %ld 3 %ld ", o1, o2, o3);
    n = (long)strlen(header);
    memcpy(packed, header, (size_t)n);
    memcpy(packed + n, objs, strlen(objs));
    zn = stored_zlib(packed, n + (long)strlen(objs), z);
    ws(&b, "%PDF-1.5\n");
    snprintf(dict, sizeof dict, "/Type /ObjStm /N 3 /First %ld /Filter "
             "/FlateDecode", n);
    stream(&b, 4, dict, z, zn);
    stream(&b, 5, "", content, (long)strlen(content));
    /* Entries: type 1 byte, field 2 four bytes, field 3 one byte. */
    xoff = b.n;
    for (i = 0; i < 7; i++) {
        int type = (i == 0) ? 0 : (i >= 1 && i <= 3) ? 2 : 1;
        long f2 = (i >= 1 && i <= 3) ? 4 : (i == 4 || i == 5) ? b.off[i]
                  : (i == 6) ? xoff : 0;
        long f3 = (i >= 1 && i <= 3) ? i - 1 : 0;

        xr[xrn++] = (char)type;
        xr[xrn++] = (char)((f2 >> 24) & 0xff);
        xr[xrn++] = (char)((f2 >> 16) & 0xff);
        xr[xrn++] = (char)((f2 >> 8) & 0xff);
        xr[xrn++] = (char)(f2 & 0xff);
        xr[xrn++] = (char)f3;
    }
    snprintf(dict, sizeof dict, "/Type /XRef /Size 7 /W [1 4 1] /Root 1 0 R");
    stream(&b, 6, dict, xr, xrn);
    snprintf(dict, sizeof dict, "startxref\n%ld\n%%%%EOF\n", xoff);
    ws(&b, dict);
    return b;
}

/* ------------------------------------------------------------- checks */

static tiku_pdf_t *
open_bytes(const pdfw_t *b, tiku_pdf_err_t *err)
{
    tiku_pdf_t *doc = NULL;

    *err = tiku_pdf_decode(b->p, (size_t)b->n, &doc);
    return doc;
}

static void
simple_test(void)
{
    pdfw_t b = simple_file(0, 0);
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    double w = 0, h = 0;
    int rot = -1;
    char *text;

    check(err == TIKU_PDF_OK && doc != NULL, "a one-page file opens");
    if (doc == NULL) {
        free(b.p);
        return;
    }
    check(tiku_pdf_pages(doc) == 1 && !tiku_pdf_repaired(doc),
          "with one page, its table believed");
    check(tiku_pdf_page_size(doc, 0, &w, &h, &rot) == 0 && w == 300 &&
          h == 400 && rot == 90, "the box comes from the parent, Rotate 90");
    text = tiku_pdf_page_text(doc, 0);
    check(text != NULL && strcmp(text, "Hello, PDF\nSecond line") == 0,
          "its words, a kerned pair joined and a gap made a space");
    if (text != NULL && strcmp(text, "Hello, PDF\nSecond line") != 0) {
        printf("    got [%s]\n", text);
    }
    free(text);
    check(tiku_pdf_page_size(doc, 1, &w, &h, &rot) < 0, "page 1 is not there");
    tiku_pdf_close(doc);
    free(b.p);
}

static void
repair_test(void)
{
    pdfw_t b = simple_file(7, 1);
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    char *text;

    check(err == TIKU_PDF_OK && doc != NULL && tiku_pdf_repaired(doc),
          "a table off by seven bytes is rebuilt from the file");
    text = doc ? tiku_pdf_page_text(doc, 0) : NULL;
    check(text != NULL && strncmp(text, "Hello, PDF", 10) == 0,
          "and a Length of 999 finds endstream instead");
    free(text);
    tiku_pdf_close(doc);
    free(b.p);
    b = simple_file(0, 0);
    memcpy(b.p + b.n - 12, "startxref\n99", 12);
    doc = open_bytes(&b, &err);
    check(err == TIKU_PDF_OK && doc != NULL && tiku_pdf_pages(doc) == 1,
          "a startxref pointing at nothing is rebuilt too");
    tiku_pdf_close(doc);
    free(b.p);
}

static void
packed_test(void)
{
    pdfw_t b = packed_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    char *text;

    check(err == TIKU_PDF_OK && doc != NULL && !tiku_pdf_repaired(doc),
          "a cross-reference stream over an object stream opens");
    check(doc != NULL && tiku_pdf_pages(doc) == 1, "and finds the packed page");
    text = doc ? tiku_pdf_page_text(doc, 0) : NULL;
    check(text != NULL && strcmp(text, "\xce\xb1\xce\xb2") == 0,
          "Differences name the glyphs: A is alpha, B is beta");
    free(text);
    tiku_pdf_close(doc);
    free(b.p);
}

static void
not_pdf_test(void)
{
    tiku_pdf_t *doc = NULL;
    const char *junk = "GIF89a not a document at all";

    check(tiku_pdf_decode(junk, strlen(junk), &doc) == TIKU_PDF_NOT_PDF &&
          doc == NULL, "bytes without a header are not a PDF");
    check(tiku_pdf_decode("%PDF-1.4\n", 9, &doc) == TIKU_PDF_BAD &&
          doc == NULL, "a header with nothing behind it is bad, not open");
}

/** @brief One filter over a stream object, through the document. */
static char *
filtered(const char *filter, const char *data, long n, long *out_n)
{
    pdfw_t b = {0};
    char dict[128];
    tiku_pdf_t *doc = NULL;
    unsigned char *out = NULL;
    char *copy = NULL;

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] >>");
    snprintf(dict, sizeof dict, "/Filter /%s", filter);
    stream(&b, 4, dict, data, n);
    xref(&b, 1, 0);
    if (tiku_pdf_decode(b.p, (size_t)b.n, &doc) == TIKU_PDF_OK &&
        pdf_stream_data(doc, pdf_object(doc, 4), &out, out_n, NULL) == 0) {
        copy = malloc((size_t)*out_n + 1);
        memcpy(copy, out, (size_t)*out_n);
        copy[*out_n] = '\0';
        free(out);
    }
    tiku_pdf_close(doc);
    free(b.p);
    return copy;
}

static void
filter_test(void)
{
    long n = 0;
    char *s;
    /* "ABBBBBBBBBBBC" as run lengths: A alone, twelve Bs, C alone. */
    const char rl[] = "\x00" "A" "\xf5" "B" "\x00" "C" "\x80";
    /* LZW of "-----A---B" from the standard's own example. */
    const unsigned char lz[] = {0x80, 0x0B, 0x60, 0x50, 0x22, 0x0C, 0x0C,
                                0x85, 0x01};

    s = filtered("ASCIIHexDecode", "48656C6C 6F>", 12, &n);
    check(s != NULL && n == 5 && strcmp(s, "Hello") == 0, "hex pairs");
    free(s);
    s = filtered("ASCII85Decode", "87cURD]i,\"Ebo80~>", 17, &n);
    check(s != NULL && n == 12 && strcmp(s, "Hello World!") == 0,
          "base 85, the standard's example");
    free(s);
    s = filtered("ASCII85Decode", "88/~>", 5, &n);
    check(s != NULL && n == 2 && strcmp(s, "Hi") == 0,
          "base 85 with a short last group");
    free(s);
    s = filtered("RunLengthDecode", rl, (long)sizeof rl - 1, &n);
    check(s != NULL && n == 14 && strcmp(s, "ABBBBBBBBBBBBC") == 0,
          "run lengths");
    free(s);
    s = filtered("LZWDecode", (const char *)lz, (long)sizeof lz, &n);
    check(s != NULL && n == 10 && strcmp(s, "-----A---B") == 0,
          "LZW, the standard's example");
    free(s);
    {
        char z[64];
        long zn = stored_zlib("flate stored", 12, z);

        s = filtered("FlateDecode", z, zn, &n);
        check(s != NULL && n == 12 && strcmp(s, "flate stored") == 0,
              "Flate through a stored block, size unknown in advance");
        free(s);
    }
}

static void
glyph_test(void)
{
    check(pdf_glyph_unicode("fi") == 0xFB01 && pdf_glyph_unicode("A") == 'A' &&
          pdf_glyph_unicode("endash") == 0x2013 &&
          pdf_glyph_unicode("uni2264") == 0x2264 &&
          pdf_glyph_unicode("a.sc") == 'a' && pdf_glyph_unicode("g42") == 0,
          "glyph names: the list, one letter, uniXXXX, a suffix, a number");
}

/** @brief Every PDF in the folder TIKU_PDF_CORPUS names, opened and read. */
static void
corpus_test(void)
{
    const char *dir = getenv("TIKU_PDF_CORPUS");
    DIR *d;
    struct dirent *e;
    int opened = 0, tried = 0;

    if (dir == NULL || (d = opendir(dir)) == NULL) {
        puts("SKIP: TIKU_PDF_CORPUS names no folder of PDFs");
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char path[1024], *text, *title;
        tiku_pdf_t *doc = NULL;
        tiku_pdf_err_t err;
        size_t len = strlen(e->d_name);
        int pages;

        if (len < 4 || strcmp(e->d_name + len - 4, ".pdf") != 0) {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        tried++;
        err = tiku_pdf_open(path, &doc);
        if (err != TIKU_PDF_OK) {
            printf("  %s: %s\n", e->d_name, err == TIKU_PDF_UNSUPPORTED
                   ? "unsupported (encrypted)" : "did not open");
            continue;
        }
        opened++;
        pages = tiku_pdf_pages(doc);
        title = tiku_pdf_title(doc);
        text = tiku_pdf_page_text(doc, pages > 1 ? 1 : 0);
        printf("  %s: %d pages%s, title %s\n    %.160s\n", e->d_name, pages,
               tiku_pdf_repaired(doc) ? " (repaired)" : "",
               title ? title : "(none)", text ? text : "(no text)");
        check(pages > 0 && text != NULL, e->d_name);
        free(text);
        free(title);
        tiku_pdf_close(doc);
    }
    closedir(d);
    printf("  corpus: %d of %d opened\n", opened, tried);
}

int
main(void)
{
    printf("PDFs built byte by byte\n");
    simple_test();
    repair_test();
    packed_test();
    not_pdf_test();
    filter_test();
    glyph_test();
    corpus_test();
    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    puts("PASS: objects, tables rebuilt, object streams, filters, text");
    return 0;
}
