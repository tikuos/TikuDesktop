/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_jbig2.c - JBIG2 decoding: the arithmetic decoder against T.88's
 * own test sequence, the standard Huffman tables coding what they say,
 * and pages others coded or read -- jbig2enc's, and streams made for
 * these tests that jbig2dec or poppler decode alike -- each to the very
 * page.  Then a file's two layouts, a PDF drawing a JBIG2 image and a
 * stencil through JBIG2Globals, and every stream damaged.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tiku_jbig2.h"
#include "tiku_pdf.h"
#include "../../kits/translation/tiku_jbig2_private.h"
#include "../fixtures/jbig2.inc"

#define OMAX (1L << 24)

static int fails;

/** @brief One check's line, and its failure counted. */
static void
check(int cond, const char *what)
{
    printf("  [%s] JB2-001   %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief FNV-1a 64 of @p n bytes. */
static unsigned long long
fnv(const unsigned char *p, long n)
{
    unsigned long long h = 0xcbf29ce484222325ull;
    long i;

    for (i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001b3ull;
    }
    return h;
}

/** @brief The MQ decoder against T.88 H.2: 256 decisions, one context. */
static void
mq_test(void)
{
    tiku_mq_t d;
    tiku_mq_cx_t cx = {0, 0};
    unsigned char got[32] = {0};
    int i;

    tiku_mq_init(&d, jb_mq_coded, sizeof jb_mq_coded);
    for (i = 0; i < 256; i++) {
        got[i / 8] |= (unsigned char)(tiku_mq_bit(&d, &cx) << (7 - i % 8));
    }
    check(memcmp(got, jb_mq_plain, sizeof got) == 0,
          "the arithmetic decoder reads T.88's test sequence (H.2)");
}

/** @brief @p n bits of @p v into @p buf at bit @p *at. */
static void
put(unsigned char *buf, long *at, uint32_t v, int n)
{
    while (n-- > 0) {
        if ((v >> n) & 1u) {
            buf[*at >> 3] |= (unsigned char)(0x80u >> (*at & 7));
        }
        (*at)++;
    }
}

/** @brief Each standard table's lines, coded by their prefix and a value
 *         one into their range, read back as that value. */
static void
huffman_test(void)
{
    int k, i, ok = 1;

    for (k = 1; k <= 15; k++) {
        jb_line_t lines[24];
        jb_table_t t;

        jb_table_std(k, &t, lines);
        for (i = 0; i < t.n; i++) {
            const jb_line_t *l = &t.line[i];
            unsigned char buf[16] = {0};
            jb_bits_t b = {buf, sizeof buf, 0};
            long at = 0;
            uint32_t off = l->rangel > 0 ? 1u : 0u;
            int32_t got = 0;
            int rc;

            put(buf, &at, l->code, l->prefl);
            put(buf, &at, off, l->rangel);
            rc = jb_huff(&b, &t, &got);
            ok &= l->kind == JB_OOB ? rc == 1 :
                  rc == 0 && got == (l->kind == JB_LOWER
                                     ? l->low - (int32_t)off
                                     : l->low + (int32_t)off);
        }
    }
    check(ok, "the fifteen standard tables code each line's values "
          "(B.1 to B.15)");
}

/** @brief Every fixture to the page others read from it. */
static void
stream_test(void)
{
    size_t i;

    for (i = 0; i < sizeof jb_fixtures / sizeof jb_fixtures[0]; i++) {
        const jb_fixture_t *f = &jb_fixtures[i];
        unsigned char *out = NULL;
        int w = 0, h = 0;
        long n = tiku_jbig2_decode(f->glob, f->gn, f->data, f->n, &out, OMAX,
                                   &w, &h);
        char what[96];

        snprintf(what, sizeof what, "%s: the %dx%d page it codes", f->name,
                 f->w, f->h);
        check(n == (long)((f->w + 7) / 8) * f->h && w == f->w && h == f->h &&
              fnv(out, n) == f->hash, what);
        free(out);
    }
}

/** @brief A JBIG2 file of one page, laid out either way; no second page;
 *         no header, no file. */
static void
file_test(void)
{
    unsigned char *out = NULL;
    int w = 0, h = 0;
    long n;

    n = tiku_jbig2_file(jb_file_sequential, sizeof jb_file_sequential, 1,
                        &out, OMAX, &w, &h);
    check(n > 0 && w == JB_TEXT_W && h == JB_TEXT_H &&
          fnv(out, n) == JB_TEXT_HASH, "a file laid out sequentially");
    free(out);
    n = tiku_jbig2_file(jb_file_random, sizeof jb_file_random, 1, &out, OMAX,
                        &w, &h);
    check(n > 0 && w == JB_TEXT_W && h == JB_TEXT_H &&
          fnv(out, n) == JB_TEXT_HASH,
          "the same file laid out for random access: headers, then data");
    free(out);
    check(tiku_jbig2_file(jb_file_random, sizeof jb_file_random, 2, &out,
                          OMAX, &w, &h) < 0 &&
          tiku_jbig2_file(gen_text, sizeof gen_text, 1, &out, OMAX, &w,
                          &h) < 0,
          "no second page in it, and no file without its header");
}

/* ------------------------------------------------------------- the PDF  */

typedef struct {
    unsigned char *p;
    long           n, cap, off[8];
    int            nobj;
} pdfw_t;

/** @brief @p n bytes onto @p b. */
static void
w(pdfw_t *b, const void *s, long n)
{
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, (size_t)b->cap);
    }
    memcpy(b->p + b->n, s, (size_t)n);
    b->n += n;
}

