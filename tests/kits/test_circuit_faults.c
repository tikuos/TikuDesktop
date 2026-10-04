/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_faults.c - the circuit readers and renderers with each of
 * their allocations failed in turn, which must leave the caller's output
 * as it was and nothing allocated; and thousands of reproducible byte
 * mutations of every fixture, read without harm.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_private.h"
#include "tiku_circuit_project.h"
#include "tiku_fontset.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A linear congruential generator's multiplier and increment: the
 * mutations are the same on every run. */
#define LCG_MUL 1664525u
#define LCG_ADD 1013904223u

static size_t calls, fail_at, live;

/** @brief Whether this allocation is the one chosen to fail. */
static int
fail(void)
{
    return ++calls == fail_at;
}

/** @brief The readers' malloc(), counted and failed on demand. */
void *
circuit_test_malloc(size_t n)
{
    if (fail()) {
        return NULL;
    }
    void *p = malloc(n);

    if (p) {
        live++;
    }
    return p;
}

/** @brief The readers' calloc(), counted and failed on demand. */
void *
circuit_test_calloc(size_t n, size_t size)
{
    if (fail()) {
        return NULL;
    }
    void *p = calloc(n, size);

    if (p) {
        live++;
    }
    return p;
}

/** @brief The readers' realloc(), counted and failed on demand. */
void *
circuit_test_realloc(void *old, size_t n)
{
    if (fail()) {
        return NULL;
    }
    int fresh = old == NULL;
    void *p = realloc(old, n);

    if (p && fresh) {
        live++;
    }
    return p;
}

/** @brief The readers' strdup(), through their malloc(). */
char *
circuit_test_strdup(const char *s)
{
    char *p = circuit_test_malloc(strlen(s) + 1);

    if (p) {
        memcpy(p, s, strlen(s) + 1);
    }
    return p;
}

/** @brief The readers' free(), counted. */
void
circuit_test_free(void *p)
{
    if (p) {
        assert(live);
        live--;
    }
    free(p);
}

/** @brief A fixture's bytes, read outside fault injection. */
static unsigned char *
read_bytes(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");

    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long n = ftell(f);

    assert(n >= 0);
    rewind(f);
    unsigned char *data = malloc((size_t)n + 1);

    assert(data);
    assert(fread(data, 1, (size_t)n, f) == (size_t)n && !fclose(f));
    *size = (size_t)n;
    data[n] = 0;
    return data;
}

/** @brief Overwrite one byte of @p data, chosen by and set from @p seed,
 *         which moves on. */
static void
mutate(unsigned char *data, size_t bytes, uint32_t *seed)
{
    *seed = *seed * LCG_MUL + LCG_ADD;
    size_t at = *seed % bytes;

    *seed = *seed * LCG_MUL + LCG_ADD;
    data[at] = (unsigned char)(*seed >> 24);
}

/** @brief A root file and the child it links, for the project loader. */
typedef struct {
    unsigned char *root, *child;
    size_t root_size, child_size;
} project_source_t;

/** @brief The resolver: the root for no parent, else the child. */
static tiku_circuit_err_t
read_project(void *ctx, const char *parent, const char *ref,
             tiku_circuit_source_t *out, tiku_circuit_diagnostic_t *diag)
{
    project_source_t *s = ctx;

    (void)diag;
    out->data = parent ? s->child : s->root;
    out->bytes = parent ? s->child_size : s->root_size;
    snprintf(out->path, sizeof out->path, "%s", ref);
    snprintf(out->key, sizeof out->key, "%s", parent ? "child" : "root");
    return TIKU_CIRCUIT_OK;
}

/** @brief The resolver's release: nothing was allocated. */
static void
release_project(void *ctx, tiku_circuit_source_t *s)
{
    (void)ctx;
    memset(s, 0, sizeof *s);
}

/** @brief Every allocation of the hierarchy import failed in turn, then
 *         mutated roots.  @return the allocations of one import */
