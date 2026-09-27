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
 * fonts read through their encoding and through a ToUnicode map; the
 * slips of careless writers, which once broke the reader.  Then,
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

#include "tiku_gfx.h"
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
    char head[512];

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

/* ------------------------------------------------------------ drawing */

static uint32_t
pixel(const tiku_pdf_pixmap_t *pm, int x, int y)
{
    return (x >= 0 && y >= 0 && x < pm->w && y < pm->h)
           ? pm->px[(size_t)y * pm->w + x] & 0xffffffu : 0xdeadbeefu;
}

/** @brief Write @p pm as a PNG, for eyes; @return 0 when it was. */
static int
png_out(const tiku_pdf_pixmap_t *pm, const char *path)
{
    tiku_surface_t *s = tiku_surface_new(pm->w, pm->h, TIKU_C_DOC);
    int rc;

    if (s == NULL) {
        return -1;
    }
    memcpy(s->px, pm->px, (size_t)pm->w * pm->h * sizeof *pm->px);
    rc = tiku_surface_png(s, path);
    tiku_surface_free(s);
    return rc;
}

/**
 * @brief Shadings and patterns on a 300-point page: an axial pattern with
 *        one end extended, a radial shading by sh in a clip, a coloured
 *        and an uncoloured tiling, and a fill at half its alpha.
 */
static pdfw_t
shade_file(void)
{
    pdfw_t b = {0};
    const char *content =
        "/Pattern cs /Ax scn 0 200 100 100 re f "
        "q 150 150 100 100 re W n /Rd sh Q "
        "/Pattern cs /Tl scn 0 0 100 100 re f "
        "/PU cs 0 1 0 /Un scn 150 0 100 100 re f "
        "/Half gs 1 0 0 rg 0 110 100 60 re f";
    const char *cell = "0 0 0 rg 0 0 5 5 re f";
    const char *stencil = "1 0 0 rg 0 0 5 5 re f";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
               "/Resources << /Pattern << /Ax 5 0 R /Tl 6 0 R /Un 7 0 R >> "
               "/Shading << /Rd 8 0 R >> /ExtGState << /Half << /ca 0.5 >> "
               ">> /ColorSpace << /PU [/Pattern /DeviceRGB] >> >> "
               "/Contents 4 0 R >>");
    stream(&b, 4, "", content, (long)strlen(content));
    obj(&b, 5, "<< /PatternType 2 /Shading << /ShadingType 2 /ColorSpace "
               "/DeviceRGB /Coords [20 0 80 0] /Extend [false true] "
               "/Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] "
               "/C1 [0 0 1] /N 1 >> >> >>");
    stream(&b, 6, "/PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 10 10]"
                  " /XStep 10 /YStep 10 /Resources << >>", cell,
           (long)strlen(cell));
    /* Uncoloured: the red it asks for is not its to choose. */
    stream(&b, 7, "/PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 10 10]"
                  " /XStep 10 /YStep 10 /Resources << >>", stencil,
           (long)strlen(stencil));
    obj(&b, 8, "<< /ShadingType 3 /ColorSpace /DeviceRGB "
               "/Coords [200 200 0 200 200 40] /Function << /FunctionType 2 "
               "/Domain [0 1] /C0 [0 1 0] /C1 [0 0 1] /N 1 >> >>");
    xref(&b, 1, 0);
    return b;
}

/** @brief Whether @p c is within @p tol of @p r, @p g, @p b. */
static int
near(uint32_t c, int r, int g, int b, int tol)
{
    return abs((int)(c >> 16 & 255) - r) <= tol &&
           abs((int)(c >> 8 & 255) - g) <= tol &&
           abs((int)(c & 255) - b) <= tol;
}

