/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_eagle.c - EAGLE schematics against an authored oracle:
 * gate resolution, mirrored and rotated coordinates, pins and their
 * pads, nets, text and frames; each strict-input defect in an otherwise
 * valid document; pin lookups on a large device; budgets, truncation
 * and cancellation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"
#include "tiku_fontset.h"
#include "tiku_xml.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DECL "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
#define DTD  "<!DOCTYPE eagle SYSTEM \"eagle.dtd\">\n"
#define OPEN "<eagle version=\"9.6\">"
#define BODY "<drawing><schematic><parts/><sheets><sheet><plain>"
#define TAIL "</plain></sheet></sheets></schematic></drawing></eagle>"
#define WIRE(x1, layer)                                                  \
    "<wire x1=\"" x1 "\" y1=\"0\" x2=\"10\" y2=\"0\" width=\"0.254\" " \
    "layer=\"" layer "\"/>"

/** @brief The whole of the file at @p path, NUL-terminated; its length
 *         in @p bytes. */
static unsigned char *
read_fixture(const char *path, size_t *bytes)
{
    FILE *f = fopen(path, "rb");

    assert(f && !fseek(f, 0, SEEK_END));
    long n = ftell(f);

    assert(n > 0);
    rewind(f);
    unsigned char *s = malloc((size_t)n + 1);

    assert(s && fread(s, 1, (size_t)n, f) == (size_t)n && !fclose(f));
    s[n] = 0;
    *bytes = (size_t)n;
    return s;
}

/** @brief A cancellation callback that fires on its @p p'th call. */
static int
cancel(void *p)
{
    return --*(int *)p <= 0;
}

/** @brief @p s refused with @p code, the output left alone. */
static void
bad(const char *s, tiku_circuit_err_t code)
{
    tiku_circuit_project_t *p = (void *)s;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_err_t e = tiku_circuit_eagle(s, strlen(s), "test.sch", NULL,
                                              &p, &diag);

    if (e != code) {
        fprintf(stderr, "EAGLE %d, not %d: %s\n", e, code, diag.message);
    }
    assert(e == code && diag.code == code && p == (void *)s);
}

/** @brief Each defect in an otherwise valid document, refused with its
 *         code; the valid document itself opens. */
static void
strict_input(void)
{
    static const struct {
        const char *text;
        tiku_circuit_err_t code;
    } cases[] = {
        {DECL DTD OPEN "<drawing><board/></drawing></eagle>",
         TIKU_CIRCUIT_UNSUPPORTED},
        {DECL "<!DOCTYPE eagle [<!ENTITY x SYSTEM 'file:///etc/passwd'>]>\n"
         OPEN BODY WIRE("0", "94") TAIL, TIKU_CIRCUIT_UNSUPPORTED},
        {DECL DTD "<eagle version=\"9.6\" version=\"9.6\">" BODY
         WIRE("0", "94") TAIL, TIKU_CIRCUIT_BAD},
        {DECL DTD OPEN BODY WIRE("0", "94&unknown;") TAIL, TIKU_CIRCUIT_BAD},
        {DECL DTD OPEN BODY WIRE("0", "94&#0;") TAIL, TIKU_CIRCUIT_BAD},
        {DECL DTD OPEN BODY WIRE("0", "94\xc0\xaf") TAIL, TIKU_CIRCUIT_BAD},
        {"junk" DECL DTD OPEN BODY WIRE("0", "94") TAIL, TIKU_CIRCUIT_BAD},
        {"<?xml version=\"1.0\" encoding=\"iso-8859-1\"?>\n" DTD OPEN BODY
         WIRE("0", "94") TAIL, TIKU_CIRCUIT_UNSUPPORTED},
        {DECL DTD "<eagle version=\"5.6\">" BODY WIRE("0", "94") TAIL,
         TIKU_CIRCUIT_UNSUPPORTED},
        {"EESchema Schematic File Version 4", TIKU_CIRCUIT_BAD},
        /* A number with no digits is malformed, not out of range. */
        {DECL DTD OPEN BODY WIRE("", "94") TAIL, TIKU_CIRCUIT_BAD},
        {DECL DTD OPEN BODY WIRE("-", "94") TAIL, TIKU_CIRCUIT_BAD},
        {DECL DTD OPEN BODY "<rectangle x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\" "
         "layer=\"94\" rot=\"R\"/>" TAIL, TIKU_CIRCUIT_BAD},
        {DECL DTD OPEN BODY WIRE("10001", "94") TAIL, TIKU_CIRCUIT_LIMIT},
    };
    const char *valid = DECL DTD OPEN BODY WIRE("0", "94")
                        "<rectangle x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\" "
                        "layer=\"94\" rot=\"R90\"/>" TAIL;
    tiku_circuit_project_t *p = NULL;

    assert(!tiku_circuit_eagle(valid, strlen(valid), "test.sch", NULL, &p,
                               NULL));
    tiku_circuit_project_free(p);
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        bad(cases[i].text, cases[i].code);
    }
}

