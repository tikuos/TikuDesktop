/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_drag.c - the drag gesture: when a press becomes one, what it
 * carries, and what a drop on a given place means.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_drag.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief A model standing in for a pose under the pointer. */
static tiku_trk_model_t
folder(const char *path, int mutable_)
{
    tiku_trk_model_t m;

    memset(&m, 0, sizeof m);
    snprintf(m.path, sizeof m.path, "%s", path);
    m.kind = TIKU_TRK_KIND_DIRECTORY;
    m.facts.perm = TIKU_TRK_P_READ | (mutable_ ? TIKU_TRK_P_WRITE : 0u);
    return m;
}

static tiku_trk_model_t
plain(const char *path)
{
    tiku_trk_model_t m = folder(path, 1);

    m.kind = TIKU_TRK_KIND_FILE;
    return m;
}

int
main(void)
{
    tiku_trk_drag_t d;

    printf("arming a drag (PVS-032)\n");

    tiku_trk_drag_arm(&d, 100, 100, 1u, 0u, 1, NULL, "/src");
    (void)tiku_trk_drag_add(&d, "/src/one.txt");
    check(!tiku_trk_drag_active(&d), "PVS-032",
          "a press alone is not a drag");
    check(!tiku_trk_drag_moved(&d, 102, 101) && !tiku_trk_drag_active(&d),
          "PVS-032", "and neither is a shaky one: 2,1 is inside the slop");
    check(tiku_trk_drag_moved(&d, 104, 100) && tiku_trk_drag_active(&d),
          "PVS-032", "four pixels starts it");
    check(!tiku_trk_drag_moved(&d, 200, 200) && tiku_trk_drag_active(&d),
          "PVS-032", "and it starts once -- later moves are just tracking");
    check(d.x0 == 100 && d.y0 == 100, "PVS-032",
          "the drag's origin is the PRESS point, not where it started "
          "moving: the frame has to sit where the user grabbed it");

    tiku_trk_drag_arm(&d, 100, 100, 1u, 0u, 2, NULL, "/src");
    (void)tiku_trk_drag_add(&d, "/src/one.txt");
    check(!tiku_trk_drag_moved(&d, 140, 140) && !tiku_trk_drag_active(&d),
          "PVS-032", "the second click of a double-click never arms one");

    tiku_trk_drag_arm(&d, 100, 100, 1u, 0u, 1, NULL, "/src");
    check(!tiku_trk_drag_moved(&d, 140, 140), "PVS-032",
          "and a drag carrying nothing is a rubber band, not a drag");

    printf("\nwhat it carries (PVS-033)\n");
    tiku_trk_drag_arm(&d, 10, 10, 1u, 0u, 1, NULL, "/src");
    (void)tiku_trk_drag_add(&d, "/src/one.txt");
    (void)tiku_trk_drag_add(&d, "/src/two.txt");
    (void)tiku_trk_drag_moved(&d, 40, 40);
    check(d.count == 2, "PVS-033", "the whole selection travels, not the "
          "pose that happened to be under the pointer");
    check(tiku_trk_drag_carries(&d, "/src/two.txt") &&
          !tiku_trk_drag_carries(&d, "/src/three.txt"),
          "PVS-033", "and it knows what it is carrying");

    printf("\nwhat will take it (MA-016, PVS-042)\n");
    {
        tiku_trk_model_t dir = folder("/dst", 1);
        tiku_trk_model_t ro  = folder("/ro", 0);
        tiku_trk_model_t f   = plain("/dst/file.txt");
        tiku_trk_model_t self = folder("/src/one.txt", 1);

        check(tiku_trk_drag_can_drop(&d, &dir), "MA-016",
              "a writable folder is a drop target");
        check(!tiku_trk_drag_can_drop(&d, &f), "MA-016",
              "a plain file is not");
        check(!tiku_trk_drag_can_drop(&d, &ro), "MA-016",
              "and neither is a folder that will not take new entries -- "
              "said during the hover rather than after the drop");
        check(!tiku_trk_drag_can_drop(&d, &self), "PVS-042",
              "a pose being dragged is never its own target: dropping on "
              "the selection means the window behind it");
    }

    printf("\nwhat a drop means (PVS-037, PVS-038, FS-002)\n");
    check(tiku_trk_drag_action(&d, "/dst", 0u, 1) == TIKU_TRK_DROP_MOVE,
          "PVS-037", "same volume moves");
    check(tiku_trk_drag_action(&d, "/dst", 0u, 0) == TIKU_TRK_DROP_COPY,
          "PVS-037", "a different volume copies -- the bytes travel only "
          "when they must");
    check(tiku_trk_drag_action(&d, "/dst", TIKU_TRK_DRAG_MOD_COPY, 1) ==
          TIKU_TRK_DROP_COPY,
          "PVS-038", "Option forces a copy even within one volume");
    check(tiku_trk_drag_action(&d, "/dst", TIKU_TRK_DRAG_MOD_LINK, 1) ==
          TIKU_TRK_DROP_LINK,
          "FS-002", "Control asks for a link");
    check(tiku_trk_drag_action(&d, "/src", 0u, 1) == TIKU_TRK_DROP_NOTHING,
          "PVS-037", "and a drop back where it came from is not an "
          "operation");
    check(tiku_trk_drag_action(&d, "/src", TIKU_TRK_DRAG_MOD_COPY, 1) ==
          TIKU_TRK_DROP_COPY,
          "PVS-038", "though Option-dropping there duplicates");

    printf("\n%s\n", fails ? "FAILURES" : "the drag gesture holds");
    return fails ? 1 : 0;
}
