/*
 * TikuOS kits -- circuit admission regression tests.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_circuit_private.h"
#include "tiku_circuit_sexpr.h"
#include <stdio.h>
#include <string.h>

static int fails;

/** @brief Count a failed contract check. */
static void check(int ok, const char *id)
{
    if (!ok) { printf("FAIL %s\n", id); fails++; }
}

/** @brief Group budget failures leave the destination scene unchanged. */
static void groups(void)
{
    circuit_point_t points[] = {{0, 0}, {1, 1}};
    tiku_circuit_t *child = circuit_new();

    check(!circuit_add(child, "Wire", "", points, 2, 0, 0, 0, 0), "setup");
    for (int bytes = 0; bytes < 2; bytes++) {
        tiku_circuit_t *d = circuit_new();
        size_t before;

        if (bytes) d->bytes = CIRCUIT_BYTES_MAX - child->bytes;
        else d->points = CIRCUIT_POINTS_MAX - child->points;
        before = d->bytes;
        check(circuit_group(d, "Group", "", child, 0, 0) ==
              TIKU_CIRCUIT_LIMIT && d->count == 0 && d->bytes == before &&
              d->points == (bytes ? 0 : CIRCUIT_POINTS_MAX - child->points),
              "D-103");
        tiku_circuit_release(d);
    }
    tiku_circuit_release(child);
}

/** @brief Font data and project label errors use their documented categories. */
static void errors(void)
{
    const char *xml = "<eagle version=\"9.6\"><drawing><schematic><parts/>"
        "<sheets><sheet><plain/></sheet></sheets></schematic></drawing></eagle>";
    tiku_circuit_import_t options = {0};
    tiku_circuit_project_t *project = NULL;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_t *scene = circuit_new();
    char name[257];
    const char *names[] = {name};

    options.font_data = (const unsigned char *)"invalid font";
    options.font_bytes = 12;
    check(tiku_circuit_eagle(xml, strlen(xml), "test.sch", &options,
                             &project, &diag) == TIKU_CIRCUIT_BAD &&
          !project && diag.code == TIKU_CIRCUIT_BAD, "D-082");
    memset(name, 'a', sizeof name - 1); name[256] = 0;
    check(circuit_project_sheets(&scene, names, "test.sch", 1, &project) ==
          TIKU_CIRCUIT_LIMIT && !project, "D-104");
    name[255] = 0;
    check(!circuit_project_sheets(&scene, names, "test.sch", 1, &project),
          "D-104 boundary");
    tiku_circuit_project_free(project);
    tiku_circuit_release(scene);
}

/** @brief Escaped CRLF pairs become one normalized line break. */
static void escapes(void)
{
    const char *source = "(text \"a\\r\\nb\\rc\\nd\")";
    circuit_sx_t tree = {0};

    check(!circuit_sx_parse(source, strlen(source), NULL, &tree, NULL),
          "D-107 parse");
    check(tree.count == 3 && !strcmp(tree.node[2].text, "a\nb\nc\nd"),
          "D-107");
    circuit_sx_free(&tree);
}

/** @brief Run the circuit-reader regressions. */
int main(void)
{
    groups(); errors(); escapes();
    printf("%s: circuit audit\n", fails ? "FAIL" : "PASS");
    return !!fails;
}
