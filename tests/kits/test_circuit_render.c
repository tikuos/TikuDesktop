/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_render.c - the circuit renderer and the vector path under
 * it: analytic masks and curve error, cancellation, viewport limits, and
 * views zoomed far past the drawing's extent, whose clipped geometry must
 * draw what an exact stroke or fill would.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"
#include "tiku_vector_raster.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define INK 0xff123456u         /* a colour no test scene paints else    */

/** @brief A cancellation that comes on the @p ctx'th call. */
static int
cancel(void *ctx)
{
    return --*(int *)ctx <= 0;
}

/** @brief Add the closed rectangle at (@p x, @p y), @p w by @p h, to
 *         @p p. */
static void
box(tiku_vector_path_t *p, double x, double y, double w, double h)
{
    tiku_vector_path_moveto(p, x, y);
    tiku_vector_path_lineto(p, x + w, y);
    tiku_vector_path_lineto(p, x + w, y + h);
    tiku_vector_path_lineto(p, x, y + h);
    tiku_vector_path_close(p);
}

/** @brief The vector path: even-odd and nonzero fills, curve error
 *         against the analytic curve, point and coordinate limits. */
static void
paths(void)
{
    tiku_vector_path_t p = {.strict = 1}, stroke = {.strict = 1};

    box(&p, 2, 2, 26, 26);
    box(&p, 8, 8, 10, 10);
    tiku_vector_mask_t m = tiku_vector_path_fill(&p, 1, 0, 0, 32, 32);

    assert(m.a && m.a[3 * 32 + 3] == 255 && m.a[10 * 32 + 10] == 0 &&
           !m.a[0]);
    tiku_vector_mask_free(&m);
    m = tiku_vector_path_fill(&p, 0, 0, 0, 32, 32);
    assert(m.a && m.a[10 * 32 + 10] == 255);
    tiku_vector_mask_free(&m);
    int left = 3;

    m = tiku_vector_path_fill_cancel(&p, 0, 0, 0, 32, 32, cancel, &left);
    assert(!m.a && !m.w && !m.h);
    tiku_vector_path_clear(&p);
    tiku_vector_path_moveto(&p, 0, 0);
    tiku_vector_path_curveto(&p, 0, 10000, 10000, 10000, 10000, 0);
    assert(!p.failed && p.n > 64);
    /* Sample the analytic curve apart from the subdivision. */
    for (int k = 0; k <= 8192; k++) {
        double t = k / 8192.0, u = 1 - t;
        double x = 30000 * u * t * t + 10000 * t * t * t, y = 30000 * u * t;
        double nearest = HUGE_VAL;

        for (int i = 1; i < p.n; i++) {
            double ax = p.pt[2 * (i - 1)], ay = p.pt[2 * (i - 1) + 1];
            double dx = p.pt[2 * i] - ax, dy = p.pt[2 * i + 1] - ay;
            double den = dx * dx + dy * dy;
            double f = den ? ((x - ax) * dx + (y - ay) * dy) / den : 0;

            f = fmin(1, fmax(0, f));
            nearest = fmin(nearest, hypot(x - ax - f * dx, y - ay - f * dy));
        }
        assert(nearest <= .25);
    }
    tiku_vector_path_clear(&p);
    p.max_points = 8;
    tiku_vector_path_moveto(&p, 0, 0);
    tiku_vector_path_curveto(&p, 0, 10000, 10000, 10000, 10000, 0);
    assert(p.failed == TIKU_VECTOR_LIMIT);
    tiku_vector_path_clear(&p);
    p.max_points = 0;
    tiku_vector_path_moveto(&p, NAN, 0);
    assert(p.failed == TIKU_VECTOR_BAD);
    tiku_vector_path_clear(&p);
    tiku_vector_path_moveto(&p, 8, 16);
    tiku_vector_path_lineto(&p, 24, 16);
    tiku_vector_path_stroke(&p, 4, 1, 1, 4, NULL, 0, 0, &stroke);
    assert(!stroke.failed);
    m = tiku_vector_path_fill(&stroke, 0, 0, 0, 32, 32);
    assert(m.a && m.a[16 * 32 + 7] > 200 && m.a[16 * 32 + 25] > 200 &&
           !m.a[5 * 32 + 16]);
    tiku_vector_mask_free(&m);
    tiku_vector_path_free(&p);
    tiku_vector_path_free(&stroke);
    m = tiku_vector_mask_full(-1, 4);
    assert(!m.a);
}

