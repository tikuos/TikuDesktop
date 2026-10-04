/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_gerber.c - Gerber artwork against an authored board's
 * KiCad export and analytic fixtures: coordinates, flashes, holes against
 * clear coverage, arcs, modal units, zero-size apertures and attribute
 * scopes; each defect in an otherwise valid file refused with its code;
 * truncation, cancellation and limits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER "%FSLAX24Y24*%%MOMM*%%ADD10C,1*%D10*G01*"
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

/** @brief @p s decoded as artwork; it must be accepted. */
static tiku_circuit_t *
parse(const char *s)
{
    tiku_circuit_t *d = NULL;
    tiku_circuit_diagnostic_t diag;
    int e = tiku_circuit_gerber(s, strlen(s), NULL, &d, &diag);

    if (e) {
        fprintf(stderr, "Gerber line %u: %s\n", diag.line, diag.message);
    }
    assert(!e && d && d->coverage);
    return d;
}

/** @brief Whether @p a and @p b agree to 1e-7. */
static void
close_to(double a, double b)
{
    assert(fabs(a - b) < 1e-7);
}

/** @brief A cancellation callback that fires on its @p ctx'th call. */
static int
cancel(void *ctx)
{
    return --*(int *)ctx <= 0;
}

/** @brief @p s refused with @p code, a line and a message, the output
 *         left alone. */
static void
bad(const char *s, tiku_circuit_err_t code)
{
    tiku_circuit_t *d = (void *)s;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_err_t e = tiku_circuit_gerber(s, strlen(s), NULL, &d, &diag);

    if (e != code) {
        fprintf(stderr, "Gerber %d, not %d: %s\n", e, code, diag.message);
    }
    assert(e == code && d == (void *)s && diag.code == code && diag.line
           && diag.message[0]);
}

/** @brief @p d rendered at 10 px/mm into 400 by 320 pixels. */
static tiku_image_t
render(tiku_circuit_t *d)
{
    tiku_circuit_view_t view = {.width = 400, .height = 320, .scale = 10,
                                .x = 0, .y = 0};
    tiku_image_t im = {0};

    assert(!tiku_circuit_render(d, &view, NULL, NULL, &im));
    return im;
}

/** @brief Whether pixel (@p x, @p y) of @p im is painted. */
static int
ink(tiku_image_t im, int x, int y)
{
    return im.px[(size_t)y * im.w + x] != PAPER;
}

/** @brief Each defect after HEADER, in a file otherwise valid, refused
 *         with its code.  Where the defect sits inside a region, the
 *         region is otherwise whole, and the file without the defect is
 *         read. */
