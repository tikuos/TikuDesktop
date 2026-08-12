/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_image.c - BMP and stored-deflate PNG readers, and thumb scaling.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Case-blind suffix test. */
static int
ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    size_t i;

    if (m > n) {
        return 0;
    }
    for (i = 0; i < m; i++) {
        char a = s[n - m + i], b = suffix[i];

        if (a >= 'A' && a <= 'Z') { a = (char)(a - 'A' + 'a'); }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

int
tiku_trk_image_is_image(const char *path)
{
    static const char *ext[] = {
        ".png", ".bmp", ".jpg", ".jpeg", ".gif", ".webp", ".tga", ".ppm"
    };
    size_t i;

    if (path == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof ext / sizeof ext[0]; i++) {
        if (ends_with(path, ext[i])) {
            return 1;
        }
    }
    return 0;
}

/** @brief Read a whole file.  Caller frees.  @return length, or -1. */
static long
slurp(const char *path, unsigned char **out)
{
    FILE *f = fopen(path, "rb");
    long n;
    unsigned char *buf;

    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        (void)fclose(f);
        return -1;
    }
    buf = malloc((size_t)n + 1u);
    if (buf == NULL) {
        (void)fclose(f);
        return -1;
    }
    if (fread(buf, 1u, (size_t)n, f) != (size_t)n) {
        free(buf);
        (void)fclose(f);
        return -1;
    }
    (void)fclose(f);
    *out = buf;
    return n;
}

static uint32_t
be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t
le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*---------------------------------------------------------------------------*/
/* BMP: uncompressed 24- and 32-bit, bottom-up or top-down                   */
/*---------------------------------------------------------------------------*/

static tiku_trk_image_err_t
load_bmp(const unsigned char *b, long n, tiku_trk_image_t *out)
{
    uint32_t off, hdr, bpp, comp;
    int32_t w, h;
    int flip, y, x;
    long stride;

    if (n < 54) {
        return TIKU_TRK_IMG_BAD;
    }
    off = le32(b + 10);
    hdr = le32(b + 14);
    w = (int32_t)le32(b + 18);
    h = (int32_t)le32(b + 22);
    bpp = (uint32_t)(b[28] | (b[29] << 8));
    comp = le32(b + 30);
    if (hdr < 40u || (bpp != 24u && bpp != 32u) || comp != 0u) {
        return TIKU_TRK_IMG_UNSUPPORTED;
    }
    /* A negative height means the rows are stored top-down. */
    flip = (h > 0);
    if (h < 0) {
        h = -h;
    }
    if (w <= 0 || h <= 0 || w > 20000 || h > 20000) {
        return TIKU_TRK_IMG_BAD;
    }
    /* Rows are padded to a multiple of four bytes. */
    stride = ((long)w * (long)(bpp / 8u) + 3L) & ~3L;
    if ((long)off + stride * h > n) {
        return TIKU_TRK_IMG_BAD;
    }
    out->px = malloc((size_t)w * (size_t)h * sizeof *out->px);
    if (out->px == NULL) {
        return TIKU_TRK_IMG_BAD;
    }
    out->w = w;
    out->h = h;
    for (y = 0; y < h; y++) {
        const unsigned char *row = b + off +
            (long)(flip ? (h - 1 - y) : y) * stride;

        for (x = 0; x < w; x++) {
            const unsigned char *p = row + (long)x * (long)(bpp / 8u);
            uint32_t a = (bpp == 32u) ? p[3] : 0xffu;

            out->px[(long)y * w + x] = (a << 24) | ((uint32_t)p[2] << 16) |
                                       ((uint32_t)p[1] << 8) | p[0];
        }
    }
    return TIKU_TRK_IMG_OK;
}

/*---------------------------------------------------------------------------*/
/* PNG, stored-deflate only                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Copy a zlib stream that uses only STORED blocks.
 *
 * Enough to read back what this project's own writer emits, which is what
 * makes its screenshots thumbnailable.  A compressed stream needs a real
 * inflate and is refused rather than half-read.
 *
 * @return bytes written, or -1.
 */
