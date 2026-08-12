/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_thumb.c - reading an image, and the thumbnail rules around it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_thumb.h"

/* A real zlib-compressed PNG, 8x6, Sub-filtered: this project has no
 * compressor, so the fixture is checked in rather than generated. */
static const unsigned char png_compressed[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x06,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x71, 0x67, 0x48, 0xac, 0x00, 0x00, 0x00,
    0x64, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x0d, 0xc9, 0x31, 0x01, 0x00,
    0x31, 0x08, 0x04, 0x41, 0xbe, 0x5a, 0x17, 0xa9, 0x11, 0x81, 0x08, 0xea,
    0x28, 0x39, 0x25, 0xa9, 0x11, 0x11, 0x11, 0xd4, 0x28, 0xfa, 0x4c, 0x3b,
    0x9f, 0x19, 0xcb, 0x08, 0x63, 0x1b, 0x32, 0x8e, 0x71, 0x8d, 0x31, 0x3e,
    0x73, 0x96, 0x13, 0xce, 0x76, 0xe4, 0x1c, 0xe7, 0x3a, 0xe3, 0x2f, 0x92,
    0x95, 0x44, 0xb2, 0x13, 0x25, 0x27, 0xb9, 0xc9, 0xe4, 0x0b, 0xb1, 0x44,
    0x88, 0x2d, 0x24, 0x8e, 0xb8, 0x62, 0xf4, 0xa2, 0x58, 0x45, 0x14, 0xbb,
    0x50, 0x71, 0x8a, 0x5b, 0x4c, 0xbd, 0x68, 0x56, 0x13, 0xcd, 0x6e, 0xd4,
    0x9c, 0xe6, 0x36, 0xd3, 0xfc, 0x3f, 0x52, 0x27, 0xc7, 0x67, 0xc3, 0x54,
    0xa0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
    0x82,
};

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief Write a 24-bit BMP of @p w x @p h, all one colour. */
static void
put_bmp(const char *path, int w, int h, unsigned char r, unsigned char g,
        unsigned char b)
{
    long stride = (w * 3L + 3L) & ~3L;
    long bytes = stride * h;
    unsigned char hdr[54];
    FILE *f = fopen(path, "wb");
    int y, x;

    if (f == NULL) { return; }
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    *(uint32_t *)(hdr + 2)  = (uint32_t)(54 + bytes);
    *(uint32_t *)(hdr + 10) = 54u;
    *(uint32_t *)(hdr + 14) = 40u;
    *(int32_t  *)(hdr + 18) = w;
    *(int32_t  *)(hdr + 22) = h;
    hdr[26] = 1; hdr[28] = 24;
    (void)fwrite(hdr, 1, sizeof hdr, f);
    for (y = 0; y < h; y++) {
        long pad = stride - w * 3L;

        for (x = 0; x < w; x++) {
            (void)fputc(b, f); (void)fputc(g, f); (void)fputc(r, f);
        }
        while (pad-- > 0) { (void)fputc(0, f); }
    }
    (void)fclose(f);
}

