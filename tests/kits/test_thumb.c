/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_thumb.c - reading an image, and the thumbnail rules around it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_thumb.h"

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

#include "../fixtures/image_fixtures.inc"

static int fails;
static volatile sig_atomic_t timed_out;

/** @brief SIGALRM: notes that the call it interrupted had blocked. */
static void
on_alarm(int sig)
{
    (void)sig;
    timed_out = 1;
}

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief Write a 24-bit BMP of @p w x @p h, all one colour. */
/** @brief @p v at @p p, least significant byte first, as BMP keeps it:
 *         byte by byte, since the header's fields sit unaligned. */
static void
le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

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
    le32(hdr + 2, (uint32_t)(54 + bytes));
    le32(hdr + 10, 54u);
    le32(hdr + 14, 40u);
    le32(hdr + 18, (uint32_t)w);
    le32(hdr + 22, (uint32_t)h);
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
    tiku_thumb_q_t q;
    tiku_store_t *store = tiku_store_memory_open();
    tiku_image_t im, th;
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

    check(tiku_image_load(wide, &im) == TIKU_IMG_OK &&
          im.w == 40 && im.h == 10, "IV-028",
          "a BMP reads back at its own size");
    check((im.px[0] >> 16 & 0xff) == 255 && (im.px[0] & 0xff) == 0,
          "IV-028", "and with its own colours, blue and red not swapped");

    printf("\nthe shape survives (IV-034)\n");
    check(tiku_image_thumb(&im, 128, &th) == 0 &&
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
    tiku_image_free(&th);
    tiku_image_free(&im);

    (void)tiku_image_load(tall, &im);
    (void)tiku_image_thumb(&im, 128, &th);
    {
        int cols = 0, x;

        for (x = 0; x < 128; x++) {
            if ((th.px[64L * 128 + x] >> 24) == 255) { cols++; }
        }
        check(cols > 28 && cols < 36, "IV-034",
              "and a tall image is narrow, not squashed wide");
    }
    tiku_image_free(&th);
    tiku_image_free(&im);



    printf("\nthe formats a real disk holds (IV-028)\n");
    {
        char pth[128];
        FILE *f;

        snprintf(pth, sizeof pth, "/tmp/trk_thumb_fx.jpg");
        f = fopen(pth, "wb");
        if (f != NULL) {
            (void)fwrite(jpeg_420, 1, sizeof jpeg_420, f);
            (void)fclose(f);
        }
        check(tiku_image_load(pth, &im) == TIKU_IMG_OK &&
              im.w == 16 && im.h == 16, "IV-028",
              "a baseline JPEG decodes -- most pictures on a disk are one");
        if (im.px != NULL) {
            /* The fixture is a smooth ramp, so the corners must differ
             * hugely; a decoder that returned grey would pass a size
             * check and fail this one. */
            uint32_t tl = im.px[0], br = im.px[15 * 16 + 15];

            check(((br >> 16) & 0xff) > ((tl >> 16) & 0xff) + 100,
                  "IV-028", "with its ramp intact left to right");
            check(((br >> 8) & 0xff) > ((tl >> 8) & 0xff) + 100,
                  "IV-028", "and top to bottom -- chroma is upsampled, not "
                  "dropped");
            {   /* Sampled against a reference decoder.  The tolerance is
                 * chroma subsampling's own loss plus this integer IDCT's
                 * last bit -- wide enough to be stable, far tighter than
                 * any wrong colour matrix or misplaced chroma block. */
                size_t k;
                int worst = 0;

                for (k = 0; k < sizeof jpeg_expect / sizeof jpeg_expect[0];
                     k++) {
                    uint32_t g = im.px[(long)jpeg_expect[k].y * 16 +
                                       jpeg_expect[k].x];
                    int dr = (int)((g >> 16) & 0xff) - jpeg_expect[k].r;
                    int dg = (int)((g >> 8) & 0xff) - jpeg_expect[k].g;
                    int db = (int)(g & 0xff) - jpeg_expect[k].b;

                    if (dr < 0) { dr = -dr; }
                    if (dg < 0) { dg = -dg; }
                    if (db < 0) { db = -db; }
                    if (dr > worst) { worst = dr; }
                    if (dg > worst) { worst = dg; }
                    if (db > worst) { worst = db; }
                }
                check(worst <= 10, "IV-028",
                      "and six sampled pixels match a reference decoder");
            }
            tiku_image_free(&im);
        }
        (void)remove(pth);
        {   /* Its progressive twin: the same coefficients in bands and
             * bits, so to the very pixel what the baseline gives. */
            tiku_image_t a, b;
            int same = 0;

            memset(&a, 0, sizeof a);
            memset(&b, 0, sizeof b);
            if (tiku_image_decode(jpeg_420, sizeof jpeg_420, &a) ==
                    TIKU_IMG_OK &&
                tiku_image_decode(jpeg_420_progressive,
                                  sizeof jpeg_420_progressive, &b) ==
                    TIKU_IMG_OK) {
                same = a.w == b.w && a.h == b.h &&
                       memcmp(a.px, b.px, (size_t)a.w * (size_t)a.h *
                                          sizeof *a.px) == 0;
            }
            check(same, "IV-028", "a progressive JPEG decodes to the very "
                  "pixels of its baseline twin");
            tiku_image_free(&a);
            tiku_image_free(&b);
        }

        snprintf(pth, sizeof pth, "/tmp/trk_thumb_fx.png");
        f = fopen(pth, "wb");
        if (f != NULL) {
            (void)fwrite(png_palette, 1, sizeof png_palette, f);
            (void)fclose(f);
        }
        check(tiku_image_load(pth, &im) == TIKU_IMG_OK &&
              im.w == 8 && im.h == 4, "IV-028",
              "a 2-bit palette PNG decodes");
        if (im.px != NULL) {
            check((im.px[0] >> 24) == 0, "IV-028",
                  "and its tRNS entry is transparent -- a cut-out icon is "
                  "a cut-out, not a square");
            check((im.px[1] >> 24) == 255, "IV-028",
                  "while an opaque entry stays opaque");
            tiku_image_free(&im);
        }
        (void)remove(pth);
    }
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
        check(tiku_image_load(pn, &im) == TIKU_IMG_OK &&
              im.w == 8 && im.h == 6, "IV-028",
              "a zlib-compressed PNG decodes -- the deflate stream is "
              "inflated, not refused");
        if (im.px != NULL) {
            check(im.px[0] == 0xff000007u, "IV-028",
                  "with the right first pixel");
            check(im.px[1] == 0xff1e000eu, "IV-028",
                  "and the Sub filter undone: the second pixel is the "
                  "first plus its own delta, not the raw byte");
            tiku_image_free(&im);
        }
        (void)remove(pn);
    }
    printf("\na path that is not a regular file (IV-028)\n");
    {   /* A FIFO with no writer: opening it to read waits for one, so a
         * one-second alarm interrupts the open if the load opens it. */
        char fifo[128];
        struct sigaction sa;
        tiku_image_err_t rc = TIKU_IMG_OK;

        snprintf(fifo, sizeof fifo, "/tmp/trk_thumb_fifo_%ld",
                 (long)getpid());
        (void)remove(fifo);
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_alarm;
        (void)sigaction(SIGALRM, &sa, NULL);
        if (mkfifo(fifo, 0600) == 0) {
            memset(&im, 0, sizeof im);
            timed_out = 0;
            (void)alarm(1);
            rc = tiku_image_load(fifo, &im);
            (void)alarm(0);
            (void)remove(fifo);
        }
        check(rc == TIKU_IMG_BAD && !timed_out, "IV-028",
              "a FIFO is refused at once, without waiting for a writer");
    }

    printf("\nwhat earns a thumbnail (IV-028, TS-053)\n");
    tiku_thumb_init(&q);
    check(q.enabled, "TS-053", "the setting is on by default");
    check(tiku_thumb_wanted(&q, wide), "IV-028", "an image earns one");
    check(!tiku_thumb_wanted(&q, txt), "IV-028",
          "a text file does not");
    {   /* Recognition is by CONTENT: the name is a hint its author may
         * not have meant. */
        char liar[128];
        FILE *lf;

        snprintf(liar, sizeof liar, "/tmp/trk_thumb_liar.txt");
        lf = fopen(liar, "wb");
        if (lf != NULL) {
            (void)fwrite(png_compressed, 1, sizeof png_compressed, lf);
            (void)fclose(lf);
        }
        check(tiku_thumb_wanted(&q, liar), "IV-028",
              "a PNG called .txt is still a PNG");
        (void)remove(liar);
        snprintf(liar, sizeof liar, "/tmp/trk_thumb_liar.png");
        lf = fopen(liar, "w");
        if (lf != NULL) { fputs("not a picture", lf); (void)fclose(lf); }
        check(!tiku_thumb_wanted(&q, liar), "IV-028",
              "and a text file called .png is not one");
        (void)remove(liar);
    }
    q.enabled = 0;
    check(!tiku_thumb_wanted(&q, wide), "TS-053",
          "and with the setting off, nothing does");
    q.enabled = 1;

    printf("\nthe queues (IV-032, IV-033)\n");
    check(tiku_thumb_queue(&q, wide, 400, 128) == 1, "IV-031",
          "a small image queues");
    check(tiku_thumb_queue(&q, wide, 400, 128) == 0, "IV-033",
          "the same file never queues twice -- two windows on one folder "
          "would otherwise both ask");
    check(q.nsmall == 1 && q.nbig == 0, "IV-032",
          "and it went on the small queue");
    check(tiku_thumb_queue(&q, big, 200000, 128) == 1 && q.nbig == 1,
          "IV-032", "a file past 128 KB goes on the big one instead");

    printf("\nmaking them (IV-031, IV-036, IV-060)\n");
    check(tiku_thumb_step(&q, store) == 1 && q.made == 1, "IV-031",
          "a step does ONE job, so the window is never held");
    check(q.nsmall == 0 && q.nbig == 1, "IV-032",
          "and it took the small one first: a big file cannot stall the "
          "small ones behind it");
    check(tiku_thumb_load(store, wide, &th) == 0 && th.w == 128,
          "IV-030", "the thumbnail is stored on the file and reads back");
    tiku_image_free(&th);
    check(tiku_thumb_step(&q, NULL) == 1, "IV-036",
          "a store that will not take it does not stop the job running");

    printf("\ntrusting a cached one (IV-029)\n");
    {
        struct stat st;

        (void)stat(wide, &st);
        check(tiku_thumb_fresh(store, wide, (int64_t)st.st_mtime),
              "IV-029", "a thumbnail newer than the file is trusted");
        check(!tiku_thumb_fresh(store, wide,
                                    (int64_t)st.st_mtime + 1000),
              "IV-029", "one older than the file is not");
        check(!tiku_thumb_fresh(store, txt, 0), "IV-029",
              "and a file with none at all is not fresh");
    }

    (void)remove(wide); (void)remove(tall); (void)remove(big);
    (void)remove(txt);
    tiku_store_free(store);
    printf("\n%s\n", fails ? "FAILURES" : "thumbnails hold");
    return fails ? 1 : 0;
}
