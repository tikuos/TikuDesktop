/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app_load.c - taking an application out of a shared object.
 *
 * The loader looks up one exported symbol and checks its interface
 * version and descriptor size before using it; a file built against
 * another toolkit is refused, with the reason in err.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_app.h"

#include <dlfcn.h>
#include <stdio.h>

/*
 * The stamp that marks a TikuOS application's binary: this string, in a
 * section of its own, which a file listing finds in the section table
 * near the head of the file.  A program that links this file carries it.
 */
#ifdef __APPLE__
/* Mach-O names a section by its segment too, and the stamp reader
 * knows both spellings. */
__attribute__((used, section("__TIKUOS,__tikuos")))
#else
__attribute__((used, section(".tikuos")))
#endif
static const char tiku_app_stamp_load[] = "TikuOS-application-1";

const tiku_app_descriptor_t *
tiku_app_load(const char *path, char *err, size_t max)
{
    const tiku_app_export_t *ex;
    void *object;

    if (err != NULL && max > 0u) {
        err[0] = '\0';
    }
    if (path == NULL) {
        snprintf(err, max, "no path");
        return NULL;
    }
    /* RTLD_NOW: a missing symbol fails the load here.  RTLD_LOCAL: one
     * application's symbols do not resolve another's. */
    object = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (object == NULL) {
        snprintf(err, max, "%s", dlerror());
        return NULL;
    }
    ex = dlsym(object, TIKU_APP_EXPORT);
    if (ex == NULL) {
        snprintf(err, max, "no %s in %s", TIKU_APP_EXPORT, path);
        (void)dlclose(object);
        return NULL;
    }
    if (ex->abi != TIKU_APP_ABI) {
        snprintf(err, max, "built for application interface %u, this is %u",
                 (unsigned)ex->abi, (unsigned)TIKU_APP_ABI);
        (void)dlclose(object);
        return NULL;
    }
    if (ex->size != (uint32_t)sizeof(tiku_app_descriptor_t)) {
        snprintf(err, max, "descriptor is %u bytes here, %u there",
                 (unsigned)sizeof(tiku_app_descriptor_t),
                 (unsigned)ex->size);
        (void)dlclose(object);
        return NULL;
    }
    if (ex->app == NULL) {
        snprintf(err, max, "exports no application");
        (void)dlclose(object);
        return NULL;
    }
    /* The object stays open for the life of the process: the descriptor
     * and everything it points at live inside it. */
    return ex->app;
}
