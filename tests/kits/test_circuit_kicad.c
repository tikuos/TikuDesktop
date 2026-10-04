/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_kicad.c - the KiCad schematic reader against authored
 * fixtures whose coordinates KiCad's own SVG export gives: transforms,
 * labels, pins, text direction and placement; and against hostile and
 * oversized input, refused with a precise diagnostic and nothing kept.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"
#include "tiku_circuit_sexpr.h"
#include "tiku_circuit_kicad_private.h"
#include "tiku_fontset.h"

#include <assert.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROOT "(kicad_sch (version 20230121) "
#define FIXTURE_MAX (8 * 1024 * 1024)   /* bytes of the largest fixture  */

static tiku_circuit_import_t options;
static unsigned char *font_bytes;

/** @brief A test-owned copy of a file; the reader is only given memory. */
static unsigned char *
read_bytes(const char *path, size_t *bytes)
{
    FILE *f = fopen(path, "rb");

    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long n = ftell(f);

    assert(n >= 0 && n < FIXTURE_MAX);
    rewind(f);
    unsigned char *data = malloc((size_t)n + 1);

    assert(data);
    assert(fread(data, 1, (size_t)n, f) == (size_t)n);
    assert(!fclose(f));
    data[n] = 0;
    *bytes = (size_t)n;
    return data;
}

/** @brief Import @p text, which must fail with @p expected and leave the
 *         output pointer as it was.  @return the diagnostic */
static tiku_circuit_diagnostic_t
rejects(const char *text, tiku_circuit_err_t expected)
{
    tiku_circuit_t *out = (void *)&options;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_err_t e = tiku_circuit_kicad(text, strlen(text), &options,
                                              &out, &diag);

    if (e != expected) {
        fprintf(stderr, "expected %d got %d: %s\n%s\n", expected, e,
                diag.message, text);
    }
    assert(e == expected && diag.code == e && diag.line && diag.column &&
           *diag.message);
    assert(out == (void *)&options);
    return diag;
}

/** @brief Import @p text, which must succeed.  @return its scene */
static tiku_circuit_t *
accepts(const char *text)
{
    tiku_circuit_t *d = NULL;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_err_t e = tiku_circuit_kicad(text, strlen(text), &options,
                                              &d, &diag);

    if (e) {
        fprintf(stderr, "%u:%u %s\n%s\n", diag.line, diag.column,
                diag.message, text);
    }
    assert(!e && d);
    return d;
}

/** @brief Decode fixture @p name.  @return its scene */
static tiku_circuit_t *
fixture(const char *name)
{
    char path[256];

    snprintf(path, sizeof path, "tests/fixtures/circuit/%s.kicad_sch",
             name);
    size_t bytes;
    unsigned char *data = read_bytes(path, &bytes);
    tiku_circuit_t *d = NULL;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_err_t e = tiku_circuit_kicad(data, bytes, &options, &d,
                                              &diag);

    if (e) {
        fprintf(stderr, "%s: %u:%u %s\n", path, diag.line, diag.column,
                diag.message);
    }
    assert(!e && d && !diag.code);
    free(data);
    return d;
}

/** @brief The extent of shape @p s's points. */
static tiku_circuit_box_t
extent(const circuit_shape_t *s)
{
    tiku_circuit_box_t b = {INFINITY, INFINITY, -INFINITY, -INFINITY};

    for (size_t i = 0; i < s->count; i++) {
        b.x0 = fmin(b.x0, s->points[i].x);
        b.y0 = fmin(b.y0, s->points[i].y);
        b.x1 = fmax(b.x1, s->points[i].x);
        b.y1 = fmax(b.y1, s->points[i].y);
    }
    return b;
}

/** @brief The @p nth text shape of @p d drawing @p text, from 0. */
static const circuit_shape_t *
nth_text(const tiku_circuit_t *d, const char *text, int nth)
{
    for (size_t i = 0; i < d->count; i++) {
        if (d->shapes[i].kind == CIRCUIT_TEXT &&
            !strcmp(d->shapes[i].text, text) && !nth--) {
            return &d->shapes[i];
        }
    }
    assert(!"no such text");
    return NULL;
}

/** @brief The first text shape of @p d drawing @p text. */
static const circuit_shape_t *
text_shape(const tiku_circuit_t *d, const char *text)
{
    return nth_text(d, text, 0);
}

/** @brief Whether box @p b's centre is within @p slack mm of (@p x, @p y).
 */
static int
centred(tiku_circuit_box_t b, double x, double y, double slack)
{
    return fabs((b.x0 + b.x1) / 2 - x) < slack &&
           fabs((b.y0 + b.y1) / 2 - y) < slack;
}

/** @brief Whether text shape @p s reads left to right, upright. */
static int
horizontal(const circuit_shape_t *s)
{
    return s->matrix[0] > 0 && fabs(s->matrix[1]) < 1e-9 &&
           fabs(s->matrix[2]) < 1e-9 && s->matrix[3] > 0;
}

/** @brief Whether text shape @p s reads bottom to top, upright. */
static int
vertical(const circuit_shape_t *s)
{
    return fabs(s->matrix[0]) < 1e-9 && s->matrix[1] < 0 &&
           s->matrix[2] > 0 && fabs(s->matrix[3]) < 1e-9;
}

/** @brief Coordinates read from KiCad's SVG export of the transforms
 *         fixture, not from the importer's own transform. */