/** @brief A one-pin symbol on a two-gate device: @p other connects for
 *         gate H, then two for gate G's pin P, to pads A and then PADS;
 *         @p parts parts each place gate G.  @return the document,
 *         malloc'd. */
static char *
connect_document(size_t other, size_t parts)
{
    size_t cap = (other + parts) * 96 + 2048, n;
    char *s = malloc(cap);

    assert(s);
    n = (size_t)snprintf(s, cap, DECL DTD OPEN
                         "<drawing><schematic><libraries><library name=\"L\">"
                         "<symbols><symbol name=\"S\"><pin name=\"P\" "
                         "x=\"0\" y=\"0\" visible=\"off\"/></symbol>"
                         "</symbols><devicesets><deviceset name=\"D\">"
                         "<gates><gate name=\"G\" symbol=\"S\" x=\"0\" "
                         "y=\"0\"/><gate name=\"H\" symbol=\"S\" x=\"0\" "
                         "y=\"0\"/></gates><devices><device name=\"\">"
                         "<connects>");
    for (size_t i = 0; i < other; i++) {
        n += (size_t)snprintf(s + n, cap - n,
                              "<connect gate=\"H\" pin=\"P%zu\" "
                              "pad=\"%zu\"/>", i, i);
    }
    n += (size_t)snprintf(s + n, cap - n,
                          "<connect gate=\"G\" pin=\"P\" pad=\"A\"/>"
                          "<connect gate=\"G\" pin=\"P\" pad=\"PADS\"/>"
                          "</connects></device></devices></deviceset>"
                          "</devicesets></library></libraries><parts>");
    for (size_t i = 0; i < parts; i++) {
        n += (size_t)snprintf(s + n, cap - n,
                              "<part name=\"U%zu\" library=\"L\" "
                              "deviceset=\"D\" device=\"\"/>", i);
    }
    n += (size_t)snprintf(s + n, cap - n, "</parts><sheets><sheet>"
                          "<instances>");
    for (size_t i = 0; i < parts; i++) {
        n += (size_t)snprintf(s + n, cap - n,
                              "<instance part=\"U%zu\" gate=\"G\" "
                              "x=\"10\" y=\"10\"/>", i);
    }
    snprintf(s + n, cap - n, "</instances></sheet></sheets></schematic>"
             "</drawing></eagle>");
    return s;
}

/** @brief A pin finds its pads among a large device's connects, the
 *         later of two connects for it counting; reading a device's
 *         connects for each instance placed is work the budget counts. */
static void
large_device(void)
{
    char *s = connect_document(3000, 1);
    tiku_circuit_project_t *p = NULL;
    tiku_circuit_diagnostic_t diag;
    size_t i;

    assert(!tiku_circuit_eagle(s, strlen(s), "test.sch", NULL, &p, &diag));
    tiku_circuit_t *d = tiku_circuit_project_scene(p, 0);

    for (i = 0; i < tiku_circuit_count(d); i++) {
        if (strstr(tiku_circuit_details(d, i), "Pin: P\nPads: PADS\n")) {
            break;
        }
    }
    assert(i < tiku_circuit_count(d));
    tiku_circuit_project_free(p);
    free(s);
    /* 101 instances of a device with 20,002 connects: over 2,000,000. */
    s = connect_document(20000, 101);
    p = NULL;
    assert(tiku_circuit_eagle(s, strlen(s), "test.sch", NULL, &p, &diag)
           == TIKU_CIRCUIT_LIMIT && !p);
    assert(strstr(diag.message, "budget"));
    free(s);
}

