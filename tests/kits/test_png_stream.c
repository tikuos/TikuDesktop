/*
 * TikuDesktop Translation Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_png_stream.c - streaming PNG pixels, sizes and callback refusals.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Exact-capacity output collector with a selected callback refusal. */
typedef struct {
    unsigned char *bytes;
    size_t capacity, used, calls, refuse;
} sink_t;

/** @brief Copy borrowed PNG bytes or refuse without consuming the chunk. */
static int
emit(void *context, const void *data, size_t length)
{
    sink_t *s = context;

    if (++s->calls == s->refuse) return 1;
    assert(length <= s->capacity - s->used);
    memcpy(s->bytes + s->used, data, length);
    s->used += length;
    return 0;
}

/** @brief Compare decoded pixels and stop at each observed callback. */
static void
roundtrip(unsigned width, unsigned height)
{
    size_t count = (size_t)width * height;
    uint32_t *pixels = malloc(count * sizeof *pixels);
    sink_t s = {.capacity = tiku_png_rgba_size(width, height)};
    tiku_image_t decoded = {0};
    size_t calls;

    s.bytes = malloc(s.capacity);
    assert(pixels && s.bytes);
    for (size_t i = 0; i < count; i++)
        pixels[i] = (uint32_t)i * 0x739ab123u;
    assert(!tiku_png_rgba_stream(pixels, width, height, emit, &s));
    assert(s.used == s.capacity);
    assert(tiku_image_decode(s.bytes, s.used, &decoded) == TIKU_IMG_OK);
    assert(decoded.w == (int)width && decoded.h == (int)height);
    assert(!memcmp(decoded.px, pixels, count * sizeof *pixels));
    tiku_image_free(&decoded);
    calls = s.calls;
    for (size_t i = 1; i <= calls; i++) {
        s.used = s.calls = 0; s.refuse = i;
        assert(tiku_png_rgba_stream(pixels, width, height, emit, &s) == -1);
        assert(s.calls == i && s.used < s.capacity);
    }
    free(s.bytes);
    free(pixels);
}

/** @brief Cover short/tall rows, block boundaries, alpha and invalid inputs. */
int
main(void)
{
    uint32_t pixel = 0;
    sink_t s = {0};

    for (unsigned width = 1; width <= 41; width++)
        roundtrip(width, width % 7 + 1);
    roundtrip(1024, 1);
    roundtrip(4095, 3);
    roundtrip(1, 4096);
    roundtrip(256, 256);
    assert(!tiku_png_rgba_size(0, 1));
    assert(!tiku_png_rgba_size(1, 0));
    assert(!tiku_png_rgba_size(UINT32_MAX, UINT32_MAX));
    assert(!tiku_png_rgba_size(TIKU_IMAGE_PIXELS_MAX + 1, 1));
    assert(tiku_png_rgba_size(TIKU_IMAGE_PIXELS_MAX, 1));
    assert(tiku_png_rgba_stream(NULL, 1, 1, emit, &s) == -1);
    assert(tiku_png_rgba_stream(&pixel, 0, 1, emit, &s) == -1);
    assert(tiku_png_rgba_stream(&pixel, 1, 1, NULL, &s) == -1);
    assert(!s.calls && !s.used);
    puts("Streaming PNG: 45 pixel oracles, sizes and callback refusals PASS");
    return 0;
}
