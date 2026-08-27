/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_splash.h - what the screen says while the desktop is coming up.
 *
 * A black screen, the mark in the middle of it, and one line under the
 * mark naming the part of the system being started.  The names are the
 * REAL stages -- each is written the moment that piece of the shell has
 * been built, not read off a list somebody wrote down -- because a boot
 * screen that recites a fixed sequence is a progress bar that has been
 * lied to, and it is the same objection the About box answers by reading
 * /proc instead of quoting constants.
 *
 * A stage is held for a moment before the next replaces it, or the whole
 * thing would be a flicker on any machine built this decade: what is
 * being shown is not how long the work took, it is what the work WAS.
 *
 * Then it lets go.  The desktop is drawn underneath and the black is
 * lifted off it over about a second, which is the transition -- the
 * shell is already running by then, so nothing is being waited for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SPLASH_H_
#define TIKU_TRK_SPLASH_H_

#include <stdint.h>

#include "tiku_gfx.h"

/** @brief The least time a stage stays up, so it can be read. */
#define TIKU_TRK_SPLASH_STEP_US 220000
/**
 * @brief How long the finished screen holds before it starts to go.
 *
 * Long enough to be seen as a finished state rather than as the last
 * frame of the loading -- and long enough that a script watching it is
 * not racing the first step of the fade, which at three hundred
 * milliseconds it was.
 */
#define TIKU_TRK_SPLASH_HOLD_US 700000
/** @brief And how long it takes to go. */
#define TIKU_TRK_SPLASH_FADE_US 900000

typedef struct {
    int     live;            /* whether there is a splash at all      */
    int64_t stage_us;        /* when the line now up was written      */
    int64_t done_us;         /* when loading finished; 0 until it has */
    int     count;           /* how many stages have been named       */
    char    what[48];        /* the line now up                       */
} tiku_trk_splash_t;

/**
 * @brief Put the splash up, or don't.
 *
 * @param live Zero leaves every call below a no-op, which is what a
 *             scripted run wants: 164 scripts would each pay the dwell.
 */
void tiku_trk_splash_begin(tiku_trk_splash_t *sp, int live, int64_t now);

/** @brief Name the part of the system that has just been built. */
void tiku_trk_splash_say(tiku_trk_splash_t *sp, const char *what,
                         int64_t now);

/**
 * @brief How long the line now up is still owed, in microseconds.
 *
 * The caller sleeps this out before starting the next piece of work, so
 * the pacing is the shell's rather than something the splash does behind
 * its back.  Zero when there is nothing owed, which is always the case
 * when the splash is not live.
 */
int64_t tiku_trk_splash_owed(const tiku_trk_splash_t *sp, int64_t now);

/** @brief The shell is up; start letting go. */
void tiku_trk_splash_done(tiku_trk_splash_t *sp, int64_t now);

/** @brief Whether any of it is still on the screen. */
int tiku_trk_splash_visible(const tiku_trk_splash_t *sp, int64_t now);

/**
 * @brief How solid it is: 1 while loading and holding, 0 once gone.
 *
 * Everything the splash draws is drawn at this strength, so the mark,
 * the line and the black all leave together.
 */
float tiku_trk_splash_veil(const tiku_trk_splash_t *sp, int64_t now);

/** @brief Lay it over @p screen, whatever is already drawn there. */
void tiku_trk_splash_draw(const tiku_trk_splash_t *sp, tiku_surface_t *s,
                          tiku_rect_t screen, int64_t now);

#endif /* TIKU_TRK_SPLASH_H_ */
