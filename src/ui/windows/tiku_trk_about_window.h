/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_about_window.h - the About box, in the R5 manner.
 *
 * One modeless window that says what this system IS: the wordmark, the
 * build, the kernel underneath, the processors, the memory with its
 * gauge, and the uptime ticking while the window stands.  Closed by its
 * own close box or Escape; nothing in it is a control.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_ABOUT_WINDOW_H_
#define TIKU_TRK_ABOUT_WINDOW_H_

#include "tiku_window.h"
#include "tiku_ns.h"

typedef struct {
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    int                    screen_w;
    int                    screen_h;
    int                   *dirty;
    /* Gathered once at open; the uptime and memory re-read per draw. */
    char                   kernel[96];
    char                   cpu[96];
    int                    ncpu;
    /*
     * When it opened, and which step of the colour cycle was last drawn.
     * The mark is kept as geometry, so its colours are arguments rather
     * than pixels -- which is what makes a cycle cost the same as a
     * still.  The step is remembered so the pulse below can say whether
     * anything a person could see has actually changed: the Deskbar's
     * clock keeps the same discipline, and for the same reason.
     */
    int64_t                opened_us;
    int                    step;
} tiku_trk_about_window_t;

void tiku_trk_about_window_init(tiku_trk_about_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty);

void tiku_trk_about_window_open(tiku_trk_about_window_t *controller);

/**
 * @brief Move the colour cycle on, if it is time.
 *
 * @return nonzero when the window would now draw differently and a
 *         repaint is owed.  Zero for the three seconds out of four that
 *         it is holding a colour, so a window standing open does not
 *         cost a repaint of the desktop every tick.
 */
int tiku_trk_about_window_pulse(tiku_trk_about_window_t *controller,
                                int64_t now_us);
void tiku_trk_about_window_close(tiku_trk_about_window_t *controller);

tiku_window_t *tiku_trk_about_window_handle(
    tiku_trk_about_window_t *controller);

/** @return Nonzero when the focused About window consumed the key. */
int tiku_trk_about_window_key(tiku_trk_about_window_t *controller,
                              unsigned key);

#endif /* TIKU_TRK_ABOUT_WINDOW_H_ */