static void
shade_test(void)
{
    pdfw_t b = shade_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};

    check(doc != NULL && tiku_pdf_render(doc, 0, 1.0, &pm) == 0 &&
          pm.w == 300, "a page of shadings and patterns renders");
    if (pm.px == NULL) {
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    /* Axial along x 20..80, drawn in the pattern fill (device y 0..100). */
    check(pixel(&pm, 10, 50) == 0xffffffu,
          "an axial shading leaves what is before an end not extended");
    check(near(pixel(&pm, 50, 50), 127, 0, 128, 3),
          "and is half one colour, half the other, at its middle");
    check(pixel(&pm, 95, 50) == 0x0000ffu, "and carries the extended end on");
    /* Radial about user 200, 200 (device 200, 100), radius 0 to 40. */
    check(near(pixel(&pm, 200, 100), 0, 255, 0, 8) &&
          near(pixel(&pm, 230, 100), 0, 64, 191, 8),
          "a radial shading by sh, green at its centre to blue at 40");
    check(pixel(&pm, 246, 100) == 0xffffffu &&
          pixel(&pm, 140, 100) == 0xffffffu,
          "and nothing past its outer circle or outside the clip");
    /* The coloured cell: black 0..5 of every 10, both ways. */
    check(pixel(&pm, 2, 298) == 0 && pixel(&pm, 12, 288) == 0 &&
          pixel(&pm, 7, 298) == 0xffffffu && pixel(&pm, 2, 293) == 0xffffffu,
          "a coloured tiling repeats its cell");
    check(pixel(&pm, 152, 298) == 0x00ff00u &&
          pixel(&pm, 157, 298) == 0xffffffu,
          "an uncoloured one paints the colour scn gave it");
    check(near(pixel(&pm, 50, 160), 255, 127, 127, 2),
          "a fill at half its alpha lets half the paper through");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Transparency and annotations on a 300-point page: Multiply and
 *        Difference, a luminosity and an alpha soft mask and their end, a
 *        group at half its alpha, and four annotations, two of them unseen.
 */
static pdfw_t
transp_file(void)
{
    pdfw_t b = {0};
    const char *content =
        "1 0 0 rg 0 250 40 40 re f q /M gs 0 0 1 rg 20 250 40 40 re f Q "
        "1 0 0 rg 100 250 40 40 re f q /D gs 1 1 1 rg 120 250 40 40 re f Q "
        "q /Lum gs 1 0 0 rg 0 150 100 50 re f /None gs 0 0 1 rg "
        "0 100 100 30 re f Q "
        "q /Al gs 1 0 0 rg 150 150 100 50 re f Q "
        "q /Half gs /Grp Do Q";
    const char *lum = "1 g 0 150 50 50 re f";
    const char *alpha = "q /Half gs 0 g 150 150 100 50 re f Q";
    const char *grp = "1 0 0 rg 150 20 60 60 re f 0 0 1 rg 180 20 60 60 re f";
    const char *green = "0 1 0 rg 0 0 10 10 re f";
    const char *red = "1 0 0 rg 0 0 10 10 re f";
    const char *blue = "0 0 1 rg 0 0 10 10 re f";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
               "/Resources << /ExtGState << /M << /BM /Multiply >> "
               "/D << /BM [/Difference] >> /Half << /ca 0.5 >> "
               "/None << /SMask /None >> "
               "/Lum << /SMask << /S /Luminosity /G 10 0 R >> >> "
               "/Al << /SMask << /S /Alpha /G 11 0 R >> >> >> "
               "/XObject << /Grp 12 0 R >> >> /Contents 4 0 R "
               "/Annots [20 0 R 21 0 R 22 0 R 23 0 R] >>");
    stream(&b, 4, "", content, (long)strlen(content));
    stream(&b, 10, "/Type /XObject /Subtype /Form /BBox [0 0 300 300] "
                   "/Group << /S /Transparency >>", lum, (long)strlen(lum));
    stream(&b, 11, "/Type /XObject /Subtype /Form /BBox [0 0 300 300] "
                   "/Group << /S /Transparency >> /Resources << /ExtGState "
                   "<< /Half << /ca 0.5 >> >> >>", alpha, (long)strlen(alpha));
    stream(&b, 12, "/Type /XObject /Subtype /Form /BBox [0 0 300 300] "
                   "/Group << /S /Transparency >>", grp, (long)strlen(grp));
    obj(&b, 20, "<< /Type /Annot /Subtype /Square /Rect [20 20 70 40] "
                "/AP << /N 30 0 R >> >>");
    obj(&b, 21, "<< /Type /Annot /Subtype /Widget /Rect [80 20 120 40] "
                "/AP << /N << /On 31 0 R /Off 32 0 R >> >> /AS /Off >>");
    obj(&b, 22, "<< /Type /Annot /Subtype /Square /F 2 /Rect [20 50 60 70] "
                "/AP << /N 31 0 R >> >>");
    obj(&b, 23, "<< /Type /Annot /Subtype /Popup /Rect [80 50 120 70] "
                "/AP << /N 31 0 R >> >>");
    stream(&b, 30, "/Type /XObject /Subtype /Form /BBox [0 0 10 10]", green,
           (long)strlen(green));
    stream(&b, 31, "/Type /XObject /Subtype /Form /BBox [0 0 10 10]", red,
           (long)strlen(red));
    stream(&b, 32, "/Type /XObject /Subtype /Form /BBox [0 0 10 10]", blue,
           (long)strlen(blue));
    xref(&b, 1, 0);
    return b;
}