static void
transforms(void)
{
    tiku_circuit_t *d = fixture("transforms");
    const double expected[][6] = {{41, 38, 45, 38, 41, 34},
                                  {58, 39, 58, 35, 54, 39},
                                  {78, 41, 78, 45, 74, 41},
                                  {102, 39, 102, 35, 106, 39}};

    assert(d->count == 4);
    for (size_t i = 0; i < 4; i++) {
        assert(d->shapes[i].count == 3);
        for (size_t p = 0; p < 3; p++) {
            assert(fabs(d->shapes[i].points[p].x - expected[i][p * 2]) <
                   1e-9);
            assert(fabs(d->shapes[i].points[p].y - expected[i][p * 2 + 1]) <
                   1e-9);
        }
    }
    assert(tiku_circuit_find(d, "u2", 0) == 1);
    assert(tiku_circuit_find(d, "MIRROR", 0) == 2);
    assert(tiku_circuit_find(d, "mirror", 3) == 3);
    assert(tiku_circuit_find(d, "mirror", 4) == SIZE_MAX);
    assert(tiku_circuit_find(d, "", 0) == SIZE_MAX);
    tiku_circuit_release(d);
}

/** @brief Text is in the scene and draws pixels; KiCad's export puts the
 *         basic fixture's pin numbers right of their vertical pins. */
static void
text_and_pins(void)
{
    tiku_circuit_t *d = fixture("basic");

    assert(strstr(tiku_circuit_warning(d), "substitute font"));
    assert(tiku_circuit_find(d, "VCC", 0) != SIZE_MAX);
    assert(tiku_circuit_find(d, "10k", 0) != SIZE_MAX);
    assert(tiku_circuit_find(d, "r1 pin 1", 0) != SIZE_MAX);
    size_t texts = 0;

    for (size_t i = 0; i < d->count; i++) {
        if (d->shapes[i].kind == CIRCUIT_TEXT) {
            texts++;
        }
    }
    assert(texts == 6);         /* label, note, two pin numbers, R1, 10k */
    /* With (pin_names (offset 0)) KiCad draws a number on the far side
     * from the name: right of a vertical pin, the pins here on x = 40. */
    for (int pin = 1; pin <= 2; pin++) {
        const circuit_shape_t *s = text_shape(d, pin == 1 ? "1" : "2");
        tiku_circuit_box_t b = extent(s);

        assert(vertical(s) && b.x0 >= 40 && b.x1 < 42);
        assert(b.y0 < (pin == 1 ? 36 : 44) && b.y1 > (pin == 1 ? 36 : 44));
    }
    tiku_circuit_view_t view;

    assert(tiku_circuit_fit(d, 800, 600, &view));
    tiku_image_t im = {0};

    assert(!tiku_circuit_render(d, &view, NULL, NULL, &im));
    size_t ink = 0;

    for (int i = 0; i < im.w * im.h; i++) {
        ink += im.px[i] != CIRCUIT_PAPER;
    }
    assert(ink > 1000 && ink < 50000);
    tiku_image_free(&im);
    size_t index = tiku_circuit_find(d, "Read-only", 0);

    assert(index != SIZE_MAX);
    tiku_circuit_box_t b = d->shapes[index].info.bounds;

    view = (tiku_circuit_view_t){700, 120, 20, 10 - b.x0 * 20,
                                 10 - b.y0 * 20};
    assert(!tiku_circuit_render(d, &view, NULL, NULL, &im));
    ink = 0;
    for (int i = 0; i < im.w * im.h; i++) {
        ink += im.px[i] != CIRCUIT_PAPER;
    }
    assert(ink > 500);
    tiku_image_free(&im);
    tiku_circuit_release(d);

    size_t bytes;
    unsigned char *data = read_bytes("tests/fixtures/circuit/basic.kicad_sch",
                                     &bytes);

    d = NULL;
    assert(tiku_circuit_kicad(data, bytes, NULL, &d, NULL) ==
           TIKU_CIRCUIT_MISSING && !d);
    free(data);
}

/** @brief Arc direction, filled-circle hit testing and degenerate
 *         geometry. */
static void
curves(void)
{
    const char *text = ROOT
        "(arc (start 0 0) (mid 2 -2) (end 4 0) "
        "(stroke (width 0.2) (type solid)))"
        "(circle (center 10 10) (radius 2) (stroke (width 0.2)) "
        "(fill (type outline))))";
    tiku_circuit_t *d = NULL;

    assert(!tiku_circuit_kicad(text, strlen(text), NULL, &d, NULL));
    assert(d->count == 3 && d->shapes[0].kind == CIRCUIT_ARC);
    assert(tiku_circuit_hit(d, 2, -2, 0) == 0);
    assert(tiku_circuit_hit(d, 2, 2, .01) == SIZE_MAX);
    assert(tiku_circuit_hit(d, 10, 10, 0) == 1);
    tiku_circuit_release(d);
    rejects(ROOT "(arc (start 0 0) (mid 1 1) (end 2 2)))", TIKU_CIRCUIT_BAD);
    rejects(ROOT "(circle (center 0 0) (radius -1)))", TIKU_CIRCUIT_BAD);
    rejects(ROOT "(wire (pts (xy 0 0) (xy 10001 2))))", TIKU_CIRCUIT_BAD);
    rejects(ROOT "(wire (pts (xy 0 0) (xy 1 2)) (stroke (type dash))))",
            TIKU_CIRCUIT_UNSUPPORTED);
}

/** @brief Only the requested unit and style, and the parts every unit
 *         shares, are drawn. */
