/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskprefs.h - what the Desktop itself looks like.
 *
 * The picture behind it, how that picture is laid, and how big its icons
 * are: three settings that live on the Desktop node rather than in the
 * settings file, because they belong to the Desktop and travel with it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKPREFS_H_
#define TIKU_TRK_DESKPREFS_H_

#include "tiku_trk_background.h"
#include "tiku_trk_state.h"

/** @brief The icon sizes the Desktop offers, smallest first (PVL-003). */
#define TIKU_TRK_DESK_ICON_STEPS 6

/** @brief How many pictures the chooser will list. */
#define TIKU_TRK_DESK_PICTURES 12

typedef struct {
    char picture[TIKU_TRK_DESK_PICTURES][TIKU_TRK_PATH_MAX];
    int  count;
} tiku_trk_deskprefs_pictures_t;

typedef struct {
    tiku_trk_bg_t bg;
    int           have_bg;
    int           icon_size;
} tiku_trk_deskprefs_values_t;

typedef struct {
    tiku_trk_deskprefs_values_t value;
    tiku_trk_deskprefs_values_t opened;
} tiku_trk_deskprefs_t;

/** @brief The size at rung @p i, or 0 past the end. */
int tiku_trk_deskprefs_icon_step(int i);

void tiku_trk_deskprefs_default(tiku_trk_deskprefs_values_t *values);
void tiku_trk_deskprefs_init(tiku_trk_deskprefs_t *prefs);

/** @brief Read what @p node says about itself.  @return 1 when it said. */
int tiku_trk_deskprefs_load(tiku_trk_deskprefs_t *prefs,
                            tiku_trk_store_t *store, const char *node,
                            int icon_size);

/** @brief Write it back.  @return 1 when the record was written. */
int tiku_trk_deskprefs_save(const tiku_trk_deskprefs_t *prefs,
                            tiku_trk_store_t *store, const char *node);

void tiku_trk_deskprefs_mark_opened(tiku_trk_deskprefs_t *prefs);
int tiku_trk_deskprefs_defaultable(const tiku_trk_deskprefs_t *prefs);
int tiku_trk_deskprefs_revertable(const tiku_trk_deskprefs_t *prefs);
void tiku_trk_deskprefs_defaults(tiku_trk_deskprefs_t *prefs);
void tiku_trk_deskprefs_revert(tiku_trk_deskprefs_t *prefs);

/** @brief Show @p path, keeping whatever mode is already chosen. */
void tiku_trk_deskprefs_set_picture(tiku_trk_deskprefs_t *prefs,
                                    const char *path);
void tiku_trk_deskprefs_clear_picture(tiku_trk_deskprefs_t *prefs);
void tiku_trk_deskprefs_set_mode(tiku_trk_deskprefs_t *prefs,
                                 tiku_trk_bg_mode_t mode);

/**
 * @brief Step the icon size one rung.  @return 1 when it moved.
 *
 * @param up Nonzero to grow.
 */
int tiku_trk_deskprefs_step_icon(tiku_trk_deskprefs_t *prefs, int up);

/**
 * @brief Every picture found in @p dirs, by content rather than by name.
 *
 * A file called .txt holding a PNG is a picture; one called .jpg holding
 * nothing is not, and a chooser that believed the name would offer it.
 *
 * @param n How many directories.
 * @return how many were found.
 */
int tiku_trk_deskprefs_pictures(tiku_trk_deskprefs_pictures_t *out,
                                const char *const *dirs, int n);

#endif /* TIKU_TRK_DESKPREFS_H_ */
