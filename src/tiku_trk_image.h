/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_image.h - reading a raster image into pixels.
 *
 * Thumbnails need something a vector icon format cannot give: the picture a
 * file actually holds.  This reads the formats that need no decompressor --
 * BMP, and the stored-deflate PNG the desktop's own writer emits -- and says
 * plainly when it cannot read one, so the thumbnail machinery above it can
 * be complete and tested while the decoder is still growing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_IMAGE_H_
#define TIKU_TRK_IMAGE_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Decoded pixels, 32-bit, 0xAARRGGBB, top row first. */
typedef struct {
    int       w, h;
    uint32_t *px;                   /* w*h, owned; free with _free      */
} tiku_trk_image_t;

/** @brief Why a load did not produce pixels. */
typedef enum {
    TIKU_TRK_IMG_OK = 0,
    TIKU_TRK_IMG_NOT_IMAGE,         /* not a format we recognise         */
    TIKU_TRK_IMG_UNSUPPORTED,       /* recognised, but not decodable yet */
    TIKU_TRK_IMG_BAD                /* recognised and malformed          */
} tiku_trk_image_err_t;

/**
 * @brief Whether @p path looks like an image at all, by its name.
 *
 * By extension rather than by content: the caller uses this to decide
 * whether a file is worth opening, and opening every file to find out is
 * the cost this avoids.
 */
int tiku_trk_image_is_image(const char *path);

/**
 * @brief Read @p path into @p out.
 *
 * @return TIKU_TRK_IMG_OK on success; @p out is untouched otherwise.
 */
tiku_trk_image_err_t tiku_trk_image_load(const char *path,
                                         tiku_trk_image_t *out);

void tiku_trk_image_free(tiku_trk_image_t *im);

/**
 * @brief Scale @p src into a @p size square, keeping its aspect ratio.
 *
 * The picture is centred and the margins left fully transparent, so a wide
 * image does not become a stretched one -- the shape of the thing is part
 * of what makes a thumbnail recognisable.
 *
 * @return 0 on success.
 */
int tiku_trk_image_thumb(const tiku_trk_image_t *src, int size,
                         tiku_trk_image_t *out);

#endif /* TIKU_TRK_IMAGE_H_ */