static size_t
project_faults(const tiku_circuit_import_t *options)
{
    project_source_t s;

    s.root = read_bytes("tests/fixtures/circuit/hierarchy.kicad_sch",
                        &s.root_size);
    s.child = read_bytes("tests/fixtures/circuit/channel.kicad_sch",
                         &s.child_size);
    tiku_circuit_resolver_t resolver = {&s, read_project, release_project};
    tiku_circuit_project_t *p = NULL;

    calls = fail_at = 0;
    assert(!tiku_circuit_project_open("hierarchy.kicad_sch", options,
                                      &resolver, &p, NULL));
    assert(!tiku_circuit_project_incomplete(p));
    size_t total = calls;

    tiku_circuit_project_free(p);
    assert(!live);
    for (size_t i = 1; i <= total; i++) {
        calls = 0;
        fail_at = i;
        p = (void *)options;
        tiku_circuit_diagnostic_t diag;
        tiku_circuit_err_t e = tiku_circuit_project_open(
            "hierarchy.kicad_sch", options, &resolver, &p, &diag);

        assert(e == TIKU_CIRCUIT_NOMEM && diag.code == e &&
               p == (void *)options && !live);
    }
    fail_at = 0;
    /* Dependency metadata is mutated as well as leaf content. */
    unsigned char *original = s.root, *mutated = malloc(s.root_size);
    uint32_t seed = 0x177fe342;

    assert(mutated);
    s.root = mutated;
    for (size_t i = 0; i < 400; i++) {
        memcpy(mutated, original, s.root_size);
        mutate(mutated, s.root_size, &seed);
        p = NULL;
        tiku_circuit_err_t e = tiku_circuit_project_open(
            "hierarchy.kicad_sch", options, &resolver, &p, NULL);

        if (!e) {
            tiku_circuit_project_free(p);
        } else {
            assert(!p);
        }
        assert(!live);
    }
    free(mutated);
    free(original);
    free(s.child);
    return total;
}

/** @brief Every allocation of the drill import failed in turn, then
 *         mutated copies.  @return the allocations of one import */
static size_t
drill_faults(void)
{
    size_t bytes;
    unsigned char *source = read_bytes("tests/fixtures/circuit/drills.drl",
                                       &bytes);
    tiku_circuit_t *d = NULL;

    calls = fail_at = 0;
    assert(!tiku_circuit_drill(source, bytes, NULL, &d, NULL));
    size_t total = calls;

    tiku_circuit_release(d);
    assert(!live);
    for (size_t i = 1; i <= total; i++) {
        tiku_circuit_diagnostic_t diag;

        calls = 0;
        fail_at = i;
        d = (void *)source;
        assert(tiku_circuit_drill(source, bytes, NULL, &d, &diag) ==
               TIKU_CIRCUIT_NOMEM);
        assert(d == (void *)source && diag.code == TIKU_CIRCUIT_NOMEM &&
               !live);
    }
    fail_at = 0;
    unsigned char *mutated = malloc(bytes);
    uint32_t seed = 0x3742142;

    assert(mutated);
    for (size_t i = 0; i < 800; i++) {
        memcpy(mutated, source, bytes);
        mutate(mutated, bytes, &seed);
        d = NULL;
        if (!tiku_circuit_drill(mutated, bytes, NULL, &d, NULL)) {
            tiku_circuit_release(d);
        } else {
            assert(!d);
        }
        assert(!live);
    }
    free(mutated);
    free(source);
    return total;
}

/** @brief Every allocation of the Gerber import failed in turn, then
 *         mutated copies.  @return the allocations of one import */
static size_t
gerber_faults(void)
{
    size_t bytes;
    unsigned char *source = read_bytes("tests/fixtures/circuit/artwork.gbr",
                                       &bytes);
    tiku_circuit_t *d = NULL;

    calls = fail_at = 0;
    assert(!tiku_circuit_gerber(source, bytes, NULL, &d, NULL));
    size_t total = calls;

    tiku_circuit_release(d);
    assert(!live);
    for (size_t i = 1; i <= total; i++) {
        tiku_circuit_diagnostic_t diag;

        calls = 0;
        fail_at = i;
        d = (void *)source;
        assert(tiku_circuit_gerber(source, bytes, NULL, &d, &diag) ==
               TIKU_CIRCUIT_NOMEM);
        assert(d == (void *)source && diag.code == TIKU_CIRCUIT_NOMEM &&
               !live);
    }
    fail_at = 0;
    unsigned char *mutated = malloc(bytes);
    uint32_t seed = 0x74724142;

    assert(mutated);
    for (size_t i = 0; i < 1000; i++) {
        memcpy(mutated, source, bytes);
        mutate(mutated, bytes, &seed);
        d = NULL;
        if (!tiku_circuit_gerber(mutated, bytes, NULL, &d, NULL)) {
            tiku_circuit_release(d);
        } else {
            assert(!d);
        }
        assert(!live);
    }
    free(mutated);
    free(source);
    return total;
}

