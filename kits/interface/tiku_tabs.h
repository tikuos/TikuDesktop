/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tabs.h - a horizontal tab strip: a row of named tabs, one of them
 * current, over a body the current one names.
 *
 * The state that makes it a control rather than a picture: which tabs
 * there are, which one is current, where each sits in a strip, which one
 * a click lands on, and the arrow that moves between them.  An
 * application supplies the names and reads back the current index to
 * decide what to show under the strip; the widget owns everything else,
 * so a second window that wants tabs does not re-derive the layout, the
 * hit-test and the look a third time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TABS_H_
#define TIKU_TABS_H_

#include "tiku_geom.h"

#define TIKU_TABS_MAX        8
#define TIKU_TABS_LABEL_MAX 40

typedef struct {
    char label[TIKU_TABS_MAX][TIKU_TABS_LABEL_MAX];
    int  count;
    int  current;
} tiku_tabs_t;

/** @brief Start empty, nothing current yet. */
void tiku_tabs_init(tiku_tabs_t *t);

/**
 * @brief Add a tab named @p label.  The first one added is current.
 * @return its index, or -1 when the strip is full.
 */
int tiku_tabs_add(tiku_tabs_t *t, const char *label);

/** @brief How many tabs there are. */
int tiku_tabs_count(const tiku_tabs_t *t);

/** @brief The current tab's index, or -1 when there are none. */
int tiku_tabs_current(const tiku_tabs_t *t);

/**
 * @brief Make @p index current, clamped to what exists.
 * @return the current index after clamping.
 */
int tiku_tabs_select(tiku_tabs_t *t, int index);

/**
 * @brief Step the current tab by @p dir (-1 or +1), wrapping the ends.
 *
 * The arrow keys of a tabbed window: past the last tab comes the first,
 * so a right arrow held never stops on nothing.
 * @return the current index after the step.
 */
int tiku_tabs_next(tiku_tabs_t *t, int dir);

/**
 * @brief Where tab @p index sits in a strip @p strip long.
 *
 * The tabs divide the strip evenly.  An index the strip does not have
 * gets a zero-width rect, so a caller looping past the count draws and
 * hits nothing.  The strip is assumed at least as wide as the count:
 * narrower, interior tabs floor to zero width and the last eats the
 * rest, hittable no longer; both callers keep the strip an order of
 * magnitude wider than the count.
 */
tiku_rect_t tiku_tabs_rect(const tiku_tabs_t *t, tiku_rect_t strip,
                           int index);

/**
 * @brief Which tab a point at @p x,@p y lands on in @p strip.
 * @return the tab's index, or -1 for none.
 */
int tiku_tabs_hit(const tiku_tabs_t *t, tiku_rect_t strip, int x, int y);

#endif /* TIKU_TABS_H_ */