static void
invalid_commands(void)
{
    static const struct {
        const char *text;
        tiku_circuit_err_t code;
    } commands[] = {
        {"%AMfoo*% %ADD11undefined*%", TIKU_CIRCUIT_BAD},
        {"%SRX2Y2I1J1*%", TIKU_CIRCUIT_BAD},
        {"%LR90*%", TIKU_CIRCUIT_UNSUPPORTED},
        {"%LMX*%", TIKU_CIRCUIT_UNSUPPORTED},
        {"%IPNEG*%", TIKU_CIRCUIT_UNSUPPORTED},
        {"G74*", TIKU_CIRCUIT_UNSUPPORTED},
        {"%ADD10C,1*%", TIKU_CIRCUIT_BAD},
        {"%ADD11R,1*%", TIKU_CIRCUIT_UNSUPPORTED},
        {"%ADD11C,1X2*%", TIKU_CIRCUIT_UNSUPPORTED},
        {"%ADD11P,2X2*%", TIKU_CIRCUIT_BAD},
        {"%ADD11C,0X0.5*%", TIKU_CIRCUIT_UNSUPPORTED},
        {"%ADD11C,1001*%", TIKU_CIRCUIT_UNSUPPORTED},
        {"%ADD11R,2X2*%D11*X0Y0D02*X10000Y0D01*", TIKU_CIRCUIT_UNSUPPORTED},
        {"%FSLAX24Y24*%", TIKU_CIRCUIT_BAD},
        {"%MOIN*%", TIKU_CIRCUIT_BAD},
        {"X1.0Y0D03*", TIKU_CIRCUIT_UNSUPPORTED},
        {"X1234567Y0D03*", TIKU_CIRCUIT_LIMIT},
        {"X0D03*", TIKU_CIRCUIT_BAD},
        {"X0Y0D01*", TIKU_CIRCUIT_BAD},
        {"D+10*", TIKU_CIRCUIT_UNSUPPORTED},
        {"D256*", TIKU_CIRCUIT_BAD},
        {"G36*X0Y0D02*X10000Y0D01*X0Y10000D01*G37*", TIKU_CIRCUIT_BAD},
        {"G36*X0Y0D02*X10000Y10000D01*X0Y10000D01*X10000Y0D01*X0Y0D01*G37*",
         TIKU_CIRCUIT_BAD},
        {"G37*", TIKU_CIRCUIT_BAD},
        {"X0Y0D02*G02*X10000Y10000I0J10000D01*", TIKU_CIRCUIT_BAD},
        {"X0Y0D02*G75*G02*X20000Y10000I0J10000D01*",
         TIKU_CIRCUIT_UNSUPPORTED},
        {"X0Y0D02*X10000Y0I10000D01*", TIKU_CIRCUIT_BAD},
        {"X0Y0D03*%TF.Late,value*%", TIKU_CIRCUIT_BAD},
    };
    /* A move that leaves a one-point contour, and a parameter, each in a
     * region that is otherwise closed. */
    static const struct {
        const char *with, *without;
        tiku_circuit_err_t code;
    } regions[] = {
        {"G36*X0Y0D02*X10000Y0D02*X20000Y0D01*X20000Y10000D01*"
         "X10000Y0D01*G37*",
         "G36*X10000Y0D02*X20000Y0D01*X20000Y10000D01*X10000Y0D01*G37*",
         TIKU_CIRCUIT_BAD},
        {"G36*X0Y0D02*X10000Y0D01*%LPC*%X0Y10000D01*X0Y0D01*G37*",
         "G36*X0Y0D02*X10000Y0D01*X0Y10000D01*X0Y0D01*G37*",
         TIKU_CIRCUIT_BAD},
    };
    char buffer[8192];

    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++) {
        snprintf(buffer, sizeof buffer, "%s%sM02*", HEADER, commands[i].text);
        bad(buffer, commands[i].code);
    }
    for (size_t i = 0; i < sizeof regions / sizeof *regions; i++) {
        snprintf(buffer, sizeof buffer, "%s%sM02*", HEADER,
                 regions[i].without);
        tiku_circuit_release(parse(buffer));
        snprintf(buffer, sizeof buffer, "%s%sM02*", HEADER, regions[i].with);
        bad(buffer, regions[i].code);
    }
    bad("%FSTAX24Y24*%%MOMM*%%ADD10C,1*%D10*X0Y0D03*M02*",
        TIKU_CIRCUIT_UNSUPPORTED);
    bad(HEADER "M02*G04 after end*", TIKU_CIRCUIT_BAD);
    bad("%FSLAX24Y24*%%MOMM*%%ADD10C,1*%D10*X0Y0D02*X10000Y0D01*M02*",
        TIKU_CIRCUIT_BAD);
    bad(HEADER "X100000Y-100000D02*G75*G02*X200003Y-200000I0J-100000D01*"
        "M02*", TIKU_CIRCUIT_UNSUPPORTED);
    memset(buffer, 'a', sizeof buffer - 1);
    buffer[sizeof buffer - 1] = 0;
    bad(buffer, TIKU_CIRCUIT_LIMIT);
}

/** @brief A zero-size aperture is defined and selected, as KiCad does
 *         before its regions; its flashes and draws make no object. */
static void
zero_size(void)
{
    tiku_circuit_t *d = parse(HEADER "%ADD11C,0.000000*%%ADD12R,0X2*%D11*"
                              "G36*X0Y0D02*X10000Y0D01*X0Y10000D01*X0Y0D01*"
                              "G37*X20000Y0D03*X30000Y0D01*D12*X0Y0D03*M02*");

    assert(d->count == 1 && strstr(d->shapes[0].info.name, "Region"));
    tiku_circuit_release(d);
}

/** @brief Eight attributes of the longest name and value fit one scope,
 *         and the object they are captured on keeps them all. */
static void
attribute_limits(void)
{
    char buffer[8192], name[64], value[192];
    size_t used;

    memset(name, 'n', sizeof name - 1);
    name[sizeof name - 1] = 0;
    memset(value, 'v', sizeof value - 1);
    value[sizeof value - 1] = 0;
    used = (size_t)snprintf(buffer, sizeof buffer,
                            "%%FSLAX24Y24*%%%%MOMM*%%");
    for (int i = 0; i < 8; i++) {
        name[1] = (char)('0' + i);
        used += (size_t)snprintf(buffer + used, sizeof buffer - used,
                                 "%%TA.%.62s,%s*%%", name + 1, value);
    }
    snprintf(buffer + used, sizeof buffer - used,
             "%%ADD10C,1*%%D10*X0Y0D03*M02*");
    tiku_circuit_t *d = parse(buffer);

    assert(strstr(tiku_circuit_details(d, 0), ".0nnn")
           && strstr(tiku_circuit_details(d, 0), ".7nnn"));
    tiku_circuit_release(d);
}