/** @brief Every allocation of an artwork render failed in turn: the
 *         viewport, its coverage and each macro's own. */
static void
coverage_faults(const char *path)
{
    size_t bytes;
    unsigned char *source = read_bytes(path, &bytes);
    tiku_circuit_t *d = NULL;

    fail_at = 0;
    assert(!tiku_circuit_gerber(source, bytes, NULL, &d, NULL));
    size_t baseline = live;
    tiku_circuit_view_t view = {.width = 400, .height = 320, .scale = 10};
    tiku_image_t im = {0};

    calls = 0;
    assert(!tiku_circuit_render(d, &view, NULL, NULL, &im));
    size_t total = calls;

    circuit_test_free(im.px);
    assert(live == baseline);
    for (size_t i = 1; i <= total; i++) {
        calls = 0;
        fail_at = i;
        im = (tiku_image_t){0};
        assert(tiku_circuit_render(d, &view, NULL, NULL, &im) ==
               TIKU_CIRCUIT_NOMEM && !im.px);
        assert(live == baseline);
    }
    fail_at = 0;
    tiku_circuit_release(d);
    free(source);
    assert(!live);
    printf("PASS: %zu artwork viewport/coverage allocation failures "
           "preserve output and ownership\n", total);
}

/** @brief Every allocation of a stack's construction and of its
 *         composite render failed in turn. */
static void
stack_faults(void)
{
    size_t bytes;
    unsigned char *source = read_bytes("tests/fixtures/circuit/polarity.gbr",
                                       &bytes);
    tiku_circuit_t *d = NULL, *stack = NULL;

    fail_at = 0;
    assert(!tiku_circuit_gerber(source, bytes, NULL, &d, NULL));
    tiku_circuit_layer_t layers[2] = {
        {.scene = d, .path = "front.gbr", .root = "", .label = "Front",
         .visible = 1, .opacity = 128, .colour = 0xff102030},
        {.scene = d, .path = "back.gbr", .root = "", .label = "Back",
         .visible = 1, .opacity = 255, .colour = 0xff405060}
    };
    size_t baseline = live;

    calls = 0;
    assert(!tiku_circuit_stack(layers, 2, &stack));
    size_t allocations = calls;

    tiku_circuit_release(stack);
    assert(live == baseline);
    for (size_t i = 1; i <= allocations; i++) {
        calls = 0;
        fail_at = i;
        stack = d;
        assert(tiku_circuit_stack(layers, 2, &stack) == TIKU_CIRCUIT_NOMEM &&
               stack == d && live == baseline);
        assert(atomic_load(&d->refs) == 1);
    }
    fail_at = 0;
    assert(!tiku_circuit_stack(layers, 2, &stack));
    baseline = live;
    tiku_circuit_view_t view = {.width = 100, .height = 100, .scale = 3};
    tiku_image_t im = {0};

    calls = 0;
    assert(!tiku_circuit_render(stack, &view, NULL, NULL, &im));
    size_t renders = calls;

    circuit_test_free(im.px);
    assert(live == baseline);
    for (size_t i = 1; i <= renders; i++) {
        calls = 0;
        fail_at = i;
        im = (tiku_image_t){0};
        assert(tiku_circuit_render(stack, &view, NULL, NULL, &im) ==
               TIKU_CIRCUIT_NOMEM && !im.px && live == baseline);
    }
    fail_at = 0;
    tiku_circuit_release(stack);
    tiku_circuit_release(d);
    free(source);
    assert(!live);
    printf("PASS: %zu stack construction and %zu composite render "
           "allocation failures\n", allocations, renders);
}

/** @brief Every allocation of a Gerber job's import failed in turn, then
 *         mutated job files. */