static void
transp_test(void)
{
    pdfw_t b = transp_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};

    check(doc != NULL && tiku_pdf_render(doc, 0, 1.0, &pm) == 0 &&
          pm.w == 300, "a page of transparency and annotations renders");
    if (pm.px == NULL) {
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    check(pixel(&pm, 10, 30) == 0xff0000u && pixel(&pm, 30, 30) == 0 &&
          pixel(&pm, 50, 30) == 0x0000ffu,
          "Multiply: blue on red is black, blue on paper stays blue");
    check(pixel(&pm, 130, 30) == 0x00ffffu && pixel(&pm, 150, 30) == 0,
          "Difference: white on red is cyan, white on paper black");
    check(pixel(&pm, 25, 125) == 0xff0000u &&
          pixel(&pm, 75, 125) == 0xffffffu,
          "a luminosity mask lets the fill through where its group is white");
    check(pixel(&pm, 75, 185) == 0x0000ffu, "and SMask None takes it away");
    check(near(pixel(&pm, 200, 125), 255, 127, 127, 3),
          "an alpha mask from a half-alpha group lets half through");
    check(near(pixel(&pm, 165, 250), 255, 127, 127, 3) &&
          near(pixel(&pm, 225, 250), 127, 127, 255, 3),
          "a group at half its alpha shows each part at half");
    check(near(pixel(&pm, 195, 250), 127, 127, 255, 3),
          "and where its parts overlap, the top one at half: as one");
    check(pixel(&pm, 45, 270) == 0x00ff00u && pixel(&pm, 15, 270) == 0xffffffu,
          "an appearance fills its annotation's Rect, scaled from its BBox");
    check(pixel(&pm, 100, 270) == 0x0000ffu,
          "a widget shows the appearance of the state its AS names");
    check(pixel(&pm, 40, 240) == 0xffffffu && pixel(&pm, 100, 240) == 0xffffffu,
          "hidden annotations and popups are not drawn");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Four pages a careless writer makes: a MediaBox of nothing; a
 *        form whose stray Q would undo its caller's clip; a Separation
 *        whose alternate is no space; an Indexed table over five
 *        colorants.
 */
static pdfw_t
damage_file(void)
{
    pdfw_t b = {0};
    const char *clip = "q 0 0 100 100 re W n /Fm Do 1 0 0 rg 0 0 300 300 re "
                       "f Q BT /F1 12 Tf 10 200 Td (After) Tj ET";
    const char *sep = "/CS0 cs 0.5 sc 0 0 300 300 re f";
    const char *idx = "/CS1 cs 1 sc 0 0 300 300 re f";
    const char *fn = "{ exch pop exch pop exch pop exch pop }";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R 6 0 R] /Count 4 "
               "/MediaBox [0 0 300 300] /Resources << /Font << /F1 << "
               "/Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> "
               "/XObject << /Fm 10 0 R >> /ColorSpace << "
               "/CS0 [/Separation /Spot /NoSuchSpace 11 0 R] "
               "/CS1 [/Indexed [/DeviceN [/A /B /C /D /E] /DeviceGray "
               "12 0 R] 1 <000000000000000000CC>] >> >> >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 0 0] >>");
    obj(&b, 4, "<< /Type /Page /Parent 2 0 R /Contents 7 0 R >>");
    obj(&b, 5, "<< /Type /Page /Parent 2 0 R /Contents 8 0 R >>");
    obj(&b, 6, "<< /Type /Page /Parent 2 0 R /Contents 9 0 R >>");
    stream(&b, 7, "", clip, (long)strlen(clip));
    stream(&b, 8, "", sep, (long)strlen(sep));
    stream(&b, 9, "", idx, (long)strlen(idx));
    stream(&b, 10, "/Type /XObject /Subtype /Form /BBox [0 0 300 300]",
           "Q Q", 3);
    obj(&b, 11, "<< /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>");
    stream(&b, 12, "/FunctionType 4 /Domain [0 1 0 1 0 1 0 1 0 1] "
                   "/Range [0 1]", fn, (long)strlen(fn));
    xref(&b, 1, 0);
    return b;
}

