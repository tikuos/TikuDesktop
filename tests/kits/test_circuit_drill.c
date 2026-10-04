/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_drill.c - drill files against an authored board's KiCad
 * export: holes, slots and routes, units, analytic arcs and attribute
 * scopes; each invalid state in an otherwise valid file refused with
 * its code; truncation, cancellation and limits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAPER 0xfffffff7            /* the renderer's empty pixel        */

/** @brief @p s decoded; it must be accepted. */
static tiku_circuit_t *
parse(const char *s)
{
    tiku_circuit_t *d = NULL;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_err_t e = tiku_circuit_drill(s, strlen(s), NULL, &d, &diag);

    if (e) {
        fprintf(stderr, "line %u: %s\n", diag.line, diag.message);
    }
    assert(!e && d);
    return d;
}

/** @brief The whole of the file at @p path, NUL-terminated. */
static char *
file(const char *path)
{
    FILE *f = fopen(path, "rb");

    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long n = ftell(f);

    assert(n >= 0);
    rewind(f);
    char *s = malloc((size_t)n + 1);

    assert(s);
    assert(fread(s, 1, (size_t)n, f) == (size_t)n && !fclose(f));
    s[n] = 0;
    return s;
}

/** @brief @p s refused with @p code, a line and a message, the output
 *         left alone. */
static void
bad(const char *s, tiku_circuit_err_t code)
{
    tiku_circuit_t *d = (void *)s;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_err_t e = tiku_circuit_drill(s, strlen(s), NULL, &d, &diag);

    if (e != code) {
        fprintf(stderr, "drill %d, not %d: %s\n", e, code, diag.message);
    }
    assert(e == code && d == (void *)s && diag.code == code && diag.line
           && diag.message[0]);
}

/** @brief Whether @p a and @p b agree to 1e-8. */
static void
close_to(double a, double b)
{
    assert(fabs(a - b) < 1e-8);
}

/** @brief A cancellation callback that fires on its @p ctx'th call. */
static int
cancel(void *ctx)
{
    return --*(int *)ctx <= 0;
}

/** @brief Each invalid state, in a body or header that is otherwise
 *         valid, refused with its code. */
static void
invalid_states(void)
{
    static const struct {
        const char *text;
        tiku_circuit_err_t code;
    } bodies[] = {
        {"X1Y2", TIKU_CIRCUIT_UNSUPPORTED},
        {"X1.0", TIKU_CIRCUIT_BAD},
        {"T2", TIKU_CIRCUIT_BAD},
        {"G91", TIKU_CIRCUIT_UNSUPPORTED},
        {"G00X0Y0\nG01X1.0Y1.0", TIKU_CIRCUIT_BAD},
        {"G00X0Y0\nM15\nG00X1.0Y1.0", TIKU_CIRCUIT_BAD},
        {"G00X0Y0\nM15", TIKU_CIRCUIT_BAD},
        {"G00X0Y0\nM15\nG02X10.0Y10.0A1.0\nM16", TIKU_CIRCUIT_BAD},
        {"G00X0Y0\nM15\nG02X0Y0A1.0\nM16", TIKU_CIRCUIT_BAD},
        {"G00X0Y0\nM15\nG02X1.0Y0I.5J0\nM16", TIKU_CIRCUIT_UNSUPPORTED},
        {"M16", TIKU_CIRCUIT_BAD},
        {"X0Y0\n; #@! TF.Late,value", TIKU_CIRCUIT_BAD},
        {"X1e2Y0", TIKU_CIRCUIT_UNSUPPORTED},
        {"X0Y0\nX1.0e2Y0", TIKU_CIRCUIT_UNSUPPORTED},
        {"X1.2.3Y0", TIKU_CIRCUIT_BAD},
        {"X10001.0Y0", TIKU_CIRCUIT_LIMIT},
        {"X0Y0X0", TIKU_CIRCUIT_UNSUPPORTED},
        {"G00X0Y0\nX1.0Y0", TIKU_CIRCUIT_BAD},
        {"G00X0Y0\nM15\nT1", TIKU_CIRCUIT_BAD},
        {"G00X0Y0\nM15\nG05", TIKU_CIRCUIT_BAD},
        {"X0Y0G85X1.0Y0R1", TIKU_CIRCUIT_UNSUPPORTED},
        /* A tab where a comment is otherwise valid. */
        {";\tcomment", TIKU_CIRCUIT_BAD},
    };
    static const struct {
        const char *text;
        tiku_circuit_err_t code;
    } headers[] = {
        {"T1C1\nMETRIC", TIKU_CIRCUIT_BAD},
        {"METRIC\nT1C0", TIKU_CIRCUIT_BAD},
        {"METRIC\nT1C1\nT1C2", TIKU_CIRCUIT_BAD},
        {"METRIC\nINCH", TIKU_CIRCUIT_BAD},
        {"METRIC,LZ", TIKU_CIRCUIT_UNSUPPORTED},
        {"; FILE_FORMAT=2:4\nMETRIC", TIKU_CIRCUIT_UNSUPPORTED},
        {"; FORMAT={-:-/ absolute / metric / decimal}\nINCH",
         TIKU_CIRCUIT_BAD},
        {"METRIC\nT256C1", TIKU_CIRCUIT_BAD},
        {"METRIC\n; #@! TX.Bad,value", TIKU_CIRCUIT_UNSUPPORTED},
        {"METRIC\n; #@! TA,value", TIKU_CIRCUIT_BAD},
    };
    char buffer[8192];

    tiku_circuit_release(parse("M48\nMETRIC\nT1C1\n%\nT1\n; comment\nM30"));
    for (size_t i = 0; i < sizeof bodies / sizeof *bodies; i++) {
        snprintf(buffer, sizeof buffer, "M48\nMETRIC\nT1C1\n%%\nT1\n%s\nM30",
                 bodies[i].text);
        bad(buffer, bodies[i].code);
    }
    for (size_t i = 0; i < sizeof headers / sizeof *headers; i++) {
        snprintf(buffer, sizeof buffer, "M48\n%s\n%%\nM30", headers[i].text);
        bad(buffer, headers[i].code);
    }
    bad("M48\n%\nM30", TIKU_CIRCUIT_BAD);
    bad("M48\nMETRIC\n%\nM30\nX0Y0", TIKU_CIRCUIT_BAD);
    memset(buffer, ';', sizeof buffer);
    buffer[sizeof buffer - 1] = 0;
    bad(buffer, TIKU_CIRCUIT_LIMIT);
}