int
main(void)
{
    tiku_trk_thumb_q_t q;
    tiku_trk_store_t *store = tiku_trk_store_memory_open();
    tiku_trk_image_t im, th;
    char wide[128], tall[128], big[128], txt[128];

    printf("reading an image\n");
    snprintf(wide, sizeof wide, "/tmp/trk_thumb_wide.bmp");
    snprintf(tall, sizeof tall, "/tmp/trk_thumb_tall.bmp");
    snprintf(big,  sizeof big,  "/tmp/trk_thumb_big.bmp");
    snprintf(txt,  sizeof txt,  "/tmp/trk_thumb_not.txt");
    put_bmp(wide, 40, 10, 255, 0, 0);
    put_bmp(tall, 10, 40, 0, 255, 0);
    put_bmp(big, 300, 300, 0, 0, 255);
    { FILE *f = fopen(txt, "w"); if (f) { fputs("no", f); fclose(f); } }

    check(tiku_trk_image_load(wide, &im) == TIKU_TRK_IMG_OK &&
          im.w == 40 && im.h == 10, "IV-028",
          "a BMP reads back at its own size");
    check((im.px[0] >> 16 & 0xff) == 255 && (im.px[0] & 0xff) == 0,
          "IV-028", "and with its own colours, blue and red not swapped");

    printf("\nthe shape survives (IV-034)\n");
    check(tiku_trk_image_thumb(&im, 128, &th) == 0 &&
          th.w == 128 && th.h == 128, "IV-030",
          "a thumbnail is a 128 square whatever went in");
    check((th.px[0] >> 24) == 0, "IV-034",
          "the margin is fully TRANSPARENT, not black");
    check((th.px[64L * 128 + 64] >> 24) == 255, "IV-034",
          "and the picture is in the middle of it");
    {   /* A 4:1 image must occupy a 4:1 band, not the whole square. */
        int rows = 0, y;

        for (y = 0; y < 128; y++) {
            if ((th.px[(long)y * 128 + 64] >> 24) == 255) { rows++; }
        }
        check(rows > 28 && rows < 36, "IV-034",
              "a 4:1 image fills a quarter of the height -- the aspect "
              "ratio is kept rather than stretched");
    }
    tiku_trk_image_free(&th);
    tiku_trk_image_free(&im);

    (void)tiku_trk_image_load(tall, &im);
    (void)tiku_trk_image_thumb(&im, 128, &th);
    {
        int cols = 0, x;

        for (x = 0; x < 128; x++) {
            if ((th.px[64L * 128 + x] >> 24) == 255) { cols++; }
        }
        check(cols > 28 && cols < 36, "IV-034",
              "and a tall image is narrow, not squashed wide");
    }
    tiku_trk_image_free(&th);
    tiku_trk_image_free(&im);


    printf("\ncompressed PNG (IV-028)\n");
    {
        char pn[128];
        FILE *f;

        snprintf(pn, sizeof pn, "/tmp/trk_thumb_c.png");
        f = fopen(pn, "wb");
        if (f != NULL) {
            (void)fwrite(png_compressed, 1, sizeof png_compressed, f);
            (void)fclose(f);
        }
        check(tiku_trk_image_load(pn, &im) == TIKU_TRK_IMG_OK &&
              im.w == 8 && im.h == 6, "IV-028",
              "a zlib-compressed PNG decodes -- the deflate stream is "
              "inflated, not refused");
        if (im.px != NULL) {
            check(im.px[0] == 0xff000007u, "IV-028",
                  "with the right first pixel");
            check(im.px[1] == 0xff1e000eu, "IV-028",
                  "and the Sub filter undone: the second pixel is the "
                  "first plus its own delta, not the raw byte");
            tiku_trk_image_free(&im);
        }
        (void)remove(pn);
    }
    printf("\nwhat earns a thumbnail (IV-028, TS-053)\n");
    tiku_trk_thumb_init(&q);
    check(q.enabled, "TS-053", "the setting is on by default");
    check(tiku_trk_thumb_wanted(&q, wide), "IV-028", "an image earns one");
    check(!tiku_trk_thumb_wanted(&q, txt), "IV-028",
          "a text file does not");
    q.enabled = 0;
    check(!tiku_trk_thumb_wanted(&q, wide), "TS-053",
          "and with the setting off, nothing does");
    q.enabled = 1;

    printf("\nthe queues (IV-032, IV-033)\n");
    check(tiku_trk_thumb_queue(&q, wide, 400, 128) == 1, "IV-031",
          "a small image queues");
    check(tiku_trk_thumb_queue(&q, wide, 400, 128) == 0, "IV-033",
          "the same file never queues twice -- two windows on one folder "
          "would otherwise both ask");
    check(q.nsmall == 1 && q.nbig == 0, "IV-032",
          "and it went on the small queue");
    check(tiku_trk_thumb_queue(&q, big, 200000, 128) == 1 && q.nbig == 1,
          "IV-032", "a file past 128 KB goes on the big one instead");

    printf("\nmaking them (IV-031, IV-036, IV-060)\n");
    check(tiku_trk_thumb_step(&q, store) == 1 && q.made == 1, "IV-031",
          "a step does ONE job, so the window is never held");
    check(q.nsmall == 0 && q.nbig == 1, "IV-032",
          "and it took the small one first: a big file cannot stall the "
          "small ones behind it");
    check(tiku_trk_thumb_load(store, wide, &th) == 0 && th.w == 128,
          "IV-030", "the thumbnail is stored on the file and reads back");
    tiku_trk_image_free(&th);
    check(tiku_trk_thumb_step(&q, NULL) == 1, "IV-036",
          "a store that will not take it does not stop the job running");

    printf("\ntrusting a cached one (IV-029)\n");
    {
        struct stat st;

        (void)stat(wide, &st);
        check(tiku_trk_thumb_fresh(store, wide, (int64_t)st.st_mtime),
              "IV-029", "a thumbnail newer than the file is trusted");
        check(!tiku_trk_thumb_fresh(store, wide,
                                    (int64_t)st.st_mtime + 1000),
              "IV-029", "one older than the file is not");
        check(!tiku_trk_thumb_fresh(store, txt, 0), "IV-029",
              "and a file with none at all is not fresh");
    }

    (void)remove(wide); (void)remove(tall); (void)remove(big);
    (void)remove(txt);
    tiku_trk_store_free(store);
    printf("\n%s\n", fails ? "FAILURES" : "thumbnails hold");
    return fails ? 1 : 0;
}
