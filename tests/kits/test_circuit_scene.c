/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_scene.c - a scene without a window or a CAD file: bounds,
 * ownership of what it copies, picking of paths and filled regions,
 * descriptions and their accounting, fit and anchored zoom.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/** @brief The authored example: its objects, picking, fit and zoom. */
static void
example(void)
{
    tiku_circuit_t *d = NULL;

    assert(tiku_circuit_example(&d) == TIKU_CIRCUIT_OK && d);
    assert(tiku_circuit_count(d) == 8);
    tiku_circuit_box_t b = tiku_circuit_bounds(d);

    assert(b.x0 < 3 && b.x1 > 60 && b.y0 < 9 && b.y1 > 40);
    assert(!strcmp(tiku_circuit_object(d, 3)->name, "R1"));
    assert(!tiku_circuit_object(d, SIZE_MAX));
    size_t hit = tiku_circuit_hit(d, 28, 9, .1);

    assert(hit == 3);
    /* Inside the resistor's bounding box but off its path. */
    assert(tiku_circuit_hit(d, 28, 12, .1) == SIZE_MAX);
    assert(tiku_circuit_hit(d, NAN, 12, 1) == SIZE_MAX);
    assert(tiku_circuit_hit(d, 8, 15, -1) == SIZE_MAX);
    tiku_circuit_view_t v;

    assert(tiku_circuit_fit(d, 640, 480, &v));
    double x, y, after_x, after_y;

    assert(tiku_circuit_world(&v, 172, 90, &x, &y));
    assert(tiku_circuit_zoom(&v, 1.5, 172, 90));
    assert(tiku_circuit_world(&v, 172, 90, &after_x, &after_y));
    assert(fabs(x - after_x) < 1e-12 && fabs(y - after_y) < 1e-12);
    tiku_circuit_view_t old = v;

    assert(!tiku_circuit_zoom(&v, NAN, 172, 90));
    assert(!memcmp(&old, &v, sizeof v));
    assert(!tiku_circuit_fit(d, 0, 480, &v));
    assert(!tiku_circuit_fit(d, TIKU_CIRCUIT_RENDER_SIDE,
                             TIKU_CIRCUIT_RENDER_SIDE, &v));
    assert(!memcmp(&old, &v, sizeof v));
    tiku_circuit_t *retained = tiku_circuit_retain(d);

    tiku_circuit_release(d);
    assert(!strcmp(tiku_circuit_object(retained, 3)->value, "1 kohm"));
    tiku_circuit_release(retained);
}

/** @brief A built scene copies what it is given, picks filled interiors
 *         and accounts for its descriptions. */
static void
built(void)
{
    tiku_circuit_t *d = circuit_new();

    assert(d);
    char name[] = "Box", value[] = "Filled";
    circuit_point_t points[] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    assert(!circuit_add(d, name, value, points, 4, .2, 0xff123456, 1, 1));
    name[0] = 'X';
    points[0].x = 999;
    assert(!strcmp(tiku_circuit_object(d, 0)->name, "Box"));
    assert(tiku_circuit_hit(d, 5, 5, 0) == 0);
    /* A fill does not paint its stroke width outside the polygon. */
    assert(tiku_circuit_hit(d, 10.05, 5, 0) == SIZE_MAX);
    tiku_circuit_box_t b = tiku_circuit_bounds(d);

    assert(b.x0 == 0 && b.x1 == 10 && b.y0 == 0 && b.y1 == 10);
    points[0].x = NAN;
    assert(circuit_add(d, "Bad", "", points, 4, .2, 0, 1, 0) ==
           TIKU_CIRCUIT_BAD);
    assert(d->count == 1);
    assert(!*tiku_circuit_details(NULL, 0) &&
           !*tiku_circuit_details(d, SIZE_MAX));
    char details[] = "Reference: U1\nValue: sensor";
    size_t original = d->bytes;

    assert(!circuit_details(d, 0, details));
    details[0] = 'X';
    assert(!strcmp(tiku_circuit_details(d, 0),
                   "Reference: U1\nValue: sensor"));
    assert(d->bytes == original + strlen(details) + 1);
    assert(!circuit_details(d, 0, "New") && d->bytes == original + 4);
    tiku_circuit_t *retained = tiku_circuit_retain(d);

    assert(circuit_details(d, 0, "Changed") == TIKU_CIRCUIT_BAD);
    tiku_circuit_release(retained);
    char huge[CIRCUIT_DETAILS_MAX + 2];

    memset(huge, 'x', sizeof huge - 1);
    huge[sizeof huge - 1] = 0;
    assert(circuit_details(d, 0, huge) == TIKU_CIRCUIT_LIMIT);
    d->bytes = CIRCUIT_BYTES_MAX;
    assert(circuit_details(d, 0, "Larger") == TIKU_CIRCUIT_LIMIT);
    assert(!strcmp(tiku_circuit_details(d, 0), "New"));
    d->bytes = original + 4;
    assert(circuit_add(d, "Bad", "", points, SIZE_MAX, .2, 0, 1, 0) ==
           TIKU_CIRCUIT_LIMIT);
    tiku_circuit_release(d);
}

/** @brief Every group; a failed check aborts. */
int
main(void)
{
    example();
    built();
    puts("PASS: circuit bounds, ownership, path selection, filled regions "
         "and anchored zoom");
    return 0;
}
