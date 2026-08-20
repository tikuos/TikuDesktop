/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_appload.c - what the loader accepts, and what it refuses.
 *
 * A shared object built against another toolkit must be turned away with
 * a reason rather than run against a descriptor that has moved under it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_desk_app.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/**
 * @brief Build a loadable object with the export @p body describes.
 *
 * Compiled here rather than shipped: the point of the check is what the
 * loader does with a file it did not build, including a wrong one.
 */
static int
build_so(const char *path, const char *body)
{
    char source[512];
    char command[1024];
    FILE *f;

    snprintf(source, sizeof source, "%s.c", path);
    f = fopen(source, "w");
    if (f == NULL) {
        return -1;
    }
    (void)fprintf(f, "#include \"tiku_desk_app.h\"\n%s\n", body);
    (void)fclose(f);
    snprintf(command, sizeof command,
             "cc -std=gnu11 -fPIC -shared -I../desktop/src -o %s %s "
             "2>/dev/null", path, source);
    return system(command);
}

int
main(void)
{
    const tiku_desk_app_descriptor_t *app;
    char why[256];

    printf("loading an application from a shared object (DEV-006)\n");

    if (build_so("/tmp/trk_load_good.so",
                 "static int s(void **st, const tiku_desk_app_services_t *v)"
                 "{ (void)st; (void)v; return 0; }\n"
                 "static const tiku_desk_app_descriptor_t a = {\n"
                 "  .id = \"org.tikuos.test.loaded\", .name = \"Loaded\",\n"
                 "  .start = s };\n"
                 "const tiku_desk_app_export_t tiku_desk_app_v1 = {\n"
                 "  TIKU_DESK_APP_ABI,\n"
                 "  (uint32_t)sizeof(tiku_desk_app_descriptor_t), &a };\n")
        != 0) {
        printf("  [FAIL] cannot build the fixture object\n");
        return 1;
    }
    why[0] = '\0';
    app = tiku_desk_app_load("/tmp/trk_load_good.so", why, sizeof why);
    check(app != NULL && strcmp(app->name, "Loaded") == 0,
          "a well-formed object hands over its application");
    check(app != NULL && app->start != NULL,
          "with its entry points intact across the boundary");

    (void)build_so("/tmp/trk_load_abi.so",
                   "static const tiku_desk_app_descriptor_t a = {\n"
                   "  .id = \"x\", .name = \"X\" };\n"
                   "const tiku_desk_app_export_t tiku_desk_app_v1 = {\n"
                   "  TIKU_DESK_APP_ABI + 1u,\n"
                   "  (uint32_t)sizeof(tiku_desk_app_descriptor_t), &a };\n");
    why[0] = '\0';
    app = tiku_desk_app_load("/tmp/trk_load_abi.so", why, sizeof why);
    check(app == NULL && why[0] != '\0',
          "an object built for another interface is refused, and says so");

    (void)build_so("/tmp/trk_load_size.so",
                   "static const tiku_desk_app_descriptor_t a = {\n"
                   "  .id = \"x\", .name = \"X\" };\n"
                   "const tiku_desk_app_export_t tiku_desk_app_v1 = {\n"
                   "  TIKU_DESK_APP_ABI, 8u, &a };\n");
    why[0] = '\0';
    app = tiku_desk_app_load("/tmp/trk_load_size.so", why, sizeof why);
    check(app == NULL && why[0] != '\0',
          "so is one whose descriptor is a different size here");

    (void)build_so("/tmp/trk_load_bare.so", "int nothing_useful = 1;\n");
    why[0] = '\0';
    app = tiku_desk_app_load("/tmp/trk_load_bare.so", why, sizeof why);
    check(app == NULL && why[0] != '\0',
          "an object exporting no application is refused by name");

    why[0] = '\0';
    app = tiku_desk_app_load("/tmp/trk_load_absent.so", why, sizeof why);
    check(app == NULL && why[0] != '\0',
          "and a file that is not there reports why, rather than crashing");

    (void)system("rm -f /tmp/trk_load_*.so /tmp/trk_load_*.so.c");

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe loader believes nothing it has not checked\n");
    return 0;
}
