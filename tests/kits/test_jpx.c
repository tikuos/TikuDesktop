/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_jpx.c - JPEG 2000 decoding: streams OpenJPEG coded in each
 * progression, code-block style and arrangement, and JP2 files with a
 * palette, opacity and sYCC, each to what a reader not the kit's makes
 * of it; a picture too big read smaller; the JPXDecode images of a PDF
 * with their colour spaces and masks; and every stream damaged.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tiku_jpx.h"
#include "tiku_pdf.h"
#include "../fixtures/jpx.inc"

#define MAXPX    (1L << 22)
#define VIEWERPX (24L * 1024L * 1024L)  /* what a PDF page decodes whole */

static int fails;

/** @brief One check's line, and its failure counted. */
static void
check(int cond, const char *what)
{
    printf("  [%s] JPX-001   %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief FNV-1a 64 of @p img's channels, interleaved, as the fixtures
 *         were hashed. */
static unsigned long long
img_hash(const tiku_jpx_t *img)
{
    unsigned long long h = 0xcbf29ce484222325ull;
    long i;
    int c;

    for (i = 0; i < (long)img->w * img->h; i++) {
        for (c = 0; c < img->ncomp; c++) {
            h = (h ^ img->comp[c][i]) * 0x100000001b3ull;
        }
    }
    return h;
}

/** @brief The fixture named @p name. */
static const jx_fixture_t *
fixture(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof jx_fixtures / sizeof jx_fixtures[0]; i++) {
        if (!strcmp(jx_fixtures[i].name, name)) {
            return &jx_fixtures[i];
        }
    }
    return NULL;
}

/** @brief Each fixture to the very samples, size, space and opacity its
 *         entry names. */
static void
fixture_test(void)
{
    size_t i;

    for (i = 0; i < sizeof jx_fixtures / sizeof jx_fixtures[0]; i++) {
        const jx_fixture_t *f = &jx_fixtures[i];
        tiku_jpx_t img;
        int ok = tiku_jpx_decode(f->data, f->n, MAXPX, &img) == 0;

        check(ok && img.w == f->w && img.h == f->h && img.ncomp == f->ncomp &&
              img.space == f->space && img.alpha == f->alpha &&
              img_hash(&img) == f->hash, f->what);
        tiku_jpx_free(&img);
    }
}

/** @brief The 9/7 stream sample by sample within one of OpenJPEG's own
 *         reading of it. */
static void
ict_test(void)
{
    const jx_fixture_t *f = fixture("ict");
    tiku_jpx_t img;
    long i, worst = 0;
    int c;

    if (f == NULL || tiku_jpx_decode(f->data, f->n, MAXPX, &img) != 0) {
        check(0, "a 9/7 picture is within one of OpenJPEG's reading");
        return;
    }
    for (i = 0; i < (long)img.w * img.h; i++) {
        for (c = 0; c < 3; c++) {
            long d = (long)img.comp[c][i] - jx_ict_opj[i * 3 + c];

            worst = d < 0 ? (-d > worst ? -d : worst) : (d > worst ? d : worst);
        }
    }
    check(img.w * img.h * 3 == (long)sizeof jx_ict_opj && worst <= 1,
          "a 9/7 picture is within one of OpenJPEG's reading, sample by "
          "sample");
    tiku_jpx_free(&img);
}

/**
 * @brief Pictures bigger than asked for: the colour one read two
 *        resolutions down as OpenJPEG's -r 2 reads it, the flat one of
 *        30M samples at half its size, and one that cannot come down
 *        far enough refused.
 */
static void
size_test(void)
{
    const jx_fixture_t *f = fixture("rct");
    tiku_jpx_t img = {0};
    long i, off = 0;

    check(f != NULL && tiku_jpx_decode(f->data, f->n, 24, &img) == 0 &&
          img.w == JX_REDUCED_W && img.h == JX_REDUCED_H &&
          img_hash(&img) == JX_REDUCED_HASH,
          "a picture past the size asked comes two resolutions down, as "
          "OpenJPEG reads it so");
    tiku_jpx_free(&img);
    if (tiku_jpx_decode(jx_flat, sizeof jx_flat, VIEWERPX, &img) == 0) {
        for (i = 0; i < (long)img.w * img.h; i++) {
            off += img.comp[0][i] != 128;
        }
    }
    check(img.w == 3000 && img.h == 2500 && off == 0,
          "30M samples a viewer will not hold come at 3000 x 2500, "
          "unchanged");
    tiku_jpx_free(&img);
    check(tiku_jpx_decode(jx_flat, sizeof jx_flat, 1, &img) < 0,
          "one that cannot come down to the size asked is refused");
}

/* --------------------------------------------------------------- PDF  */

/** @brief A PDF being written. */
typedef struct {
    unsigned char *p;               /* its bytes                         */
    long           n, cap;          /* how many; room for                */
    long           off[8];          /* each object's offset              */
    int            nobj;            /* objects numbered below this       */
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

/** @brief One page's picture: the image's own entries, its stream, and
 *         the soft mask's stream when it has one. */
typedef struct {
    const char          *entries;   /* ColorSpace, Width, masks ...      */
    const unsigned char *data;      /* the JPEG 2000 stream              */
    long                 n;         /* its bytes                         */
    const unsigned char *mask;      /* a JPEG 2000 soft mask, or NULL    */
    long                 maskn;     /* its bytes                         */
    const char          *under;     /* what is painted first             */
} pic_t;

/** @brief A page @p pw by @p ph drawing @p pic over all of it. */
static pdfw_t
jpx_pdf(int pw, int ph, const pic_t *pic)
{
    pdfw_t b = {0};
    char head[2400], content[160];
    long start;
    int i;

    w(&b, "%PDF-1.5\n", 9);
    obj(&b, 1, "<< /Type /Catalog /Pages 2 0 R >>", NULL, 0);
    obj(&b, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", NULL, 0);
    snprintf(head, sizeof head, "<< /Type /Page /Parent 2 0 R /MediaBox "
             "[0 0 %d %d] /Resources << /XObject << /Im1 5 0 R >> >> "
             "/Contents 4 0 R >>", pw, ph);
    obj(&b, 3, head, NULL, 0);
    snprintf(content, sizeof content, "%s q %d 0 0 %d 0 0 cm /Im1 Do Q",
             pic->under, pw, ph);
    snprintf(head, sizeof head, "<< /Length %ld >>", (long)strlen(content));
    obj(&b, 4, head, (const unsigned char *)content, (long)strlen(content));
    snprintf(head, sizeof head, "<< /Type /XObject /Subtype /Image "
             "/Filter /JPXDecode %s%s /Length %ld >>", pic->entries,
             pic->mask != NULL ? " /SMask 6 0 R" : "", pic->n);
    obj(&b, 5, head, pic->data, pic->n);
    if (pic->mask != NULL) {
        snprintf(head, sizeof head, "<< /Type /XObject /Subtype /Image "
                 "/Width 24 /Height 16 /ColorSpace /DeviceGray "
                 "/BitsPerComponent 8 /Filter /JPXDecode /Length %ld >>",
                 pic->maskn);
        obj(&b, 6, head, pic->mask, pic->maskn);
    }
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

/** @brief How many of page @p pm's pixels are more than one away from
 *         @p want (RGB triples); every pixel when it is another size. */
static long
unlike(const tiku_pdf_pixmap_t *pm, const unsigned char *want, int ww,
       int wh)
{
    long i, bad = 0;
    int k;

    if (pm->w != ww || pm->h != wh) {
        return (long)ww * wh;
    }
    for (i = 0; i < (long)ww * wh; i++) {
        for (k = 0; k < 3; k++) {
            int d = (int)((pm->px[i] >> (16 - 8 * k)) & 255) - want[i * 3 + k];

            if (d > 1 || d < -1) {
                bad++;
                break;
            }
        }
    }
    return bad;
}

/** @brief @p pic drawn on a page @p pw by @p ph at 72 dpi, and checked
 *         against @p want as @p what. */
static void
draw_check(int pw, int ph, const pic_t *pic, const unsigned char *want,
           const char *what)
{
    pdfw_t b = jpx_pdf(pw, ph, pic);
    tiku_pdf_t *doc = NULL;
    tiku_pdf_pixmap_t pm;

    if (tiku_pdf_decode(b.p, (size_t)b.n, &doc) != TIKU_PDF_OK ||
        tiku_pdf_render(doc, 0, 1.0, 0, &pm) != 0) {
        check(0, what);
    } else {
        check(unlike(&pm, want, pw, ph) == 0, what);
        tiku_pdf_pixmap_free(&pm);
    }
    tiku_pdf_close(doc);
    free(b.p);
}

/** @brief @p c at opacity @p a over @p bg, as eight bits. */
static unsigned char
over(unsigned c, unsigned a, unsigned bg)
{
    return (unsigned char)((c * a + bg * (255u - a) + 127u) / 255u);
}

/**
 * @brief The expected pages: @p rgb of the colour fixture, then over
 *        red the RGBA one's colours at its opacity (@p alpha), over blue
 *        the colour one through the grey one (@p masked), and the grey
 *        one's samples as indices (@p indexed).  @return 0, or -1.
 */
static int
expected(unsigned char *rgb, unsigned char *alpha, unsigned char *masked,
         unsigned char *indexed)
{
    const jx_fixture_t *fc = fixture("rct"), *fa = fixture("rgba");
    const jx_fixture_t *fg = fixture("grey");
    tiku_jpx_t c = {0}, a = {0}, g = {0};
    long i;
    int k, rc = -1;

    if (tiku_jpx_decode(fc->data, fc->n, MAXPX, &c) == 0 &&
        tiku_jpx_decode(fa->data, fa->n, MAXPX, &a) == 0 &&
        tiku_jpx_decode(fg->data, fg->n, MAXPX, &g) == 0) {
        for (i = 0; i < 24 * 16; i++) {
            unsigned s = g.comp[0][i];

            for (k = 0; k < 3; k++) {
                rgb[i * 3 + k] = c.comp[k][i];
                alpha[i * 3 + k] = over(a.comp[k][i], a.comp[3][i],
                                        k == 0 ? 255u : 0u);
                masked[i * 3 + k] = over(c.comp[k][i], s, k == 2 ? 255u : 0u);
            }
            indexed[i * 3] = (unsigned char)s;
            indexed[i * 3 + 1] = (unsigned char)(255u - s);
            indexed[i * 3 + 2] = (unsigned char)((s * 3u) & 255u);
        }
        rc = 0;
    }
    tiku_jpx_free(&c);
    tiku_jpx_free(&a);
    tiku_jpx_free(&g);
    return rc;
}

/**
 * @brief JPXDecode through a PDF: in the dictionary's colour space and
 *        in the file's own, its opacity under SMaskInData, a JPEG 2000
 *        soft mask, an Indexed space over its samples, a palette of its
 *        own, and a picture bigger than a page decodes whole.
 */
static void
pdf_test(void)
{
    static unsigned char rgb[24 * 16 * 3], alpha[24 * 16 * 3];
    static unsigned char masked[24 * 16 * 3], indexed[24 * 16 * 3];
    static unsigned char pal[24 * 16 * 3], flat[60 * 50 * 3];
    static char lookup[2048];
    const jx_fixture_t *fc = fixture("rct"), *fp = fixture("palette");
    const jx_fixture_t *fa = fixture("rgba"), *fg = fixture("grey");
    tiku_jpx_t p;
    int i, at;

    if (expected(rgb, alpha, masked, indexed) != 0 ||
        tiku_jpx_decode(fp->data, fp->n, MAXPX, &p) != 0) {
        check(0, "the fixtures a PDF draws decode");
        return;
    }
    for (i = 0; i < 24 * 16 * 3; i++) {
        pal[i] = p.comp[i % 3][i / 3];
    }
    tiku_jpx_free(&p);
    memset(flat, 128, sizeof flat);
    at = snprintf(lookup, sizeof lookup, "/Width 24 /Height 16 /ColorSpace "
                  "[/Indexed /DeviceRGB 255 <");
    for (i = 0; i < 256; i++) {
        at += snprintf(lookup + at, sizeof lookup - (size_t)at, "%02x%02x%02x",
                       i, 255 - i, (i * 3) & 255);
    }
    snprintf(lookup + at, sizeof lookup - (size_t)at, ">]");
    {
        const pic_t pics[7] = {
            {"/Width 24 /Height 16 /ColorSpace /DeviceRGB "
             "/BitsPerComponent 8", fc->data, fc->n, NULL, 0, ""},
            {"/Width 24 /Height 16", fc->data, fc->n, NULL, 0, ""},
            {"/Width 24 /Height 16 /SMaskInData 1", fa->data, fa->n, NULL, 0,
             "1 0 0 rg 0 0 24 16 re f"},
            {"/Width 24 /Height 16 /ColorSpace /DeviceRGB", fc->data, fc->n,
             fg->data, fg->n, "0 0 1 rg 0 0 24 16 re f"},
            {lookup, fg->data, fg->n, NULL, 0, ""},
            {"/Width 24 /Height 16", fp->data, fp->n, NULL, 0, ""},
            {"/Width 6000 /Height 5000 /ColorSpace /DeviceGray", jx_flat,
             sizeof jx_flat, NULL, 0, ""},
        };

        draw_check(24, 16, &pics[0], rgb, "a JPEG 2000 in a PDF draws in "
                   "the colour space its dictionary names");
        draw_check(24, 16, &pics[1], rgb, "one without a colour space draws "
                   "in the file's own");
        draw_check(24, 16, &pics[2], alpha, "its opacity is its alpha when "
                   "SMaskInData asks");
        draw_check(24, 16, &pics[3], masked, "a JPEG 2000 soft mask lets "
                   "what is under show through");
        draw_check(24, 16, &pics[4], indexed, "an Indexed space takes its "
                   "samples as indices");
        draw_check(24, 16, &pics[5], pal, "a JP2's own palette colours it "
                   "when the dictionary names no space");
        draw_check(60, 50, &pics[6], flat, "a picture past what a page "
                   "decodes whole draws smaller, not grey");
    }
}

/* ----------------------------------------------------------- damage  */

/** @brief Every fixture cut short and with bytes changed, and a grid
 *         2^31 samples on a side: each read without harm, soon. */
static void
damage_test(void)
{
    unsigned long seed = 0x5eed2c00ul;
    unsigned char *copy;
    tiku_jpx_t img;
    clock_t start = clock();
    size_t i;
    int k;

    for (i = 0; i < sizeof jx_fixtures / sizeof jx_fixtures[0]; i++) {
        const jx_fixture_t *f = &jx_fixtures[i];

        copy = malloc((size_t)f->n);
        for (k = 0; k < 48; k++) {
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
            if (tiku_jpx_decode(copy, cut, MAXPX, &img) == 0) {
                tiku_jpx_free(&img);
            }
        }
        free(copy);
    }
    copy = malloc(sizeof jx_grey);
    memcpy(copy, jx_grey, sizeof jx_grey);
    for (k = 0; k < 4; k++) {       /* Xsiz, Ysiz, XTsiz, YTsiz */
        memcpy(copy + 8 + (k < 2 ? 0 : 16) + (k & 1) * 4,
               "\x7f\xff\xff\xff", 4);
    }
    check(tiku_jpx_decode(copy, sizeof jx_grey, MAXPX, &img) < 0,
          "a grid 2^31 on a side, one tile, is refused");
    free(copy);
    check((double)(clock() - start) / CLOCKS_PER_SEC < 10.0,
          "every stream cut short or changed reads without harm, soon");
}

int
main(void)
{
    printf("JPEG 2000: OpenJPEG's streams, JP2 files, sizes, a PDF, "
           "damage\n");
    fixture_test();
    ict_test();
    size_test();
    pdf_test();
    damage_test();
    if (fails) {
        printf("%d FAILED\n", fails);
        return 1;
    }
    puts("PASS: JPEG 2000 decoded as OpenJPEG and Apple's ImageIO read it");
    return 0;
}
