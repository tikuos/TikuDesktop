/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_gerber_extended.c - aperture macros, repeats and cut-ins
 * against analytic coverage: macro isolation and arithmetic, KiCad's own
 * rounded-rectangle and free-polygon macros, macro size and empty
 * macros, large thermals, repeat order and nesting, cut-in holes, errors
 * and cancellation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER "%FSLAX24Y24*%%MOMM*%"
#define PAPER  0xfffffff7           /* the renderer's empty pixel        */

/** @brief The whole of the file at @p path, NUL-terminated. */
static char *
read_fixture(const char *path)
{
    FILE *f = fopen(path, "rb");

    assert(f && !fseek(f, 0, SEEK_END));
    long n = ftell(f);

    assert(n > 0);
    rewind(f);
    char *s = malloc((size_t)n + 1);

    assert(s && fread(s, 1, (size_t)n, f) == (size_t)n && !fclose(f));
    s[n] = 0;
    return s;
}

/** @brief @p s decoded; it must be accepted. */
static tiku_circuit_t *
parse(const char *s)
{
    tiku_circuit_t *d = NULL;
    tiku_circuit_diagnostic_t diag;
    int e = tiku_circuit_gerber(s, strlen(s), NULL, &d, &diag);

    if (e) {
        fprintf(stderr, "Gerber: %s\n", diag.message);
    }
    assert(!e && d);
    return d;
}

/** @brief The fixture at @p path decoded; it must be accepted. */
static tiku_circuit_t *
fixture(const char *path)
{
    char *s = read_fixture(path);
    tiku_circuit_t *d = parse(s);

    free(s);
    return d;
}

/** @brief @p d rendered at 10 px/mm, 400 px square, offset (@p x, @p y)
 *         pixels. */
static tiku_image_t
render(tiku_circuit_t *d, double x, double y)
{
    tiku_image_t im = {0};
    tiku_circuit_view_t view = {.width = 400, .height = 400, .scale = 10,
                                .x = x, .y = y};

    assert(!tiku_circuit_render(d, &view, NULL, NULL, &im));
    return im;
}

/** @brief Whether pixel (@p x, @p y) of @p im is painted. */
static int
ink(tiku_image_t im, int x, int y)
{
    return im.px[(size_t)y * im.w + x] != PAPER;
}

/** @brief @p s refused with @p code, the output left alone. */
static void
bad(const char *s, tiku_circuit_err_t code)
{
    tiku_circuit_t *d = (void *)s;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_err_t e = tiku_circuit_gerber(s, strlen(s), NULL, &d, &diag);

    if (e != code) {
        fprintf(stderr, "Gerber %d, not %d: %s\n", e, code, diag.message);
    }
    assert(e == code && d == (void *)s && diag.code == code);
}

/** @brief A cancellation callback that fires on its @p p'th call. */
static int
cancel(void *p)
{
    return --*(int *)p <= 0;
}

/** @brief The contour validator: horizontal and vertical cut-ins are
 *         holes; a diagonal bridge, a partial overlap and a hole wound
 *         like its outline are refused. */
static void
contours(void)
{
    circuit_point_t p[] = {{0, 0}, {20, 0}, {20, 20}, {0, 20}, {0, 5},
                           {3, 5}, {3, 7}, {7, 7}, {7, 3}, {3, 3}, {3, 5},
                           {0, 5}};
    circuit_point_t q[12];

    assert(!circuit_contour(p, 12, NULL));
    for (size_t i = 0; i < 12; i++) {
        q[i] = (circuit_point_t){p[i].y, p[i].x};
    }
    assert(!circuit_contour(q, 12, NULL));      /* vertical cut-in */
    for (size_t i = 0; i < 12; i++) {
        q[i] = (circuit_point_t){p[i].x - p[i].y, p[i].x + p[i].y};
    }
    assert(circuit_contour(q, 12, NULL) == TIKU_CIRCUIT_BAD);  /* diagonal */
    memcpy(q, p, sizeof q);
    q[10].x = 2;
    assert(circuit_contour(q, 12, NULL) == TIKU_CIRCUIT_BAD);  /* overlap */
    memcpy(q, p, sizeof q);
    for (size_t i = 6; i <= 9; i++) {
        q[i] = p[15 - i];
    }
    assert(circuit_contour(q, 12, NULL) == TIKU_CIRCUIT_BAD);  /* winding */
}

/** @brief KiCad 7's own macros, exported from authored boards: rounded
 *         rectangles whose bodies or sides have no area, and a free
 *         polygon that writes its start again before closing.  Points
 *         come from the boards' pad definitions. */
