/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_press.h - a button held under the hand.
 *
 * What every pressable thing shares: a press ARMS it and draws it down, a
 * move takes it up when the hand leaves and down again when it returns,
 * and the release ON it is what acts -- a release anywhere else did
 * nothing, which is the one way to change your mind.  tiku_dialog keeps
 * this for a whole window of controls; this is the same rule for a
 * window that draws its own few buttons and would otherwise act on the
 * press.  It knows no rectangles: the window's own hit test says what is
 * under the hand, and this only remembers what was pressed.  Behaviour
 * only; it draws nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PRESS_H
#define TIKU_PRESS_H

/** @brief One press in flight: what was pressed, and whether the hand is on it. */
typedef struct {
    int id;         /* what was pressed, 0 when nothing is   */
    int inside;     /* the hand is on it right now           */
} tiku_press_t;

/**
 * @brief The hand came down on @p id (0 for nothing pressable).
 * @return nonzero when a press was taken and a repaint is owed.
 */
int tiku_press_arm(tiku_press_t *p, int id);

/**
 * @brief The hand moved; @p under is what the hit test finds there now.
 * @return nonzero when the look changed and a repaint is owed.
 */
int tiku_press_track(tiku_press_t *p, int under);

/**
 * @brief The hand let go over @p under.  Disarms either way.
 * @return the id that acts -- released on what was pressed -- or 0.
 */
int tiku_press_release(tiku_press_t *p, int under);

/** @brief Whether @p id is held down right now, for drawing it so. */
int tiku_press_held(const tiku_press_t *p, int id);

#endif /* TIKU_PRESS_H */