static void
units(void)
{
    const char *prefix = ROOT "(lib_symbols (symbol \"Test:Multi\""
        "(symbol \"Multi_0_0\" (circle (center 0 0) (radius 1)))"
        "(symbol \"Multi_1_1\" (rectangle (start 1 2) (end 3 4)))"
        "(symbol \"Multi_2_1\" (rectangle (start 5 6) (end 7 8)))"
        "(symbol \"Multi_1_2\" (rectangle (start 9 10) (end 11 12)))))"
        "(symbol (lib_id \"Test:Multi\") (at 50 50 0) ";
    const char *suffix = "(property \"Reference\" \"U1\" (effects hide))"
        "(property \"Value\" \"Multi\" (effects hide))))";
    char text[4096];

    for (int test = 0; test < 3; test++) {
        int unit = test == 1 ? 2 : 1, style = test == 2 ? 2 : 1;

        snprintf(text, sizeof text, "%s(unit %d) (convert %d) %s", prefix,
                 unit, style, suffix);
        tiku_circuit_t *d = NULL;

        assert(!tiku_circuit_kicad(text, strlen(text), NULL, &d, NULL));
        assert(d->count == 2 && d->shapes[0].kind == CIRCUIT_ARC);
        assert(fabs(d->shapes[1].points[0].x - (51 + test * 4)) < 1e-9);
        tiku_circuit_release(d);
    }
    snprintf(text, sizeof text, "%s(unit 3) %s", prefix, suffix);
    rejects(text, TIKU_CIRCUIT_MISSING);
    snprintf(text, sizeof text, "%s(unit 1) (dnp yes) %s", prefix, suffix);
    tiku_circuit_t *d = NULL;

    assert(!tiku_circuit_kicad(text, strlen(text), NULL, &d, NULL));
    assert(d->count == 4 &&
           strstr(tiku_circuit_details(d, 0), "Do not populate: Yes"));
    tiku_circuit_release(d);
}

/** @brief Tag directions and the do-not-populate cross's extent, as
 *         KiCad's SVG export of the labels fixture gives them. */
static void
global_labels(void)
{
    tiku_circuit_t *d = fixture("labels");
    const char *names[] = {"INPUT", "OUTPUT", "BIDIR", "TRI", "PASSIVE"};
    const double anchors[][4] = {{30, 30, 1, 0}, {65, 30, -1, 0},
                                 {95, 30, 0, -1}, {110, 30, 0, 1},
                                 {30, 40, 1, 0}};

    for (size_t i = 0; i < 5; i++) {
        size_t at = tiku_circuit_find(d, names[i], 0);

        assert(at != SIZE_MAX);
        circuit_shape_t *s = &d->shapes[at];

        assert(s->kind == CIRCUIT_POLYLINE && s->count == 6);
        assert(fabs(s->points[0].x - anchors[i][0]) < 1e-9);
        assert(fabs(s->points[0].y - anchors[i][1]) < 1e-9);
        double endx = s->points[3].x - anchors[i][0];
        double endy = s->points[3].y - anchors[i][1];

        assert(endx * anchors[i][2] + endy * anchors[i][3] > 2);
        assert(fabs(endx * anchors[i][3] - endy * anchors[i][2]) < 1e-9);
        /* Input and both-ended tags have their point at the anchor. */
        double near = (s->points[1].x - anchors[i][0]) * anchors[i][2] +
                      (s->points[1].y - anchors[i][1]) * anchors[i][3];

        assert(fabs(near - (i == 0 || i == 2 || i == 3 ? .875 * 1.27 : 0)) <
               1e-9);
        const circuit_shape_t *text = &d->shapes[at + 1];

        assert(text->kind == CIRCUIT_TEXT);
        for (size_t p = 0; p < text->count; p++) {
            assert(text->points[p].x >= s->info.bounds.x0 &&
                   text->points[p].x <= s->info.bounds.x1);
            assert(text->points[p].y >= s->info.bounds.y0 &&
                   text->points[p].y <= s->info.bounds.y1);
        }
        assert(strstr(tiku_circuit_details(d, at), "Global label"));
    }
    const double crosses[][4] = {{47.9, 55, 52.1, 65}, {52.1, 55, 47.9, 65},
                                 {80, 57.9, 90, 62.1}, {90, 57.9, 80, 62.1}};
    size_t count = 0;

    for (size_t i = 0; i < d->count; i++) {
        if (d->shapes[i].colour == KC_DNP_CROSS) {
            circuit_shape_t *s = &d->shapes[i];

            assert(count < 4 && s->count == 2);
            for (int p = 0; p < 2; p++) {
                assert(fabs(s->points[p].x - crosses[count][p * 2]) < .0002);
                assert(fabs(s->points[p].y - crosses[count][p * 2 + 1]) <
                       .0002);
            }
            count++;
        }
    }
    assert(count == 4);
    size_t at = tiku_circuit_find(d, "U1", 0);
    const char *details = tiku_circuit_details(d, at);

    assert(strstr(details, "Footprint: Fixture:Test_Footprint"));
    assert(strstr(details, "Datasheet: https://example.invalid/test.pdf"));
    assert(strstr(details, "Include in BOM: Yes"));
    at = tiku_circuit_find(d, "U2", 0);
    assert(strstr(tiku_circuit_details(d, at), "Include in BOM: No"));
    at = tiku_circuit_find(d, "U1 pin 1", 0);
    assert(strstr(tiku_circuit_details(d, at), "Electrical: IN (passive)"));
    assert(strstr(tiku_circuit_details(d, at), "Component: U1"));
    tiku_circuit_view_t v;
    tiku_image_t image = {0};

    assert(tiku_circuit_fit(d, 800, 500, &v));
    assert(!tiku_circuit_render(d, &v, NULL, NULL, &image));
    tiku_image_free(&image);
    tiku_circuit_release(d);

    /* Each tag is complete but for the one feature it is refused for. */
    const char *bad[] = {
        "(shape unknown) (at 0 0) (effects (font (size 1 1)))",
        "(shape input) (at 0 0 45) (effects (font (size 1 1)))",
        "(shape input) (at 0 0) (effects (font (size 1 2)))",
        "(shape input) (at 0 0) (effects (font (size 1 1))) "
        "(property \"Intersheetrefs\" \"${INTERSHEET_REFS}\" (at 0 0))"
    };

    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        char text[1024];

        snprintf(text, sizeof text, ROOT "(global_label \"X\" %s))", bad[i]);
        rejects(text, TIKU_CIRCUIT_UNSUPPORTED);
    }
    rejects(ROOT "(global_label \"${NET}\" (shape input) (at 0 0) "
            "(effects (font (size 1 1)))))", TIKU_CIRCUIT_UNSUPPORTED);
    const char *shapes[] = {"input", "output", "bidirectional", "tri_state",
                            "passive"};

    for (int shape = 0; shape < 5; shape++) {
        for (int rotation = 0; rotation < 4; rotation++) {
            char source[512];

            snprintf(source, sizeof source,
                     ROOT "(global_label \"NET\" (shape %s) (at 10 20 %d) "
                     "(effects (font (size 1 1)))))", shapes[shape],
                     rotation * 90);
            d = NULL;
            assert(!tiku_circuit_kicad(source, strlen(source), &options, &d,
                                       NULL));
            assert(d->count == 2 && d->shapes[0].points[0].x == 10 &&
                   d->shapes[0].points[0].y == 20);
            assert(d->shapes[1].kind == CIRCUIT_TEXT);
            assert(fabs(d->shapes[1].matrix[0] - (rotation % 2 ? 0 : 1)) <
                   1e-9);
            assert(fabs(d->shapes[1].matrix[1] - (rotation % 2 ? -1 : 0)) <
                   1e-9);
            tiku_circuit_release(d);
        }
    }
}