/** @brief Attribute scopes: eight of the longest attributes fit one
 *         scope, a ninth does not, and details too long when the scopes
 *         are combined are refused rather than cut. */
static void
attribute_limits(void)
{
    static const char scopes[] = {'F', 'A', 'O'};
    char buffer[8192], name[64], value[192];
    size_t used;
    tiku_circuit_t *d;

    memset(name, 'n', sizeof name - 1);
    name[sizeof name - 1] = 0;
    memset(value, 'v', sizeof value - 1);
    value[sizeof value - 1] = 0;
    used = (size_t)snprintf(buffer, sizeof buffer, "M48\nMETRIC\n");
    for (int i = 0; i < 8; i++) {
        name[1] = (char)('0' + i);
        used += (size_t)snprintf(buffer + used, sizeof buffer - used,
                                 "; #@! TA.%.62s,%s\n", name + 1, value);
    }
    snprintf(buffer + used, sizeof buffer - used,
             "T1C1\n%%\nT1\nX0Y0\nM30");
    d = parse(buffer);
    assert(strstr(tiku_circuit_details(d, 0), ".7nnn"));
    tiku_circuit_release(d);
    used = (size_t)snprintf(buffer, sizeof buffer, "M48\nMETRIC\n");
    for (int i = 0; i < 9; i++) {
        used += (size_t)snprintf(buffer + used, sizeof buffer - used,
                                 "; #@! TA.Key%d,value\n", i);
    }
    snprintf(buffer + used, sizeof buffer - used, "T1C1\n%%\nM30");
    bad(buffer, TIKU_CIRCUIT_LIMIT);
    used = (size_t)snprintf(buffer, sizeof buffer, "M48\nMETRIC\n");
    for (size_t scope = 0; scope < 3; scope++) {
        for (int i = 0; i < 8; i++) {
            used += (size_t)snprintf(buffer + used, sizeof buffer - used,
                                     "; #@! T%c.Key%d,%s\n", scopes[scope],
                                     i, value);
        }
    }
    snprintf(buffer + used, sizeof buffer - used, "T1C1\n%%\nT1\nX0Y0\nM30");
    d = NULL;
    assert(tiku_circuit_drill(buffer, strlen(buffer), NULL, &d, NULL)
           == TIKU_CIRCUIT_LIMIT && !d);
}

