/* Load the actual split application bundles through their unchanged ABI.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include "tiku_app.h"

#ifndef TIKU_TEST_APPS
#define TIKU_TEST_APPS "build/runtime/apps"
#endif

int main(void)
{
    const struct {
        const char *path, *id, *name, *private_symbol;
    } cases[] = {
        {"basic/TikuBasic.so", "org.tikuos.tikubasic", "TikuBasic",
         "tiku_ide_project_parse"},
        {"c/TikuC.so", "org.tikuos.tikuc", "TikuC",
         "tiku_ide_project_parse"},
        {"editor/editor.so", "org.tikuos.edit", "Edit",
         "tiku_editor_read_document"},
        {"reader/TikuReader.so", "reader", "TikuReader",
         "tiku_reader_app_start"},
        {"marker/TikuMarker.so", "marker", "TikuMarker",
         "tiku_reader_app_start"},
        {"bench/Bench.so", "tiku.bench", "Bench",
         "tiku_bench_command_build"},
        {"console/TikuConsole.so", "tiku.console", "TikuConsole",
         "tiku_console_queue_bytes"},
    };
    void *handles[sizeof cases / sizeof cases[0]];
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", TIKU_TEST_APPS, cases[i].path);
        handles[i] = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!handles[i]) fprintf(stderr, "%s: %s\n", path, dlerror());
        assert(handles[i]);
        const tiku_app_export_t *entry = dlsym(handles[i], "tiku_app_v1");
        assert(entry);
        /* The public loader validates version/descriptor size as well. */
        char why[256];
        const tiku_app_descriptor_t *app = tiku_app_load(path, why, sizeof why);
        assert(app && !strcmp(app->id, cases[i].id));
        assert(!strcmp(app->name, cases[i].name));
        assert(app->start && app->stop && app->tick && app->pick);
        assert(app->event || app->event_in);
        assert(!dlsym(handles[i], cases[i].private_symbol));
    }
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
        assert(!dlclose(handles[i]));
    puts("PASS: BASIC, C, Editor, Reader, Marker, Bench and Console keep "
         "their ABI, IDs and private seams");
    return 0;
}