/** @brief Observations from KiCad's SVG export: upright text, its
 *         direction and the side of its anchor it lies on. */
static void
property_placement(void)
{
    tiku_circuit_t *d = fixture("properties");
    /* +1 grows right (horizontal) or up (vertical); -1 left or down.  The
     * signs were read from each exported text centre against its anchor,
     * not computed with the importer's transform.  Rows are N / X / Y. */
    const int ref_side[] = {1, 1, -1, 1, -1, 1, -1, -1, 1, -1, 1, -1};
    const int val_side[] = {1, -1, 1, -1, -1, 1, -1, 1, -1, 1, 1, -1};

    assert(d->count == 36);
    for (int i = 0; i < 12; i++) {
        const circuit_shape_t *r = &d->shapes[i * 3 + 1];
        const circuit_shape_t *v = &d->shapes[i * 3 + 2];

        assert(r->kind == CIRCUIT_TEXT && v->kind == CIRCUIT_TEXT);
        int turned = (i / 3) % 2;

        assert(fabs(r->matrix[0] - (turned ? 0 : 1)) < 1e-9);
        assert(fabs(r->matrix[1] - (turned ? -1 : 0)) < 1e-9);
        assert(fabs(v->matrix[0] - (turned ? 1 : 0)) < 1e-9);
        assert(fabs(v->matrix[1] - (turned ? 0 : -1)) < 1e-9);
        assert(fabs(r->matrix[2] - (turned ? 1 : 0)) < 1e-9 &&
               fabs(r->matrix[3] - (turned ? 0 : 1)) < 1e-9);
        assert(fabs(v->matrix[2] - (turned ? 0 : 1)) < 1e-9 &&
               fabs(v->matrix[3] - (turned ? 1 : 0)) < 1e-9);
        assert(r->cx == -ref_side[i] && v->cx == -val_side[i]);
        assert(r->matrix[4] == 32 + (i % 3) * 60 &&
               r->matrix[5] == 20 + (i / 3) * 40);
        assert(v->matrix[4] == 32 + (i % 3) * 60 &&
               v->matrix[5] == 30 + (i / 3) * 40);
    }
    tiku_circuit_release(d);
}

/** @brief Unknown drawing state is refused, even after valid objects. */
static void
invalid(void)
{
    rejects("", TIKU_CIRCUIT_BAD);
    rejects("(pcbnew)", TIKU_CIRCUIT_FORMAT);
    rejects("(\"kicad_sch\" (version 20230121))", TIKU_CIRCUIT_BAD);
    rejects(ROOT "(text))", TIKU_CIRCUIT_BAD);
    rejects(ROOT "(text \"bad\\q\"))", TIKU_CIRCUIT_BAD);
    rejects(ROOT "(text \"\xc0\xaf\"))", TIKU_CIRCUIT_BAD);
    rejects(ROOT ") trailing", TIKU_CIRCUIT_BAD);
    rejects(ROOT "(version 20230121))", TIKU_CIRCUIT_BAD);
    rejects(ROOT "(wire (pts (xy 0 0) (xy 1 1))) (image))",
            TIKU_CIRCUIT_UNSUPPORTED);
    rejects(ROOT "(sheet (property \"Sheetfile\" \"../../private\")))",
            TIKU_CIRCUIT_UNSUPPORTED);
    rejects(ROOT "(text \"${REFERENCE}\" (at 0 0)))",
            TIKU_CIRCUIT_UNSUPPORTED);
    rejects(ROOT "(symbol (lib_id \"Missing:Symbol\") (at 0 0) (unit 1)))",
            TIKU_CIRCUIT_MISSING);
    rejects(ROOT "(wire (pts (xy nan 0) (xy 1 1))))", TIKU_CIRCUIT_BAD);
    rejects(ROOT "(wire (pts (xy 1e9 0) (xy 1 1))))", TIKU_CIRCUIT_BAD);
    rejects(ROOT "(text \"x\" (at 0 0) (hide maybe)))", TIKU_CIRCUIT_BAD);
}