/** @brief Every group. */
int
main(void)
{
    char *source = file("tests/fixtures/circuit/drills.drl");
    tiku_circuit_t *d = parse(source);

    assert(d->count == 4 && !d->font);
    close_to(d->shapes[0].cx, 10);
    close_to(d->shapes[0].cy, 10);
    close_to(d->shapes[0].r, .5);
    close_to(d->shapes[1].cx, 20);
    close_to(d->shapes[1].r, .75);
    close_to(d->shapes[2].points[0].x, 10);
    close_to(d->shapes[2].points[0].y, 19);
    close_to(d->shapes[2].points[1].y, 21);
    close_to(d->shapes[2].width, 1);
    close_to(d->shapes[3].points[0].x, 19.293);
    close_to(d->shapes[3].points[1].y, 20.707);
    assert(strstr(tiku_circuit_details(d, 0), "Plated,PTH,ComponentDrill"));
    assert(strstr(tiku_circuit_details(d, 0), "MixedPlating,1,2"));
    char *routed = file("tests/fixtures/circuit/drills-routed.drl");
    tiku_circuit_t *routes = parse(routed);

    free(routed);
    assert(routes->count == d->count);
    for (size_t i = 0; i < d->count; i++) {
        assert(d->shapes[i].kind == routes->shapes[i].kind);
        close_to(d->shapes[i].width, routes->shapes[i].width);
        for (size_t j = 0; j < d->shapes[i].count; j++) {
            close_to(d->shapes[i].points[j].x, routes->shapes[i].points[j].x);
            close_to(d->shapes[i].points[j].y, routes->shapes[i].points[j].y);
        }
    }
    tiku_circuit_release(routes);
    tiku_circuit_view_t v = {.width = 256, .height = 256, .scale = 10,
                             .x = 0, .y = 0};
    tiku_image_t im = {0};

    assert(!tiku_circuit_render(d, &v, NULL, NULL, &im));
    assert(im.px[100 * 256 + 100] != PAPER && im.px[200 * 256 + 100] != PAPER);
    assert(im.px[200 * 256 + 120] == PAPER);
    tiku_image_free(&im);
    tiku_circuit_release(d);
    for (size_t n = 1; n < strlen(source) - 2; n++) {
        tiku_circuit_t *out = (void *)source;

        assert(tiku_circuit_drill(source, n, NULL, &out, NULL)
               && out == (void *)source);
    }
    for (int n = 1; n <= 10; n++) {
        int left = n;
        tiku_circuit_import_t opt = {.cancel = cancel, .ctx = &left};

        d = NULL;
        assert(tiku_circuit_drill(source, strlen(source), &opt, &d, NULL)
               == TIKU_CIRCUIT_CANCELLED && !d);
    }
    free(source);
    source = file("tests/fixtures/circuit/routes.xnc");
    d = parse(source);
    assert(d->count == 4);
    close_to(d->shapes[0].cx, 0);
    close_to(d->shapes[0].cy, -10);
    close_to(d->shapes[0].start, M_PI / 2);
    close_to(d->shapes[0].sweep, -M_PI / 2);
    close_to(d->shapes[1].cx, 10);
    close_to(d->shapes[1].cy, 0);
    close_to(d->shapes[1].sweep, M_PI / 2);
    tiku_circuit_release(d);
    free(source);
    d = parse("M48\r\nINCH\r\nT1C.1\r\n%\r\nT1\r\nX1.0Y-2.0\r\nY-3.0\r\n"
              "M30\r\n");
    close_to(d->shapes[0].cx, 25.4);
    close_to(d->shapes[0].cy, 50.8);
    close_to(d->shapes[0].r, 1.27);
    close_to(d->shapes[1].cx, 25.4);
    close_to(d->shapes[1].cy, 76.2);
    tiku_circuit_release(d);
    d = parse("M48\n; FORMAT={-:-/ absolute / metric / decimal}\nMETRIC\n"
              "T1C1\n%\nT1\nX10Y20\nM30");
    close_to(d->shapes[0].cx, 10);
    tiku_circuit_release(d);
    d = parse("M48\nMETRIC\n; #@! TA.Kind,first\nT1C1\n"
              "; #@! TA.Kind,second\nT2C2\n%\n; #@! TO.Name,A\nT1\nX0Y0\n"
              "; #@! TD\nT2\nX1.0Y0\nM30");
    assert(strstr(tiku_circuit_details(d, 0), "first")
           && strstr(tiku_circuit_details(d, 0), "Name: A"));
    assert(strstr(tiku_circuit_details(d, 1), "second")
           && !strstr(tiku_circuit_details(d, 1), "Name: A"));
    tiku_circuit_release(d);
    d = parse("M48\nMETRIC\nT1C1\n%\nT1\nG00X0Y0\nM15\nM16\nM30");
    assert(d->count == 1 && d->shapes[0].fill);
    tiku_circuit_release(d);
    d = parse("M48\nINCH\n; FORMAT={-:-/ absolute / inch / decimal}\n"
              "T255C.01\nM95\nT255\nX1Y1\nM30");
    close_to(d->shapes[0].cx, 25.4);
    tiku_circuit_release(d);
    invalid_states();
    attribute_limits();
    const char embedded_nul[] = "M48\nMETRIC\n%\n\0M30";

    d = NULL;
    assert(tiku_circuit_drill(embedded_nul, sizeof embedded_nul - 1, NULL,
                              &d, NULL) == TIKU_CIRCUIT_BAD && !d);
    d = NULL;
    assert(tiku_circuit_drill("x", 8u * 1024u * 1024u + 1, NULL, &d, NULL)
           == TIKU_CIRCUIT_LIMIT && !d);
    size_t cap = 40000;
    char *many = malloc(cap);

    assert(many);
    size_t at = (size_t)snprintf(many, cap, "M48\nMETRIC\nT1C1\n%%\nT1\n");

    for (int i = 0; i < 4097; i++) {
        memcpy(many + at, "X0Y0\n", 5);
        at += 5;
    }
    memcpy(many + at, "M30", 4);
    d = NULL;
    assert(tiku_circuit_drill(many, at + 3, NULL, &d, NULL)
           == TIKU_CIRCUIT_LIMIT && !d);
    free(many);
    puts("PASS: drill oracle, units, arcs, slots, metadata, truncation, "
         "cancellation and limits");
    return 0;
}
