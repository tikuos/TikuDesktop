/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_project.c - KiCad hierarchies through a test resolver:
 * sheet instances and their references against KiCad's export, project
 * buckets, children that fail on their own, cycles, cancellation and the
 * budgets that end an import whole.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_project.h"
#include "tiku_circuit_private.h"
#include "tiku_fontset.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief What the test resolver serves, and how it misbehaves. */
typedef struct {
    const char *root, *child;       /* the files' text                   */
    size_t reads, releases;         /* calls seen                        */
    int missing, cycle, changed;    /* fail children; one key; edit      */
    int unique, cancel, stop;       /* fresh keys; cancel; end the chain */
} files_t;

static tiku_circuit_import_t options;

/** @brief The whole of the file at @p path, with a zero after it. */
static char *
read_text(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");

    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long n = ftell(f);

    assert(n >= 0);
    rewind(f);
    char *data = malloc((size_t)n + 1);

    assert(data);
    assert(fread(data, 1, (size_t)n, f) == (size_t)n && !fclose(f));
    data[n] = 0;
    if (size) {
        *size = (size_t)n;
    }
    return data;
}

/** @brief The resolver: the root for no parent, else the child, with the
 *         faults @p ctx asks for. */
static tiku_circuit_err_t
read_source(void *ctx, const char *parent, const char *ref,
            tiku_circuit_source_t *out, tiku_circuit_diagnostic_t *diag)
{
    files_t *f = ctx;

    (void)diag;
    f->reads++;
    if (parent && f->missing) {
        return TIKU_CIRCUIT_MISSING;
    }
    if (parent && f->cancel) {
        return TIKU_CIRCUIT_CANCELLED;
    }
    out->data = (const unsigned char *)(parent ? f->child : f->root);
    if (parent && f->stop && f->reads >= (size_t)f->stop) {
        out->data = (const unsigned char *)"(kicad_sch (version 20230121))";
    }
    if (parent && f->changed && f->reads == 3) {
        out->data = (const unsigned char *)"changed";
    }
    out->bytes = strlen((const char *)out->data);
    snprintf(out->path, sizeof out->path, "%s",
             parent ? "channel.kicad_sch" : ref);
    snprintf(out->key, sizeof out->key, "file-%zu",
             f->unique ? f->reads : parent && !f->cycle ? 1uL : 0uL);
    return TIKU_CIRCUIT_OK;
}

/** @brief The resolver's release: counted. */
static void
release_source(void *ctx, tiku_circuit_source_t *out)
{
    ((files_t *)ctx)->releases++;
    memset(out, 0, sizeof *out);
}

/** @brief Open @p root through @p f, expecting @p expected and as many
 *         releases as reads.  @return the project, or NULL */
static tiku_circuit_project_t *
open_project(files_t *f, const char *root, tiku_circuit_err_t expected)
{
    f->reads = f->releases = 0;
    tiku_circuit_resolver_t r = {f, read_source, release_source};
    tiku_circuit_project_t *p = (void *)&options;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_err_t e = tiku_circuit_project_open(root, &options, &r, &p,
                                                     &diag);

    if (e != expected) {
        fprintf(stderr, "project: expected %d got %d: %s\n", expected, e,
                diag.message);
    }
    assert(e == expected && f->reads == f->releases);
    if (e) {
        assert(p == (void *)&options && diag.code == e && *diag.message);
        return NULL;
    }
    return p;
}

/** @brief Point @p p is (@p x, @p y). */
static void
point(circuit_point_t p, double x, double y)
{
    assert(fabs(p.x - x) < 1e-8 && fabs(p.y - y) < 1e-8);
}

/** @brief A copy of @p text with its first @p from replaced by @p to. */
static char *
replace(const char *text, const char *from, const char *to)
{
    const char *at = strstr(text, from);

    assert(at);
    size_t before = (size_t)(at - text), tail = strlen(at + strlen(from));
    char *out = malloc(before + strlen(to) + tail + 1);

    assert(out);
    memcpy(out, text, before);
    memcpy(out + before, to, strlen(to));
    memcpy(out + before + strlen(to), at + strlen(from), tail + 1);
    return out;
}

