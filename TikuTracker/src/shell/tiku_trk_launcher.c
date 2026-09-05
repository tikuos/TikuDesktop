/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_launcher.c - enter whichever shell this build linked.
 *
 * This file holds the only main(), and the Makefile links it into both
 * build/TikuTracker and build/TikuDesktop, so tiku_trk_application resolves
 * to tiku_trk_app.c's descriptor in one binary and tiku_trk_desktop.c's in
 * the other.  Both enter through the Application kit's registry, from
 * kits/application/tiku_app.h, so a linked-in shell is looked up by id the
 * same way a loaded application would be.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_application.h"

#include <stdio.h>

/** @brief Register the browser application and run it. */
int
main(int argc, char **argv)
{
    tiku_app_registry_t registry;
    const tiku_app_descriptor_t *application;

    tiku_app_registry_init(&registry);
    if (tiku_app_registry_add(&registry, &tiku_trk_application) < 0) {
        fprintf(stderr, "tracker: application registration failed\n");
        return 1;
    }
    application = tiku_app_registry_find(&registry,
                                               tiku_trk_application.id);
    if (application == NULL) {
        fprintf(stderr, "tracker: registered application was not found\n");
        return 1;
    }
    return tiku_app_run(application, argc, argv);
}
