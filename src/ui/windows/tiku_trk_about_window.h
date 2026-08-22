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
} tiku_trk_about_window_t;

void tiku_trk_about_window_init(tiku_trk_about_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    int *dirty);

void tiku_trk_about_window_open(tiku_trk_about_window_t *controller);
void tiku_trk_about_window_close(tiku_trk_about_window_t *controller);

tiku_window_t *tiku_trk_about_window_handle(
    tiku_trk_about_window_t *controller);

/** @return Nonzero when the focused About window consumed the key. */
int tiku_trk_about_window_key(tiku_trk_about_window_t *controller,
                              unsigned key);

#endif /* TIKU_TRK_ABOUT_WINDOW_H_ */
