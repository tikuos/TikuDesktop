/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_image.h - reading a raster image into pixels.
 *
 * Content-sniffed BMP, PNG (including deflate and Adam7), and baseline JPEG.
 * Both file and memory entry points bound encoded bytes and decoded pixels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IMAGE_H_
#define TIKU_IMAGE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** @brief Resource ceilings shared by thumbnails and snapshot viewers. */
#define TIKU_IMAGE_BYTES_MAX (32u * 1024u * 1024u)
#define TIKU_IMAGE_PIXELS_MAX (16u * 1024u * 1024u)

/** @brief Decoded pixels, 32-bit, 0xAARRGGBB, top row first. */
typedef struct {
    int       w, h;
    uint32_t *px;                   /* w*h, owned; free with _free      */
} tiku_image_t;

/** @brief Why a load did not produce pixels. */
typedef enum {
    TIKU_IMG_OK = 0,
    TIKU_IMG_NOT_IMAGE,         /* not a recognised format           */
    TIKU_IMG_UNSUPPORTED,       /* recognised, but not decodable yet */
    TIKU_IMG_BAD                /* recognised and malformed          */
} tiku_image_err_t;

/**
 * @brief Whether @p path holds an image, by its first bytes.
 *
 * By content, not by name: a file called .txt that holds a PNG is a PNG,
 * and one called .jpg that holds nothing is not a picture.
 */
int tiku_image_is_image(const char *path);

/**
 * @brief Read @p path into @p out.
 *
 * @return TIKU_IMG_OK on success; @p out is untouched otherwise.
 */
tiku_image_err_t tiku_image_load(const char *path,
                                         tiku_image_t *out);

/** @brief Decode binary bytes, including embedded NULs. No ownership transfer.
 * On success out receives owned pixels (caller frees any previous image).
 * On failure out is untouched. The same limits as file loading apply.
 */
tiku_image_err_t tiku_image_decode(const void *bytes, size_t len,
                                  tiku_image_t *out);

void tiku_image_free(tiku_image_t *im);

/**
 * @brief @p w by @p h pixels, 0xAARRGGBB, written to @p path as a PNG:
 *        RGBA when @p alpha is set, else RGB with the top byte ignored,
 *        each row filtered the way that leaves least to store.
 * @return 0, or -1 when it could not be made or written.
 */
int tiku_png_write(const char *path, const uint32_t *px, int w, int h,
                   int alpha);
/** @brief Encode to an open stream; does not flush or close it. Suitable
 * for tiku_atomic_file producers. Same return convention as _write. */
int tiku_png_write_stream(FILE *stream, const uint32_t *px, int w, int h,
                          int alpha);

/**
 * @brief Scale @p src into a @p size square, keeping its aspect ratio.
 *
 * The picture is centred and the margins left fully transparent, so a wide
 * image does not become a stretched one -- the shape of the thing is part
 * of what makes a thumbnail recognisable.
 *
 * @return 0 on success.
 */
int tiku_image_thumb(const tiku_image_t *src, int size,
                         tiku_image_t *out);

#endif /* TIKU_IMAGE_H_ */