static long
inflate_stored(const unsigned char *z, long n, unsigned char *out, long max)
{
    long i = 2, w = 0;              /* skip the two-byte zlib header */

    if (n < 2 || (z[0] & 0x0fu) != 8u) {
        return -1;
    }
    if ((z[1] & 0x20u) != 0u) {
        return -1;                  /* a preset dictionary: not ours */
    }
    for (;;) {
        int final;
        long len;

        if (i >= n) {
            return -1;
        }
        final = z[i] & 1;
        if (((z[i] >> 1) & 3) != 0) {
            return -1;              /* a compressed block: needs inflate */
        }
        i++;                        /* stored blocks are byte-aligned    */
        if (i + 4 > n) {
            return -1;
        }
        len = (long)z[i] | ((long)z[i + 1] << 8);
        i += 4;                     /* LEN and its complement            */
        if (i + len > n || w + len > max) {
            return -1;
        }
        memcpy(out + w, z + i, (size_t)len);
        w += len;
        i += len;
        if (final) {
            break;
        }
    }
    return w;
}

/** @brief Undo one scanline's PNG filter, in place. */
static void
unfilter(unsigned char *cur, const unsigned char *prev, long len, int bpp,
         int type)
{
    long i;

    for (i = 0; i < len; i++) {
        int a = (i >= bpp) ? cur[i - bpp] : 0;
        int b = (prev != NULL) ? prev[i] : 0;
        int c = (prev != NULL && i >= bpp) ? prev[i - bpp] : 0;
        int v = cur[i];

        switch (type) {
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) / 2; break;
        case 4: {
            int p = a + b - c, pa = abs(p - a), pb = abs(p - b);
            int pc = abs(p - c);

            v += (pa <= pb && pa <= pc) ? a : ((pb <= pc) ? b : c);
            break;
        }
        default: break;
        }
        cur[i] = (unsigned char)v;
    }
}