/** @brief The careless writer's pages, read and drawn as it meant them. */
static void
damage_test(void)
{
    pdfw_t b = damage_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL};
    char *text = doc != NULL ? tiku_pdf_page_text(doc, 1) : NULL;
    double pw = 0, ph = 0;
    int rot = 0;

    check(doc != NULL && tiku_pdf_page_size(doc, 0, &pw, &ph, &rot) == 0 &&
          pw == 612 && ph == 792,
          "a MediaBox of nothing leaves the page Letter, not nothing");
    check(doc != NULL && tiku_pdf_render(doc, 1, 1.0, &pm) == 0 &&
          pixel(&pm, 50, 250) == 0xff0000u &&
          pixel(&pm, 150, 150) == 0xffffffu,
          "a form's stray Q does not undo its caller's clip");
    tiku_pdf_pixmap_free(&pm);
    check(text != NULL && strstr(text, "After") != NULL,
          "nor lose the text that follows it");
    free(text);
    check(doc != NULL && tiku_pdf_render(doc, 2, 1.0, &pm) == 0 &&
          pixel(&pm, 150, 150) == 0x808080u,
          "a Separation whose alternate is no space paints its tint grey");
    tiku_pdf_pixmap_free(&pm);
    check(doc != NULL && tiku_pdf_render(doc, 3, 1.0, &pm) == 0 &&
          pixel(&pm, 150, 150) == 0xccccccu,
          "an Indexed table over five colorants reads all five");
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Three pages, an outline over them and links between them: an
 *        entry and a link by name (the Names tree and the older Dests
 *        dictionary), one straight to a page, one to an address.
 */
