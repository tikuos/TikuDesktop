/* TikuOS interface kit. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_PREFERENCES_LAYOUT_H_
#define TIKU_PREFERENCES_LAYOUT_H_
#include "tiku_geom.h"
#define TIKU_PREFERENCES_ACTIONS_MAX 8
typedef struct {
    const char *label;
    int minimum;
} tiku_preferences_action_t;

/** @brief Minimum width for a leading utility and trailing action group.
 * leading may be NULL; trailing count may be zero. Returns -1 on invalid input.
 * Widths use the same measured label padding as native buttons.
 */
int tiku_preferences_actions_width(const tiku_preferences_action_t *leading,
    const tiku_preferences_action_t *trailing, int count, int gap);
/** @brief Place a leading utility and right-aligned trailing buttons.
 * Returns 1 if they fit, 0 if too small, -1 for invalid input. Outputs are
 * cleared on failure, never overlapping hit targets. Minimum width is queried
 * separately so callers can size their window. out has room for count entries.
 */
int tiku_preferences_actions(tiku_rect_t r,
    const tiku_preferences_action_t *leading,
    const tiku_preferences_action_t *trailing, int count, int gap,
    tiku_rect_t *leading_out, tiku_rect_t *out);
/** @brief Etched group's content: font-aware title clearance and inner padding.
 * Tiny bounds yield an empty interior, not negative rectangles.
 */
tiku_rect_t tiku_preferences_group_body(tiku_rect_t frame);
#endif