/** @brief A cancellation that comes on the @p ctx'th call. */
static int
cancel(void *ctx)
{
    return --*(int *)ctx <= 0;
}

/** @brief A cancellation that comes at once. */
static int
cancel_now(void *ctx)
{
    (void)ctx;
    return 1;
}

/** @brief @p depth lists, each inside the one before, into @p out. */
static void
nest(char *out, int depth)
{
    size_t n = 0;

    for (int i = 0; i < depth; i++) {
        memcpy(out + n, "(x ", 3);
        n += 3;
    }
    for (int i = 0; i < depth; i++) {
        out[n++] = ')';
    }
    out[n] = 0;
}

/** @brief A list holding one string of @p bytes bytes, quoted or not, in
 *         @p out.  @return the text's length */
static size_t
long_atom(char *out, size_t bytes, int quoted)
{
    size_t n = 0;

    memcpy(out, "(x ", 3);
    n += 3;
    if (quoted) {
        out[n++] = '"';
    }
    memset(out + n, 'a', bytes);
    n += bytes;
    if (quoted) {
        out[n++] = '"';
    }
    out[n++] = ')';
    out[n] = 0;
    return n;
}

/** @brief The parser's input, node, atom and nesting budgets, accepted to
 *         their boundary and refused past it, and its cancellation. */
static void
parser_limits(void)
{
    char nested[2048];
    circuit_sx_t tree = {0};

    nest(nested, CIRCUIT_NEST_MAX);
    assert(!circuit_sx_parse(nested, strlen(nested), NULL, &tree, NULL));
    circuit_sx_free(&tree);
    nest(nested, CIRCUIT_NEST_MAX + 1);
    assert(circuit_sx_parse(nested, strlen(nested), NULL, &tree, NULL) ==
           TIKU_CIRCUIT_LIMIT && !tree.node);
    nest(nested, 140);
    rejects(nested, TIKU_CIRCUIT_LIMIT);
    char *large = malloc(CIRCUIT_INPUT_MAX + 1);
    tiku_circuit_t *d = NULL;
    tiku_circuit_diagnostic_t diag;

    assert(large);
    for (int quoted = 0; quoted < 2; quoted++) {
        size_t n = long_atom(large, CIRCUIT_TOKEN_BYTES, quoted);

        assert(!circuit_sx_parse(large, n, NULL, &tree, NULL));
        assert(strlen(tree.node[2].text) == CIRCUIT_TOKEN_BYTES);
        circuit_sx_free(&tree);
        n = long_atom(large, CIRCUIT_TOKEN_BYTES + 1, quoted);
        assert(circuit_sx_parse(large, n, NULL, &tree, &diag) ==
               TIKU_CIRCUIT_LIMIT && !tree.node);
        assert(strstr(diag.message, "4096 bytes"));
    }
    assert(tiku_circuit_kicad(large, CIRCUIT_INPUT_MAX + 1, NULL, &d,
                              NULL) == TIKU_CIRCUIT_LIMIT && !d);
    size_t n = strlen(ROOT);

    memcpy(large, ROOT, n);
    for (int i = 0; i < 10000; i++) {
        memcpy(large + n, "(x 12) ", 7);
        n += 7;
    }
    large[n++] = ')';
    int left = 5;
    tiku_circuit_import_t opt = {.cancel = cancel, .ctx = &left};

    assert(tiku_circuit_kicad(large, n, &opt, &d, NULL) ==
           TIKU_CIRCUIT_CANCELLED && !d);
    memcpy(large, "(x ", 3);
    n = 3;
    for (size_t i = 0; i < CIRCUIT_TOKEN_MAX; i++) {
        large[n++] = 'a';
        large[n++] = ' ';
    }
    large[n++] = ')';
    assert(circuit_sx_parse(large, n, NULL, &tree, NULL) ==
           TIKU_CIRCUIT_LIMIT && !tree.node);
    free(large);
    const char nul[] = "(kicad_sch\0 (version 20230121))";

    assert(tiku_circuit_kicad(nul, sizeof nul - 1, NULL, &d, NULL) ==
           TIKU_CIRCUIT_BAD && !d);
    assert(!circuit_sx_parse("(x -1.25 +.5)", 13, NULL, &tree, NULL));
    double number = 0;

    assert(circuit_sx_number(&tree, circuit_sx_arg(&tree, 0, 0), &number) &&
           number == -1.25);
    assert(circuit_sx_number(&tree, circuit_sx_arg(&tree, 0, 1), &number) &&
           number == .5);
    circuit_sx_free(&tree);
}

/** @brief A schematic of @p placed symbols each looking through @p library
 *         definitions: about their product in checked operations.
 *  @return its length, in a buffer the caller frees */
