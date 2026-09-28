/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_pdf_cjk.c - composite fonts past Identity-H: codes of one to four
 * bytes by a CMap the file carries or names, the Unicode schemes read
 * without data, CMaps and CID tables read from a folder, text written
 * down the page, and a CJK face standing in for one the file leaves out.
 *
 * The glyphs are a face made here, a box and a half box, so where each
 * lands is known to the pixel on any machine.  Run with a folder, the
 * files are written there too, for poppler to read them beside.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_pdf.h"

static int fails;
static const char *out_dir;         /* where the files go for a look    */

static void
check(int cond, const char *what)
{
    printf("  [%s] CJK-001   %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/* ------------------------------------------------------------ building */

typedef struct {
    char *p;
    long  n, cap;
    long  off[32];
    int   nobj;
} pdfw_t;

static void
w(pdfw_t *b, const void *s, long n)
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

/** @brief Object @p num holding @p body. */
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

/** @brief Stream object @p num: @p inner in its dictionary, @p n bytes. */
static void
stream(pdfw_t *b, int num, const char *inner, const void *data, long n)
{
    char head[512];

    b->off[num] = b->n;
    if (num >= b->nobj) {
        b->nobj = num + 1;
    }
    snprintf(head, sizeof head, "%d 0 obj\n<< %s /Length %ld >>\nstream\n",
             num, inner, n);
    ws(b, head);
    w(b, data, n);
    ws(b, "\nendstream\nendobj\n");
}

/** @brief The table and trailer; the catalogue is object 1. */
static void
finish(pdfw_t *b)
{
    char line[64];
    long start = b->n;
    int i;

    snprintf(line, sizeof line, "xref\n0 %d\n", b->nobj);
    ws(b, line);
    ws(b, "0000000000 65535 f \n");
    for (i = 1; i < b->nobj; i++) {
        snprintf(line, sizeof line, "%010ld 00000 n \n", b->off[i]);
        ws(b, line);
    }
    snprintf(line, sizeof line, "trailer\n<< /Size %d /Root 1 0 R >>\n",
             b->nobj);
    ws(b, line);
    snprintf(line, sizeof line, "startxref\n%ld\n%%%%EOF\n", start);
    ws(b, line);
}

/* ------------------------------------------------------ the face here  */

/** @brief Big-endian 16 bits at @p p. */
static void
be16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

/** @brief Big-endian 32 bits at @p p. */
static void
be32(unsigned char *p, unsigned long v)
{
    be16(p, (unsigned)(v >> 16));
    be16(p + 2, (unsigned)(v & 0xFFFFu));
}

/** @brief A box glyph, 0 to @p right across and 0 to 800 up, into @p p
 *         (36 bytes). */
static void
box_glyph(unsigned char *p, int right)
{
    static const int dy[4] = {0, 800, 0, -800};
    int dx[4] = {0, 0, right, 0}, k;

    memset(p, 0, 36);
    be16(p, 1);                     /* one contour */
    be16(p + 6, (unsigned)right);
    be16(p + 8, 800);
    be16(p + 10, 3);                /* its last point */
    for (k = 0; k < 4; k++) {
        p[14 + k] = 1;              /* on the curve, long deltas */
        be16(p + 18 + 2 * k, (unsigned)dx[k] & 0xFFFFu);
        be16(p + 26 + 2 * k, (unsigned)dy[k] & 0xFFFFu);
    }
}

/**
 * @brief A TrueType face of three glyphs: none, a box 1000 wide and a
 *        half box 500 wide, 800 high, 1000 units to the em.  No
 *        character map: a CID font reaches them by number.
 * @return its length, into @p f (at most 512 bytes).
 */
static long
face(unsigned char *f)
{
    static const char *const tags[6] = {"glyf", "head", "hhea", "hmtx",
                                        "loca", "maxp"};
    static const long len[6] = {72, 54, 36, 12, 8, 6};
    unsigned char *t;
    long at = 12 + 16 * 6;
    int i;

    memset(f, 0, 512);
    be32(f, 0x00010000ul);
    be16(f + 4, 6);
    for (i = 0; i < 6; i++) {
        memcpy(f + 12 + 16 * i, tags[i], 4);
        be32(f + 12 + 16 * i + 8, (unsigned long)at);
        be32(f + 12 + 16 * i + 12, (unsigned long)len[i]);
        t = f + at;
        if (i == 0) {
            box_glyph(t, 1000);
            box_glyph(t + 36, 500);
        } else if (i == 1) {
            be32(t, 0x00010000ul);
            be32(t + 12, 0x5F0F3CF5ul);
            be16(t + 18, 1000);     /* units per em; short loca */
        } else if (i == 2) {
            be32(t, 0x00010000ul);
            be16(t + 4, 880);
            be16(t + 6, (unsigned)-120 & 0xFFFFu);
            be16(t + 34, 3);        /* advances for all three */
        } else if (i == 3) {
            be16(t, 1000);
            be16(t + 4, 1000);
            be16(t + 8, 500);
        } else if (i == 4) {
            be16(t + 4, 18);        /* glyph 1 at 36, 2 at 72, end 108 */
            be16(t + 6, 36);
        } else {
            be32(t, 0x00005000ul);
            be16(t + 4, 3);
        }
        at += (len[i] + 3) & ~3L;
    }
    /* loca's first two entries are 0: glyph 0 is empty. */
    return at;
}

/* -------------------------------------------------------- the documents */

/**
 * @brief A page showing @p content in font F1, a Type 0 font with
 *        /Encoding @p enc (a name, or "stream" for the CMap text
 *        @p cmap), over @p cidfont's CID font; @p tounicode maps its
 *        codes when not NULL.  The face made here is embedded when
 *        @p embed.
 */
static pdfw_t
document(const char *enc, const char *cmap, const char *cidfont,
         const char *tounicode, const char *content, int embed)
{
    pdfw_t b = {0};
    unsigned char font[512];
    char body[1024];
    long flen = face(font);

    ws(&b, "%PDF-1.7\n%\xe2\xe3\xcf\xd3\n");
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj(&b, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 800] "
               "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>");
    stream(&b, 4, "", content, (long)strlen(content));
    snprintf(body, sizeof body, "<< /Type /Font /Subtype /Type0 "
             "/BaseFont /TikuBox /Encoding %s /DescendantFonts [6 0 R]%s >>",
             strcmp(enc, "stream") == 0 ? "9 0 R" : enc,
             tounicode != NULL ? " /ToUnicode 10 0 R" : "");
    obj(&b, 5, body);
    snprintf(body, sizeof body, "<< /Type /Font /Subtype /CIDFontType2 "
             "/BaseFont /TikuBox /FontDescriptor 7 0 R "
             "/CIDToGIDMap /Identity %s >>", cidfont);
    obj(&b, 6, body);
    obj(&b, 7, embed ? "<< /Type /FontDescriptor /FontName /TikuBox "
                       "/Flags 4 /FontBBox [0 -120 1000 880] /ItalicAngle 0 "
                       "/Ascent 880 /Descent -120 /CapHeight 800 /StemV 80 "
                       "/FontFile2 8 0 R >>"
                     : "<< /Type /FontDescriptor /FontName /TikuBox "
                       "/Flags 4 /FontBBox [0 -120 1000 880] /ItalicAngle 0 "
                       "/Ascent 880 /Descent -120 /CapHeight 800 "
                       "/StemV 80 >>");
    stream(&b, 8, "", font, flen);
    stream(&b, 9, "/Type /CMap /CMapName /Tiku-Mixed-H", cmap ? cmap : "",
           cmap ? (long)strlen(cmap) : 0);
    stream(&b, 10, "", tounicode ? tounicode : "",
           tounicode ? (long)strlen(tounicode) : 0);
    finish(&b);
    return b;
}

/** @brief Open @p b, with CMaps from @p cmaps ("" for none at all), and
 *         write it to the look folder as @p name when there is one. */
static tiku_pdf_t *
open_doc(pdfw_t *b, const char *cmaps, const char *name)
{
    tiku_pdf_t *doc = NULL;

    if (out_dir != NULL) {
        char path[512];
        FILE *f;

        snprintf(path, sizeof path, "%s/%s.pdf", out_dir, name);
        f = fopen(path, "wb");
        if (f != NULL) {
            (void)fwrite(b->p, 1, (size_t)b->n, f);
            fclose(f);
        }
    }
    if (tiku_pdf_decode(b->p, (size_t)b->n, &doc) != TIKU_PDF_OK) {
        return NULL;
    }
    tiku_pdf_cmaps(doc, cmaps);
    return doc;
}

/** @brief The box of the first character @p cp on page 0, or 0. */
static int
char_box(tiku_pdf_t *doc, uint32_t cp, int nth, float *box)
{
    tiku_pdf_char_t *ch = NULL;
    long n = tiku_pdf_page_chars(doc, 0, &ch), i;
    int seen = 0;

    for (i = 0; i < n; i++) {
        if (ch[i].cp == cp && seen++ == nth) {
            box[0] = ch[i].x0;
            box[1] = ch[i].y0;
            box[2] = ch[i].x1;
            box[3] = ch[i].y1;
            free(ch);
            return 1;
        }
    }
    free(ch);
    return 0;
}

/** @brief Whether the drawn page is dark at user-space point @p x, @p y. */
static int
inked(const tiku_pdf_pixmap_t *pm, double x, double y)
{
    int px = (int)x, py = pm->h - 1 - (int)y;

    if (px < 0 || py < 0 || px >= pm->w || py >= pm->h) {
        return 0;
    }
    return (pm->px[(size_t)py * pm->w + px] & 0xFFu) < 0x40u;
}

static const char mixed_cmap[] =
    "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
    "/CIDSystemInfo << /Registry (Adobe) /Ordering (Japan1) /Supplement 6 "
    ">> def\n/CMapName /Tiku-Mixed-H def /CMapType 1 def\n"
    "2 begincodespacerange\n<00> <7F>\n<8140> <9FFC>\nendcodespacerange\n"
    "1 begincidchar\n<41> 2\nendcidchar\n"
    "1 begincidrange\n<8140> <8141> 1\nendcidrange\n"
    "endcmap CMapName currentdict /CMap defineresource pop end end\n";

static const char mixed_tounicode[] =
    "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
    "2 begincodespacerange\n<00> <7F>\n<8140> <9FFC>\nendcodespacerange\n"
    "3 beginbfchar\n<41> <0041>\n<42> <0042>\n<8140> <4E00>\nendbfchar\n"
    "1 beginbfrange\n<8141> <8141> <4E8C>\nendbfrange\n"
    "endcmap end end\n";

/**
 * @brief A CMap the file carries, of one- and two-byte codes: each code
 *        takes its own bytes, reaches the CID its map says and is as
 *        wide as that CID; one it does not map is .notdef.
 */
static void
carried_test(void)
{
    pdfw_t b = document("stream", mixed_cmap,
                        "/CIDSystemInfo << /Registry (Adobe) /Ordering "
                        "(Japan1) /Supplement 6 >> /DW 1000 /W [2 [500]]",
                        mixed_tounicode,
                        "BT /F1 100 Tf 20 600 Td <41814042> Tj ET\n"
                        "BT /F1 100 Tf 20 400 Td <8141> Tj ET", 1);
    tiku_pdf_t *doc = open_doc(&b, "", "cjk-carried");
    tiku_pdf_pixmap_t pm;
    float a[4], one[4], bb[4];
    char *text;

    check(doc != NULL, "a CMap stream's font opens");
    if (doc == NULL) {
        free(b.p);
        return;
    }
    text = tiku_pdf_page_text(doc, 0);
    check(text != NULL && strcmp(text, "A\xe4\xb8\x80" "B\n\xe4\xba\x8c") == 0,
          "one-byte and two-byte codes read as ToUnicode says");
    check(char_box(doc, 'A', 0, a) && char_box(doc, 0x4E00, 0, one) &&
          char_box(doc, 'B', 0, bb), "each code has its box");
    check(fabsf(a[2] - a[0] - 50) < 0.5f && fabsf(one[0] - 70) < 0.5f &&
          fabsf(one[2] - one[0] - 100) < 0.5f && fabsf(bb[0] - 170) < 0.5f,
          "widths by CID: the half box 50, the box 100, .notdef DW's 100");
    check(tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0, "the page draws");
    check(inked(&pm, 45, 640) && inked(&pm, 120, 640) &&
          !inked(&pm, 220, 640) && !inked(&pm, 45, 690),
          "<41> draws the half box, <8140> the box, <42> nothing");
    check(inked(&pm, 60, 440) && inked(&pm, 25, 440) && !inked(&pm, 90, 440),
          "<8141> is CID 2 by the run: the half box");
    tiku_pdf_pixmap_free(&pm);
    free(text);
    tiku_pdf_close(doc);
    free(b.p);
}

/** @brief The text of a page whose font's /Encoding is @p enc, showing
 *         @p hex, read with no CMap folder at all. */
static char *
text_of(const char *enc, const char *hex, const char *name)
{
    char content[256];
    pdfw_t b;
    tiku_pdf_t *doc;
    char *text = NULL;

    snprintf(content, sizeof content, "BT /F1 40 Tf 20 600 Td <%s> Tj ET",
             hex);
    b = document(enc, NULL, "/CIDSystemInfo << /Registry (Adobe) "
                 "/Ordering (Japan1) /Supplement 6 >>", NULL, content, 0);
    doc = open_doc(&b, "", name);
    if (doc != NULL) {
        text = tiku_pdf_page_text(doc, 0);
        tiku_pdf_close(doc);
    }
    free(b.p);
    return text;
}

/**
 * @brief Adobe's Unicode CMaps read with no data: their codes are the
 *        characters, UCS-2, UTF-16 with its surrogate pairs, UTF-32 and
 *        UTF-8; a legacy map is its code space, so ToUnicode still reads.
 */
static void
scheme_test(void)
{
    char *t;
    pdfw_t b;
    tiku_pdf_t *doc;

    t = text_of("/UniJIS-UCS2-H", "304230440041", "cjk-ucs2");
    check(t && strcmp(t, "\xe3\x81\x82\xe3\x81\x84" "A") == 0,
          "UniJIS-UCS2-H: the codes are the characters");
    free(t);
    t = text_of("/UniJIS-UTF16-H", "D840DC0B3042", "cjk-utf16");
    check(t && strcmp(t, "\xf0\xa0\x80\x8b\xe3\x81\x82") == 0,
          "UniJIS-UTF16-H: a surrogate pair is one character of 4 bytes");
    free(t);
    t = text_of("/UniJIS-UTF32-H", "0002000B00003042", "cjk-utf32");
    check(t && strcmp(t, "\xf0\xa0\x80\x8b\xe3\x81\x82") == 0,
          "UniJIS-UTF32-H: four bytes a character");
    free(t);
    t = text_of("/UniGB-UTF8-H", "E4B8AD41F0A0808B", "cjk-utf8");
    check(t && strcmp(t, "\xe4\xb8\xad" "A\xf0\xa0\x80\x8b") == 0,
          "UniGB-UTF8-H: one to four bytes a character");
    free(t);
    t = text_of("/Identity-H", "000100020003", "cjk-identity");
    check(t && t[0] == '\0', "Identity-H with no ToUnicode and no table: "
          "no text made up");
    free(t);

    b = document("/90ms-RKSJ-H", NULL, "/CIDSystemInfo << /Registry (Adobe) "
                 "/Ordering (Japan1) /Supplement 2 >>",
                 "begincmap 1 begincodespacerange <00> <FF> "
                 "endcodespacerange 3 beginbfchar <41> <0041> <82A0> <3042> "
                 "<82A2> <3044> endbfchar endcmap",
                 "BT /F1 40 Tf 20 600 Td <82A04182A2> Tj ET", 0);
    doc = open_doc(&b, "", "cjk-rksj");
    t = doc ? tiku_pdf_page_text(doc, 0) : NULL;
    check(t && strcmp(t, "\xe3\x81\x82" "A\xe3\x81\x84") == 0,
          "90ms-RKSJ-H with no data: its code space splits the bytes");
    free(t);
    tiku_pdf_close(doc);
    free(b.p);
}

/* A folder of CMaps and a CID table, as poppler-data lays them out; the
 * collection's CIDs mean here what these lines say. */
static const char test_h[] =
    "%!PS-Adobe-3.0 Resource-CMap\n"
    "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
    "/CIDSystemInfo 3 dict dup begin\n  /Registry (Adobe) def\n"
    "  /Ordering (Japan1) def\n  /Supplement 6 def\nend def\n"
    "/CMapName /Tiku-Test-H def\n/WMode 0 def\n"
    "2 begincodespacerange\n  <00> <80>\n  <8140> <9FFC>\n"
    "endcodespacerange\n"
    "1 beginnotdefrange\n<00> <1f> 3\nendnotdefrange\n"
    "2 begincidrange\n<20> <7e> 10\n<8140> <817e> 200\nendcidrange\n"
    "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
static const char test_v[] =
    "%!PS-Adobe-3.0 Resource-CMap\n"
    "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
    "/Tiku-Test-H usecmap\n"
    "/CMapName /Tiku-Test-V def\n/WMode 1 def\n"
    "1 begincidchar\n<8141> 1\nendcidchar\n"
    "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";

/** @brief @p text into file @p dir/@p rel, its folders made. */
static void
put_file(const char *dir, const char *rel, const char *text)
{
    char path[512], *slash;
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", dir, rel);
    for (slash = strchr(path + strlen(dir) + 1, '/'); slash != NULL;
         slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        (void)mkdir(path, 0755);
        *slash = '/';
    }
    f = fopen(path, "w");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

/** @brief Make the folder: the two CMaps and a CID table whose line n is
 *         CID n's character (200 and on: U+4E00 on). */
static void
make_folder(const char *dir)
{
    char *table = malloc(210 * 8), *p = table;
    int cid;

    put_file(dir, "cMap/Adobe-Japan1/Tiku-Test-H", test_h);
    put_file(dir, "cMap/Adobe-Japan1/Tiku-Test-V", test_v);
    for (cid = 0; cid < 210; cid++) {
        unsigned u = cid >= 200 ? 0x4E00u + (unsigned)(cid - 200)
                   : cid >= 10 && cid < 105 ? 0x20u + (unsigned)(cid - 10)
                   : 0u;

        p += sprintf(p, "%04x\n", u);
    }
    put_file(dir, "cidToUnicode/Adobe-Japan1", table);
    free(table);
}

/**
 * @brief CMaps named by the file and read from a folder: the code space
 *        and runs of one, the one it builds on (usecmap) with a code it
 *        changes, and text from the collection's CID table.
 */
static void
folder_test(const char *dir)
{
    const char *info = "/CIDSystemInfo << /Registry (Adobe) /Ordering "
                       "(Japan1) /Supplement 6 >>";
    pdfw_t b = document("/Tiku-Test-H", NULL, info, NULL,
                        "BT /F1 40 Tf 20 600 Td <418140418141> Tj ET", 1);
    tiku_pdf_t *doc = open_doc(&b, dir, "cjk-folder");
    char *t = doc ? tiku_pdf_page_text(doc, 0) : NULL;

    check(t && strcmp(t, "A\xe4\xb8\x80" "A\xe4\xb8\x81") == 0,
          "a named CMap from the folder, text from its CID table");
    free(t);
    tiku_pdf_close(doc);
    free(b.p);
    b = document("/Tiku-Test-V", NULL, info, NULL,
                 "BT /F1 40 Tf 200 700 Td <81408141> Tj ET", 1);
    doc = open_doc(&b, dir, "cjk-folder-v");
    t = doc ? tiku_pdf_page_text(doc, 0) : NULL;
    check(t && strcmp(t, "\xe4\xb8\x80") == 0,
          "usecmap: <8140> as its base maps it, <8141> changed to CID 1");
    free(t);
    tiku_pdf_close(doc);
    free(b.p);
    b = document("/Tiku-Test-H", NULL, info, NULL,
                 "BT /F1 40 Tf 20 600 Td <418140> Tj ET", 1);
    doc = open_doc(&b, "", "cjk-folder-none");
    t = doc ? tiku_pdf_page_text(doc, 0) : NULL;
    check(t && t[0] == '\0', "without the folder the same file reads as "
          "nothing, not as garbage");
    free(t);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Text written down the page (Identity-V): each glyph hangs from
 *        its vertical origin, half its width to the left and 880 up by
 *        DW2, or where W2 says; the pen goes down by its advance, TJ's
 *        numbers move it down too, and a new column reads as a new line.
 */
static void
vertical_test(void)
{
    pdfw_t b = document("/Identity-V", NULL,
                        "/CIDSystemInfo << /Registry (Adobe) /Ordering "
                        "(Identity) /Supplement 0 >> /W [2 [500]] "
                        "/W2 [2 [-500 250 880]]",
                        "begincmap 1 begincodespacerange <0000> <FFFF> "
                        "endcodespacerange 2 beginbfchar <0001> <0058> "
                        "<0002> <0059> endbfchar endcmap",
                        "BT /F1 100 Tf 300 700 Td <000100020001> Tj "
                        "-150 0 Td [<0001> 500 <0001>] TJ ET", 1);
    tiku_pdf_t *doc = open_doc(&b, "", "cjk-vertical");
    tiku_pdf_pixmap_t pm;
    float x[4], y[4], x2[4];
    char *t = doc ? tiku_pdf_page_text(doc, 0) : NULL;

    check(t && strcmp(t, "XYX\nX X") == 0,
          "down a column, the next column a new line, TJ's gap a space");
    check(char_box(doc, 'X', 0, x) && char_box(doc, 'Y', 0, y) &&
          char_box(doc, 'X', 1, x2), "each glyph has its box");
    /* Boxes are as drawn, y down from the page's top edge (800): a
     * glyph's em from its vertical origin down by its advance. */
    check(fabsf(x[0] - 250) < 0.5f && fabsf(x[2] - 350) < 0.5f &&
          fabsf(x[1] - 100) < 0.5f && fabsf(x[3] - 200) < 0.5f,
          "a glyph of DW2: 50 either side of the pen, an em down");
    check(fabsf(y[0] - 275) < 0.5f && fabsf(y[2] - 325) < 0.5f &&
          fabsf(y[1] - 200) < 0.5f && fabsf(y[3] - 250) < 0.5f,
          "W2's glyph: 25 either side, its advance half an em");
    check(fabsf(x2[1] - 250) < 0.5f, "the pen went down 100, then 50");
    check(doc && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0, "the page draws");
    if (doc != NULL) {
        check(inked(&pm, 260, 690) && !inked(&pm, 240, 690) &&
              inked(&pm, 340, 620) && !inked(&pm, 300, 700),
              "the box hangs from the pen: 50 left, 8 to 88 below");
        check(inked(&pm, 285, 580) && !inked(&pm, 270, 580) &&
              !inked(&pm, 330, 580) && inked(&pm, 255, 470) &&
              !inked(&pm, 255, 455),
              "the half box by W2, then the box 50 lower");
        check(inked(&pm, 160, 650) && !inked(&pm, 160, 580) &&
              inked(&pm, 160, 500),
              "TJ's 500 moves the second column's pen 50 down");
        tiku_pdf_pixmap_free(&pm);
    }
    free(t);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief A CJK font the file names and does not carry: a face of the
 *        machine's stands in, chosen by the font's collection, and draws
 *        each character where the file's widths put it.
 */
static void
standin_test(void)
{
    pdfw_t b = document("/UniJIS-UCS2-H", NULL,
                        "/CIDSystemInfo << /Registry (Adobe) /Ordering "
                        "(Japan1) /Supplement 6 >>", NULL,
                        "BT /F1 60 Tf 20 600 Td <65E5672C> Tj ET", 0);
    tiku_pdf_t *doc = open_doc(&b, NULL, "cjk-standin");
    tiku_pdf_pixmap_t pm;
    long ink = 0, x, y;

    if (doc == NULL || tiku_pdf_render(doc, 0, 1.0, 0, &pm) != 0) {
        check(0, "the stand-in page draws");
        tiku_pdf_close(doc);
        free(b.p);
        return;
    }
    for (y = 560; y < 680; y++) {
        for (x = 20; x < 140; x++) {
            ink += inked(&pm, x, y);
        }
    }
    if (ink == 0) {
        puts("  SKIP: no CJK face on this machine to stand in");
    } else {
        check(ink > 300, "a CJK face stands in: \xe6\x97\xa5\xe6\x9c\xac "
              "drawn");
    }
    tiku_pdf_pixmap_free(&pm);
    tiku_pdf_close(doc);
    free(b.p);
}

/**
 * @brief Maps a careless or hostile writer makes: a run over every code
 *        and every CID, a code space that is not one, a map built on
 *        itself, codes cut short, and W2 gone wrong.  Read, not trusted.
 */
static void
damage_test(void)
{
    static const char *const cmaps[] = {
        "begincmap 1 begincodespacerange <00000000> <FFFFFFFF> "
        "endcodespacerange 1 begincidrange <00000000> <FFFFFFFF> 0 "
        "endcidrange endcmap",
        "begincmap 1 begincodespacerange <0000> <FFFF> endcodespacerange "
        "1 begincidrange <0000> <FFFF> 65000 endcidrange endcmap",
        "begincmap 2 begincodespacerange <FF> <00> <00> endcodespacerange "
        "1 begincidchar <41> 99999999 endcidchar /Tiku-Mixed-H usecmap "
        "/Identity-H usecmap /Identity-H usecmap /WMode (1) def endcmap",
        "begincmap 1 begincodespacerange <0000> <FFFF> endcodespacerange "
        "3 begincidrange <FFFF> <0000> 1 <41> <4142> 2 <> <> 3 endcidrange",
    };
    size_t i;

    for (i = 0; i < sizeof cmaps / sizeof cmaps[0]; i++) {
        pdfw_t b = document("stream", cmaps[i],
                            "/W2 [1 [1 2] 5 4 [] 1 2 3 4 5 6] /DW2 [1]",
                            NULL, "BT /F1 50 Tf 20 600 Td <FFFFFFFF41> Tj "
                            "[<00> -1e30 <8140>] TJ ET", 1);
        tiku_pdf_t *doc = open_doc(&b, "", "cjk-damage");
        tiku_pdf_pixmap_t pm;
        char *t = doc ? tiku_pdf_page_text(doc, 0) : NULL;

        if (doc != NULL && tiku_pdf_render(doc, 0, 1.0, 0, &pm) == 0) {
            tiku_pdf_pixmap_free(&pm);
        }
        free(t);
        tiku_pdf_close(doc);
        free(b.p);
    }
    check(1, "damaged CMaps and W2 read without harm");
}

/** @brief The folder given back. */
static void
clean_folder(const char *dir)
{
    static const char *const made[] = {
        "cMap/Adobe-Japan1/Tiku-Test-H", "cMap/Adobe-Japan1/Tiku-Test-V",
        "cidToUnicode/Adobe-Japan1", "cMap/Adobe-Japan1", "cMap",
        "cidToUnicode", "",
    };
    char path[512];
    size_t i;

    for (i = 0; i < sizeof made / sizeof made[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, made[i]);
        if (unlink(path) != 0) {
            (void)rmdir(path);
        }
    }
}

int
main(int argc, char **argv)
{
    char dir[] = "/tmp/tikupdf-cjk-XXXXXX";

    out_dir = argc > 1 ? argv[1] : NULL;
    printf("Composite fonts: CMaps, vertical text, CJK stand-ins\n");
    if (mkdtemp(dir) == NULL) {
        puts("FAIL: no temporary folder");
        return 1;
    }
    make_folder(dir);
    carried_test();
    scheme_test();
    folder_test(dir);
    vertical_test();
    standin_test();
    damage_test();
    clean_folder(dir);
    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    puts("PASS: CMaps carried and named, Unicode schemes, folders, "
         "vertical text, stand-ins");
    return 0;
}