/** @brief Object @p num: a dictionary @p head, and stream bytes when
 *         @p data is not NULL. */
static void
obj(pdfw_t *b, int num, const char *head, const unsigned char *data,
    long n)
{
    char line[64];

    b->off[num] = b->n;
    b->nobj = num + 1 > b->nobj ? num + 1 : b->nobj;
    snprintf(line, sizeof line, "%d 0 obj\n", num);
    w(b, line, (long)strlen(line));
    w(b, head, (long)strlen(head));
    if (data != NULL) {
        w(b, "\nstream\n", 8);
        w(b, data, n);
        w(b, "\nendstream", 10);
    }
    w(b, "\nendobj\n", 8);
}

/** @brief A page drawing the symbol-coded fixture @p iw wide: as a grey
 *         image, or as a stencil in red; its JBIG2Globals apart. */
static pdfw_t
jbig2_pdf(int stencil, int iw)
{
    const jb_fixture_t *f = &jb_fixtures[1];
    pdfw_t b = {0};
    char head[320], content[96];
    long start;
    int i;

    w(&b, "%PDF-1.5\n", 9);
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>", NULL, 0);
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", NULL, 0);
    snprintf(head, sizeof head, "<< /Type /Page /Parent 2 0 R /MediaBox "
             "[0 0 %d %d] /Resources << /XObject << /Im1 5 0 R >> >> "
             "/Contents 4 0 R >>", f->w, f->h);
    obj(&b, 3, head, NULL, 0);
    snprintf(content, sizeof content, "%sq %d 0 0 %d 0 0 cm /Im1 Do Q",
             stencil ? "1 0 0 rg " : "", iw, f->h);
    snprintf(head, sizeof head, "<< /Length %ld >>", (long)strlen(content));
    obj(&b, 4, head, (const unsigned char *)content, (long)strlen(content));
    snprintf(head, sizeof head, "<< /Type /XObject /Subtype /Image /Width %d "
             "/Height %d %s /Filter /JBIG2Decode /DecodeParms << "
             "/JBIG2Globals 6 0 R >> /Length %ld >>", iw, f->h,
             stencil ? "/ImageMask true"
                     : "/ColorSpace /DeviceGray /BitsPerComponent 1", f->n);
    obj(&b, 5, head, f->data, f->n);
    snprintf(head, sizeof head, "<< /Length %ld >>", f->gn);
    obj(&b, 6, head, f->glob, f->gn);
    start = b.n;
    snprintf(head, sizeof head, "xref\n0 %d\n0000000000 65535 f \n", b.nobj);
    w(&b, head, (long)strlen(head));
    for (i = 1; i < b.nobj; i++) {
        snprintf(head, sizeof head, "%010ld 00000 n \n", b.off[i]);
        w(&b, head, (long)strlen(head));
    }
    snprintf(head, sizeof head, "trailer\n<< /Size %d /Root 1 0 R >>\n"
             "startxref\n%ld\n%%%%EOF\n", b.nobj, start);
    w(&b, head, (long)strlen(head));
    return b;
}

/** @brief How many of page @p pm's pixels differ from the JBIG2 page
 *         @p bits (1 black) drawn @p ink on white, over @p iw columns. */