/** @brief Instance tables choose references by project bucket and UUID
 *         path; ambiguity and missing units leave a sheet unavailable. */
static void
identities(files_t *f)
{
    const char *child = f->child;
    const char *prefix = "(instances (project \"hierarchy\"";
    const char *extra = "(instances (project \"other\" "
        "(path \"/11111111-1111-4111-8111-111111111111/"
        "22222222-2222-4222-8222-222222222222\" "
        "(reference \"R99\") (unit 1))) (project \"hierarchy\"";
    char *both = replace(child, prefix, extra);

    f->child = both;
    tiku_circuit_project_t *p = open_project(f, "hierarchy.kicad_sch",
                                             TIKU_CIRCUIT_OK);

    assert(!tiku_circuit_project_incomplete(p));
    assert(tiku_circuit_find(tiku_circuit_project_scene(p, 1), "R1", 0) ==
           0);
    assert(tiku_circuit_find(tiku_circuit_project_scene(p, 1), "R99", 0) ==
           SIZE_MAX);
    tiku_circuit_project_free(p);
    p = open_project(f, "renamed.kicad_sch", TIKU_CIRCUIT_OK);
    assert(tiku_circuit_project_incomplete(p) &&
           !tiku_circuit_project_scene(p, 1));
    assert(tiku_circuit_project_scene(p, 2));
    assert(strstr(tiku_circuit_project_sheet(p, 1)->diagnostic->message,
                  "Ambiguous"));
    tiku_circuit_project_free(p);
    char *duplicate = replace(both, "project \"other\"",
                              "project \"hierarchy\"");

    f->child = duplicate;
    p = open_project(f, "hierarchy.kicad_sch", TIKU_CIRCUIT_OK);
    assert(!tiku_circuit_project_scene(p, 1));
    assert(tiku_circuit_project_sheet(p, 1)->diagnostic->code ==
           TIKU_CIRCUIT_BAD);
    tiku_circuit_project_free(p);
    free(duplicate);
    free(both);
    char *unit = replace(child, "(reference \"R1\") (unit 1)",
                         "(reference \"R1\")");

    f->child = unit;
    p = open_project(f, "hierarchy.kicad_sch", TIKU_CIRCUIT_OK);
    assert(!tiku_circuit_project_scene(p, 1) &&
           tiku_circuit_project_scene(p, 2));
    tiku_circuit_project_free(p);
    free(unit);
    f->child = child;
    const char *root = f->root;
    char *uuid = replace(root, "33333333-3333-4333-8333-333333333333",
                         "22222222-2222-4222-8222-222222222222");

    f->root = uuid;
    open_project(f, "hierarchy.kicad_sch", TIKU_CIRCUIT_BAD);
    free(uuid);
    f->root = root;
    char *page = replace(root, "(page \"1\")", "(page \"A0\")");

    f->root = page;
    p = open_project(f, "hierarchy.kicad_sch", TIKU_CIRCUIT_OK);
    assert(!strcmp(tiku_circuit_project_sheet(p, 0)->page, "A0"));
    tiku_circuit_project_free(p);
    free(page);
    f->root = root;
}

/** @brief The hierarchy fixture against KiCad's SVG export: repeated
 *         instances with their own references, units and markers. */
