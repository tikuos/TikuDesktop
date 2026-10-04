/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_stack.c - layer stacks: clear operations confined to their
 * own layer, drill and artwork on one source origin, front-to-back
 * picking, opacity, hidden layers, snapshots that keep their scenes, and
 * cancellation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define HEAD "%FSLAX24Y24*%%MOMM*%%ADD10C,10*%D10*"

/** @brief The Gerber layer @p s, which must decode. */
static tiku_circuit_t *
parse(const char *s)
{
    tiku_circuit_t *d = NULL;

    assert(!tiku_circuit_gerber(s, strlen(s), NULL, &d, NULL));
    return d;
}

/** @brief Pixel (@p x, @p y) of @p d drawn at 10 px/mm in 200 by 200. */
static uint32_t
pixel(tiku_circuit_t *d, int x, int y)
{
    tiku_circuit_view_t view = {.width = 200, .height = 200, .scale = 10};
    tiku_image_t im = {0};

    assert(!tiku_circuit_render(d, &view, NULL, NULL, &im));
    uint32_t p = im.px[y * 200 + x];

    tiku_image_free(&im);
    return p;
}

/** @brief A cancellation that comes on the @p p'th call. */
static int
cancel(void *p)
{
    return --*(int *)p <= 0;
}

/** @brief Two artwork layers and a drill layer stacked every way the
 *         checks need; a failed check aborts. */
int
main(void)
{
    tiku_circuit_t *back = parse(HEAD "X100000Y-100000D03*M02*");
    tiku_circuit_t *front = parse(HEAD "X100000Y-100000D03*%ADD11C,4*%D11*"
                                  "%LPC*%X100000Y-100000D03*M02*");
    tiku_circuit_layer_t layers[] = {
        {.scene = front, .path = "front.gbr", .root = "", .label = "Front",
         .colour = 0xffff0000, .opacity = 255, .visible = 1},
        {.scene = back, .path = "back.gbr", .root = "", .label = "Back",
         .colour = 0xff0000ff, .opacity = 255, .visible = 1}
    };
    tiku_circuit_t *stack = NULL;

    assert(!tiku_circuit_stack(layers, 2, &stack));
    assert(tiku_circuit_layer_count(stack) == 2 &&
           tiku_circuit_count(stack) == 3);
    /* The clear on the front layer shows the back layer, not the paper. */
    assert(pixel(stack, 100, 100) == 0xff0000ff);
    assert(pixel(stack, 130, 100) == 0xffff0000);
    assert(tiku_circuit_object_layer(stack,
                                     tiku_circuit_hit(stack, 10, 10, 0)) ==
           1);
    assert(tiku_circuit_object_layer(stack,
                                     tiku_circuit_hit(stack, 13, 10, 0)) ==
           0);
    assert(tiku_circuit_find(stack, "D10", 0) == 0 &&
           tiku_circuit_find(stack, "D10", 2) == 2);
    layers[0].visible = 0;
    tiku_circuit_t *hidden = NULL;

    assert(!tiku_circuit_stack(layers, 2, &hidden));
    assert(pixel(hidden, 130, 100) == 0xff0000ff &&
           pixel(stack, 130, 100) == 0xffff0000);
    assert(tiku_circuit_count(hidden) == 1 &&
           tiku_circuit_object_layer(hidden, 0) == 1);
    tiku_circuit_release(hidden);
    layers[0].visible = 1;
    layers[0].opacity = 128;
    assert(!tiku_circuit_stack(layers, 2, &hidden));
    assert(pixel(hidden, 130, 100) == 0xff80007f);
    tiku_circuit_release(hidden);
    layers[0].opacity = 255;
    layers[0].scene = back;
    layers[1].scene = front;
    assert(!tiku_circuit_stack(layers, 2, &hidden));
    assert(pixel(hidden, 100, 100) == 0xffff0000);
    tiku_circuit_release(hidden);
    tiku_circuit_t *drill = NULL;
    const char *s = "M48\nMETRIC\nT1C1\n%\nT1\nX10.0Y-10.0\nM30";

    assert(!tiku_circuit_drill(s, strlen(s), NULL, &drill, NULL));
    layers[0].scene = drill;
    layers[0].colour = 0xff00ff00;
    layers[1].scene = back;
    assert(!tiku_circuit_stack(layers, 2, &hidden));
    assert(pixel(hidden, 100, 100) == 0xff00ff00 &&
           pixel(hidden, 110, 100) == 0xff0000ff);
    assert(tiku_circuit_object_layer(hidden,
                                     tiku_circuit_hit(hidden, 10, 10, 0)) ==
           0);
    assert(tiku_circuit_bounds(hidden).x0 == 5 &&
           tiku_circuit_bounds(hidden).x1 == 15);
    tiku_circuit_release(hidden);
    layers[0].scene = NULL;
    layers[0].diagnostic.code = TIKU_CIRCUIT_MISSING;
    snprintf(layers[0].diagnostic.message,
             sizeof layers[0].diagnostic.message, "Missing fixture");
    assert(!tiku_circuit_stack(layers, 2, &hidden));
    assert(tiku_circuit_stack_incomplete(hidden));
    assert(strstr(tiku_circuit_warning(hidden), "incomplete") &&
           pixel(hidden, 100, 100) == 0xff0000ff);
    tiku_circuit_release(hidden);
    layers[0].scene = stack;
    hidden = (void *)layers;
    assert(tiku_circuit_stack(layers, 2, &hidden) == TIKU_CIRCUIT_BAD &&
           hidden == (void *)layers);
    assert(tiku_circuit_stack(layers, TIKU_CIRCUIT_LAYERS_MAX + 1,
                              &hidden) == TIKU_CIRCUIT_LIMIT &&
           hidden == (void *)layers);
    tiku_circuit_view_t view = {.width = 200, .height = 200, .scale = 10};

    for (int i = 1; i <= 20; i++) {
        int left = i;
        tiku_image_t im = {0};

        assert(tiku_circuit_render(stack, &view, cancel, &left, &im) ==
               TIKU_CIRCUIT_CANCELLED && !im.px);
    }
    tiku_circuit_release(front);
    tiku_circuit_release(back);
    tiku_circuit_release(drill);
    assert(pixel(stack, 100, 100) == 0xff0000ff);
    tiku_circuit_release(stack);
    puts("PASS: layer-local clear, drill alignment, ordering, opacity, "
         "hidden picking, ownership and cancellation");
    return 0;
}
