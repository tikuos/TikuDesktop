/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_select.h - the Select window's state.
 *
 * The window is a persistent thing, not a dialog: closing it HIDES it, so
 * the expression, the rule and the checkboxes are still there next time.
 * That is the whole point of it -- selecting by pattern is something you do
 * repeatedly with small variations, and a dialog that forgets makes the
 * second use as expensive as the first.
 *
 * Only the state and the geometry live here.  Drawing belongs to whichever
 * shell is showing it, and keeping it out means the rules can be checked
 * without a screen.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SELECT_H_
#define TIKU_TRK_SELECT_H_

#include "tiku_trk_edit.h"
#include "tiku_trk_match.h"

/** @brief The five rules, in the order the popup offers them. */
#define TIKU_TRK_SELECT_RULES 5

/** @brief The window keeps 20 px of itself on screen (MoveCloseToMouse). */
#define TIKU_TRK_SELECT_MARGIN 20

typedef struct {
    tiku_trk_edit_t  expr;
    int              rule;          /* index into the rule table          */
    int              ignore_case;
    int              invert;
    int              open;          /* shown now                          */
    int              ever;          /* has been opened at least once      */
    int              x, y;          /* where it was last placed           */
} tiku_trk_select_t;

/**
 * @brief Set the defaults: wildcard, ignore case on, invert off.
 *
 * Wildcard rather than "contains" because a wildcard with no metacharacters
 * is an exact match, so the default rule is the one that does what a typed
 * name looks like it should do.
 */
void tiku_trk_select_init(tiku_trk_select_t *w);

/** @brief The rule at index @p i, for the popup and for matching. */
tiku_trk_match_t tiku_trk_select_rule(int i);

/** @brief Its label, for the popup. */
const char *tiku_trk_select_rule_label(int i);

/**
 * @brief Show it, keeping everything it held.
 *
 * @param mouse_x,mouse_y Where the pointer is.
 * @param w,h             The window's size.
 * @param screen_w,screen_h The area it must stay inside.
 */
void tiku_trk_select_show(tiku_trk_select_t *sw, int mouse_x, int mouse_y,
                          int w, int h, int screen_w, int screen_h);

/** @brief Hide it.  Nothing is discarded -- that is what makes it reusable. */
void tiku_trk_select_hide(tiku_trk_select_t *sw);

/**
 * @brief Centre a window of @p w by @p h on the pointer, clamped on screen.
 *
 * Clamped rather than merely centred: a pointer near an edge would otherwise
 * put the window's controls off-screen, where they cannot be reached.
 */
void tiku_trk_select_place(int mouse_x, int mouse_y, int w, int h,
                           int screen_w, int screen_h, int *out_x,
                           int *out_y);

/** @brief Step the rule popup to index @p i, ignoring nonsense. */
void tiku_trk_select_set_rule(tiku_trk_select_t *sw, int i);

#endif /* TIKU_TRK_SELECT_H_ */