static void
normal(files_t *f)
{
    tiku_circuit_project_t *p = open_project(f, "hierarchy.kicad_sch",
                                             TIKU_CIRCUIT_OK);

    assert(tiku_circuit_project_count(p) == 3 &&
           !tiku_circuit_project_incomplete(p));
    assert(f->reads == 3);
    const tiku_circuit_sheet_t *a = tiku_circuit_project_sheet(p, 1);
    const tiku_circuit_sheet_t *b = tiku_circuit_project_sheet(p, 2);

    assert(a->parent == 0 && a->depth == 1 && !strcmp(a->name, "Channel A"));
    assert(!strcmp(a->path, b->path) && strcmp(a->instance, b->instance));
    assert(!strcmp(a->page, "2") && !strcmp(b->page, "3"));
    assert(!tiku_circuit_project_sheet(p, 3) &&
           !tiku_circuit_project_scene(p, SIZE_MAX));
    tiku_circuit_t *root = tiku_circuit_project_scene(p, 0);
    tiku_circuit_t *da = tiku_circuit_project_scene(p, 1);
    tiku_circuit_t *db = tiku_circuit_project_scene(p, 2);

    assert(da && db && da != db);
    assert(tiku_circuit_find(da, "R1", 0) == 0 &&
           tiku_circuit_find(da, "R2", 0) == SIZE_MAX);
    assert(tiku_circuit_find(db, "R2", 0) == 0 &&
           tiku_circuit_find(db, "R1", 0) == SIZE_MAX);
    /* KiCad 7's SVG: a rectangle at 47,39..53,41 and the second unit's
     * circle at 50,40 r=2; shown references carry the unit letter. */
    assert(da->shapes[0].kind == CIRCUIT_POLYLINE &&
           db->shapes[0].kind == CIRCUIT_ARC);
    point(da->shapes[0].points[0], 47, 41);
    point(da->shapes[0].points[2], 53, 39);
    assert(db->shapes[0].cx == 50 && db->shapes[0].cy == 40 &&
           db->shapes[0].r == 2);
    assert(!strcmp(da->shapes[1].text, "R1A") &&
           !strcmp(db->shapes[1].text, "R2B"));
    size_t label = tiku_circuit_find(da, "IN", 0);

    assert(label != SIZE_MAX && da->shapes[label].count == 5);
    point(da->shapes[label].points[0], 35, 40);
    point(da->shapes[label].points[2], 36.27, 40.635);
    label = tiku_circuit_find(da, "OUT", 0);
    point(da->shapes[label].points[0], 63.73, 40);
    label = tiku_circuit_find(root, "IN", 0);
    point(root->shapes[label].points[0], 31.27, 40);
    label = tiku_circuit_find(root, "OUT", 0);
    point(root->shapes[label].points[0], 70, 40);
    tiku_circuit_view_t view;

    assert(tiku_circuit_fit(db, 640, 420, &view));
    tiku_image_t image = {0};

    assert(!tiku_circuit_render(db, &view, NULL, NULL, &image));
    tiku_image_free(&image);
    tiku_circuit_retain(db);
    tiku_circuit_project_free(p);
    assert(tiku_circuit_count(db) > 0);
    tiku_circuit_release(db);
    /* A renamed root has exactly one matching stored project bucket. */
    p = open_project(f, "renamed.kicad_sch", TIKU_CIRCUIT_OK);
    assert(!tiku_circuit_project_incomplete(p));
    tiku_circuit_project_free(p);
}

/** @brief A child that is missing, cyclic, changed or broken stays in the
 *         project unavailable; a root failure refuses the import. */
static void
failures(files_t *f)
{
    tiku_circuit_project_t *p;

    for (int i = 0; i < 4; i++) {
        const char *saved = f->child;

        f->missing = i == 0;
        f->cycle = i == 1;
        f->changed = i == 2;
        if (i == 3) {
            f->child = "(bad";
        }
        p = open_project(f, "hierarchy.kicad_sch", TIKU_CIRCUIT_OK);
        assert(tiku_circuit_project_count(p) == 3 &&
               tiku_circuit_project_incomplete(p));
        assert(tiku_circuit_project_scene(p, 0));
        size_t failed = i == 2 ? 2 : 1;

        assert(!tiku_circuit_project_scene(p, failed));
        const tiku_circuit_sheet_t *s = tiku_circuit_project_sheet(p,
                                                                   failed);

        assert(s->diagnostic->code && *s->diagnostic->message);
        tiku_circuit_project_free(p);
        f->child = saved;
    }
    f->missing = f->cycle = f->changed = 0;
    f->cancel = 1;
    open_project(f, "hierarchy.kicad_sch", TIKU_CIRCUIT_CANCELLED);
    f->cancel = 0;
    /* A root failure refuses the import rather than showing an
     * unavailable root. */
    const char *saved = f->root;

    f->root = "(bad";
    open_project(f, "hierarchy.kicad_sch", TIKU_CIRCUIT_BAD);
    f->root = saved;
    /* A child opened without its parent must not take property R1 for
     * R2. */
    f->root = f->child;
    open_project(f, "channel.kicad_sch", TIKU_CIRCUIT_MISSING);
    f->root = saved;
}

