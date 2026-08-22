/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_icondir.c - art taken from a directory rather than the binary.
 *
 * A device says what its folders and programs look like by dropping
 * files somewhere; a name nothing bakes must work the same way.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_icons.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief Write the baked art of @p from out as the file @p as. */
static void
plant(const char *dir, const char *as, const char *from)
{
    const tiku_icon_t *ic = tiku_icons_find(from);
    char path[512];
    FILE *f;

    if (ic == NULL) {
        return;
    }
    snprintf(path, sizeof path, "%s/%s.hvif", dir, as);
    f = fopen(path, "wb");
    if (f != NULL) {
        (void)fwrite(ic->data, 1u, ic->len, f);
        (void)fclose(f);
    }
}

int
main(void)
{
    const char *dir = "/tmp/trk_icondir";
    const tiku_icon_t *before;
    const tiku_icon_t *after;
    size_t folder_len;

    printf("art a directory supplies (DEV-008)\n");
    (void)system("rm -rf /tmp/trk_icondir && mkdir -p /tmp/trk_icondir");

    before = tiku_icons_find("folder");
    check(before != NULL, "the baked set has the art it was built with");
    folder_len = (before != NULL) ? before->len : 0u;

    check(tiku_icons_find("thermostat") == NULL,
          "and nothing by a name it never heard of");

    /* One file replaces art that exists, another names art that does
     * not: the two things a device needs to be able to say. */
    plant(dir, "folder", "printer");
    plant(dir, "thermostat", "query");
    check(tiku_icons_load_dir(dir) == 2, "both files are taken");

    after = tiku_icons_find("folder");
    check(after != NULL && after->len != folder_len,
          "the folder is drawn from the file now, not from the binary");
    check(tiku_icons_find("thermostat") != NULL,
          "and a name only the directory knows resolves");
    check(tiku_icons_bitmap("thermostat", 16) != NULL,
          "with art that actually renders at a size");

    check(tiku_icons_load_dir("/tmp/trk_icondir_absent") == 0,
          "a directory that is not there takes nothing, and says so");

    (void)system("rm -rf /tmp/trk_icondir");
    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe set is what was baked plus what was dropped\n");
    return 0;
}