static char *
busy(size_t library, size_t placed, size_t *bytes)
{
    size_t cap = 64 + library * 32 + placed * 160, n = 0;
    char *text = malloc(cap);

    assert(text);
    n += (size_t)snprintf(text + n, cap - n, "%s(lib_symbols", ROOT);
    for (size_t i = 0; i < library; i++) {
        n += (size_t)snprintf(text + n, cap - n, " (symbol \"L:%zu\")", i);
    }
    n += (size_t)snprintf(text + n, cap - n, ")");
    for (size_t i = 0; i < placed; i++) {
        n += (size_t)snprintf(text + n, cap - n,
                              " (symbol (lib_id \"L:0\") (at 0 0) (unit 1)"
                              " (property \"Reference\" \"R\" (effects hide))"
                              " (property \"Value\" \"V\" (effects hide)))");
    }
    n += (size_t)snprintf(text + n, cap - n, ")");
    assert(n < cap);
    *bytes = n;
    return text;
}

/** @brief The interpreter's work budget and its cancellation, apart from
 *         the parser's. */
static void
interpreter_limits(void)
{
    size_t bytes;
    char *text = busy(2000, 900, &bytes);
    tiku_circuit_t *d = NULL;
    tiku_circuit_diagnostic_t diag;

    assert(!tiku_circuit_kicad(text, bytes, NULL, &d, NULL));
    tiku_circuit_release(d);
    free(text);
    text = busy(2000, 1100, &bytes);
    d = NULL;
    assert(tiku_circuit_kicad(text, bytes, NULL, &d, &diag) ==
           TIKU_CIRCUIT_LIMIT && !d);
    assert(strstr(diag.message, "work limit"));
    free(text);

    const char *small = ROOT "(wire (pts (xy 0 0) (xy 1 1))))";
    circuit_sx_t tree = {0};
    tiku_circuit_import_t opt = {.cancel = cancel_now};

    assert(!circuit_sx_parse(small, strlen(small), NULL, &tree, NULL));
    d = (void *)&opt;
    assert(circuit_kc_decode(&tree, &opt, NULL, NULL, 0, NULL, &d, &diag) ==
           TIKU_CIRCUIT_CANCELLED && d == (void *)&opt);
    assert(diag.code == TIKU_CIRCUIT_CANCELLED);
    circuit_sx_free(&tree);
}

/** @brief A visible reference too long to keep whole is refused, not cut
 *         through a character. */
static void
long_reference(void)
{
    char ref[300], text[4096];

    memset(ref, 'A', 254);
    ref[254] = (char)0xc3;          /* U+00E9 across bytes 255 and 256 */
    ref[255] = (char)0xa9;
    ref[256] = 0;
    snprintf(text, sizeof text,
             ROOT "(lib_symbols (symbol \"L:X\" (symbol \"X_0_1\")))"
             " (symbol (lib_id \"L:X\") (at 10 10 0) (unit 1)"
             " (property \"Reference\" \"%s\" (at 10 10 0)"
             " (effects (font (size 1.27 1.27))))"
             " (property \"Value\" \"v\" (at 10 12 0)"
             " (effects (font (size 1.27 1.27)) hide))))", ref);
    tiku_circuit_diagnostic_t diag = rejects(text, TIKU_CIRCUIT_LIMIT);

    assert(strstr(diag.message, "Reference is too long"));
}

/** @brief Measuring stops at the terminator even when a cut string ends
 *         inside a character. */
static void
cut_character(void)
{
    tiku_circuit_t *d = circuit_new();
    char *cut = malloc(3);

    assert(d && cut);
    d->font = tiku_ttf_open_mem(font_bytes, options.font_bytes);
    assert(d->font);
    cut[0] = 'A';
    cut[1] = (char)0xc3;            /* a two-byte lead, its tail missing */
    cut[2] = 0;
    assert(circuit_text_line_width(d, cut) == -1);
    free(cut);
    tiku_circuit_release(d);
}

/** @brief KiCad's SVG export of the orientation fixture: a label turned
 *         180 degrees reads left to right, ending at its anchor; one at
 *         270 reads upward below it; library text in a symbol turned 180
 *         stays upright, centred where the symbol puts it. */
static void
text_direction(void)
{
    tiku_circuit_t *d = fixture("orientation");
    const circuit_shape_t *s = text_shape(d, "LEFTNET");
    tiku_circuit_box_t b = extent(s);

    /* KiCad: x 22.52 to 29.78, y 48.22 to 49.49 */
    assert(horizontal(s) && b.x1 < 30.5 && b.x0 < 26 && b.y1 < 50.5 &&
           (b.y0 + b.y1) / 2 < 50);
    s = text_shape(d, "DOWNNET");
    b = extent(s);
    /* KiCad: x 78.22 to 79.49, y 50.22 to 58.63 */
    assert(vertical(s) && b.y0 > 49.5 && b.y1 > 55 && (b.x0 + b.x1) / 2 < 80);
    s = text_shape(d, "LIBTXT");
    b = extent(s);
    /* KiCad: x 47.28 to 52.90, y 80.31 to 81.58 */
    assert(horizontal(s) && fabs((b.x0 + b.x1) / 2 - 50.09) < .3 &&
           fabs((b.y0 + b.y1) / 2 - 80.94) < .5);
    tiku_circuit_release(d);
}

/** @brief KiCad's SVG export of the angles fixture: free text and labels
 *         read upward only when the angle's whole degrees are 90 or 270;
 *         library text sits on its turned box, up an upright symbol at 45
 *         and 180 degrees and across one turned a quarter. */