/** @brief The depth, sheet and byte budgets: the boundary taken, one past
 *         it refused whole. */
static void
budgets(void)
{
    const char *head = "(kicad_sch (version 20230121) "
        "(uuid 11111111-1111-4111-8111-111111111111) ";
    const char *link = "(sheet (at 10 10) (size 10 10) "
        "(uuid %08u-2222-4222-8222-222222222222) "
        "(property \"Sheetname\" \"Child\" (effects hide)) "
        "(property \"Sheetfile\" \"next.kicad_sch\" (effects hide))) ";
    char chain[1024];
    size_t n = (size_t)snprintf(chain, sizeof chain, "%s", head);

    n += (size_t)snprintf(chain + n, sizeof chain - n, link, 1);
    snprintf(chain + n, sizeof chain - n, ")");
    files_t f = {.root = chain, .child = chain, .unique = 1};

    f.stop = 3;
    tiku_circuit_project_t *p = open_project(&f, "chain.kicad_sch",
                                             TIKU_CIRCUIT_OK);

    assert(tiku_circuit_project_count(p) == 3 &&
           !tiku_circuit_project_incomplete(p));
    const tiku_circuit_sheet_t *leaf = tiku_circuit_project_sheet(p, 2);

    assert(leaf->parent == 1 && leaf->depth == 2 &&
           strlen(leaf->instance) == 37 * 3);
    tiku_circuit_project_free(p);
    f.stop = TIKU_CIRCUIT_DEPTH_MAX + 1;
    p = open_project(&f, "chain.kicad_sch", TIKU_CIRCUIT_OK);
    assert(tiku_circuit_project_count(p) == TIKU_CIRCUIT_DEPTH_MAX + 1);
    assert(tiku_circuit_project_sheet(p, TIKU_CIRCUIT_DEPTH_MAX)->depth ==
           TIKU_CIRCUIT_DEPTH_MAX);
    tiku_circuit_project_free(p);
    f.stop = 0;
    open_project(&f, "chain.kicad_sch", TIKU_CIRCUIT_LIMIT);
    assert(f.reads == TIKU_CIRCUIT_DEPTH_MAX + 1);
    char many[40000];

    n = (size_t)snprintf(many, sizeof many, "%s", head);
    for (unsigned i = 0; i < TIKU_CIRCUIT_SHEETS_MAX; i++) {
        n += (size_t)snprintf(many + n, sizeof many - n, link, i + 1);
    }
    snprintf(many + n, sizeof many - n, ")");
    f = (files_t){.root = many, .child = "(kicad_sch (version 20230121))"};
    open_project(&f, "many.kicad_sch", TIKU_CIRCUIT_LIMIT);
    /* Few tokens but large cached sources still exhaust the budget. */
    size_t bytes = 7u * 1024u * 1024u;
    char *large = malloc(bytes + 1);

    assert(large);
    memcpy(large, chain, strlen(chain));
    memset(large + strlen(chain), ' ', bytes - strlen(chain));
    large[bytes] = 0;
    f = (files_t){.root = large, .child = large, .unique = 1};
    open_project(&f, "large.kicad_sch", TIKU_CIRCUIT_LIMIT);
    assert(f.reads < 10);
    free(large);
}

/** @brief Every group against the hierarchy fixtures; a failed check
 *         aborts. */
int
main(void)
{
    tiku_fontset_face_t face;

    assert(!tiku_fontset_find("DejaVu Sans", 400, 0, &face));
    char *font = read_text(face.path, &options.font_bytes);

    options.font_data = (unsigned char *)font;
    char *root = read_text("tests/fixtures/circuit/hierarchy.kicad_sch",
                           NULL);
    char *child = read_text("tests/fixtures/circuit/channel.kicad_sch",
                            NULL);
    files_t f = {.root = root, .child = child};

    normal(&f);
    identities(&f);
    failures(&f);
    budgets();
    tiku_circuit_project_free(NULL);
    assert(!tiku_circuit_project_count(NULL) &&
           !tiku_circuit_project_incomplete(NULL));
    free(root);
    free(child);
    free(font);
    puts("PASS: hierarchy instances, KiCad coordinate/reference oracle, "
         "partial failures, cycles, cancellation and project budgets");
    return 0;
}
