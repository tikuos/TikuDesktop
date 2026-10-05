/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_png_write.c - DEFLATE and PNG written, then read back.  Streams
 * of every kind -- nothing, text, a megabyte of zeros, random bytes --
 * come back through the kit's inflater exactly, and through Python's zlib
 * where the machine has one.  PNGs, RGB and RGBA, come back through the
 * image kit pixel for pixel, and a desk-sized picture shrinks as a
 * screenshot should, quickly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tiku_image.h"
#include "tiku_inflate.h"

static int fails;
static unsigned long long seed = 0x2545f4914f6cdd1dull;

/** @brief One row of the report, PASS or FAIL; a FAIL is counted. */
static void check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) fails++;
}

/** @brief The next of a fixed stream of numbers. */
static unsigned rnd(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return (unsigned)(seed >> 11);
}

/** @brief @p n bytes deflated and inflated again: whether they come back,
 *         the compressed size into @p zn. */
static int round_trip(const unsigned char *in, size_t n, size_t *zn)
{
    unsigned char *z = tiku_deflate_zlib(in, n, zn);
    unsigned char *back = malloc(n + 1);
    long got = z && back ? tiku_inflate_zlib(z, (long)*zn, back, (long)n + 1)
                         : -2;
    int ok = got == (long)n && (n == 0 || !memcmp(back, in, n));

    free(z);
    free(back);
    return ok;
}

/** @brief Streams of every kind, and their sizes. */
static void deflate_tests(void)
{
    static const char text[] = "It was the best of times, it was the worst "
                               "of times, it was the age of wisdom.";
    size_t n = 1 << 20, zn = 0, i;
    unsigned char *buf = calloc(n, 1);

    check(round_trip((const unsigned char *)"", 0, &zn) && zn > 6, "PW-001",
          "nothing: a stream that inflates to nothing");
    check(round_trip((const unsigned char *)text, sizeof text - 1, &zn) &&
          zn < sizeof text, "PW-002", "a sentence: back whole, smaller");
    check(round_trip(buf, n, &zn) && zn < 20000, "PW-003",
          "a megabyte of zeros: back whole, in under 20 KB");
    for (i = 0; i < n / 4; i++) buf[i] = (unsigned char)rnd();
    check(round_trip(buf, n / 4, &zn) && zn < n / 4 + n / 32, "PW-004",
          "256 KB of noise: back whole, hardly grown");
    for (i = 0; i < n; i++) buf[i] = (unsigned char)("abcab c\n"[rnd() % 8]);
    check(round_trip(buf, n, &zn) && zn < n / 2, "PW-005",
          "a megabyte of eight letters: back whole, halved at least");
    free(buf);
}

/** @brief A stream Python's zlib inflates to what went in, where there is
 *         a Python here. */
static void zlib_tests(void)
{
    char dir[] = "/tmp/tikupng-XXXXXX", zp[64], cmd[256];
    unsigned char in[5000];
    size_t zn = 0, i;
    unsigned char *z;
    FILE *f;

    if (system("python3 -c 'import zlib' > /dev/null 2>&1") != 0 ||
        !mkdtemp(dir)) {
        printf("  [SKIP] PW-010    no python3 here\n");
        return;
    }
    for (i = 0; i < sizeof in; i++) in[i] = (unsigned char)(i % 97 + i / 300);
    z = tiku_deflate_zlib(in, sizeof in, &zn);
    snprintf(zp, sizeof zp, "%s/z", dir);
    f = fopen(zp, "wb");
    if (f) {
        fwrite(z, 1, zn, f);
        fclose(f);
    }
    snprintf(cmd, sizeof cmd, "python3 -c \"import zlib;d=zlib.decompress("
             "open('%s','rb').read());import sys;sys.exit(0 if d==bytes("
             "(i%%97+i//300)%%256 for i in range(5000)) else 1)\"", zp);
    check(z && system(cmd) == 0, "PW-010", "Python's zlib reads it, "
                                           "checksum and all");
    free(z);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    (void)system(cmd);
}

/** @brief A picture written and read back: whether every pixel came back
 *         as it went, alpha too when @p alpha. */
static int png_back(const char *path, const uint32_t *px, int w, int h,
                    int alpha)
{
    tiku_image_t im = { 0, 0, NULL };
    int ok = tiku_png_write(path, px, w, h, alpha) == 0 &&
             tiku_image_load(path, &im) == TIKU_IMG_OK && im.w == w &&
             im.h == h;

    for (long i = 0; ok && i < (long)w * h; i++)
        ok = alpha ? im.px[i] == px[i]
                   : im.px[i] == (0xff000000u | (px[i] & 0xffffffu));
    tiku_image_free(&im);
    return ok;
}

/** @brief PNGs of both kinds back pixel for pixel; a desk-sized picture
 *         small and quick. */
static void png_tests(void)
{
    char path[] = "/tmp/tikupng-XXXXXX";
    int fd = mkstemp(path), w = 1000, h = 680;
    uint32_t *px = malloc((size_t)w * h * sizeof *px);
    FILE *f;
    long size = 0;
    clock_t t0;
    double secs;

    if (fd >= 0) close(fd);
    for (int i = 0; i < 37 * 23; i++)
        px[i] = (uint32_t)rnd() | (i % 5 == 0 ? 0xff000000u : 0);
    check(png_back(path, px, 37, 23, 0), "PW-020",
          "RGB: every pixel back, opaque");
    check(png_back(path, px, 37, 23, 1), "PW-021",
          "RGBA: every pixel back, its alpha too");
    f = fopen(path, "wb");
    int stream_ok = f && !tiku_png_write_stream(f, px, 37, 23, 1);
    if (f && fclose(f)) stream_ok = 0;
    tiku_image_t streamed = {0};
    stream_ok = stream_ok && !tiku_image_load(path, &streamed) &&
                streamed.w == 37 && streamed.h == 23 &&
                !memcmp(streamed.px, px, 37u * 23u * sizeof *px);
    check(stream_ok, "PW-024", "open-stream encoding round-trips RGBA");
    tiku_image_free(&streamed);
    f = tmpfile();
    check(f && tiku_png_write_stream(f, px, INT_MAX, INT_MAX, 0) == -1 &&
          ftell(f) == 0, "PW-025", "oversized image refused before writing");
    if (f) fclose(f);
    for (int y = 0; y < h; y++)             /* a desk: flat, with windows */
        for (int x = 0; x < w; x++)
            px[(size_t)y * w + x] = (x > 100 && x < 700 && y > 60 && y < 500)
                ? ((y % 18 < 12 && x % 9 < 6 && rnd() % 3) ? 0xff202020u
                                                           : 0xfff4f4f4u)
                : 0xff31669au;
    t0 = clock();
    check(png_back(path, px, w, h, 0), "PW-022", "a desk: back whole");
    secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    if ((f = fopen(path, "rb"))) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
    }
    printf("           a 1000 by 680 desk: %ld bytes of %d raw, %.3f s\n",
           size, w * h * 3, secs);
    check(size > 0 && size < w * h * 3 / 12 && secs < 2.0, "PW-023",
          "and a twelfth of its raw size at most, quickly");
    unlink(path);
    free(px);
}

/** @brief Every group; @return 0 when every row passes. */
int main(void)
{
    printf("PNG writing: DEFLATE and PNG written, then read back\n");
    deflate_tests();
    zlib_tests();
    png_tests();
    printf("%s\n", fails ? "FAIL" : "PASS: DEFLATE and PNG written and read "
                                    "back whole");
    return fails ? 1 : 0;
}