static pdfw_t
nav_file(void)
{
    pdfw_t b = {0};
    const char *text = "BT /F1 20 Tf 100 700 Td (Hi) Tj ET "
                       "BT /F1 20 Tf 100 600 Td (Yo) Tj ET";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R /Outlines 10 0 R "
               "/Dests << /old [4 0 R /FitH 500] >> "
               "/Names << /Dests 20 0 R >> >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 "
               "/MediaBox [0 0 612 792] /Resources << /Font << /F1 7 0 R "
               ">> >> >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /Contents 6 0 R "
               "/Annots [8 0 R 9 0 R 13 0 R] >>");
    obj(&b, 4, "<< /Type /Page /Parent 2 0 R >>");
    obj(&b, 5, "<< /Type /Page /Parent 2 0 R /Rotate 90 >>");
    stream(&b, 6, "", text, (long)strlen(text));
    /* Widths from H (72): H 500, Y 600, i 250, o 550, the rest 0. */
    obj(&b, 7, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
               "/FirstChar 72 /LastChar 111 /Widths [500 0 0 0 0 0 0 0 0 0 0 0 "
               "0 0 0 0 0 600 0 0 0 0 0 0 0 0 0 0 "
               "0 0 0 0 0 250 0 0 0 0 0 550] >>");
    obj(&b, 8, "<< /Type /Annot /Subtype /Link /Rect [100 700 200 720] "
               "/Dest (sec2) >>");
    obj(&b, 9, "<< /Type /Annot /Subtype /Link /Rect [300 50 400 70] "
               "/A << /S /URI /URI (https://tiku-os.org/) >> >>");
    obj(&b, 13, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] "
                "/A << /S /Named /N /NextPage >> >>");
    obj(&b, 10, "<< /Type /Outlines /First 11 0 R /Last 12 0 R >>");
    obj(&b, 11, "<< /Title (One) /Parent 10 0 R /Next 12 0 R /First 14 0 R "
                "/Last 14 0 R /Count 1 /Dest [3 0 R /Fit] >>");
    obj(&b, 14, "<< /Title <FEFF0054007700F8> /Parent 11 0 R "
                "/Dest [4 0 R /XYZ 0 700 0] >>");
    obj(&b, 12, "<< /Title (Three\x8d) /Parent 10 0 R /Prev 11 0 R "
                "/A << /S /GoTo /D /old >> >>");
    obj(&b, 20, "<< /Kids [21 0 R 22 0 R] >>");
    obj(&b, 21, "<< /Limits [(a) (m)] /Names [(abc) [3 0 R /Fit]] >>");
    obj(&b, 22, "<< /Limits [(n) (z)] /Names [(sec2) [5 0 R /XYZ 72 792 0]"
                " (zz) [3 0 R /Fit]] >>");
    xref(&b, 1, 0);
    return b;
}

static void
nav_test(void)
{
    pdfw_t b = nav_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_outline_t *o = NULL;
    tiku_pdf_link_t *l = NULL;
    tiku_pdf_char_t *c = NULL;
    int n = 0, k = 0;
    long nc = 0;

    check(doc != NULL && tiku_pdf_pages(doc) == 3, "three pages, linked");
    if (doc == NULL) {
        free(b.p);
        return;
    }
    n = tiku_pdf_outline(doc, &o);
    check(n == 3 && o[0].level == 0 && o[0].below == 1 && o[0].open &&
          o[1].level == 1 && o[2].level == 0 && o[2].below == 0,
          "the outline, depth first, with what lies under each entry");
    check(n == 3 && !strcmp(o[0].title, "One") &&
          !strcmp(o[1].title, "Tw\xc3\xb8") &&
          !strcmp(o[2].title, "Three\xe2\x80\x9c"),
          "titles in UTF-16 and PDFDocEncoding come out as UTF-8");
    /* XYZ 0 700 on a 792-point page: 92 points down from the top. */
    check(n == 3 && o[0].to.page == 0 && o[0].to.top == 0 &&
          o[1].to.page == 1 && o[1].to.top > 91.9 && o[1].to.top < 92.1,
          "an entry leads to its page and the point on it");
    check(n == 3 && o[2].to.page == 1 && o[2].to.top > 291.9 &&
          o[2].to.top < 292.1, "a GoTo by a name the Dests dictionary holds");
    tiku_pdf_outline_free(o, n);

    k = tiku_pdf_links(doc, 0, &l);
    check(k == 3, "three links on the first page");
    /* Rect 100 700 200 720 is 72..92 down the page as drawn. */
    check(k == 3 && l[0].x0 == 100 && l[0].x1 == 200 && l[0].y0 == 72 &&
          l[0].y1 == 92, "a link's area, on the page as drawn");
    /* sec2 by the Names tree: the third page, turned; x 72 is 72 down. */
    check(k == 3 && l[0].to.page == 2 && l[0].to.top > 71.9 &&
          l[0].to.top < 72.1, "a name found through the tree's Limits");
    check(k == 3 && l[1].to.page == -1 && l[1].to.uri != NULL &&
          !strcmp(l[1].to.uri, "https://tiku-os.org/"), "an address");
    check(k == 3 && l[2].to.page == 1, "a Named step to the next page");
    tiku_pdf_links_free(l, k);
    check(tiku_pdf_links(doc, 1, &l) == 0 && l == NULL,
          "a page without links has none");

    /* "Hi" at 100, 700 in 20 points: H is 500 wide, i 250. */
    nc = tiku_pdf_page_chars(doc, 0, &c);
    check(nc == 5 && c[0].cp == 'H' && c[1].cp == 'i' && c[2].cp == '\n' &&
          c[3].cp == 'Y' && c[4].cp == 'o', "the characters, in order");
    check(nc == 5 && c[0].x0 == 100 && c[0].x1 == 110 && c[1].x0 == 110 &&
          c[1].x1 == 115 && c[0].y0 == 76 && c[0].y1 == 96,
          "each where it stands: the em from below the baseline");
    check(nc == 5 && c[2].x0 == c[2].x1, "a made-up break has no width");
    free(c);
    tiku_pdf_close(doc);
    free(b.p);
}

