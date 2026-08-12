/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_volume.c - the mount table, and what it means for an operation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_volume.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

int
main(void)
{
    tiku_trk_volumes_t vs;
    const tiku_trk_volume_t *v;
    char why[200];
    int n, i, boots = 0;

    printf("reading the mount table\n");
    n = tiku_trk_volumes_scan(&vs);
    check(n > 0, "MA-021", "at least one volume is mounted");
    for (i = 0; i < vs.n; i++) {
        boots += vs.v[i].is_boot;
    }
    check(boots == 1, "FS-041",
          "exactly one of them is the boot volume: the system booted from "
          "one place");
    {   /* The kernel's own bookkeeping filesystems are not places files
         * live, and listing them as volumes would fill the Disks window
         * with things nobody can browse. */
        int junk = 0;

        for (i = 0; i < vs.n; i++) {
            if (strcmp(vs.v[i].mount, "/proc") == 0 ||
                strcmp(vs.v[i].mount, "/sys") == 0) {
                junk++;
            }
        }
        check(junk == 0, "TS-046",
              "and /proc and /sys are not offered as volumes");
    }

    printf("\nwhich volume a path is on\n");
    v = tiku_trk_volume_of(&vs, "/tmp");
    check(v != NULL, "FS-012", "a real path resolves to a volume");
    check(tiku_trk_volume_of(&vs, "/no/such/path") == NULL, "FS-012",
          "and one that does not exist resolves to none");
    check(tiku_trk_volume_is_root(&vs, "/"), "FS-059",
          "\"/\" is a volume's mount point");
    check(!tiku_trk_volume_is_root(&vs, "/tmp/somefile"), "FS-059",
          "an ordinary path is not");

    printf("\nwhat a volume refuses (FS-045, FS-041)\n");
    check(tiku_trk_volume_may_write(&vs, "/tmp", 1, why, sizeof why),
          "FS-045", "a writable volume takes a change");
    check(tiku_trk_volume_may_write(&vs, "/tmp", 0, why, sizeof why),
          "FS-013", "and reading is always allowed -- a copy FROM a "
          "read-only volume is still a copy");
    check(!tiku_trk_volume_may_unmount(&vs, "/", why, sizeof why),
          "FS-041", "the boot volume can never be unmounted");
    check(strstr(why, "boot volume") != NULL, "FS-041",
          "and says so, naming it");
    check(!tiku_trk_volume_may_unmount(&vs, "/tmp/notavolume", why,
                                       sizeof why),
          "FS-041", "while something that is not a volume is not this "
          "gesture at all");

    {   /* A synthesised read-only volume: the rule must not depend on
         * this host happening to have one mounted. */
        tiku_trk_volumes_t ro;

        memset(&ro, 0, sizeof ro);
        ro.n = 1;
        snprintf(ro.v[0].mount, sizeof ro.v[0].mount, "/");
        snprintf(ro.v[0].name, sizeof ro.v[0].name, "Disc");
        ro.v[0].read_only = 1;
        ro.v[0].dev = vs.v[0].dev;
        {
            /* Point it at a path that really is on that device. */
            const tiku_trk_volume_t *w;

            ro.v[0].dev = tiku_trk_volume_of(&vs, "/tmp")->dev;
            w = tiku_trk_volume_of(&ro, "/tmp");
            check(w != NULL && w->read_only, "FS-045",
                  "a read-only volume is recognised as one");
            check(!tiku_trk_volume_may_write(&ro, "/tmp", 1, why,
                                             sizeof why),
                  "FS-045", "and refuses a move or a delete");
            check(strstr(why, "read-only") != NULL &&
                  strstr(why, "Disc") != NULL, "FS-045",
                  "naming the volume, because a user with several mounted "
                  "needs to know which refused");
            check(tiku_trk_volume_may_write(&ro, "/tmp", 0, why,
                                            sizeof why),
                  "FS-013", "but still allows reading, which is what makes "
                  "a move off it become a copy");
        }
    }



    printf("\nwhich volumes get an icon (PVN-041)\n");
    {
        tiku_trk_volumes_t t;

        memset(&t, 0, sizeof t);
        t.n = 4;
        snprintf(t.v[0].mount, sizeof t.v[0].mount, "/");
        t.v[0].is_boot = 1;
        snprintf(t.v[1].mount, sizeof t.v[1].mount, "/media/stick");
        snprintf(t.v[2].mount, sizeof t.v[2].mount, "/media/stick/inner");
        snprintf(t.v[3].mount, sizeof t.v[3].mount, "/media/stick-two");

        check(tiku_trk_volume_shows_icon(&t, &t.v[0]), "PVN-041",
              "the boot volume always shows: everything is inside it, and "
              "it is the disk");
        check(tiku_trk_volume_shows_icon(&t, &t.v[1]), "PVN-041",
              "a volume mounted under the boot volume still shows -- "
              "otherwise nothing but the boot volume ever would");
        check(!tiku_trk_volume_shows_icon(&t, &t.v[2]), "PVN-041",
              "one mounted INSIDE another does not: it is a detail of that "
              "volume, not a second disk");
        check(tiku_trk_volume_shows_icon(&t, &t.v[3]), "PVN-041",
              "while one whose path merely starts with another's does -- "
              "the test is on a whole path component");
    }

    printf("\nthe space bar (PVL-072, TS-054)\n");
    {
        tiku_trk_volume_t s1;
        int pct = -1;

        memset(&s1, 0, sizeof s1);
        s1.total = 1000;
        s1.avail = 500;
        check(tiku_trk_volume_space(&s1, &pct) == TIKU_TRK_SPACE_USED &&
              pct == 50, "PVL-072",
              "a half-full volume reads half full, in the ordinary colour");

        s1.avail = 60;              /* 6% free */
        check(tiku_trk_volume_space(&s1, &pct) == TIKU_TRK_SPACE_USED &&
              pct == 94, "PVL-072",
              "six per cent free is not yet a warning");
        s1.avail = 40;              /* 4% free */
        check(tiku_trk_volume_space(&s1, &pct) == TIKU_TRK_SPACE_WARNING,
              "TS-054", "four per cent free is");
        s1.avail = 50;              /* exactly 5% */
        check(tiku_trk_volume_space(&s1, &pct) == TIKU_TRK_SPACE_USED,
              "TS-054", "and exactly five per cent is not -- the threshold "
              "is BELOW, and it is measured on what is left rather than on "
              "a rounded used-share");

        s1.total = 0;
        s1.avail = 0;
        check(tiku_trk_volume_space(&s1, &pct) == TIKU_TRK_SPACE_FREE &&
              pct == 0, "PVL-072",
              "a volume that will not say its size shows an empty bar, not "
              "a full one -- guessing full would warn about every device "
              "whose driver has no free-space call");

        s1.total = 3;
        s1.avail = 0;
        check(tiku_trk_volume_space(&s1, &pct) == TIKU_TRK_SPACE_WARNING &&
              pct == 100, "PVL-072", "and a full one is full");
    }

    printf("\n%s\n", fails ? "FAILURES" : "the volume model holds");
    return fails ? 1 : 0;
}
