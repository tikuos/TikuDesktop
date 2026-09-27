/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpeg.h - JPEG decoding, sequential and progressive.
 *
 * Most pictures on a real disk are JPEGs, so a thumbnailer that could not
 * read one would be a thumbnailer for screenshots.  Huffman-coded 8-bit
 * JPEGs, sequential or progressive; grey, YCbCr and the four-ink CMYK and
 * YCCK a printed datasheet's photographs come in.  A progressive file cut
 * short shows the scans that came; a sequential one is refused.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_JPEG_H_
#define TIKU_JPEG_H_

#include <stdint.h>

/**
 * @brief Decode a JPEG into 0xAARRGGBB pixels.
 *
 * @param out  Receives a malloc'd buffer of w*h pixels; the caller frees.
 * @return 0 on success, 1 when the file is a JPEG this cannot decode
 *         (arithmetic, lossless, 12-bit), -1 when it is malformed.
 */
int tiku_jpeg_decode(const unsigned char *src, long n, int *w, int *h,
                         uint32_t **out);

#endif /* TIKU_JPEG_H_ */