/** @brief Every group. */
int
main(void)
{
    char *source = read_fixture("tests/fixtures/circuit/artwork.gbr");
    tiku_circuit_t *d = parse(source);

    assert(d->count == 4 && !d->font);
    close_to(d->shapes[0].cx, 10);
    close_to(d->shapes[0].cy, 10);
    close_to(d->shapes[0].r, 1);
    close_to(d->shapes[1].info.bounds.x0, 18.5);
    close_to(d->shapes[1].info.bounds.y0, 9);
    close_to(d->shapes[2].info.bounds.x0, 9);
    close_to(d->shapes[2].info.bounds.y0, 18);
    assert(d->shapes[2].kind == CIRCUIT_OBROUND);
    close_to(d->shapes[3].points[0].x, 20.707107);
    close_to(d->shapes[3].points[0].y, 22.121320);
    assert(strstr(tiku_circuit_details(d, 0), "Copper,L1,Top")
           && strstr(tiku_circuit_details(d, 2), ".P: ,3"));
    tiku_image_t im = render(d);

    assert(ink(im, 100, 100) && ink(im, 200, 100) && ink(im, 100, 182)
           && !ink(im, 90, 180));
    assert(ink(im, 200, 200) && !ink(im, 178, 178));
    tiku_image_free(&im);
    assert(tiku_circuit_hit(d, 10, 18.1, 0) == 2
           && tiku_circuit_hit(d, 9, 18, 0) == SIZE_MAX);
    tiku_circuit_release(d);
    for (size_t n = 1; n < strlen(source) - 1; n++) {
        d = (void *)source;
        assert(tiku_circuit_gerber(source, n, NULL, &d, NULL)
               && d == (void *)source);
    }
    for (int n = 1; n < 12; n++) {
        int left = n;
        tiku_circuit_import_t opt = {.cancel = cancel, .ctx = &left};

        d = NULL;
        assert(tiku_circuit_gerber(source, strlen(source), &opt, &d, NULL)
               == TIKU_CIRCUIT_CANCELLED && !d);
    }
    free(source);
    source = read_fixture("tests/fixtures/circuit/polarity.gbr");
    d = parse(source);
    free(source);
    im = render(d);
    assert(!ink(im, 100, 100) && ink(im, 115, 100)); /* clear square */
    assert(!ink(im, 250, 100) && ink(im, 280, 100)); /* transparent hole */
    assert(!ink(im, 100, 220) && ink(im, 125, 220) && !ink(im, 140, 200));
    assert(!ink(im, 250, 220) && ink(im, 275, 220));
    assert(tiku_circuit_hit(d, 10, 10, 0) == SIZE_MAX);
    /* The annulus's hole leaves the earlier disk selectable. */
    assert(tiku_circuit_hit(d, 11.5, 10, 0) == 0);
    assert(tiku_circuit_hit(d, 25, 10, 0) == SIZE_MAX);
    tiku_image_free(&im);
    tiku_circuit_release(d);
    d = parse(HEADER "X100000Y-100000D03*%ADD11C,8X4*%D11*D03*M02*");
    im = render(d);
    assert(ink(im, 100, 100));  /* a hole does not erase the earlier disk */
    tiku_image_free(&im);
    tiku_circuit_release(d);
    d = parse(HEADER "X0Y0D02*G75*G03*X100000Y100000I0J100000D01*G02*"
              "X0Y0I-100000J0D01*M02*");
    assert(d->count == 2);
    close_to(d->shapes[0].cx, 0);
    close_to(d->shapes[0].cy, -10);
    close_to(d->shapes[0].sweep, -M_PI / 2);
    close_to(d->shapes[1].sweep, M_PI / 2);
    tiku_circuit_release(d);
    d = parse(HEADER "X100000Y-100000D02*G75*G02*X200001Y-200000I0J-100000D01*"
              "M02*");
    close_to(d->shapes[0].r, 10);
    close_to(d->shapes[0].end_r, 10.0001);
    close_to(d->shapes[0].sweep, M_PI / 2);
    assert(tiku_circuit_hit(d, 20.0001, 20, 0) == 0);
    im = render(d);
    assert(ink(im, 200, 200) && ink(im, 100, 100) && !ink(im, 100, 200));
    tiku_image_free(&im);
    tiku_circuit_release(d);
    source = read_fixture("tests/fixtures/circuit/curved-region.gbr");
    d = parse(source);
    free(source);
    assert(d->count == 4 && d->shapes[3].count > 100);
    im = render(d);
    assert(ink(im, 200, 200) && !ink(im, 178, 178));
    tiku_image_free(&im);
    tiku_circuit_release(d);
    d = parse(HEADER "G36*X0Y0D02*X10000Y0D01*X0Y10000D01*X0Y0D01*"
              "X20000Y0D02*X30000Y0D01*X20000Y10000D01*X20000Y0D01*G37*M02*");
    assert(d->count == 2);
    tiku_circuit_release(d);
    /* Nested contours fill independently; inner contours are not holes. */
    d = parse(HEADER "G36*X0Y0D02*X100000Y0D01*X100000Y-100000D01*"
              "X0Y-100000D01*X0Y0D01*X30000Y-30000D02*X70000Y-30000D01*"
              "X70000Y-70000D01*X30000Y-70000D01*X30000Y-30000D01*G37*M02*");
    im = render(d);
    assert(ink(im, 50, 50));
    tiku_image_free(&im);
    tiku_circuit_release(d);
    d = parse(HEADER "X100000Y0D02*G75*G02*I-100000J0D01*M02*");
    close_to(d->shapes[0].sweep, 2 * M_PI);
    tiku_circuit_release(d);
    d = parse("%FSLAX24Y24*%%MOIN*%%ADD10C,.1*%D10*X10000Y-20000D03*"
              "Y-30000*M02*");
    close_to(d->shapes[0].cx, 25.4);
    close_to(d->shapes[0].cy, 50.8);
    close_to(d->shapes[0].r, 1.27);
    close_to(d->shapes[1].cy, 76.2);
    tiku_circuit_release(d);
    d = parse(HEADER "%TF.FilePolarity,Negative*%X100000Y-100000D03*M02*");
    im = render(d);
    assert(ink(im, 100, 100) && !ink(im, 20, 20));  /* metadata, not LP */
    tiku_image_free(&im);
    tiku_circuit_release(d);
    invalid_commands();
    zero_size();
    attribute_limits();
    const char nul[] = HEADER "X0Y0D03*\0M02*";

    d = NULL;
    assert(tiku_circuit_gerber(nul, sizeof nul - 1, NULL, &d, NULL)
           == TIKU_CIRCUIT_BAD && !d);
    assert(tiku_circuit_gerber("x", 8u * 1024u * 1024u + 1, NULL, &d, NULL)
           == TIKU_CIRCUIT_LIMIT && !d);
    size_t cap = 50000, used = strlen(HEADER);
    char *many = malloc(cap);

    assert(many);
    memcpy(many, HEADER, used);
    for (int i = 0; i < 4097; i++) {
        memcpy(many + used, "X0Y0D03*", 8);
        used += 8;
    }
    memcpy(many + used, "M02*", 5);
    assert(tiku_circuit_gerber(many, used + 4, NULL, &d, NULL)
           == TIKU_CIRCUIT_LIMIT && !d);
    free(many);
    /* The largest admitted contour, then one extra edge. */
    many = malloc(cap);
    assert(many);
    for (int extra = 0; extra < 2; extra++) {
        used = (size_t)snprintf(many, cap, "%sG36*X0Y0D02*%s", HEADER,
                                extra ? "X0Y-1D01*" : "");
        for (int edge = 1; edge <= 1024; edge++) {
            int x = edge <= 256 ? edge : edge <= 512 ? 256
                  : edge <= 768 ? 768 - edge : 0;
            int y = edge <= 256 ? 0 : edge <= 512 ? edge - 256
                  : edge <= 768 ? 256 : 1024 - edge;

            used += (size_t)snprintf(many + used, cap - used, "X%dY%dD01*",
                                     x, y);
        }
        used += (size_t)snprintf(many + used, cap - used, "G37*M02*");
        d = NULL;
        int result = tiku_circuit_gerber(many, used, NULL, &d, NULL);

        if (extra) {
            assert(result == TIKU_CIRCUIT_LIMIT && !d);
        } else {
            assert(!result && d->count == 1);
            tiku_circuit_release(d);
        }
    }
    free(many);
    d = parse(HEADER "X100000Y-100000D03*M02*");
    tiku_circuit_view_t view = {.width = 400, .height = 320, .scale = 10};

    for (int n = 1; n <= 3; n++) {
        int left = n;

        im = (tiku_image_t){0};
        assert(tiku_circuit_render(d, &view, cancel, &left, &im)
               == TIKU_CIRCUIT_CANCELLED && !im.px);
    }
    tiku_circuit_release(d);
    puts("PASS: Gerber coordinate oracle, flashes, holes, clear coverage, "
         "arcs, modal units, strict states and limits");
    return 0;
}