/** @brief The example drawn, cancelled and refused past its limits. */
static void
example(void)
{
    tiku_circuit_t *d = NULL;

    assert(!tiku_circuit_example(&d));
    tiku_circuit_view_t v;

    assert(tiku_circuit_fit(d, 640, 480, &v));
    tiku_image_t im = {0};

    assert(!tiku_circuit_render(d, &v, NULL, NULL, &im));
    assert(im.w == 640 && im.h == 480 && im.px);
    size_t ink = 0;

    for (size_t i = 0; i < 640u * 480u; i++) {
        ink += im.px[i] != CIRCUIT_PAPER;
    }
    assert(ink > 500 && ink < 40000);
    tiku_image_free(&im);
    int left = 1;

    assert(tiku_circuit_render(d, &v, cancel, &left, &im) ==
           TIKU_CIRCUIT_CANCELLED);
    assert(!im.px);
    v.width = v.height = 8192;
    assert(tiku_circuit_render(d, &v, NULL, NULL, &im) ==
           TIKU_CIRCUIT_LIMIT && !im.px);
    v.scale = NAN;
    assert(tiku_circuit_render(d, &v, NULL, NULL, &im) ==
           TIKU_CIRCUIT_BAD && !im.px);
    tiku_circuit_release(d);
}

/** @brief Whether every pixel of @p im's rows @p y0 to @p y1 and columns
 *         @p x0 to @p x1 is @p colour. */
static int
all(const tiku_image_t *im, int x0, int y0, int x1, int y1,
    uint32_t colour)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (im->px[(size_t)y * im->w + x] != colour) {
                return 0;
            }
        }
    }
    return 1;
}

/** @brief A KiCad wire zoomed on until the scale stops growing, every
 *         view drawn: the wires reach far past the viewport. */
static void
zoomed_wires(void)
{
    const char *text = "(kicad_sch (version 20230121)"
        " (wire (pts (xy 20 100) (xy 400 100)))"
        " (wire (pts (xy 20 50) (xy 20 250))))";
    tiku_circuit_t *d = NULL;
    tiku_circuit_view_t v;

    assert(!tiku_circuit_kicad(text, strlen(text), NULL, &d, NULL));
    assert(tiku_circuit_fit(d, 800, 600, &v));
    double px = 100 * v.scale + v.x, py = 100 * v.scale + v.y;
    int zooms = 0;

    while (v.scale < CIRCUIT_SCALE_MAX) {
        tiku_image_t im = {0};

        assert(tiku_circuit_zoom(&v, 1.25, px, py));
        assert(!tiku_circuit_render(d, &v, NULL, NULL, &im));
        assert(im.px[(size_t)py * im.w + (size_t)px] != CIRCUIT_PAPER);
        tiku_image_free(&im);
        zooms++;
    }
    assert(zooms > 10);
    tiku_circuit_release(d);

    /* A short wire far from the origin: its fitted view is drawn. */
    text = "(kicad_sch (version 20230121)"
        " (wire (pts (xy 5000 5000) (xy 5000.1 5000))))";
    d = NULL;
    assert(!tiku_circuit_kicad(text, strlen(text), NULL, &d, NULL));
    assert(tiku_circuit_fit(d, 800, 600, &v));
    assert(fabs(v.x) > 1e7);
    tiku_image_t im = {0};

    assert(!tiku_circuit_render(d, &v, NULL, NULL, &im));
    assert(im.px[300 * 800 + 400] != CIRCUIT_PAPER);
    tiku_image_free(&im);
    tiku_circuit_release(d);
}

/** @brief Strokes and fills millions of pixels long, drawn at 1000 px/mm
 *         about the origin, match their exact coverage. */