static void
kicad_macros(void)
{
    tiku_circuit_t *d = fixture("tests/fixtures/circuit/roundrect.gbr");

    assert(d->count == 4);
    /* 2 x 1 mm, corner radius 0.25 mm, at (10, 10). */
    assert(tiku_circuit_hit(d, 10, 10, 0) == 0);
    assert(tiku_circuit_hit(d, 10.95, 10.45, 0) == SIZE_MAX);
    assert(tiku_circuit_hit(d, 10.9, 10.3, 0) == 0);
    /* 2 x 1 mm, radius 0.5 mm: a stadium from x 19 to 21. */
    assert(tiku_circuit_hit(d, 20, 10.45, 0) == 1);
    assert(tiku_circuit_hit(d, 19.1, 10, 0) == 1);
    assert(tiku_circuit_hit(d, 19.1, 9.6, 0) == SIZE_MAX);
    /* 1 x 1 mm, radius 0.5 mm: a disc. */
    assert(tiku_circuit_hit(d, 30.4, 10, 0) == 2);
    assert(tiku_circuit_hit(d, 30.4, 10.4, 0) == SIZE_MAX);
    /* The stadium turned 30 degrees: its axis runs up and right. */
    assert(tiku_circuit_hit(d, 40 + .9 * .866025, 10 - .9 * .5, 0) == 3);
    assert(tiku_circuit_hit(d, 40 + .9 * .5, 10 + .9 * .866025, 0)
           == SIZE_MAX);
    tiku_circuit_release(d);
    d = fixture("tests/fixtures/circuit/freepoly.gbr");
    assert(d->count == 2);
    /* An arrow with its tip at (11, 10). */
    assert(tiku_circuit_hit(d, 10.9, 10, 0) == 0);
    assert(tiku_circuit_hit(d, 9.6, 10.6, 0) == 0);
    assert(tiku_circuit_hit(d, 10.9, 10.3, 0) == SIZE_MAX);
    /* The same arrow turned 90 degrees, its tip at (20, 9). */
    assert(tiku_circuit_hit(d, 20, 9.1, 0) == 1);
    assert(tiku_circuit_hit(d, 20.3, 9.1, 0) == SIZE_MAX);
    tiku_circuit_release(d);
}

/** @brief A macro is held to the standard apertures' 1,000 mm; one whose
 *         primitives cover nothing flashes no object; a thermal larger
 *         than the chord budget allows at full accuracy still draws. */
static void
macro_limits(void)
{
    tiku_circuit_t *d;
    tiku_image_t im;

    bad(HEADER "%AMb*1,1,5000,0,0*%%ADD10b*%D10*X0Y0D03*M02*",
        TIKU_CIRCUIT_UNSUPPORTED);
    bad(HEADER "%AMb*21,1,1001,1,0,0,0*%%ADD10b*%M02*",
        TIKU_CIRCUIT_UNSUPPORTED);
    d = parse(HEADER "%AMb*21,1,1000,1,0,0,0*%%ADD10b*%D10*X0Y0D03*M02*");
    assert(d->count == 1);
    tiku_circuit_release(d);
    d = parse(HEADER "%AMe*1,1,0,0,0*21,1,0,2,0,0,0*20,1,1,3,3,3,3,0*"
              "4,1,3,0,0,1,0,2,0,0,0,0*%%ADD10e*%%ADD11C,1*%"
              "D10*X0Y0D03*X10000Y0D03*D11*X20000Y0D03*M02*");
    assert(d->count == 1 && strstr(d->shapes[0].info.name, "D11"));
    tiku_circuit_release(d);
    /* An outer diameter of 40 mm and an inner one of 30 mm, 4 mm gaps. */
    d = parse(HEADER "%AMt*7,0,0,40,30,4,0*%%ADD10t*%D10*X200000Y-200000D03*"
              "M02*");
    assert(d->count == 1 && d->shapes[0].group->count == 4);
    assert(tiku_circuit_hit(d, 20 + 12.37, 20 + 12.37, 0) == 0);
    assert(tiku_circuit_hit(d, 20 + 17.5, 20, 0) == SIZE_MAX);
    assert(tiku_circuit_hit(d, 20, 20, 0) == SIZE_MAX);
    im = render(d, 0, 0);
    assert(ink(im, 200 + 124, 200 + 124) && !ink(im, 200 + 175, 200));
    tiku_image_free(&im);
    tiku_circuit_release(d);
}