static long
unlike(const tiku_pdf_pixmap_t *pm, const unsigned char *bits, int bw,
       int iw, uint32_t ink)
{
    long rb = (bw + 7) / 8, bad = 0, x, y;

    for (y = 0; y < pm->h; y++) {
        for (x = 0; x < pm->w; x++) {
            int black = x < iw && x < bw &&
                        (bits[y * rb + x / 8] & (0x80 >> (x & 7)));
            uint32_t want = black ? ink : 0xffffffffu;

            bad += (pm->px[y * pm->w + x] | 0xff000000u) != want;
        }
    }
    return bad;
}

/** @brief JBIG2 through a PDF: the image and the stencil drawn exactly
 *         as the page decodes, and an image narrower than the page cut
 *         to its width. */
static void
pdf_test(void)
{
    const jb_fixture_t *f = &jb_fixtures[1];
    unsigned char *bits = NULL;
    int bw = 0, bh = 0, k;
    static const struct { int stencil, iw; uint32_t ink; const char *what; }
    cases[3] = {
        {0, 0, 0xff000000u, "a JBIG2 image in a PDF draws its page, "
                            "JBIG2Globals first"},
        {1, 0, 0xffff0000u, "a JBIG2 stencil paints the fill colour"},
        {0, 400, 0xff000000u, "an image narrower than its JBIG2 page is "
                              "the page cut to its width"},
    };

    (void)tiku_jbig2_decode(f->glob, f->gn, f->data, f->n, &bits, OMAX, &bw,
                            &bh);
    for (k = 0; k < 3; k++) {
        int iw = cases[k].iw ? cases[k].iw : f->w;
        pdfw_t b = jbig2_pdf(cases[k].stencil, iw);
        tiku_pdf_t *doc = NULL;
        tiku_pdf_pixmap_t pm;

        if (tiku_pdf_decode(b.p, (size_t)b.n, &doc) != TIKU_PDF_OK ||
            tiku_pdf_render(doc, 0, 1.0, 0, &pm) != 0) {
            check(0, cases[k].what);
        } else {
            check(bits != NULL && unlike(&pm, bits, bw, iw, cases[k].ink) == 0,
                  cases[k].what);
            tiku_pdf_pixmap_free(&pm);
        }
        tiku_pdf_close(doc);
        free(b.p);
    }
    free(bits);
}

/* ----------------------------------------------------------- damage  */

/** @brief Every fixture cut short and with bytes changed, a page too
 *         big to make, and one that asks for endless drawing: each read
 *         without harm, a page or none, soon. */
static void
damage_test(void)
{
    static const unsigned char huge[] = {
        0, 0, 0, 0, 48, 0, 1, 0, 0, 0, 19,
        0x7f, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0,
    };
    unsigned long seed = 0x5eed2b16ul;
    unsigned char *copy, *out;
    clock_t start;
    size_t i;
    int k, w, h;

    for (i = 0; i < sizeof jb_fixtures / sizeof jb_fixtures[0]; i++) {
        const jb_fixture_t *f = &jb_fixtures[i];

        copy = malloc((size_t)f->n);
        for (k = 0; k < 40; k++) {
            long cut = k < 8 ? f->n * k / 8 : f->n;

            memcpy(copy, f->data, (size_t)f->n);
            if (k >= 8) {
                int flips = 1 + k % 4;

                while (flips-- > 0) {
                    seed = seed * 1103515245ul + 12345ul;
                    copy[(seed >> 8) % (unsigned long)f->n] ^=
                        (unsigned char)(seed >> 20);
                }
            }
            out = NULL;
            (void)tiku_jbig2_decode(f->glob, f->gn, copy, cut, &out, OMAX, &w,
                                    &h);
            free(out);
        }
        free(copy);
    }
    out = NULL;
    check(tiku_jbig2_decode(NULL, 0, huge, sizeof huge, &out, OMAX, &w, &h) <
          0 && out == NULL, "a page too big to make is refused, not made");
    start = clock();
    (void)tiku_jbig2_decode(NULL, 0, jb_bomb, sizeof jb_bomb, &out, OMAX, &w,
                            &h);
    free(out);
    check((double)(clock() - start) / CLOCKS_PER_SEC < 3.0,
          "a small stream placing billions of pixels stops at the bound");
    check(1, "every stream cut short or changed reads without harm");
}

int
main(void)
{
    printf("JBIG2: the coders, others' pages, files, a PDF, damage\n");
    mq_test();
    huffman_test();
    stream_test();
    file_test();
    pdf_test();
    damage_test();
    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    puts("PASS: JBIG2 decoded as jbig2enc wrote it and jbig2dec and "
         "poppler read it");
    return 0;
}
