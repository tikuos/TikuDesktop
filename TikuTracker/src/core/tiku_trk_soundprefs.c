/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_soundprefs.c - the sound settings, read and written by name.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_soundprefs.h"

#include <stdio.h>
#include <string.h>

#define VOLUME_KEY "sound.volume"

const char *
tiku_trk_soundprefs_key(tiku_sound_event_t event)
{
    /* One slot per event: the names are handed out by pointer and two
     * may be in hand at once, as when a table is being drawn. */
    static char keys[TIKU_SOUND_EVENTS][32];

    if (event < 0 || event >= TIKU_SOUND_EVENTS) {
        return "";
    }
    snprintf(keys[event], sizeof keys[event], "sound.%s",
             tiku_sound_name(event));
    return keys[event];
}

int
tiku_trk_soundprefs_enabled(tiku_trk_prefs_t *prefs, tiku_sound_event_t event)
{
    return tiku_trk_prefs_get(prefs, tiku_trk_soundprefs_key(event)) != 0;
}

void
tiku_trk_soundprefs_set_enabled(tiku_trk_prefs_t *prefs,
                                tiku_sound_event_t event, int on)
{
    tiku_trk_prefs_set(prefs, tiku_trk_soundprefs_key(event), on ? 1 : 0);
}

int
tiku_trk_soundprefs_volume(tiku_trk_prefs_t *prefs)
{
    int32_t v = tiku_trk_prefs_get(prefs, VOLUME_KEY);

    return (v < 0) ? 0 : (v > 100) ? 100 : (int)v;
}

int
tiku_trk_soundprefs_step_volume(tiku_trk_prefs_t *prefs, int up)
{
    int v = tiku_trk_soundprefs_volume(prefs);
    int to = up ? v + TIKU_TRK_SOUND_VOLUME_STEP
                : v - TIKU_TRK_SOUND_VOLUME_STEP;

    if (to < 0 || to > 100) {
        return 0;
    }
    tiku_trk_prefs_set(prefs, VOLUME_KEY, to);
    return 1;
}

int
tiku_trk_soundprefs_defaultable(tiku_trk_prefs_t *prefs)
{
    int i;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        const tiku_trk_pref_t *pr =
            tiku_trk_prefs_find(prefs, tiku_trk_soundprefs_key(i));

        if (pr != NULL && pr->value != pr->def) {
            return 1;
        }
    }
    {
        const tiku_trk_pref_t *pr = tiku_trk_prefs_find(prefs, VOLUME_KEY);

        return pr != NULL && pr->value != pr->def;
    }
}

void
tiku_trk_soundprefs_defaults(tiku_trk_prefs_t *prefs)
{
    int i;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        tiku_trk_pref_t *pr =
            tiku_trk_prefs_find(prefs, tiku_trk_soundprefs_key(i));

        if (pr != NULL) {
            pr->value = pr->def;
        }
    }
    {
        tiku_trk_pref_t *pr = tiku_trk_prefs_find(prefs, VOLUME_KEY);

        if (pr != NULL) {
            pr->value = pr->def;
        }
    }
}

void
tiku_trk_soundprefs_apply(tiku_trk_prefs_t *prefs)
{
    int i;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        tiku_sound_set_enabled(i, tiku_trk_soundprefs_enabled(prefs, i));
    }
    tiku_sound_set_volume(tiku_trk_soundprefs_volume(prefs));
}
