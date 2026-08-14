/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_deskbar_prefs.c - Deskbar preference contracts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_deskbar_prefs.h"

static int fails;

static void
check(int condition, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", condition ? "PASS" : "FAIL", row, what);
    if (!condition) { fails++; }
}

int
main(void)
{
    tiku_trk_deskbar_prefs_t prefs;
    tiku_trk_store_t *store = tiku_trk_store_memory_open();
    tiku_trk_db_dock_t dock;

    tiku_trk_deskbar_prefs_init(&prefs);
    check(!tiku_trk_deskbar_prefs_defaultable(&prefs), "DB-062",
          "Defaults starts disabled when every compared value is default");
    check(!tiku_trk_deskbar_prefs_revertable(&prefs), "DB-062",
          "Revert starts disabled at the snapshot taken on open");
    prefs.value.icon_size = 99;
    prefs.value.recent_count[TIKU_TRK_DB_RECENT_DOCS] = -4;
    prefs.value.always_on_top = 1;
    prefs.value.auto_raise = 1;
    tiku_trk_deskbar_settings_normalize(&prefs.value);
    check(prefs.value.icon_size == TIKU_TRK_DB_ICON_MAX &&
          prefs.value.recent_count[TIKU_TRK_DB_RECENT_DOCS] == 0 &&
          !prefs.value.auto_raise, "DB-061",
          "ranges clamp and always-on-top forces auto-raise off");
    check(!tiku_trk_deskbar_prefs_recent_enabled(&prefs,
              TIKU_TRK_DB_RECENT_DOCS), "DB-061",
          "a zero recent count disables that type even when checked");
    check(tiku_trk_deskbar_prefs_revertable(&prefs), "DB-062",
          "Revert enables as soon as a compared value changes");
    tiku_trk_deskbar_prefs_revert(&prefs);
    check(!tiku_trk_deskbar_prefs_revertable(&prefs), "DB-062",
          "Revert restores exactly the values present when the window opened");
    prefs.value.hide_app_names = 1;
    tiku_trk_deskbar_prefs_defaults(&prefs);
    check(!prefs.value.hide_app_names &&
          !tiku_trk_deskbar_prefs_defaultable(&prefs), "DB-062",
          "Defaults restores all fifteen compared settings");

    prefs.value.icon_size = 32;
    prefs.value.auto_hide = 1;
    prefs.value.show_seconds = 1;
    prefs.value.show_day_of_week = 1;
    prefs.value.show_timezone = 1;
    check(tiku_trk_deskbar_prefs_save(&prefs, store) == 0, "DB-061",
          "the settings model writes to the durable state seam");
    tiku_trk_deskbar_prefs_init(&prefs);
    check(tiku_trk_deskbar_prefs_load(&prefs, store) == 0 &&
          prefs.value.icon_size == 32 && prefs.value.auto_hide &&
          prefs.value.show_seconds && prefs.value.show_day_of_week &&
          prefs.value.show_timezone, "DB-071",
          "the next controller restores the settings and snapshots them");

    check(tiku_trk_deskbar_prefs_expanders_enabled(&prefs,
              TIKU_TRK_DB_LEFT_EXPANDED) &&
          !tiku_trk_deskbar_prefs_expanders_enabled(&prefs,
              TIKU_TRK_DB_LEFT_MINI) &&
          !tiku_trk_deskbar_prefs_expanders_enabled(&prefs,
              TIKU_TRK_DB_TOP), "DB-061",
          "expander controls enable only for a vertical expanded bar");
    prefs.value.always_on_top = 1;
    check(!tiku_trk_deskbar_prefs_auto_raise_enabled(&prefs), "DB-061",
          "auto-raise is disabled while always-on-top is selected");

    check(tiku_trk_deskbar_prefs_dock_at(5, 5, 90, 90,
              TIKU_TRK_DB_BOTTOM) == TIKU_TRK_DB_LEFT_MINI &&
          tiku_trk_deskbar_prefs_dock_at(5, 45, 90, 90,
              TIKU_TRK_DB_BOTTOM) == TIKU_TRK_DB_LEFT_EXPANDED &&
          tiku_trk_deskbar_prefs_dock_at(45, 5, 90, 90,
              TIKU_TRK_DB_BOTTOM) == TIKU_TRK_DB_TOP &&
          tiku_trk_deskbar_prefs_dock_at(45, 85, 90, 90,
              TIKU_TRK_DB_TOP) == TIKU_TRK_DB_BOTTOM &&
          tiku_trk_deskbar_prefs_dock_at(85, 45, 90, 90,
              TIKU_TRK_DB_TOP) == TIKU_TRK_DB_RIGHT_EXPANDED &&
          tiku_trk_deskbar_prefs_dock_at(85, 85, 90, 90,
              TIKU_TRK_DB_TOP) == TIKU_TRK_DB_RIGHT_MINI, "DB-063",
          "the selector thirds map to its six dock locations");
    dock = TIKU_TRK_DB_TOP;
    check(tiku_trk_deskbar_prefs_dock_key("7", &dock) &&
          dock == TIKU_TRK_DB_LEFT_MINI &&
          tiku_trk_deskbar_prefs_dock_key("Down", &dock) &&
          dock == TIKU_TRK_DB_BOTTOM &&
          tiku_trk_deskbar_prefs_dock_key("PageUp", &dock) &&
          dock == TIKU_TRK_DB_RIGHT_MINI, "DB-063",
          "numpad and navigation keys select the same six locations");
    {
        tiku_desk_rect_t kept = tiku_trk_deskbar_prefs_frame(
            1, 120, 80, 1000, 680, 470, 548);
        tiku_desk_rect_t repaired = tiku_trk_deskbar_prefs_frame(
            1, 900, 600, 1000, 680, 470, 548);

        check(kept.x == 120 && kept.y == 80 && repaired.x == 265 &&
              repaired.y == 66, "DB-064",
              "an on-screen saved position returns while an off-screen one "
              "is refused and centered");
    }

    tiku_trk_store_free(store);
    printf("\n%s\n", fails ? "FAILURES" : "Deskbar preferences hold");
    return fails ? 1 : 0;
}
