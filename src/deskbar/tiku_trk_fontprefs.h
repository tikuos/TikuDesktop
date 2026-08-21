/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fontprefs.h - the letters the interface is drawn in.
 *
 * Two of the Deskbar record's fields, given a model of their own: a Fonts
 * window offers those two and nothing else, so its Defaults and Revert
 * must mean the fonts rather than every Deskbar setting there is.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FONTPREFS_H_
#define TIKU_TRK_FONTPREFS_H_

#include "tiku_trk_state.h"

/** @brief The sizes the interface is baked at, smallest first. */
#define TIKU_TRK_FONT_MIN  10
#define TIKU_TRK_FONT_MAX  16
#define TIKU_TRK_FONT_STEP 2

typedef struct {
    int family;
    int size;
} tiku_trk_fontprefs_values_t;

typedef struct {
    tiku_trk_fontprefs_values_t value;
    tiku_trk_fontprefs_values_t opened;
} tiku_trk_fontprefs_t;

void tiku_trk_fontprefs_default(tiku_trk_fontprefs_values_t *values);
void tiku_trk_fontprefs_init(tiku_trk_fontprefs_t *prefs);

/**
 * @brief Read the two values out of the Deskbar's record.
 *
 * @return 1 when the record said, 0 when the defaults stand.
 */
int tiku_trk_fontprefs_load(tiku_trk_fontprefs_t *prefs,
                            tiku_trk_store_t *store);

/**
 * @brief Write them back, leaving every other setting in the record.
 *
 * The record holds fifteen other things; a Fonts window that wrote a
 * whole record from what it knows would take the rest back to default.
 *
 * @return 1 when the record was written.
 */
int tiku_trk_fontprefs_save(const tiku_trk_fontprefs_t *prefs,
                            tiku_trk_store_t *store);

void tiku_trk_fontprefs_mark_opened(tiku_trk_fontprefs_t *prefs);
int tiku_trk_fontprefs_defaultable(const tiku_trk_fontprefs_t *prefs);
int tiku_trk_fontprefs_revertable(const tiku_trk_fontprefs_t *prefs);
void tiku_trk_fontprefs_defaults(tiku_trk_fontprefs_t *prefs);
void tiku_trk_fontprefs_revert(tiku_trk_fontprefs_t *prefs);

/** @brief Draw the interface in family @p family.  @return 1 when it moved. */
int tiku_trk_fontprefs_set_family(tiku_trk_fontprefs_t *prefs, int family);

/**
 * @brief Step the size one rung.  @return 1 when it moved.
 *
 * @param up Nonzero to grow.  The ends stop rather than wrap.
 */
int tiku_trk_fontprefs_step_size(tiku_trk_fontprefs_t *prefs, int up);

/** @brief How many families there are to choose between. */
int tiku_trk_fontprefs_family_count(void);

/** @brief What family @p family is called, or NULL past the last. */
const char *tiku_trk_fontprefs_family_name(int family);

#endif /* TIKU_TRK_FONTPREFS_H_ */
