/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_dbdrop.c - dropping files on the Deskbar.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_deskbar.h"

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
    tiku_trk_backend_t *b = tiku_trk_backend_posix_open();
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(b);
    tiku_desk_rect_t f;
    tiku_trk_model_t m;
    const char *txt[1] = { "text/plain" };
    const char *mix[2] = { "text/plain", "image/png" };
    int which = -99, tracker, editor;

    if (db == NULL) { return 1; }
    tracker = tiku_trk_deskbar_add_team(db, "Tracker");
    editor  = tiku_trk_deskbar_add_team(db, "Editor");
    (void)tiku_trk_deskbar_add_team(db, "Player");
    (void)tiku_trk_deskbar_team_type(db, editor, "text/plain");
    {   /* The frame is settled by a draw, so the bar is laid out once
         * into a surface nobody looks at: hit-testing a bar that has never
         * been drawn would be testing a rectangle of zeroes. */
        tiku_desk_surface_t *sfc =
            tiku_desk_surface_new(1000, 700, TIKU_DESK_C_BACKDROP);

        tiku_trk_deskbar_draw(db, sfc);
        tiku_desk_surface_free(sfc);
    }
    f = tiku_trk_deskbar_frame(db);

    printf("what a drag is over (DB-009, DB-035)\n");
    check(tiku_trk_deskbar_drop_at(db, f.x + 10, f.y + 4, &which) ==
          TIKU_TRK_DB_DROP_LEAF, "DB-009",
          "the logo takes a drop: links go in the Deskbar folder");
    check(tiku_trk_deskbar_drop_at(db, f.x + 10, f.y + 34, &which) ==
          TIKU_TRK_DB_DROP_TEAM && which == 0, "DB-035",
          "the first application row is a target, and says which app");
    check(tiku_trk_deskbar_drop_at(db, f.x + 10, f.y + 55, &which) ==
          TIKU_TRK_DB_DROP_TEAM && which == 1, "DB-035",
          "and so is the second");
    /* The unpainted gutter between two rows belongs to neither of them,
     * the same way a menu's seam does: a drop there is a drop on the bar. */
    check(tiku_trk_deskbar_drop_at(db, f.x + 10, f.y + 50, &which) !=
          TIKU_TRK_DB_DROP_TEAM, "DB-035",
          "the seam between two application rows is not either of them");
    check(tiku_trk_deskbar_drop_at(db, f.x - 40, f.y + 34, &which) ==
          TIKU_TRK_DB_DROP_NONE, "DB-035",
          "while beside the bar is not a Deskbar drop at all");

    printf("\nwhat an application will take (DB-035)\n");
    check(tiku_trk_deskbar_team_accepts(db, tracker, txt, 1, 0), "DB-035",
          "Tracker opens anything: it is what is asked when nothing else "
          "claims a file");
    check(tiku_trk_deskbar_team_accepts(db, editor, txt, 1, 0), "DB-035",
          "an application takes a type it declares");
    check(!tiku_trk_deskbar_team_accepts(db, 2, txt, 1, 0), "DB-035",
          "and refuses one it does not -- a drop that appears to work and "
          "does nothing is worse than one that is plainly unavailable");
    check(tiku_trk_deskbar_team_accepts(db, 2, txt, 1, 1), "DB-035",
          "unless Ctrl forces it");
    check(!tiku_trk_deskbar_team_accepts(db, editor, mix, 2, 0), "DB-035",
          "EVERY type must be one it opens: a mixed selection it can only "
          "half read is a drop that half works");
    check(!tiku_trk_deskbar_team_accepts(db, editor, NULL, 0, 0), "DB-035",
          "and knowing nothing about the types is a refusal, not a guess");

    printf("\nthe menu while a drag is in flight (DB-008)\n");
    check(tiku_trk_deskbar_item_enabled(db, 1), "DB-008",
          "a fixed item is available ordinarily");
    tiku_trk_deskbar_set_dragging(db, 1);
    check(!tiku_trk_deskbar_item_enabled(db, 1), "DB-008",
          "and goes dead while dragging: About and Power off are not drop "
          "targets, and leaving them live invites a drop that does "
          "something unrelated");
    check(tiku_trk_deskbar_item_enabled(db, 0), "DB-008",
          "while folders and recents stay live -- those are exactly the "
          "targets the menu exists to offer during a drag");
    tiku_trk_deskbar_set_dragging(db, 0);
    check(tiku_trk_deskbar_item_enabled(db, 1), "DB-008",
          "and it comes back when the drag ends");

    printf("\nwhat a drop launches (DB-066, DB-075)\n");
    memset(&m, 0, sizeof m);
    m.kind = TIKU_TRK_KIND_FILE;
    check(tiku_trk_deskbar_launchable(&m), "DB-066", "a file is launched");
    m.kind = TIKU_TRK_KIND_DIRECTORY;
    check(!tiku_trk_deskbar_launchable(&m), "DB-066",
          "a directory is SILENTLY ignored -- launching one has no meaning "
          "and complaining about an accident would be noise");
    m.kind = TIKU_TRK_KIND_VOLUME;
    check(!tiku_trk_deskbar_launchable(&m), "DB-075",
          "and neither is a volume, for the same reason");

    tiku_trk_deskbar_free(db);
    tiku_trk_backend_close(b);
    printf("\n%s\n", fails ? "FAILURES" : "Deskbar drops hold");
    return fails ? 1 : 0;
}
