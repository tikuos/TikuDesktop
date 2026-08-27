/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_splash.c - the boot screen's pacing.
 *
 * A script can only see the last stage and the way out, because every
 * stage before it is held during startup, before the script's own loop
 * begins.  What paces them is arithmetic, and arithmetic is testable
 * without a screen at all -- so it is tested here, with the clock handed
 * in rather than read.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "tiku_trk_splash.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

int
main(void)
{
    tiku_trk_splash_t sp;
    int64_t t = 1000000;

    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("a scripted run gets no boot screen unless it asks (TS-BOOT)\n");
    tiku_trk_splash_begin(&sp, 0, t);
    tiku_trk_splash_say(&sp, "storage", t);
    check(sp.count == 0 && sp.what[0] == '\0', "TS-BOOT",
          "a splash that is not live does not even remember the stages: "
          "the 164 other scripts pay nothing for this one");
    check(tiku_trk_splash_owed(&sp, t) == 0, "TS-BOOT",
          "and owes no dwell, which is the whole of what it would cost");
    check(!tiku_trk_splash_visible(&sp, t) &&
          tiku_trk_splash_veil(&sp, t) == 0.0f, "TS-BOOT",
          "and is never on the screen");

    printf("\nwhat a stage is owed (TS-BOOT)\n");
    tiku_trk_splash_begin(&sp, 1, t);
    check(tiku_trk_splash_owed(&sp, t) == 0, "TS-BOOT",
          "before the first stage nothing is owed: the dwell is a floor "
          "under a line that is UP, and none is yet");
    tiku_trk_splash_say(&sp, "storage", t);
    check(tiku_trk_splash_owed(&sp, t) == TIKU_TRK_SPLASH_STEP_US,
          "TS-BOOT", "a line just written owes the whole step");
    /*
     * The point of the whole arrangement: work done under a stage counts
     * toward that stage's own dwell.  A piece that takes most of the step
     * is topped up by the remainder, and one that takes longer than the
     * step is not held back at all.
     */
    check(tiku_trk_splash_owed(&sp, t + TIKU_TRK_SPLASH_STEP_US / 4) ==
          TIKU_TRK_SPLASH_STEP_US - TIKU_TRK_SPLASH_STEP_US / 4,
          "TS-BOOT",
          "and work done while it is up counts toward it: a quarter of "
          "the step spent leaves three quarters owed");
    check(tiku_trk_splash_owed(&sp, t + TIKU_TRK_SPLASH_STEP_US) == 0 &&
          tiku_trk_splash_owed(&sp, t + TIKU_TRK_SPLASH_STEP_US * 4) == 0,
          "TS-BOOT",
          "so a stage that took longer than the step is not held back "
          "at all -- what is paced is the reading, not the loading");

    printf("\nwhile it is loading it is solid (TS-BOOT)\n");
    check(tiku_trk_splash_veil(&sp, t) == 1.0f &&
          tiku_trk_splash_veil(&sp, t + 10000000) == 1.0f, "TS-BOOT",
          "however long the loading takes, the screen stays up: there is "
          "no clock running that could time it out from under the work");

    printf("\nand then it lets go (TS-BOOT)\n");
    t += 5000000;
    tiku_trk_splash_done(&sp, t);
    check(tiku_trk_splash_veil(&sp, t) == 1.0f, "TS-BOOT",
          "still solid the moment loading ends: the hold is so the "
          "finished screen reads as finished rather than as the last "
          "frame of the loading");
    check(tiku_trk_splash_veil(&sp, t + TIKU_TRK_SPLASH_HOLD_US - 1) ==
          1.0f, "TS-BOOT", "and solid to the end of the hold");
    {
        /* Down the whole way, without going back up anywhere. */
        float last = 1.0f;
        int steps = 0, rising = 0;
        int64_t at;

        for (at = t + TIKU_TRK_SPLASH_HOLD_US;
             at <= t + TIKU_TRK_SPLASH_HOLD_US + TIKU_TRK_SPLASH_FADE_US;
             at += TIKU_TRK_SPLASH_FADE_US / 32) {
            float v = tiku_trk_splash_veil(&sp, at);

            if (v > last) {
                rising = 1;
            }
            if (v < last) {
                steps++;
            }
            last = v;
        }
        check(!rising && steps >= 30, "TS-BOOT",
              "the fade only ever goes one way, and in enough steps to "
              "be a fade rather than a couple of jumps");
    }
    check(tiku_trk_splash_veil(&sp,
              t + TIKU_TRK_SPLASH_HOLD_US +
              TIKU_TRK_SPLASH_FADE_US / 2) > 0.4f &&
          tiku_trk_splash_veil(&sp,
              t + TIKU_TRK_SPLASH_HOLD_US +
              TIKU_TRK_SPLASH_FADE_US / 2) < 0.6f, "TS-BOOT",
          "halfway through the fade it is halfway gone, so the black "
          "comes off evenly rather than in a rush at one end");
    check(tiku_trk_splash_veil(&sp,
              t + TIKU_TRK_SPLASH_HOLD_US +
              TIKU_TRK_SPLASH_FADE_US) == 0.0f &&
          !tiku_trk_splash_visible(&sp,
              t + TIKU_TRK_SPLASH_HOLD_US + TIKU_TRK_SPLASH_FADE_US),
          "TS-BOOT",
          "and at the end it is gone, not merely faint: a screen left at "
          "one part in a thousand is a screen still being drawn over the "
          "desktop for ever");
    check(!tiku_trk_splash_visible(&sp, t + 60000000), "TS-BOOT",
          "and stays gone");

    printf("\nand it only lets go once (TS-BOOT)\n");
    tiku_trk_splash_done(&sp, t + 30000000);
    check(tiku_trk_splash_veil(&sp, t + 30000000) == 0.0f, "TS-BOOT",
          "a second done does not restart the fade -- the shell says it "
          "in one place, but a boot screen that came back would be worse "
          "than one that never went");

    printf("\n%s\n", fails ? "FAILURES" : "the boot screen keeps its time");
    return fails ? 1 : 0;
}