/** @brief Every group. */
int
main(void)
{
    size_t bytes, font_bytes;
    unsigned char *s = read_fixture("tests/fixtures/circuit/eagle.sch",
                                    &bytes);
    tiku_fontset_face_t face;

    assert(!tiku_fontset_find("DejaVu Sans", 400, 0, &face));
    unsigned char *font = read_fixture(face.path, &font_bytes);
    tiku_circuit_import_t opt = {.font_data = font, .font_bytes = font_bytes};
    tiku_circuit_project_t *p = NULL;
    tiku_circuit_diagnostic_t diag;
    int error = tiku_circuit_eagle(s, bytes, "eagle.sch", &opt, &p, &diag);

    if (error) {
        fprintf(stderr, "EAGLE: %s\n", diag.message);
    }
    assert(!error && p);
    assert(tiku_circuit_project_count(p) == 2
           && !tiku_circuit_project_incomplete(p));
    tiku_circuit_t *d = tiku_circuit_project_scene(p, 0);

    assert(d->count > 20);
    size_t first = tiku_circuit_find(d, "U1", 0);

    assert(first != SIZE_MAX);
    assert(fabs(d->shapes[first].points[0].x - 18) < 1e-8
           && fabs(d->shapes[first].points[0].y + 23) < 1e-8);
    assert(strstr(tiku_circuit_details(d, first), "Gate: A"));
    int mirrored = 0, pads = 0, rating = 0, names = 0;

    for (size_t i = 0; i < d->count; i++) {
        if (d->shapes[i].kind == CIRCUIT_TEXT) {
            const double *m = d->shapes[i].matrix;

            assert(m[0] * m[3] - m[1] * m[2] > 0);
            if (!strcmp(d->shapes[i].text, "3.3 V")) {
                rating++;
            }
            if (!strcmp(d->shapes[i].text, "U1A")
                || !strcmp(d->shapes[i].text, "U1B")) {
                names++;
            }
        }
        if (strstr(tiku_circuit_details(d, i), "Gate: B")
            && d->shapes[i].kind == CIRCUIT_POLYLINE
            && d->shapes[i].count == 2
            && fabs(d->shapes[i].points[0].x - 52) < 1e-8
            && fabs(d->shapes[i].points[0].y + 27) < 1e-8) {
            mirrored = 1;
        }
        if (strstr(tiku_circuit_details(d, i), "Pads: 4")) {
            pads = 1;
        }
    }
    assert(mirrored && pads && rating == 2 && names == 2
           && tiku_circuit_find(d, "SIGNAL", 0) != SIZE_MAX);
    assert(tiku_circuit_find(tiku_circuit_project_scene(p, 1), "DATA", 0)
           != SIZE_MAX);
    tiku_circuit_t *second = tiku_circuit_project_scene(p, 1);
    int frame = 0, page = 0;

    for (size_t i = 0; i < second->count; i++) {
        circuit_shape_t *shape = &second->shapes[i];

        if (shape->kind != CIRCUIT_TEXT) {
            continue;
        }
        if (!strcmp(shape->text, "3") && shape->matrix[4] < 20) {
            frame++;
        }
        if (!strcmp(shape->text, "A") && shape->matrix[5] < -20) {
            frame++;
        }
        if (!strcmp(shape->text, "2/2")) {
            page++;
        }
    }
    assert(frame == 4 && page == 1);
    tiku_circuit_view_t view;
    tiku_image_t image = {0};

    assert(tiku_circuit_fit(d, 640, 400, &view));
    assert(!tiku_circuit_render(d, &view, NULL, NULL, &image) && image.px);
    tiku_image_free(&image);
    tiku_circuit_project_free(p);
    for (int i = 1; i <= 40; i++) {
        int left = i;

        opt.cancel = cancel;
        opt.ctx = &left;
        p = NULL;
        assert(tiku_circuit_eagle(s, bytes, "eagle.sch", &opt, &p, NULL)
               == TIKU_CIRCUIT_CANCELLED && !p);
    }
    opt.cancel = NULL;
    opt.ctx = NULL;
    for (size_t i = 1; i < bytes - 2; i += 17) {
        p = NULL;
        assert(tiku_circuit_eagle(s, i, "eagle.sch", &opt, &p, NULL) && !p);
    }
    strict_input();
    large_device();
    tiku_xml_limits_t limits = {1024, 100, NULL, NULL};
    tiku_xml_status_t status;

    assert(!tiku_xml_parse_limited("<a/>", 4, &limits, &status, NULL, 0)
           && status == TIKU_XML_LIMIT);
    limits.bytes = 1024 * 1024;
    limits.nodes = 1;
    assert(!tiku_xml_parse_limited("<a><b/></a>", 11, &limits, &status,
                                   NULL, 0)
           && status == TIKU_XML_LIMIT);
    free(s);
    free(font);
    puts("PASS: EAGLE sheets, mirrored/rotated gates, pins/pads, nets, text, "
         "strict XML, limits and cancellation");
    return 0;
}