/** @brief A page with a red square, a blue stroke, a clipped green fill
 *        and a form, on a Rotate 90 page. */
static pdfw_t
drawn_file(void)
{
    pdfw_t b = {0};
    const char *content =
        "1 0 0 rg 0 0 50 50 re f "
        "0 0 1 RG 4 w 100 100 m 200 100 l S "
        "q 150 150 40 40 re W n 0 1 0 rg 100 100 200 200 re f Q "
        "0.5 g q 1 0 0 1 250 350 cm /Fx Do Q "
        "q 40 0 0 20 100 300 cm /Im Do Q "
        "1 0 1 rg q 20 0 0 20 200 300 cm /St Do Q "
        "q 20 0 0 20 100 200 cm BI /W 1 /H 1 /CS /G /BPC 8 ID \x40 EI Q "
        "BT /T3 20 Tf 200 200 Td 0 0.5 0 rg (a) Tj 3 Tr (a) Tj ET "
        "0 0 0 rg 300 400 m 300 400 l f";
    const char *fx = "0 0 1 rg 0 0 20 20 re f";
    /* Two by one: a yellow sample then a cyan one. */
    const char im[] = "\xff\xff\x00\x00\xff\xff";
    /* Four by two, one bit: the left half painted (0), the right not. */
    const char st[] = "\x30\x30";
    /* A Type 3 glyph filling its whole em. */
    const char glyph[] = "1000 0 d0 0 0 1000 1000 re f";

    ws(&b, "%PDF-1.4\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] "
               "/Rotate 90 /Resources << /XObject << /Fx 5 0 R /Im 6 0 R "
               "/St 7 0 R >> /Font << /T3 8 0 R >> >> /Contents 4 0 R >>");
    stream(&b, 4, "", content, (long)strlen(content));
    stream(&b, 5, "/Type /XObject /Subtype /Form /BBox [0 0 20 20]", fx,
           (long)strlen(fx));
    stream(&b, 6, "/Type /XObject /Subtype /Image /Width 2 /Height 1 "
                  "/ColorSpace /DeviceRGB /BitsPerComponent 8", im, 6);
    stream(&b, 7, "/Type /XObject /Subtype /Image /Width 4 /Height 2 "
                  "/ImageMask true", st, 2);
    obj(&b, 8, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] "
               "/FontMatrix [0.001 0 0 0.001 0 0] /CharProcs << /box 9 0 R >> "
               "/Encoding << /Type /Encoding /Differences [97 /box] >> "
               "/FirstChar 97 /LastChar 97 /Widths [1000] >>");
    stream(&b, 9, "", glyph, (long)strlen(glyph));
    xref(&b, 1, 0);
    return b;
}

