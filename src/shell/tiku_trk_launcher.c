/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_launcher.c - select Tracker from the desktop app registry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_application.h"

#include <stdio.h>

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
