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
#include <unistd.h>

#include "tiku_trk_deskbar.h"
#include "tiku_trk_input.h"
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

    printf("replicant persistence and removal (DB-039, DB-040)\n");
    {
        const char *path = "/tmp/trk_deskbar_replicant";
        FILE *out = fopen(path, "w");
        tiku_trk_backend_t *backend = tiku_trk_backend_posix_open();
        tiku_trk_store_t *store = tiku_trk_store_memory_open();
        tiku_trk_deskbar_t *first = tiku_trk_deskbar_new(backend);
        tiku_trk_deskbar_t *second = tiku_trk_deskbar_new(backend);

        if (out != NULL) { fputs("42\n", out); fclose(out); }
        tiku_trk_deskbar_set_store(first, store);
        check(tiku_trk_deskbar_add_replicant(first, "answer", path) == 0,
              "DB-039", "adding a replicant records it in the store");
        tiku_trk_deskbar_set_store(second, store);
        check(tiku_trk_deskbar_load_replicants(second) == 1,
              "DB-039", "a new Deskbar restores the saved replicant");
        check(tiku_trk_deskbar_remove_replicant(second, path) == 1,
              "DB-040", "removing a replicant updates the live tray");
        check(tiku_trk_deskbar_load_replicants(first) == 0, "DB-040",
              "the removal is persisted so it stays gone after reload");
        tiku_trk_deskbar_free(first);
        tiku_trk_deskbar_free(second);
        tiku_trk_store_free(store);
        tiku_trk_backend_close(backend);
        (void)unlink(path);
    }

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

    printf("\nreplicant handles and size negotiation (DB-007, DB-041)\n");
    tiku_trk_deskbar_set_show_replicants(db, 1);
    check(tiku_trk_deskbar_show_replicants(db), "DB-007",
          "the global show-replicants state is retained");
    check(tiku_trk_deskbar_set_replicant_height(db, "/sys/mem/free", 13) == 0,
          "DB-041", "a replicant taller than the tray contract is refused");
    check(tiku_trk_deskbar_set_replicant_height(db, "/sys/mem/free", 10) == 1 &&
          tiku_trk_deskbar_replicant_height(db, "/sys/mem/free") == 10,
          "DB-041", "an accepted size change is stored and relayout-safe");
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    tiku_trk_deskbar_open_menu(db);
    check(tiku_trk_deskbar_menu_keeps_open(db, f.x - 10, f.y + 10), "DB-036",
          "the open menu keeps tracking through its 20px pointer margin");
    check(tiku_trk_deskbar_quit_policy(1, 1) == 1 &&
          tiku_trk_deskbar_quit_policy(0, 1) == 0 &&
          tiku_trk_deskbar_quit_policy(0, 0) == 0,
          "DB-053", "the bar refuses quit while Cmd+Q closes only preferences");
    tiku_trk_deskbar_close_menu(db);

    printf("\ncalendar steppers and selection (DB-046)\n");
    tiku_trk_deskbar_set_width(db, TIKU_TRK_DB_MIN_W);
    tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
    tiku_trk_deskbar_draw(db, s);
    f = tiku_trk_deskbar_frame(db);
    check(tiku_trk_deskbar_click(db, f.x + 4, f.y + f.h - 4, 0, 1, 1,
                                 &(tiku_trk_db_pick_t){0}) == 1 &&
          tiku_trk_deskbar_calendar_open(db), "DB-046",
          "clicking the clock opens a calendar popup");
    {
        int month, old_month, day;
        tiku_trk_db_pick_t pick;

        tiku_trk_deskbar_calendar_date(db, NULL, &month, NULL);
        old_month = month;
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, f.x - 135, f.y + f.h - 140,
                                     0, 1, 1, &pick);
        tiku_trk_deskbar_calendar_date(db, NULL, &month, NULL);
        check(month == (old_month == 12 ? 1 : old_month + 1), "DB-046",
              "the month stepper advances the displayed month");
        (void)tiku_trk_deskbar_click(db, f.x - 180 + 6 + 2 * 25 + 5,
                                     f.y + f.h - 150 + 22 + 2 * 20 + 5,
                                     0, 1, 1, &pick);
        tiku_trk_deskbar_calendar_date(db, NULL, NULL, &day);
        check(day >= 1 && day <= 31, "DB-046",
              "clicking a valid calendar cell changes the selected day");
        tiku_trk_deskbar_close_calendar(db);
    }

    printf("\nminimising one window, and the whole application (CW-014)\n");
    {
        tiku_trk_dbwin_t wins[2];
        tiku_trk_db_pick_t pick;
        tiku_desk_rect_t bar;
        int y, row_y = -1;

        tiku_trk_deskbar_set_dock(db, TIKU_TRK_DB_RIGHT_EXPANDED);
        memset(wins, 0, sizeof wins);
        snprintf(wins[0].title, sizeof wins[0].title, "%s", "first");
        wins[0].tag = (void *)1;
        snprintf(wins[1].title, sizeof wins[1].title, "%s", "second");
        wins[1].tag = (void *)2;
        tiku_trk_deskbar_set_windows(db, 0, wins, 2, 1);
        tiku_trk_deskbar_draw(db, s);
        bar = tiku_trk_deskbar_frame(db);
        /* Found by asking the bar rather than by arithmetic over its
         * constants: where a row sits is the bar's business. */
        for (y = bar.y; y < bar.y + bar.h && row_y < 0; y++) {
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(db, bar.x + 20, y, TIKU_TRK_MOD_CMD,
                                         1, 1, &pick);
            if (pick.what == TIKU_TRK_DB_MINIMIZE_WINDOW &&
                pick.tag == (void *)1) {
                row_y = y;
            }
        }
        check(row_y >= 0, "CW-014",
              "a modified click on a window row minimises that window");

        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, row_y,
                                     TIKU_TRK_MOD_CMD | TIKU_DESK_MOD_OPTION,
                                     1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_HIDE_ALL, "CW-014",
              "and Option on that same gesture takes the whole application "
              "down, rather than only the window under the pointer");
        tiku_trk_deskbar_set_windows(db, 0, NULL, 0, 0);
    }

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


    printf("\nthe roster's other half: instances and removal (DB-015)\n");
    {
        int changed = 0, t1, t2;
        long live1[1] = { 100 };

        /* The earlier scenes filled the roster; removal clears the stage
         * and is itself the API a kill uses (DB-020). */
        while (tiku_trk_deskbar_team_count(db) > 1) {
            tiku_trk_deskbar_remove_team(db, 1);
        }
        check(tiku_trk_deskbar_team_count(db) == 1, "DB-015",
              "an entry can be removed outright");
        t1 = tiku_trk_deskbar_add_team_pid(db, "editor", 100, "/bin/ed",
                                           &changed);
        t2 = tiku_trk_deskbar_add_team_pid(db, "EDITOR", 200, "/bin/ed",
                                           &changed);
        check(t1 >= 0 && t1 == t2, "DB-015",
              "a second instance joins the existing entry, matched "
              "without regard to case");
        check(tiku_trk_deskbar_prune_teams(db, live1, 1) &&
              tiku_trk_deskbar_team_count(db) >= 1, "DB-015",
              "one instance quitting changes the roster but not the row");
        {
            int still = 0, k;

            for (k = 0; k < tiku_trk_deskbar_team_count(db); k++) {
                if (strcasecmp(tiku_trk_deskbar_team_name(db, k),
                               "editor") == 0) {
                    still = 1;
                }
            }
            check(still, "DB-015", "the entry stands while one lives");
        }
        (void)tiku_trk_deskbar_prune_teams(db, NULL, 0);
        {
            int still = 0, k;

            for (k = 0; k < tiku_trk_deskbar_team_count(db); k++) {
                if (strcasecmp(tiku_trk_deskbar_team_name(db, k),
                               "editor") == 0) {
                    still = 1;
                }
            }
            check(!still, "DB-015",
                  "the entry disappears only when its LAST instance quits");
        }
        check(tiku_trk_deskbar_team_count(db) >= 1, "DB-015",
              "the shell-seeded team is never pruned away");
    }

    printf("\nwhat a click on an application asks for (DB-019/020/021)\n");
    {
        tiku_trk_db_pick_t pick;
        tiku_desk_rect_t bar;
        int y, team_y = -1, changed = 0;

        (void)tiku_trk_deskbar_add_team_pid(db, "editor", 300, "/bin/ed",
                                            &changed);
        tiku_trk_deskbar_draw(db, s);
        bar = tiku_trk_deskbar_frame(db);
        for (y = bar.y; y < bar.y + bar.h && team_y < 0; y++) {
            memset(&pick, 0, sizeof pick);
            (void)tiku_trk_deskbar_click(db, bar.x + 20, y,
                                         TIKU_TRK_MOD_CMD, 1, 1, &pick);
            if (pick.what == TIKU_TRK_DB_SHOW_ALL) {
                team_y = y;
            }
        }
        check(team_y >= 0, "DB-021",
              "Ctrl+click on an application asks for all its windows "
              "to the front");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y,
                                     TIKU_TRK_MOD_CMD | TIKU_TRK_MOD_SHIFT,
                                     1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_HIDE_ALL, "DB-021",
              "and Ctrl+Shift+click asks to minimize them all");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y, 0u, 2, 1,
                                     &pick);
        check(pick.what == TIKU_TRK_DB_LAUNCH_TEAM, "DB-021",
              "middle-click asks for a new instance");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y,
                                     TIKU_TRK_MOD_SHIFT, 2, 1, &pick);
        check(pick.what == TIKU_TRK_DB_NONE, "DB-021",
              "but not with Shift held, exactly as the source requires");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y, 0u, 1, 2,
                                     &pick);
        check(pick.what == TIKU_TRK_DB_ACTIVATE_TEAM, "DB-019",
              "a double-click activates the application; the single "
              "click never did");
        memset(&pick, 0, sizeof pick);
        (void)tiku_trk_deskbar_click(db, bar.x + 20, team_y,
                                     TIKU_TRK_MOD_CMD | TIKU_TRK_MOD_SHIFT |
                                     TIKU_DESK_MOD_OPTION, 1, 1, &pick);
        check(pick.what == TIKU_TRK_DB_KILL_TEAM && pick.team >= 0,
              "DB-020", "and the full grip asks for the kill, naming "
              "the row");
    }

    tiku_trk_deskbar_free(db);
    tiku_desk_surface_free(s);
    printf("\n%s\n", fails ? "FAILURES" : "the bar sits where it is put");
    return fails ? 1 : 0;
}
