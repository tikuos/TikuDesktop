/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_deskbar_dock.c - where the bar sits, how big it is, and when it hides.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskbar_prefs.h"

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
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(NULL);
    tiku_desk_surface_t *s = tiku_desk_surface_new(1000, 680,
                                                   TIKU_DESK_C_BACKDROP);
    tiku_desk_rect_t f;

    printf("which edge, and how far into the corner (DB-048)\n");
    check(tiku_trk_deskbar_dock_at(4, 300, 1000, 680,
                                   TIKU_TRK_DB_RIGHT_EXPANDED) ==
          TIKU_TRK_DB_LEFT_MINI, "DB-048",
          "near the left edge, low down: the collapsed left dock");
    check(tiku_trk_deskbar_dock_at(4, 40, 1000, 680,
                                   TIKU_TRK_DB_RIGHT_EXPANDED) ==
          TIKU_TRK_DB_LEFT_EXPANDED, "DB-048",
          "near the left edge, high up: the expanded one -- the same split "
          "the original's little monitor makes between a tall strip and a "
          "corner");
    check(tiku_trk_deskbar_dock_at(500, 4, 1000, 680,
                                   TIKU_TRK_DB_RIGHT_EXPANDED) ==
          TIKU_TRK_DB_TOP, "DB-048", "near the top: along it");
    check(tiku_trk_deskbar_dock_at(500, 676, 1000, 680,
                                   TIKU_TRK_DB_RIGHT_EXPANDED) ==
          TIKU_TRK_DB_BOTTOM, "DB-048", "near the bottom: along that");
    check(tiku_trk_deskbar_dock_at(500, 340, 1000, 680,
                                   TIKU_TRK_DB_LEFT_EXPANDED) ==
          TIKU_TRK_DB_LEFT_EXPANDED, "DB-048",
          "and a drop in the MIDDLE keeps the dock it started from: a drag "
          "that ended nowhere in particular is a mis-drag, and moving the "
          "bar for it would be worse than ignoring it");

    printf("\nthe frame follows the dock (DB-033)\n");
    (void)tiku_trk_deskbar_add_team(db, "one");
    (void)tiku_trk_deskbar_add_team(db, "two");
    (void)tiku_trk_deskbar_add_replicant(db, "free", "/sys/mem/free");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.x + f.w == 1000, "DB-033",
          "docked right it sits against the right edge");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_LEFT_MINI);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.x == 0, "DB-037", "the collapsed left dock is against the left");
    {
        int mini_h = f.h;

        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_LEFT_EXPANDED);
        tiku_trk_deskbar_draw(db, s);
        f = tiku_trk_deskbar_frame(db);
        check(f.h >= mini_h, "DB-037",
              "and it is no taller than the expanded one, since collapsing "
              "is what drops the application list");
    }
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_TOP);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.y == 0 && f.w == 1000 && f.h < 100, "DB-033",
          "along the top it spans the screen and is one row deep -- a "
          "horizontal bar is a different shape, not a rotated one");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_BOTTOM);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.y + f.h == 680, "DB-033", "and along the bottom it sits on it");

    printf("\nthe width the user sets (DB-050)\n");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    check(tiku_trk_deskbar_set_width(db, 200) == 200, "DB-050",
          "the short axis is the user's to set");
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.w == 200 && f.x == 800, "DB-050",
          "and the bar keeps its edge as it grows, rather than growing off "
          "the screen");
    check(tiku_trk_deskbar_set_width(db, 10) == TIKU_TRK_DB_MIN_W, "DB-050",
          "below the minimum a row's text is unreadable, so it clamps");
    check(tiku_trk_deskbar_set_width(db, 9000) == TIKU_TRK_DB_MAX_W,
          "DB-050",
          "and above the maximum the bar would be taking the screen from "
          "the windows it exists to list");

    printf("\nscrolling the list (DB-034)\n");
    check(!tiku_trk_deskbar_scrollable(db), "DB-034",
          "two applications fit, so there is nothing to scroll");
    check(tiku_trk_deskbar_scroll(db, 1) == 0 &&
          tiku_trk_deskbar_scroll_at(db) == 0, "DB-034",
          "and the wheel does nothing rather than sliding a list that is "
          "entirely visible away");
    {
        /* A SHORT screen rather than more applications: the roster is
         * bounded, and what makes a list overflow is the room it has. */
        tiku_desk_surface_t *small = tiku_desk_surface_new(400, 150,
                                              TIKU_DESK_C_BACKDROP);
        int i;
        char name[16];

        for (i = 0; i < 6; i++) {
            snprintf(name, sizeof name, "app%d", i);
            (void)tiku_trk_deskbar_add_team(db, name);
        }
        tiku_trk_deskbar_draw(db, small);
        check(tiku_trk_deskbar_scrollable(db), "DB-034",
              "enough of them and the list runs past the tray");
        check(tiku_trk_deskbar_scroll(db, 1) == 1 &&
              tiku_trk_deskbar_scroll_at(db) == 1, "DB-034",
              "then the wheel moves it");
        check(tiku_trk_deskbar_scroll(db, -5) == 1 &&
              tiku_trk_deskbar_scroll_at(db) == 0, "DB-034",
              "and it stops at the top rather than scrolling past it");
        tiku_trk_deskbar_draw(db, s);
        check(!tiku_trk_deskbar_scrollable(db), "DB-034",
              "back on a screen with room, there is nothing to scroll "
              "again -- overflow is about the room, not the count");
        tiku_desk_surface_free(small);
    }

    printf("\nhiding, and coming back (DB-051)\n");
    tiku_trk_deskbar_set_width(db, TIKU_TRK_DB_MIN_W);
    tiku_trk_deskbar_draw(db, s);
    check(tiku_trk_deskbar_shown(db), "DB-051",
          "with auto-hide off the bar is always out");
    tiku_trk_deskbar_set_autohide(db, 1);
    check(!tiku_trk_deskbar_shown(db), "DB-051",
          "turned on, and with the pointer elsewhere, it is not");
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(f.w == TIKU_TRK_DB_SLIVER && f.x + f.w == 1000, "DB-051",
          "what is left is a sliver against its own edge");
    check(tiku_trk_deskbar_pointer(db, 999, 300) == 1 &&
          tiku_trk_deskbar_shown(db), "DB-051",
          "touching that sliver brings it back -- judged against the edge "
          "and not the hidden frame, or the pointer could never reach it");
    check(tiku_trk_deskbar_pointer(db, 100, 300) == 1 &&
          !tiku_trk_deskbar_shown(db), "DB-051",
          "and leaving puts it away again");
    tiku_trk_deskbar_open_menu(db);
    check(tiku_trk_deskbar_shown(db), "DB-051",
          "except while its own menu is open: a bar that slid away under "
          "its menu would take the menu with it");
    tiku_trk_deskbar_close_menu(db);
    tiku_trk_deskbar_set_autohide(db, 0);

    printf("\nraising (DB-052)\n");
    check(tiku_trk_deskbar_raised(db), "DB-052",
          "without auto-raise the bar is always above the windows, which "
          "is what it has always been");
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    tiku_trk_deskbar_set_autoraise(db, 1);
    (void)tiku_trk_deskbar_pointer(db, 100, 300);
    check(!tiku_trk_deskbar_raised(db), "DB-052",
          "with it on, a window may cover the bar");
    (void)tiku_trk_deskbar_pointer(db, f.x + 2, f.y + 2);
    check(tiku_trk_deskbar_raised(db), "DB-052",
          "until the user reaches for it");

    printf("\nlive preferences (DB-016, DB-017, DB-073)\n");
    {
        tiku_trk_deskbar_t *ordered = tiku_trk_deskbar_new(NULL);
        tiku_trk_deskbar_settings_t settings;
        int small_h, large_h;

        (void)tiku_trk_deskbar_add_team(ordered, "Zulu");
        (void)tiku_trk_deskbar_add_team(ordered, "Tracker");
        (void)tiku_trk_deskbar_add_team(ordered, "Alpha");
        tiku_trk_deskbar_settings_default(&settings);
        settings.sort_apps = 1;
        tiku_trk_deskbar_apply_settings(ordered, &settings);
        check(strcmp(tiku_trk_deskbar_team_name(ordered, 0), "Tracker") == 0 &&
              strcmp(tiku_trk_deskbar_team_name(ordered, 1), "Alpha") == 0 &&
              strcmp(tiku_trk_deskbar_team_name(ordered, 2), "Zulu") == 0,
              "DB-016", "Tracker pins first and the remaining applications "
              "sort by name when those preferences are selected");
        tiku_trk_deskbar_draw(ordered, s);
        small_h = tiku_trk_deskbar_frame(ordered).h;
        settings.icon_size = 96;
        tiku_trk_deskbar_apply_settings(ordered, &settings);
        tiku_trk_deskbar_draw(ordered, s);
        large_h = tiku_trk_deskbar_frame(ordered).h;
        check(large_h > small_h, "DB-017",
              "changing the application icon size re-lays out the bar");
        settings.always_on_top = 1;
        settings.auto_raise = 1;
        tiku_trk_deskbar_apply_settings(ordered, &settings);
        (void)tiku_trk_deskbar_pointer(ordered, 100, 300);
        check(tiku_trk_deskbar_raised(ordered), "DB-073",
              "always-on-top wins over auto-raise and changes live z-order");
        tiku_trk_deskbar_free(ordered);
    }

    tiku_trk_deskbar_free(db);
    tiku_desk_surface_free(s);
    printf("\n%s\n", fails ? "FAILURES" : "the bar sits where it is put");
    return fails ? 1 : 0;
}
