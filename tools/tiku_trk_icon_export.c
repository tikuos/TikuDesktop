/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_icon_export.c - write a baked icon out as a file.
 *
 * The icon directory takes files; this is where the first one comes
 * from, so a device starts from the art it already has rather than from
 * nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_icons.h"

int
main(int argc, char **argv)
{
    const tiku_trk_icon_t *icon;
    FILE *out;

    if (argc < 3) {
        int i;

        fprintf(stderr, "usage: icon-export <name> <file.hvif>\nnames:");
        for (i = 0; i < tiku_trk_icons_count(); i++) {
            fprintf(stderr, " %s", tiku_trk_icons_at(i)->name);
        }
        fprintf(stderr, "\n");
        return 2;
    }
    icon = tiku_trk_icons_find(argv[1]);
    if (icon == NULL) {
        fprintf(stderr, "icon-export: no icon called %s\n", argv[1]);
        return 2;
    }
    out = fopen(argv[2], "wb");
    if (out == NULL) {
        fprintf(stderr, "icon-export: cannot write %s\n", argv[2]);
        return 2;
    }
    if (fwrite(icon->data, 1u, icon->len, out) != icon->len ||
        fclose(out) != 0) {
        fprintf(stderr, "icon-export: %s was not written whole\n", argv[2]);
        return 2;
    }
    return 0;
}