static void
odd_angles(void)
{
    tiku_circuit_t *d = fixture("angles");
    const char *across[] = {"T45", "TM90", "L45", "LM90"};
    const double anchor[][2] = {{20, 20}, {40, 20}, {20, 40}, {40, 40}};

    /* KiCad: each from its anchor rightward, above it; T45 x 20.22 to
     * 23.24, y 18.26 to 19.59. */
    for (int i = 0; i < 4; i++) {
        const circuit_shape_t *s = text_shape(d, across[i]);
        tiku_circuit_box_t b = extent(s);

        assert(horizontal(s) && b.x0 >= anchor[i][0] - .1 &&
               b.x1 > anchor[i][0] + 2 && b.y1 < anchor[i][1] + .5);
    }
    /* KiCad: 90.7 reads as 90; x 58.57 to 59.84, y 15.24 to 19.53. */
    const circuit_shape_t *s = text_shape(d, "T907");
    tiku_circuit_box_t b = extent(s);

    assert(vertical(s) && b.x1 < 60.5 && b.y1 <= 20.1 && b.y0 < 17.5);
    /* Library text; the substitute font's widths move centres along the
     * text by up to about a millimetre.  KiCad: SLANT x 31.38 to 32.65,
     * y 60.24 to 65.50 and TURNED x 25.54 to 26.81, y 71.58 to 78.54 in
     * the upright U1; SLANT x 60.36 to 65.62, y 67.24 to 68.51 and TURNED
     * x 71.46 to 78.42, y 73.08 to 74.35 in U2, turned a quarter. */
    s = nth_text(d, "SLANT", 0);
    assert(vertical(s) && centred(extent(s), 32.0, 62.9, 1));
    s = nth_text(d, "TURNED", 0);
    b = extent(s);
    assert(vertical(s) && b.x1 < 30 && centred(b, 26.2, 75.1, 1.5));
    s = nth_text(d, "SLANT", 1);
    assert(horizontal(s) && centred(extent(s), 63.0, 67.9, 1));
    s = nth_text(d, "TURNED", 1);
    assert(horizontal(s) && centred(extent(s), 74.9, 73.7, 1.5));
    tiku_circuit_release(d);
}

/** @brief KiCad's SVG export with (pin_names (offset 0)): a horizontal
 *         pin's name above its line and its number below. */
static void
pin_sides(void)
{
    tiku_circuit_t *d = fixture("orientation");
    tiku_circuit_box_t name = extent(text_shape(d, "NAME"));
    tiku_circuit_box_t number = extent(text_shape(d, "7"));

    /* KiCad: NAME y 78.31 to 79.58, 7 y 80.31 to 81.58; the pin is on
     * y = 80 from x = 100 to 110. */
    assert(name.y1 <= 80 && name.y0 < 79);
    assert(number.y0 >= 80 && number.y1 > 81);
    assert(fabs((name.x0 + name.x1) / 2 - 105) < .3);
    assert(fabs((number.x0 + number.x1) / 2 - 105) < .3);
    tiku_circuit_release(d);
}

/** @brief A string or a positional value that reads like a flag is text,
 *         not the flag. */
static void
quoted_flags(void)
{
    const char *texts[] = {
        ROOT "(text \"hide\" (at 10 10 0) (effects (font (size 1 1)))))",
        ROOT "(label \"hide\" (at 10 10 0) (effects (font (size 1 1)))))",
        ROOT "(text \"show_name\" (at 10 10 0)"
        " (effects (font (size 1 1)))))",
        ROOT "(text hide (at 10 10 0) (effects (font (size 1 1)))))"
    };

    for (size_t i = 0; i < sizeof texts / sizeof *texts; i++) {
        tiku_circuit_t *d = accepts(texts[i]);

        assert(d->count == 1 && d->shapes[0].kind == CIRCUIT_TEXT);
        tiku_circuit_release(d);
    }
    tiku_circuit_t *d = accepts(ROOT "(text \"x\" (at 10 10 0) hide"
                                " (effects (font (size 1 1)))))");

    assert(!d->count);
    tiku_circuit_release(d);
}

/** @brief A required list that is missing is reported at the object that
 *         lacks it, not at the start of the file. */
static void
missing_lists(void)
{
    const char *objects[] = {
        "(junction (diameter 0) (color 0 0 0 0))",
        "(text \"note\" (effects (font (size 1.27 1.27))))",
        "(wire (stroke (width 0)))",
        "(global_label \"N\" (shape input) (at 1 1 0))"
    };

    for (size_t i = 0; i < sizeof objects / sizeof *objects; i++) {
        char text[512];

        snprintf(text, sizeof text, ROOT "\n\n\n  %s)", objects[i]);
        tiku_circuit_diagnostic_t diag = rejects(text, TIKU_CIRCUIT_BAD);

        assert(diag.line == 4 && diag.column == 3);
    }
}

/** @brief A character the substitute font lacks is named as such. */
static void
missing_glyph(void)
{
    const char *texts[] = {
        ROOT "(text \"a\\tb\" (at 10 10 0) (effects (font (size 1 1)))))",
        ROOT "(text \"\xe4\xb8\xad\" (at 10 10 0)"
        " (effects (font (size 1 1)))))"
    };

    for (size_t i = 0; i < sizeof texts / sizeof *texts; i++) {
        tiku_circuit_diagnostic_t diag = rejects(texts[i],
                                                 TIKU_CIRCUIT_UNSUPPORTED);

        assert(strstr(diag.message, "substitute font lacks"));
    }
}

/** @brief The font copied into a scene that has no memory for it is
 *         NOMEM, not BAD: the import runs in a child whose address space
 *         has no room left for the copy. */