/** @brief Every group. */
int
main(void)
{
    contours();
    tiku_circuit_t *d = fixture("tests/fixtures/circuit/macros.gbr");

    assert(d->count == 7);
    tiku_image_t im = render(d, 0, 0);

    assert(!ink(im, 50, 50) && ink(im, 70, 50));            /* hole */
    assert(ink(im, 150, 50) && !ink(im, 170, 30));          /* vector */
    assert(ink(im, 260, 40) && !ink(im, 270, 30));          /* triangle */
    assert(ink(im, 50, 150));                               /* polygon */
    assert(!ink(im, 150, 150) && !ink(im, 150, 134)
           && ink(im, 161, 139));                           /* thermal */
    assert(ink(im, 250, 150) && ink(im, 269, 150));         /* moire */
    assert(tiku_circuit_hit(d, 5, 5, 0) == SIZE_MAX
           && tiku_circuit_hit(d, 7, 5, 0) == 0);
    tiku_image_free(&im);
    tiku_circuit_release(d);
    d = parse(HEADER "%AMRing*1,1,6,0,0*1,0,2,0,0*%%ADD10C,1*%%ADD11Ring*%"
              "D10*X50000Y-50000D03*D11*D03*M02*");
    im = render(d, 0, 0);
    assert(ink(im, 50, 50) && !ink(im, 57, 50) && ink(im, 70, 50));
    assert(tiku_circuit_hit(d, 5, 5, 0) == 0);
    tiku_image_free(&im);
    tiku_circuit_release(d);
    d = parse(HEADER "%AMRing*1,1,6,0,0*1,0,2,0,0*%%ADD10C,8*%%ADD11Ring*%"
              "D10*X50000Y-50000D03*%LPC*%D11*D03*M02*");
    im = render(d, 0, 0);
    assert(ink(im, 50, 50) && !ink(im, 70, 50) && ink(im, 85, 50));
    tiku_image_free(&im);
    tiku_circuit_release(d);
    kicad_macros();
    macro_limits();
    d = fixture("tests/fixtures/circuit/repeats.gbr");
    assert(d->count == 12);
    assert(d->shapes[2].cx == 5 && d->shapes[2].cy == 12
           && d->shapes[6].cx == 8 && d->shapes[6].cy == 15);
    im = render(d, 0, 0);
    assert(!ink(im, 50, 150) && ink(im, 65, 150) && !ink(im, 80, 90));
    tiku_image_free(&im);
    tiku_circuit_release(d);
    d = parse(HEADER "%ADD10C,1*%%SRX2Y1I3J0*%%SRX1Y2I0J3*%D10*"
              "X50000Y-50000D03*%SR*%%SR*%M02*");
    assert(d->count == 4);
    tiku_circuit_release(d);
    d = parse(HEADER "%AMa*1,1,4,0,0*1,0,2,0,0*%%ADD10a*%%SRX2Y1I6J0*%D10*"
              "X50000Y-50000D03*%SR*%M02*");
    assert(d->count == 2 && d->shapes[0].group == d->shapes[1].group);
    im = render(d, 0, 0);
    assert(!ink(im, 50, 50) && !ink(im, 110, 50) && ink(im, 125, 50));
    assert(tiku_circuit_hit(d, 12.5, 5, 0) == 1);
    tiku_image_free(&im);
    tiku_circuit_release(d);
    d = fixture("tests/fixtures/circuit/cutins.gbr");
    assert(d->count == 1);
    im = render(d, 20, 250);
    assert(!ink(im, 70, 200) && !ink(im, 140, 130) && ink(im, 110, 160)
           && ink(im, 35, 200));
    assert(tiku_circuit_hit(d, 5, -5, 0) == SIZE_MAX
           && tiku_circuit_hit(d, 12, -12, 0) == SIZE_MAX
           && tiku_circuit_hit(d, 9, -9, 0) == 0);
    tiku_image_free(&im);
    tiku_circuit_release(d);
    static const struct {
        const char *text;
        tiku_circuit_err_t code;
    } invalid[] = {
        {HEADER "%AMC*1,1,2,0,0*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "%AMa*$1=1/0*1,1,$1,0,0*%%ADD10a*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "%AMa*$1=1*$1=2*%%ADD10a*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "%AMa*$0=1*%%ADD10a*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "%AMa*1,1,(2+3,0,0*%%ADD10a*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "%AMa*1,2,3,0,0*%%ADD10a*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "%AMa*99,1,3*%%ADD10a*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "%AMa*1.5,1,3*%%ADD10a*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "%AMa*4,1,3,0,0,1,0,1,1,1,0,0*%%ADD10a*%M02*",
         TIKU_CIRCUIT_BAD},
        {HEADER "%AMa*4,1,4,0,0,1,1,1,0,0,1,0,0,0*%%ADD10a*%M02*",
         TIKU_CIRCUIT_BAD},
        {HEADER "%SR*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "%SRX0Y1I2J0*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "%SRX999Y999I2J2*%M02*", TIKU_CIRCUIT_LIMIT},
        {HEADER "%SRX2Y2I-1J1*%M02*", TIKU_CIRCUIT_BAD},
        {HEADER "G01*G36*X0Y0D02*X100000Y100000D01*X0Y100000D01*"
         "X100000Y0D01*X0Y0D01*G37*M02*", TIKU_CIRCUIT_BAD},
    };

    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        bad(invalid[i].text, invalid[i].code);
    }
    const char *paths[] = {"tests/fixtures/circuit/macros.gbr",
                           "tests/fixtures/circuit/repeats.gbr",
                           "tests/fixtures/circuit/cutins.gbr"};

    for (size_t p = 0; p < 3; p++) {
        char *s = read_fixture(paths[p]);

        for (int i = 1; i <= 24; i++) {
            int left = i;
            tiku_circuit_import_t opt = {.cancel = cancel, .ctx = &left};

            d = NULL;
            assert(tiku_circuit_gerber(s, strlen(s), &opt, &d, NULL)
                   == TIKU_CIRCUIT_CANCELLED && !d);
        }
        free(s);
    }
    puts("PASS: macro primitives/arithmetic/isolation, KiCad macros, macro "
         "limits, repeat order/nesting, cut-in holes, errors and "
         "cancellation");
    return 0;
}
