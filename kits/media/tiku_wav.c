/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wav.c - the RIFF walk and the resample behind tiku_wav_read().
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned channels, rate, bits;
} format_t;

/** @brief A little-endian field of @p n bytes at @p p. */
static uint32_t
le(const unsigned char *p, int n)
{
    uint32_t v = 0;
    int i;

    for (i = n - 1; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

/**
 * @brief Walk the chunks for "fmt " and "data".
 * @return the data chunk's bytes read into a malloc'd buffer, or NULL.
 */
static unsigned char *
chunks(FILE *f, format_t *fmt, size_t *bytes)
{
    unsigned char head[12], ch[8];

    if (fread(head, 1, 12, f) != 12 || memcmp(head, "RIFF", 4) != 0 ||
        memcmp(head + 8, "WAVE", 4) != 0) {
        return NULL;
    }
    memset(fmt, 0, sizeof *fmt);
    while (fread(ch, 1, 8, f) == 8) {
        uint32_t size = le(ch + 4, 4);

        if (memcmp(ch, "fmt ", 4) == 0) {
            unsigned char body[16];

            if (size < 16 || fread(body, 1, 16, f) != 16 ||
                le(body, 2) != 1) {
                return NULL;               /* not plain PCM */
            }
            fmt->channels = le(body + 2, 2);
            fmt->rate = le(body + 4, 4);
            fmt->bits = le(body + 14, 2);
            if (fseek(f, (long)(size - 16 + (size & 1)), SEEK_CUR) != 0) {
                return NULL;
            }
        } else if (memcmp(ch, "data", 4) == 0) {
            unsigned char *data;

            if (fmt->rate == 0 || size == 0) {
                return NULL;
            }
            data = malloc(size);
            if (data == NULL) {
                return NULL;
            }
            *bytes = fread(data, 1, size, f);
            return data;
        } else if (fseek(f, (long)(size + (size & 1)), SEEK_CUR) != 0) {
            return NULL;
        }
    }
    return NULL;
}

/** @brief Frame @p i of @p data as one signed 16-bit value, averaged. */
static int16_t
frame(const unsigned char *data, const format_t *fmt, size_t i)
{
    long sum = 0;
    unsigned c;
    size_t width = fmt->bits / 8;

    for (c = 0; c < fmt->channels; c++) {
        const unsigned char *p = data + (i * fmt->channels + c) * width;

        sum += (fmt->bits == 8) ? ((int)p[0] - 128) * 256
                                : (int16_t)le(p, 2);
    }
    return (int16_t)(sum / (long)fmt->channels);
}

int16_t *
tiku_wav_read(const char *path, unsigned rate, unsigned max_s, size_t *frames)
{
    FILE *f;
    format_t fmt;
    unsigned char *data;
    int16_t *out;
    size_t bytes = 0, in, n, i;

    if (frames != NULL) {
        *frames = 0;
    }
    if (path == NULL || rate == 0 || frames == NULL) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    data = chunks(f, &fmt, &bytes);
    fclose(f);
    if (data == NULL) {
        return NULL;
    }
    if ((fmt.bits != 8 && fmt.bits != 16) || fmt.channels < 1 ||
        fmt.channels > 2) {
        free(data);
        return NULL;
    }
    in = bytes / (fmt.channels * (fmt.bits / 8));
    n = (size_t)((double)in * rate / fmt.rate);
    if (n > (size_t)rate * max_s) {
        n = (size_t)rate * max_s;
    }
    out = (n > 0) ? malloc(n * sizeof *out) : NULL;
    if (out == NULL) {
        free(data);
        return NULL;
    }
    /* Each output frame takes the nearest source frame, unfiltered. */
    for (i = 0; i < n; i++) {
        size_t src = (size_t)((double)i * fmt.rate / rate);

        out[i] = frame(data, &fmt, src < in ? src : in - 1);
    }
    free(data);
    *frames = n;
    return out;
}