static void
job_faults(void)
{
    project_source_t s;
    const char json[] = "{\"FilesAttributes\":[{\"Path\":\"a.gbr\"},"
                        "{\"Path\":\"b.gbr\"}]}";

    s.root = (unsigned char *)json;
    s.root_size = strlen(json);
    s.child = read_bytes("tests/fixtures/circuit/curved-region.gbr",
                         &s.child_size);
    tiku_circuit_resolver_t resolver = {&s, read_project, release_project};
    tiku_circuit_t *d = NULL;

    calls = fail_at = 0;
    assert(!tiku_circuit_job_open("job.gbrjob", NULL, &resolver, &d, NULL));
    assert(!tiku_circuit_stack_incomplete(d));
    size_t total = calls;

    tiku_circuit_release(d);
    assert(!live);
    for (size_t i = 1; i <= total; i++) {
        tiku_circuit_diagnostic_t diag;

        calls = 0;
        fail_at = i;
        d = (void *)&s;
        assert(tiku_circuit_job_open("job.gbrjob", NULL, &resolver, &d,
                                     &diag) == TIKU_CIRCUIT_NOMEM);
        assert(d == (void *)&s && diag.code == TIKU_CIRCUIT_NOMEM && !live);
    }
    fail_at = 0;
    unsigned char mutated[sizeof json];
    uint32_t seed = 0x74921312;

    s.root = mutated;
    for (size_t i = 0; i < 500; i++) {
        memcpy(mutated, json, sizeof json);
        mutate(mutated, s.root_size, &seed);
        d = NULL;
        if (!tiku_circuit_job_open("job.gbrjob", NULL, &resolver, &d,
                                   NULL)) {
            tiku_circuit_release(d);
        } else {
            assert(!d);
        }
        assert(!live);
    }
    free(s.child);
    printf("PASS: %zu job/curved-region allocation failures and 500 job "
           "JSON mutations\n", total);
}

/** @brief Every allocation of the macro, repeat, cut-in and EAGLE imports
 *         failed in turn, then mutated copies of each. */
static void
format_faults(const tiku_circuit_import_t *options)
{
    const char *paths[] = {"tests/fixtures/circuit/macros.gbr",
                           "tests/fixtures/circuit/repeats.gbr",
                           "tests/fixtures/circuit/cutins.gbr",
                           "tests/fixtures/circuit/eagle.sch"};

    for (size_t which = 0; which < 4; which++) {
        size_t bytes;
        unsigned char *source = read_bytes(paths[which], &bytes);
        tiku_circuit_t *d = NULL;
        tiku_circuit_project_t *p = NULL;
        int eagle = which == 3;

        calls = fail_at = 0;
        if (eagle) {
            assert(!tiku_circuit_eagle(source, bytes, paths[which], options,
                                       &p, NULL));
        } else {
            assert(!tiku_circuit_gerber(source, bytes, NULL, &d, NULL));
        }
        size_t total = calls;

        tiku_circuit_release(d);
        tiku_circuit_project_free(p);
        assert(!live);
        for (size_t i = 1; i <= total; i++) {
            tiku_circuit_diagnostic_t diag;

            calls = 0;
            fail_at = i;
            d = (void *)source;
            p = (void *)source;
            tiku_circuit_err_t error = eagle
                ? tiku_circuit_eagle(source, bytes, paths[which], options,
                                     &p, &diag)
                : tiku_circuit_gerber(source, bytes, NULL, &d, &diag);

            if (error != TIKU_CIRCUIT_NOMEM) {
                fprintf(stderr, "fault %s allocation %zu: %d %s\n",
                        paths[which], i, error, diag.message);
            }
            assert(error == TIKU_CIRCUIT_NOMEM && d == (void *)source &&
                   p == (void *)source && !live);
        }
        fail_at = 0;
        unsigned char *mutated = malloc(bytes);
        uint32_t seed = 0x21fe73 + (uint32_t)which;

        assert(mutated);
        for (size_t i = 0; i < 300; i++) {
            memcpy(mutated, source, bytes);
            mutate(mutated, bytes, &seed);
            d = NULL;
            p = NULL;
            tiku_circuit_err_t error = eagle
                ? tiku_circuit_eagle(mutated, bytes, paths[which], options,
                                     &p, NULL)
                : tiku_circuit_gerber(mutated, bytes, NULL, &d, NULL);

            if (error) {
                assert(!d && !p);
            }
            tiku_circuit_release(d);
            tiku_circuit_project_free(p);
            assert(!live);
        }
        free(mutated);
        free(source);
        printf("PASS: %s: %zu allocation failures and 300 deterministic "
               "mutations\n", paths[which], total);
    }
}

/** @brief A font the reader refuses is BAD; one it could not copy for
 *         want of memory is NOMEM.  The import's last allocation, after
 *         the reader has refused the font, is one the copy's size. */