static void
font_memory(void)
{
#if !defined(__SANITIZE_ADDRESS__)
    const char *text = ROOT "(text \"x\" (at 0 0)))";
    pid_t pid;
    int status;

    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (!pid) {
        FILE *f = fopen("/proc/self/statm", "r");
        unsigned long pages;
        tiku_circuit_t *d = NULL;
        tiku_circuit_diagnostic_t diag;

        if (!f || fscanf(f, "%lu", &pages) != 1) {
            _exit(2);
        }
        fclose(f);
        /* Room for the import's own small allocations, not for the
         * font's copy. */
        rlim_t room = (rlim_t)pages * (rlim_t)sysconf(_SC_PAGESIZE) +
                      options.font_bytes / 2;
        struct rlimit limit = {room, room};

        if (setrlimit(RLIMIT_AS, &limit)) {
            _exit(3);
        }
        tiku_circuit_err_t e = tiku_circuit_kicad(text, strlen(text),
                                                  &options, &d, &diag);

        _exit(e == TIKU_CIRCUIT_NOMEM &&
              strstr(diag.message, "substitute font") ? 0 : 1);
    }
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif
}

/** @brief Opaque colours the file gives strokes, junctions and sheet
 *         outlines are drawn; translucent ones are refused. */
static void
colours(void)
{
    tiku_circuit_t *d = accepts(ROOT "(junction (at 5 5)"
                                " (color 255 0 0 1)))");

    assert(d->count == 1 && d->shapes[0].colour == 0xffff0000u);
    tiku_circuit_release(d);
    d = accepts(ROOT "(junction (at 5 5) (color 0 0 0 0)))");
    assert(d->shapes[0].colour == KC_WIRE);
    tiku_circuit_release(d);
    d = accepts(ROOT "(wire (pts (xy 0 0) (xy 5 0))"
                " (stroke (width 0) (type default) (color 0 0 255 1))))");
    assert(d->shapes[0].colour == 0xff0000ffu);
    tiku_circuit_release(d);
    rejects(ROOT "(junction (at 5 5) (color 255 0 0 0.5)))",
            TIKU_CIRCUIT_UNSUPPORTED);
    rejects(ROOT "(wire (pts (xy 0 0) (xy 5 0))"
            " (stroke (color 0 0 255 0.5))))", TIKU_CIRCUIT_UNSUPPORTED);

    const char *sheet = ROOT "(sheet (at 10 10) (size 20 10)"
        " (stroke (width 0) (type solid) (color 0 128 0 1))"
        " (fill (color 0 0 0 0))"
        " (uuid 11111111-1111-4111-8111-111111111111)"
        " (property \"Sheetname\" \"Child\" (at 10 9 0)"
        " (effects (font (size 1 1)) hide))"
        " (property \"Sheetfile\" \"child.kicad_sch\" (at 10 21 0)"
        " (effects (font (size 1 1)) hide))))";
    circuit_sx_t tree = {0};
    circuit_kc_links_t *links = calloc(1, sizeof *links);
    tiku_circuit_diagnostic_t diag;

    assert(links);
    assert(!circuit_sx_parse(sheet, strlen(sheet), NULL, &tree, NULL));
    d = NULL;
    assert(!circuit_kc_decode(&tree, &options, NULL, NULL, 0, links, &d,
                              &diag));
    assert(links->count == 1 && d->count == 1 &&
           d->shapes[0].colour == 0xff008000u);
    tiku_circuit_release(d);
    circuit_sx_free(&tree);
    free(links);
}

/** @brief Every group, or with a file argument that file decoded and
 *         reported, and with a second, drawn to that PNG. */
int
main(int argc, char **argv)
{
    tiku_fontset_face_t face;

    assert(!tiku_fontset_find("DejaVu Sans", 400, 0, &face) && !face.cff &&
           !face.index);
    font_bytes = read_bytes(face.path, &options.font_bytes);
    options.font_data = font_bytes;
    if (argc > 1) {
        /* Inspect a local file, and optionally write its PNG. */
        size_t bytes;
        unsigned char *data = read_bytes(argv[1], &bytes);
        tiku_circuit_t *d = NULL;
        tiku_circuit_diagnostic_t diag;
        int result = tiku_circuit_kicad(data, bytes, &options, &d, &diag);

        printf("%s: %s (%u:%u), %zu objects\n", argv[1],
               result ? diag.message : tiku_circuit_warning(d), diag.line,
               diag.column, tiku_circuit_count(d));
        if (!result && argc > 2) {
            tiku_circuit_view_t v;
            tiku_image_t im = {0};

            assert(tiku_circuit_fit(d, 1000, 750, &v));
            result = tiku_circuit_render(d, &v, NULL, NULL, &im);
            if (!result) {
                result = tiku_png_write(argv[2], im.px, im.w, im.h, 0);
            }
            tiku_image_free(&im);
        }
        free(data);
        free(font_bytes);
        tiku_circuit_release(d);
        return !!result;
    }
    font_memory();
    (void)setlocale(LC_NUMERIC, "");    /* decimals ignore the locale */
    transforms();
    text_and_pins();
    curves();
    units();
    global_labels();
    property_placement();
    invalid();
    parser_limits();
    interpreter_limits();
    long_reference();
    cut_character();
    text_direction();
    odd_angles();
    pin_sides();
    quoted_flags();
    missing_lists();
    missing_glyph();
    colours();
    free(font_bytes);
    puts("PASS: KiCad geometry, text direction and pin oracles, search, "
         "strict failures, budgets and cancellation");
    return 0;
}