static tiku_trk_image_err_t
load_png(const unsigned char *b, long n, tiku_trk_image_t *out)
{
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    unsigned char *idat = NULL, *raw = NULL;
    long ilen = 0, i = 8, rawlen;
    int w = 0, h = 0, depth = 0, colour = 0, bpp = 0, y;
    tiku_trk_image_err_t rc = TIKU_TRK_IMG_BAD;

    if (n < 8 || memcmp(b, sig, 8) != 0) {
        return TIKU_TRK_IMG_NOT_IMAGE;
    }
    while (i + 8 <= n) {
        uint32_t len = be32(b + i);
        const char *type = (const char *)(b + i + 4);

        if ((long)len > n || i + 12 + (long)len > n) {
            break;
        }
        if (memcmp(type, "IHDR", 4) == 0 && len >= 13u) {
            w = (int)be32(b + i + 8);
            h = (int)be32(b + i + 12);
            depth = b[i + 16];
            colour = b[i + 17];
            if (b[i + 20] != 0) {
                free(idat);
                return TIKU_TRK_IMG_UNSUPPORTED;  /* interlaced */
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            unsigned char *grown = realloc(idat, (size_t)(ilen + len));

            if (grown == NULL) {
                free(idat);
                return TIKU_TRK_IMG_BAD;
            }
            idat = grown;
            memcpy(idat + ilen, b + i + 8, len);
            ilen += (long)len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        i += 12 + (long)len;
    }
    if (w <= 0 || h <= 0 || idat == NULL || depth != 8 ||
        (colour != 2 && colour != 6)) {
        free(idat);
        return (w > 0) ? TIKU_TRK_IMG_UNSUPPORTED : TIKU_TRK_IMG_BAD;
    }
    bpp = (colour == 6) ? 4 : 3;
    rawlen = ((long)w * bpp + 1L) * h;
    raw = malloc((size_t)rawlen);
    if (raw == NULL) {
        free(idat);
        return TIKU_TRK_IMG_BAD;
    }
    if (inflate_stored(idat, ilen, raw, rawlen) != rawlen) {
        /* Compressed: recognised, and honestly out of reach for now. */
        rc = TIKU_TRK_IMG_UNSUPPORTED;
        goto done;
    }
    out->px = malloc((size_t)w * (size_t)h * sizeof *out->px);
    if (out->px == NULL) {
        goto done;
    }
    out->w = w;
    out->h = h;
    for (y = 0; y < h; y++) {
        unsigned char *cur = raw + (long)y * ((long)w * bpp + 1L);
        int type = cur[0];
        int x;

        cur++;
        unfilter(cur, (y > 0) ? raw + (long)(y - 1) *
                                    ((long)w * bpp + 1L) + 1 : NULL,
                 (long)w * bpp, bpp, type);
        for (x = 0; x < w; x++) {
            const unsigned char *p = cur + (long)x * bpp;
            uint32_t a = (bpp == 4) ? p[3] : 0xffu;

            out->px[(long)y * w + x] = (a << 24) | ((uint32_t)p[0] << 16) |
                                       ((uint32_t)p[1] << 8) | p[2];
        }
    }
    rc = TIKU_TRK_IMG_OK;
done:
    free(idat);
    free(raw);
    return rc;
}

tiku_trk_image_err_t
tiku_trk_image_load(const char *path, tiku_trk_image_t *out)
{
    unsigned char *b = NULL;
    long n;
    tiku_trk_image_err_t rc;

    if (path == NULL || out == NULL) {
        return TIKU_TRK_IMG_BAD;
    }
    n = slurp(path, &b);
    if (n < 0) {
        return TIKU_TRK_IMG_BAD;
    }
    memset(out, 0, sizeof *out);
    if (n >= 2 && b[0] == 'B' && b[1] == 'M') {
        rc = load_bmp(b, n, out);
    } else if (n >= 8 && b[0] == 137 && b[1] == 'P') {
        rc = load_png(b, n, out);
    } else {
        rc = TIKU_TRK_IMG_NOT_IMAGE;
    }
    free(b);
    return rc;
}

void
tiku_trk_image_free(tiku_trk_image_t *im)
{
    if (im != NULL) {
        free(im->px);
        im->px = NULL;
        im->w = 0;
        im->h = 0;
    }
}

int
tiku_trk_image_thumb(const tiku_trk_image_t *src, int size,
                     tiku_trk_image_t *out)
{
    int tw, th, ox, oy, y, x;

    if (src == NULL || out == NULL || src->px == NULL || size <= 0 ||
        src->w <= 0 || src->h <= 0) {
        return -1;
    }
    /* The longer side sets the scale, so the whole picture fits and its
     * shape survives -- a stretched thumbnail is harder to recognise than
     * a small one. */
    if (src->w >= src->h) {
        tw = size;
        th = (int)(((long)src->h * size + src->w / 2) / src->w);
    } else {
        th = size;
        tw = (int)(((long)src->w * size + src->h / 2) / src->h);
    }
    if (tw < 1) { tw = 1; }
    if (th < 1) { th = 1; }
    ox = (size - tw) / 2;
    oy = (size - th) / 2;

    out->w = size;
    out->h = size;
    out->px = calloc((size_t)size * (size_t)size, sizeof *out->px);
    if (out->px == NULL) {
        return -1;
    }
    /* calloc leaves the margins at 0: fully transparent, not black. */
    for (y = 0; y < th; y++) {
        long sy = (long)y * src->h / th;

        for (x = 0; x < tw; x++) {
            long sx = (long)x * src->w / tw;

            out->px[(long)(y + oy) * size + (x + ox)] =
                src->px[sy * src->w + sx];
        }
    }
    return 0;
}