static void
font_faults(void)
{
    static const unsigned char junk[64];
    const char text[] = "(kicad_sch (version 20230121) (text \"x\" (at 0 0)))";
    tiku_circuit_import_t opt = {.font_data = junk,
                                 .font_bytes = sizeof junk};
    tiku_circuit_t *d = (void *)&opt;
    tiku_circuit_diagnostic_t diag;

    calls = fail_at = 0;
    assert(tiku_circuit_kicad(text, strlen(text), &opt, &d, &diag) ==
           TIKU_CIRCUIT_BAD && d == (void *)&opt && !live);
    assert(strstr(diag.message, "Cannot read the supplied TrueType font"));
    size_t total = calls;

    calls = 0;
    fail_at = total;
    assert(tiku_circuit_kicad(text, strlen(text), &opt, &d, &diag) ==
           TIKU_CIRCUIT_NOMEM && d == (void *)&opt && !live);
    assert(strstr(diag.message, "substitute font"));
    fail_at = 0;
    printf("PASS: an unreadable font is BAD; one without memory to copy "
           "it is NOMEM\n");
}

/** @brief Every importer and renderer failed at each allocation, and the
 *         mutations; a failed check aborts. */
int
main(void)
{
    size_t bytes, font_size;
    unsigned char *data = read_bytes("tests/fixtures/circuit/basic.kicad_sch",
                                     &bytes);
    tiku_fontset_face_t face;

    assert(!tiku_fontset_find("DejaVu Sans", 400, 0, &face));
    unsigned char *font = read_bytes(face.path, &font_size);
    tiku_circuit_import_t opt = {.font_data = font, .font_bytes = font_size};
    tiku_circuit_t *d = NULL;

    assert(!tiku_circuit_kicad(data, bytes, &opt, &d, NULL));
    size_t total = calls;

    tiku_circuit_release(d);
    assert(!live);
    for (size_t i = 1; i <= total; i++) {
        tiku_circuit_diagnostic_t diag;

        calls = 0;
        fail_at = i;
        d = (void *)&opt;
        tiku_circuit_err_t e = tiku_circuit_kicad(data, bytes, &opt, &d,
                                                  &diag);

        assert(e == TIKU_CIRCUIT_NOMEM && diag.code == e &&
               d == (void *)&opt && !live);
    }
    fail_at = 0;
    unsigned char *mutated = malloc(bytes);
    uint32_t seed = 0x923fac72;

    assert(mutated);
    for (int i = 0; i < 800; i++) {
        memcpy(mutated, data, bytes);
        mutate(mutated, bytes, &seed);
        d = NULL;
        tiku_circuit_err_t e = tiku_circuit_kicad(mutated, bytes, &opt, &d,
                                                  NULL);

        if (!e) {
            tiku_circuit_release(d);
        } else {
            assert(!d);
        }
        assert(!live);
    }
    size_t label_bytes;
    unsigned char *labels = read_bytes(
        "tests/fixtures/circuit/labels.kicad_sch", &label_bytes);

    calls = fail_at = 0;
    d = NULL;
    assert(!tiku_circuit_kicad(labels, label_bytes, &opt, &d, NULL));
    size_t label_calls = calls;

    tiku_circuit_release(d);
    assert(!live);
    for (size_t i = 1; i <= label_calls; i++) {
        calls = 0;
        fail_at = i;
        d = (void *)&opt;
        assert(tiku_circuit_kicad(labels, label_bytes, &opt, &d, NULL) ==
               TIKU_CIRCUIT_NOMEM);
        assert(d == (void *)&opt && !live);
    }
    fail_at = 0;
    free(labels);
    size_t project = project_faults(&opt);
    size_t drill = drill_faults();
    size_t gerber = gerber_faults();

    coverage_faults("tests/fixtures/circuit/polarity.gbr");
    coverage_faults("tests/fixtures/circuit/macros.gbr");
    stack_faults();
    job_faults();
    format_faults(&opt);
    font_faults();
    calls = fail_at = 0;
    d = NULL;
    assert(!tiku_circuit_example(&d));
    assert(!circuit_details(d, 0, "Original details"));
    size_t retained_bytes = d->bytes;

    fail_at = calls + 1;
    assert(circuit_details(d, 0, "Replacement") == TIKU_CIRCUIT_NOMEM);
    assert(!strcmp(tiku_circuit_details(d, 0), "Original details") &&
           d->bytes == retained_bytes);
    tiku_circuit_release(d);
    assert(!live);
    fail_at = 0;
    free(mutated);
    free(data);
    free(font);
    printf("PASS: %zu scene + %zu labels/DNP + %zu project + %zu drill + "
           "%zu Gerber allocation failures, unchanged outputs, zero live "
           "allocations and 3000 deterministic mutations\n", total,
           label_calls, project, drill, gerber);
    return 0;
}
