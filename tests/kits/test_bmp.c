/* BMP decoding and thumbnail opacity for uncompressed RGB pixels.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tiku_image.h"

/** @brief Write an unaligned little-endian BMP field. */
static void put32(unsigned char *p, uint32_t value)
{
    for (int i = 0; i < 4; i++) p[i] = (unsigned char)(value >> (8 * i));
}

/** @brief BI_RGB's reserved byte does not affect pixel or thumbnail opacity. */
static void rgb32(int top_down)
{
    unsigned char bmp[78] = {0};
    const unsigned char reserved[] = {0, 128, 255};
    tiku_image_t image = {0}, thumb = {0};

    bmp[0] = 'B'; bmp[1] = 'M';
    put32(bmp + 2, sizeof bmp); put32(bmp + 10, 54);
    put32(bmp + 14, 40); put32(bmp + 18, 3);
    put32(bmp + 22, top_down ? (uint32_t)-2 : 2);
    bmp[26] = 1; bmp[28] = 32;
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 3; x++) {
            unsigned char *p = bmp + 54 + 12 * y + 4 * x;
            p[0] = 0x33; p[1] = 0x22; p[2] = (unsigned char)(0x11 + y);
            p[3] = reserved[x];
        }
    assert(tiku_image_decode(bmp, sizeof bmp, &image) == TIKU_IMG_OK);
    assert(image.w == 3 && image.h == 2);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 3; x++) {
            uint32_t red = (uint32_t)(0x11 + (top_down ? y : 1 - y));
            assert(image.px[y * 3 + x] == (0xff002233u | red << 16));
        }
    assert(tiku_image_thumb(&image, 12, &thumb) == 0);
    assert(thumb.px[0] == 0);
    for (int x = 0; x < thumb.w; x++)
        assert((thumb.px[6 * thumb.w + x] >> 24) == 255);
    tiku_image_free(&thumb);
    tiku_image_free(&image);
    assert(tiku_image_decode(bmp, sizeof bmp - 1, &image) == TIKU_IMG_BAD);
    put32(bmp + 30, 3);
    assert(tiku_image_decode(bmp, sizeof bmp, &image) == TIKU_IMG_UNSUPPORTED);
}

/** @brief Both BMP row orders decode opaque RGB data. */
int main(void)
{
    rgb32(0);
    rgb32(1);
    puts("test_bmp: pass");
    return 0;
}