static void
clipped_shapes(void)
{
    tiku_circuit_t *d = circuit_new();
    const circuit_point_t line[] = {{-5000, 0}, {5000, 0}};
    tiku_circuit_view_t v = {640, 480, 1000, 320, 240};
    tiku_image_t im = {0};

    /* 0.1 mm wide: 100 px about row 240, the full width. */
    assert(d && !circuit_add(d, "Wire", "", line, 2, .1, INK, 0, 0));
    assert(!tiku_circuit_render(d, &v, NULL, NULL, &im));
    assert(all(&im, 0, 191, 639, 288, INK));
    assert(all(&im, 0, 0, 639, 188, CIRCUIT_PAPER));
    assert(all(&im, 0, 291, 639, 479, CIRCUIT_PAPER));
    tiku_image_free(&im);
    tiku_circuit_release(d);

    /* Filled to x = 0.05 mm: column 370. */
    const circuit_point_t area[] = {{-5000, -5000}, {.05, -5000},
                                    {.05, 5000}, {-5000, 5000}};

    d = circuit_new();
    assert(d && !circuit_add(d, "Fill", "", area, 4, 0, INK, 1, 1));
    assert(!tiku_circuit_render(d, &v, NULL, NULL, &im));
    assert(all(&im, 0, 0, 368, 479, INK));
    assert(all(&im, 371, 0, 639, 479, CIRCUIT_PAPER));
    tiku_image_free(&im);
    tiku_circuit_release(d);

    /* A circle of radius 5 m whose top passes through the origin. */
    d = circuit_new();
    assert(d && !circuit_arc(d, "Arc", "", 0, 5000, 5000, 0, 2 * M_PI, .1,
                             INK, 0));
    assert(!tiku_circuit_render(d, &v, NULL, NULL, &im));
    assert(all(&im, 300, 192, 340, 287, INK));
    assert(all(&im, 300, 0, 340, 187, CIRCUIT_PAPER));
    assert(all(&im, 300, 292, 340, 479, CIRCUIT_PAPER));
    tiku_image_free(&im);
    tiku_circuit_release(d);
}

/** @brief Fit, zoom and render share one translation limit; a stroke too
 *         wide to clip within the rasteriser's coordinates is a limit,
 *         not invalid data. */
static void
shared_limit(void)
{
    tiku_circuit_t *d = circuit_new();
    const circuit_point_t line[] = {{-10, 0}, {10, 0}};
    tiku_circuit_view_t v = {64, 48, 1, CIRCUIT_SHIFT_MAX, 0};
    tiku_image_t im = {0};

    assert(d && !circuit_add(d, "Wire", "", line, 2, .1, INK, 0, 0));
    assert(!tiku_circuit_render(d, &v, NULL, NULL, &im));
    tiku_image_free(&im);
    v.x = -CIRCUIT_SHIFT_MAX * 1.000001;
    assert(tiku_circuit_render(d, &v, NULL, NULL, &im) ==
           TIKU_CIRCUIT_BAD && !im.px);
    /* Zooming in about a pixel 20 m out would move the drawing past the
     * limit; zooming about the viewport's centre does not. */
    v = (tiku_circuit_view_t){64, 48, 1, 0, 0};
    tiku_circuit_view_t before = v;

    assert(!tiku_circuit_zoom(&v, CIRCUIT_SCALE_MAX, 20000, 0));
    assert(!memcmp(&before, &v, sizeof v));
    assert(tiku_circuit_zoom(&v, CIRCUIT_SCALE_MAX, 32, 24));
    tiku_circuit_release(d);

    /* A wire a metre from the origin, zoomed fully in: the view moves the
     * drawing four million pixels, and is drawn. */
    const circuit_point_t far[] = {{990, 0}, {1010, 0}};

    d = circuit_new();
    assert(d && !circuit_add(d, "Far", "", far, 2, .1, INK, 0, 0));
    v = (tiku_circuit_view_t){640, 480, 1, 320 - 1000, 240};
    assert(tiku_circuit_zoom(&v, CIRCUIT_SCALE_MAX, 320, 240));
    assert(v.scale == CIRCUIT_SCALE_MAX && fabs(v.x) > 4e6);
    assert(!tiku_circuit_render(d, &v, NULL, NULL, &im));
    assert(all(&im, 0, 200, 639, 280, INK));
    tiku_image_free(&im);
    tiku_circuit_release(d);

    d = circuit_new();
    assert(d && !circuit_add(d, "Wide", "", line, 2, CIRCUIT_WIDTH_MAX, INK,
                             0, 0));
    v = (tiku_circuit_view_t){640, 480, CIRCUIT_SCALE_MAX, 320, 240};
    assert(tiku_circuit_render(d, &v, NULL, NULL, &im) ==
           TIKU_CIRCUIT_LIMIT && !im.px);
    tiku_circuit_release(d);
}

/** @brief Every group; a failed check aborts. */
int
main(void)
{
    paths();
    example();
    zoomed_wires();
    clipped_shapes();
    shared_limit();
    puts("PASS: circuit masks, holes, strict curves, clipped views, render "
         "cancellation and limits");
    return 0;
}
