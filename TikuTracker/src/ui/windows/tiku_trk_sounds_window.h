/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_sounds_window.h - the Sounds preferences: a row per moment
 * with its on/off and a Test button, and one volume under them.
 *
 * Every change is saved to the settings file and applied to the kit at
 * once, in the manner of the other preference windows: there is no OK.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SOUNDS_WINDOW_H_
#define TIKU_TRK_SOUNDS_WINDOW_H_

#include "tiku_window.h"
#include "tiku_trk_soundprefs.h"

#define TIKU_TRK_SOUNDS_W 372

typedef struct {
    tiku_trk_prefs_t *prefs;        /* the settings, owned elsewhere */
    char              path[512];    /* the file they are saved to, or "" */
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    int               screen_w;
    int               screen_h;
    int              *dirty;
    int               opened[TIKU_SOUND_EVENTS]; /* the enables at open */
    int               opened_volume;             /* the volume at open */
} tiku_trk_sounds_window_t;

/**
 * @brief Bind the controller to its workspace and to the settings it
 *        edits, saved to @p path when one is given.  Opens no window.
 */
void tiku_trk_sounds_window_init(tiku_trk_sounds_window_t *controller,
                                 tiku_workspace_t *workspace,
                                 tiku_trk_prefs_t *prefs, const char *path,
                                 int screen_w, int screen_h, int *dirty);
/** @brief Close the window if open; nothing else is held. */
void tiku_trk_sounds_window_destroy(tiku_trk_sounds_window_t *controller);
/** @brief Open the window on the settings as they are; an open one is
 *         activated instead. */
void tiku_trk_sounds_window_open(tiku_trk_sounds_window_t *controller);
/** @brief Close the window and ask for a redraw. */
void tiku_trk_sounds_window_close(tiku_trk_sounds_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_sounds_window_handle(
    tiku_trk_sounds_window_t *controller);

#endif /* TIKU_TRK_SOUNDS_WINDOW_H_ */
