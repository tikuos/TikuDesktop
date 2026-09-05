/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_soundprefs.h - which moments sound, and how loud, as settings.
 *
 * One line per event in the settings file ("sound.beep on") and one for
 * the volume ("sound.volume 80"), so the Sounds window and a hand edit
 * of the file are the same act; apply() is the only road into the kit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SOUNDPREFS_H_
#define TIKU_TRK_SOUNDPREFS_H_

#include "tiku_sound.h"
#include "tiku_trk_prefs.h"

#define TIKU_TRK_SOUND_VOLUME_STEP 10

/** @brief The setting's name for @p event: "sound." and the kit's name. */
const char *tiku_trk_soundprefs_key(tiku_sound_event_t event);

/** @brief Whether @p event is on in @p prefs. */
int tiku_trk_soundprefs_enabled(tiku_trk_prefs_t *prefs,
                                tiku_sound_event_t event);

/** @brief Turn @p event on or off in @p prefs; nothing is applied. */
void tiku_trk_soundprefs_set_enabled(tiku_trk_prefs_t *prefs,
                                     tiku_sound_event_t event, int on);

/** @brief The volume in @p prefs, clamped to 0..100. */
int tiku_trk_soundprefs_volume(tiku_trk_prefs_t *prefs);

/**
 * @brief Move the volume one step up or down.
 * @return 1 when it moved, 0 at either end.
 */
int tiku_trk_soundprefs_step_volume(tiku_trk_prefs_t *prefs, int up);

/** @brief Whether any sound setting differs from its default. */
int tiku_trk_soundprefs_defaultable(tiku_trk_prefs_t *prefs);

/** @brief Put every sound setting back to its default. */
void tiku_trk_soundprefs_defaults(tiku_trk_prefs_t *prefs);

/** @brief Push the enables and the volume in @p prefs into the kit. */
void tiku_trk_soundprefs_apply(tiku_trk_prefs_t *prefs);

#endif /* TIKU_TRK_SOUNDPREFS_H_ */