static void
render_test(void)
{
    pdfw_t b = drawn_file();
    tiku_pdf_err_t err;
    tiku_pdf_t *doc = open_bytes(&b, &err);
    tiku_pdf_pixmap_t pm = {0, 0, NULL}, band = {0, 0, NULL};
    int w = 0, h = 0;

    check(doc != NULL && tiku_pdf_render_size(doc, 0, 1.0, &w, &h) == 0 &&
          w == 400 && h == 300, "a 300 by 400 page turned 90 is 400 by 300");
    check(doc != NULL && tiku_pdf_render(doc, 0, 1.0, &pm) == 0 &&
          pm.w == 400 && pm.h == 300, "and renders at that size");
    if (pm.px == NULL) {
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    /* Turned clockwise: the page's bottom left is now the top left. */
    check(pixel(&pm, 10, 10) == 0xff0000u, "the red square, at the top left");
    check(pixel(&pm, 390, 290) == 0xffffffu, "paper where nothing was drawn");
    /* Turned clockwise, user (x, y) lands at device (y, x).  The stroke
     * ran along y=100 from x=100 to 200, 4 wide: now down x=100. */
    check(pixel(&pm, 100, 150) == 0x0000ffu && pixel(&pm, 96, 150) != 0x0000ffu,
          "a 4-point stroke, blue, where the turn put it");
    /* Green only inside the 40-point clip square (150..190). */
    check(pixel(&pm, 170, 170) == 0x00ff00u &&
          pixel(&pm, 120, 120) == 0xffffffu,
          "a fill stops at its clip");
    /* The form: blue 20-point square at user 250..270, 350..370. */
    check(pixel(&pm, 360, 260) == 0x0000ffu,
          "a form draws where its matrix puts it");
    /* The image: user 100..140 by 300..320 is device 300..320 by
     * 100..140; its first sample (yellow) is the left half in user
     * space, so the top in device space. */
    check(pixel(&pm, 310, 105) == 0xffff00u &&
          pixel(&pm, 310, 135) == 0x00ffffu,
          "an image's samples land where the matrix turns them");
    /* The stencil: left half of user 200..220 painted magenta. */
    check(pixel(&pm, 305, 205) == 0xff00ffu &&
          pixel(&pm, 305, 215) == 0xffffffu,
          "a stencil paints the fill colour through its bits");
    check(pixel(&pm, 210, 110) == 0x404040u, "an inline image draws too");
    /* Type 3 text: the em square at user 200..220 by 200..220, filled
     * dark green; the second glyph, invisible, would be at 220..240. */
    check(pixel(&pm, 210, 210) == 0x008000u &&
          pixel(&pm, 210, 230) == 0xffffffu,
          "a Type 3 glyph runs its procedure; render mode 3 draws none");
    check(tiku_pdf_render_rect(doc, 0, 1.0, 0, 150, 400, 20, &band) == 0 &&
          band.h == 20 && pixel(&band, 100, 0) == 0x0000ffu &&
          pixel(&band, 170, 19) == 0x00ff00u,
          "a band of rows shows the same ink");
    tiku_pdf_pixmap_free(&band);
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
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
        if (getenv("TIKU_PDF_PNG_DIR") != NULL) {
            /* The first pages drawn, for eyes: not a check, a look. */
            tiku_pdf_pixmap_t pm;
            char png[1200];
            int k;

            for (k = 0; k < pages && k < 3; k++) {
                if (tiku_pdf_render(doc, k, 1.5, &pm) == 0) {
                    snprintf(png, sizeof png, "%s/%.*s-%d.png",
                             getenv("TIKU_PDF_PNG_DIR"), (int)len - 4,
                             e->d_name, k);
                    (void)png_out(&pm, png);
                    tiku_pdf_pixmap_free(&pm);
                }
            }
        }
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
    render_test();
    shade_test();
    transp_test();
    nav_test();
    damage_test();
    corpus_test();
    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    puts("PASS: objects, tables rebuilt, object streams, filters, text");
    return 0;
}
