/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_fontprefs.c - the letters the interface is drawn in.
 *
 * What Defaults and Revert mean for a window that owns two of a record's
 * seventeen fields, and what saving those two does to the other fifteen.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_deskbar_prefs.h"
#include "tiku_trk_fontprefs.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief Climb the ladder, bounded: a wrapping one must fail, not hang. */
static void
climb(tiku_trk_fontprefs_t *prefs, int up)
{
    int guard;

    for (guard = 0; guard < 8; guard++) {
        if (!tiku_trk_fontprefs_step_size(prefs, up)) {
            return;
        }
    }
}

int
main(void)
{
    tiku_trk_fontprefs_t prefs;
    tiku_trk_store_t *store = tiku_trk_store_memory_open();

    printf("the letters the interface is drawn in (DEV-010)\n");

    tiku_trk_fontprefs_init(&prefs);
    check(prefs.value.family == 0 && prefs.value.size == 12, "DEV-010",
          "an interface nobody has dressed is the first family at 12 px");
    check(!tiku_trk_fontprefs_defaultable(&prefs) &&
          !tiku_trk_fontprefs_revertable(&prefs), "DEV-010",
          "so neither Defaults nor Revert has anything to do");

    check(tiku_trk_fontprefs_family_count() >= 2 &&
          tiku_trk_fontprefs_family_name(0) != NULL, "DEV-010",
          "there is more than one family, and each says what it is called");
    check(!tiku_trk_fontprefs_set_family(&prefs, 0), "DEV-010",
          "choosing the family already in force is not a change");
    check(tiku_trk_fontprefs_set_family(&prefs, 1) &&
          tiku_trk_fontprefs_defaultable(&prefs) &&
          tiku_trk_fontprefs_revertable(&prefs), "DEV-010",
          "choosing another one gives both buttons something to do");
    check(!tiku_trk_fontprefs_set_family(&prefs,
              tiku_trk_fontprefs_family_count()) &&
          prefs.value.family == 1, "DEV-010",
          "and a family that is not there is not chosen at all");
    tiku_trk_fontprefs_revert(&prefs);
    check(prefs.value.family == 0, "DEV-010",
          "Revert puts back the family the window opened on");

    climb(&prefs, 1);
    check(prefs.value.size == TIKU_TRK_FONT_MAX, "DEV-010",
          "the size climbs its ladder and stops at the top");
    climb(&prefs, 0);
    check(prefs.value.size == TIKU_TRK_FONT_MIN, "DEV-010",
          "and the same at the foot, rather than wrapping round");

    /* The two fields this window owns sit in a record of seventeen. */
    {
        tiku_trk_deskbar_prefs_t record;
        tiku_trk_fontprefs_t again;

        tiku_trk_deskbar_prefs_init(&record);
        record.value.icon_size = 24;
        record.value.auto_hide = 1;
        record.value.recent_count[0] = 7;
        (void)tiku_trk_deskbar_prefs_save(&record, store);

        (void)tiku_trk_fontprefs_load(&again, store);
        check(again.value.family == 0 && again.value.size == 12, "DEV-010",
              "a Fonts window opens on what the Deskbar's record says");
        (void)tiku_trk_fontprefs_set_family(&again, 1);
        (void)tiku_trk_fontprefs_step_size(&again, 1);
        check(tiku_trk_fontprefs_save(&again, store), "DEV-010",
              "what it was told is written back");

        tiku_trk_deskbar_prefs_init(&record);
        (void)tiku_trk_deskbar_prefs_load(&record, store);
        check(record.value.font_family == 1 && record.value.font_size == 14,
              "DEV-010", "and the Deskbar reads the fonts it was given");
        check(record.value.icon_size == 24 && record.value.auto_hide == 1 &&
              record.value.recent_count[0] == 7, "DEV-010",
              "while the fifteen settings it did NOT own are untouched");
    }

    /* Defaults, from a window opened on something else entirely. */
    {
        tiku_trk_fontprefs_t opened;

        (void)tiku_trk_fontprefs_load(&opened, store);
        check(!tiku_trk_fontprefs_revertable(&opened) &&
              tiku_trk_fontprefs_defaultable(&opened), "DEV-010",
              "a window opening on a dressed interface can only default");
        tiku_trk_fontprefs_defaults(&opened);
        check(opened.value.family == 0 && opened.value.size == 12 &&
              tiku_trk_fontprefs_revertable(&opened), "DEV-010",
              "Defaults undresses it, which Revert can then undo");
        tiku_trk_fontprefs_revert(&opened);
        check(opened.value.family == 1 && opened.value.size == 14,
              "DEV-010", "and does");
    }

    check(!tiku_trk_fontprefs_save(&prefs, NULL) &&
          !tiku_trk_fontprefs_load(&prefs, NULL), "DEV-010",
          "with nowhere to keep them, the defaults stand and nothing dies");

    tiku_trk_store_free(store);
    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe interface's letters hold\n");
    return 0;
}
