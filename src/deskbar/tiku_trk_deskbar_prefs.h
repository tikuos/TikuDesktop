/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_prefs.h - durable Deskbar preference model.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_PREFS_H_
#define TIKU_TRK_DESKBAR_PREFS_H_

#include "tiku_trk_deskbar.h"
#include "tiku_state.h"

#define TIKU_TRK_DB_ICON_MIN 16
#define TIKU_TRK_DB_ICON_MAX 96
#define TIKU_TRK_DB_RECENT_LIMIT 50

/** @brief The fifteen values covered by Defaults and Revert (DB-062). */
typedef struct tiku_trk_deskbar_settings {
    int sort_apps;
    int tracker_first;
    int show_expander;
    int expand_new_apps;
    int hide_app_names;
    int icon_size;
    int recent_enabled[TIKU_TRK_DB_RECENT_KINDS];
    int recent_count[TIKU_TRK_DB_RECENT_KINDS];
    int always_on_top;
    int auto_raise;
    int auto_hide;
    int show_clock;
    int show_seconds;
    int show_day_of_week;
    int show_timezone;
    int font_size;      /* UI text size in px; the toolkit snaps it */
    int font_family;    /* which baked family the interface is drawn in */
} tiku_trk_deskbar_settings_t;

typedef struct {
    tiku_trk_deskbar_settings_t value;
    tiku_trk_deskbar_settings_t opened;
} tiku_trk_deskbar_prefs_t;

void tiku_trk_deskbar_settings_default(tiku_trk_deskbar_settings_t *settings);
void tiku_trk_deskbar_settings_normalize(
    tiku_trk_deskbar_settings_t *settings);
void tiku_trk_deskbar_prefs_init(tiku_trk_deskbar_prefs_t *prefs);
void tiku_trk_deskbar_prefs_mark_opened(tiku_trk_deskbar_prefs_t *prefs);
int tiku_trk_deskbar_prefs_defaultable(
    const tiku_trk_deskbar_prefs_t *prefs);
int tiku_trk_deskbar_prefs_revertable(
    const tiku_trk_deskbar_prefs_t *prefs);
void tiku_trk_deskbar_prefs_defaults(tiku_trk_deskbar_prefs_t *prefs);
void tiku_trk_deskbar_prefs_revert(tiku_trk_deskbar_prefs_t *prefs);
int tiku_trk_deskbar_prefs_recent_enabled(
    const tiku_trk_deskbar_prefs_t *prefs, tiku_trk_db_recent_t which);
int tiku_trk_deskbar_prefs_expanders_enabled(
    const tiku_trk_deskbar_prefs_t *prefs, tiku_trk_db_dock_t dock);
int tiku_trk_deskbar_prefs_auto_raise_enabled(
    const tiku_trk_deskbar_prefs_t *prefs);
int tiku_trk_deskbar_prefs_load(tiku_trk_deskbar_prefs_t *prefs,
                                tiku_store_t *store);
int tiku_trk_deskbar_prefs_save(const tiku_trk_deskbar_prefs_t *prefs,
                                tiku_store_t *store);

/** @brief Map the little monitor's thirds to its six supported docks. */
tiku_trk_db_dock_t tiku_trk_deskbar_prefs_dock_at(
    int x, int y, int width, int height, tiku_trk_db_dock_t current);

/** @brief Map the selector's arrow/numpad key names to a dock. */
int tiku_trk_deskbar_prefs_dock_key(const char *key,
                                    tiku_trk_db_dock_t *dock);

/** @brief Restore a complete on-screen preferences frame, or center it. */
tiku_rect_t tiku_trk_deskbar_prefs_frame(int saved, int x, int y,
                                              int screen_w, int screen_h,
                                              int width, int height);

#endif /* TIKU_TRK_DESKBAR_PREFS_H_ */
