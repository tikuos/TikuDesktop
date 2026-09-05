/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_soundprefs.c - the sound settings, from their defaults through a
 * file and into the kit.
 *
 * The defaults are the chime, the beep and the shutdown on with the
 * window ticks off; a change round-trips through the settings file as
 * "sound.<event>" and "sound.volume" lines; apply() is what the kit then
 * answers for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_trk_soundprefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief Whether the file at @p path has a line reading @p line. */
static int
file_has(const char *path, const char *line)
{
    FILE *f = fopen(path, "r");
    char buf[256];
    int hit = 0;

    if (f == NULL) {
        return 0;
    }
    while (fgets(buf, sizeof buf, f) != NULL) {
        buf[strcspn(buf, "\r\n")] = '\0';
        if (strcmp(buf, line) == 0) {
            hit = 1;
        }
    }
    fclose(f);
    return hit;
}

int
main(void)
{
    tiku_trk_prefs_t prefs, again;
    char path[256];
    int guard, i;

    snprintf(path, sizeof path, "/tmp/trk_soundprefs_%d", (int)getpid());
    printf("the defaults\n");
    tiku_trk_prefs_init(&prefs);
    check(strcmp(tiku_trk_soundprefs_key(TIKU_SOUND_WINDOW_OPEN),
                 "sound.window-open") == 0,
          "a setting is named after the kit's own name for the moment");
    check(tiku_trk_soundprefs_enabled(&prefs, TIKU_SOUND_STARTUP) &&
          tiku_trk_soundprefs_enabled(&prefs, TIKU_SOUND_BEEP) &&
          tiku_trk_soundprefs_enabled(&prefs, TIKU_SOUND_SHUTDOWN),
          "the chime, the beep and the shutdown start on");
    check(!tiku_trk_soundprefs_enabled(&prefs, TIKU_SOUND_WINDOW_OPEN) &&
          !tiku_trk_soundprefs_enabled(&prefs, TIKU_SOUND_WINDOW_CLOSE) &&
          !tiku_trk_soundprefs_enabled(&prefs, TIKU_SOUND_WINDOW_ACTIVATE),
          "the window ticks start off");
    check(tiku_trk_soundprefs_volume(&prefs) == 80, "the volume starts at 80");
    check(!tiku_trk_soundprefs_defaultable(&prefs),
          "and nothing is there to put back");

    printf("a change\n");
    tiku_trk_soundprefs_set_enabled(&prefs, TIKU_SOUND_BEEP, 0);
    tiku_trk_soundprefs_set_enabled(&prefs, TIKU_SOUND_WINDOW_OPEN, 1);
    check(tiku_trk_soundprefs_step_volume(&prefs, 0) &&
          tiku_trk_soundprefs_volume(&prefs) == 70,
          "a step down takes the volume from 80 to 70");
    for (guard = 0; guard < 20 && tiku_trk_soundprefs_step_volume(&prefs, 1);
         guard++) {
    }
    check(tiku_trk_soundprefs_volume(&prefs) == 100 &&
          !tiku_trk_soundprefs_step_volume(&prefs, 1),
          "and up stops at 100");
    check(tiku_trk_soundprefs_defaultable(&prefs),
          "now there is something to put back");

    printf("the file\n");
    check(tiku_trk_prefs_save(&prefs, path) == 0, "the settings save");
    check(file_has(path, "sound.beep off") &&
          file_has(path, "sound.window-open on") &&
          file_has(path, "sound.volume 100"),
          "as sound.<event> and sound.volume lines");
    tiku_trk_prefs_init(&again);
    check(tiku_trk_prefs_load(&again, path) == 0 &&
          !tiku_trk_soundprefs_enabled(&again, TIKU_SOUND_BEEP) &&
          tiku_trk_soundprefs_enabled(&again, TIKU_SOUND_WINDOW_OPEN) &&
          tiku_trk_soundprefs_volume(&again) == 100,
          "which read back as they were written");

    printf("the kit\n");
    tiku_sound_init(NULL);
    check(tiku_sound_enabled(TIKU_SOUND_BEEP) && tiku_sound_volume() == 100,
          "before apply the kit is on its own defaults");
    tiku_trk_soundprefs_apply(&again);
    check(!tiku_sound_enabled(TIKU_SOUND_BEEP) &&
          tiku_sound_enabled(TIKU_SOUND_WINDOW_OPEN) &&
          tiku_sound_enabled(TIKU_SOUND_STARTUP),
          "after it the kit says what the file says");
    tiku_trk_soundprefs_defaults(&again);
    tiku_trk_soundprefs_apply(&again);
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        if (tiku_sound_enabled((tiku_sound_event_t)i) !=
            tiku_trk_soundprefs_enabled(&prefs, i)) {
            break;
        }
    }
    check(tiku_sound_enabled(TIKU_SOUND_BEEP) &&
          !tiku_sound_enabled(TIKU_SOUND_WINDOW_OPEN) &&
          tiku_sound_volume() == 80,
          "Defaults puts every moment and the volume back");
    tiku_sound_free();
    tiku_trk_prefs_free(&prefs);
    tiku_trk_prefs_free(&again);
    unlink(path);
    printf("%s\n", fails ? "FAILURES" : "the sound settings round-trip");
    return fails ? 1 : 0;
}
