/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_limits.c - a scene's admission limits: objects, points,
 * names, coordinates, text and viewports accepted up to their boundary
 * and refused one past it, before anything is allocated or published.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"
#include "tiku_fontset.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Object and point limits take the boundary, not one more. */
static void
scene_limits(void)
{
    const circuit_point_t line[] = {{0, 0}, {1, 1}};
    tiku_circuit_t *d = circuit_new();

    assert(d);
    for (size_t i = 0; i < CIRCUIT_OBJECTS_MAX; i++) {
        assert(!circuit_add(d, "Wire", "", line, 2, .2, CIRCUIT_OPAQUE, 0,
                            0));
    }
    size_t bytes = d->bytes;

    assert(circuit_add(d, "Extra", "", line, 2, .2, 0, 0, 0) ==
           TIKU_CIRCUIT_LIMIT);
    assert(d->count == CIRCUIT_OBJECTS_MAX && d->bytes == bytes);
    tiku_circuit_release(d);

    circuit_point_t *points = calloc(CIRCUIT_POINTS_MAX, sizeof *points);

    d = circuit_new();
    assert(d && points);
    assert(!circuit_add(d, "Path", "", points, CIRCUIT_POINTS_MAX, 0, 0, 0,
                        0));
    assert(circuit_add(d, "Extra", "", line, 2, 0, 0, 0, 0) ==
           TIKU_CIRCUIT_LIMIT);
    assert(d->count == 1 && d->points == CIRCUIT_POINTS_MAX);
    free(points);
    tiku_circuit_release(d);
}

/** @brief Bad geometry and long strings leave a retained scene as it
 *         was. */
static void
invalid_objects(void)
{
    circuit_point_t line[] = {{0, 0}, {CIRCUIT_COORD_MAX, CIRCUIT_COORD_MAX}};
    char name[CIRCUIT_NAME_BYTES + 1];

    memset(name, 'a', sizeof name);
    name[CIRCUIT_NAME_BYTES - 1] = 0;
    tiku_circuit_t *d = circuit_new();

    assert(d);
    assert(!circuit_add(d, name, "", line, 2, CIRCUIT_WIDTH_MAX, 0, 0, 0));
    name[CIRCUIT_NAME_BYTES - 1] = 'a';
    name[CIRCUIT_NAME_BYTES] = 0;
    assert(circuit_add(d, name, "", line, 2, 1, 0, 0, 0) ==
           TIKU_CIRCUIT_LIMIT);
    line[1].x = CIRCUIT_COORD_MAX + .01;
    assert(circuit_add(d, "Bad", "", line, 2, 1, 0, 0, 0) ==
           TIKU_CIRCUIT_BAD);
    line[1].x = INFINITY;
    assert(circuit_add(d, "Bad", "", line, 2, 1, 0, 0, 0) ==
           TIKU_CIRCUIT_BAD);
    line[1].x = 1;
    assert(circuit_add(d, "Bad", "", line, 2, CIRCUIT_WIDTH_MAX + .01, 0, 0,
                       0) == TIKU_CIRCUIT_BAD);
    assert(circuit_add(d, "Bad", "", line, 2, -1, 0, 0, 0) ==
           TIKU_CIRCUIT_BAD);
    tiku_circuit_t *snapshot = tiku_circuit_retain(d);

    assert(circuit_add(d, "Wire", "", line, 2, 1, 0, 0, 0) ==
           TIKU_CIRCUIT_BAD);
    assert(d->count == 1);
    tiku_circuit_release(snapshot);
    tiku_circuit_release(d);
}

/** @brief Text of CIRCUIT_TEXT_MAX bytes is admitted, a byte more is
 *         not. */
static void
text_limit(void)
{
    tiku_fontset_face_t face;

    assert(!tiku_fontset_find("DejaVu Sans", 400, 0, &face));
    FILE *f = fopen(face.path, "rb");

    assert(f && !fseek(f, 0, SEEK_END));
    long n = ftell(f);

    assert(n > 0);
    rewind(f);
    unsigned char *font = malloc((size_t)n);

    assert(font && fread(font, 1, (size_t)n, f) == (size_t)n && !fclose(f));
    tiku_circuit_t *d = circuit_new();

    assert(d);
    d->font = tiku_ttf_open_mem(font, (size_t)n);
    assert(d->font);
    char *text = malloc(CIRCUIT_TEXT_MAX + 2);
    const double m[6] = {.1, 0, 0, .1, 0, 0};

    assert(text);
    memset(text, 'i', CIRCUIT_TEXT_MAX + 1);
    text[CIRCUIT_TEXT_MAX] = 0;
    assert(!circuit_text(d, "Text", "", text, m, -1, 0, 0, CIRCUIT_OPAQUE));
    text[CIRCUIT_TEXT_MAX] = 'i';
    text[CIRCUIT_TEXT_MAX + 1] = 0;
    assert(circuit_text(d, "Text", "", text, m, -1, 0, 0, CIRCUIT_OPAQUE) ==
           TIKU_CIRCUIT_LIMIT);
    assert(d->count == 1);
    free(text);
    free(font);
    tiku_circuit_release(d);
}

/** @brief Pixel and dimension bounds are separate; a failure keeps the
 *         output. */
static void
viewport_limits(void)
{
    tiku_circuit_t *d = circuit_new();
    tiku_circuit_view_t v = {2048, 2048, 1, 0, 0};
    tiku_image_t image = {0};

    assert(d && (size_t)v.width * v.height == TIKU_CIRCUIT_RENDER_PIXELS);
    assert(!tiku_circuit_render(d, &v, NULL, NULL, &image));
    tiku_image_free(&image);
    v.width++;
    assert(tiku_circuit_render(d, &v, NULL, NULL, &image) ==
           TIKU_CIRCUIT_LIMIT);
    assert(!image.px);
    v.width = TIKU_CIRCUIT_RENDER_SIDE;
    v.height = 1;
    assert(!tiku_circuit_render(d, &v, NULL, NULL, &image));
    uint32_t *pixels = image.px;

    assert(tiku_circuit_render(d, &v, NULL, NULL, &image) ==
           TIKU_CIRCUIT_BAD);
    assert(image.px == pixels && image.w == TIKU_CIRCUIT_RENDER_SIDE &&
           image.h == 1);
    tiku_image_free(&image);
    v.width++;
    assert(tiku_circuit_render(d, &v, NULL, NULL, &image) ==
           TIKU_CIRCUIT_LIMIT);
    assert(!image.px);
    tiku_circuit_release(d);
}

/** @brief Every group; a failed check aborts. */
int
main(void)
{
    scene_limits();
    invalid_objects();
    text_limit();
    viewport_limits();
    puts("PASS: circuit point, object, string, coordinate, text and "
         "viewport limits");
    return 0;
}
