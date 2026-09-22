/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TRK_SPRINGMENU_H_
#define TIKU_TRK_SPRINGMENU_H_

#include <stdint.h>

#define TIKU_TRK_SPRING_DELAY_US 500000
#define TIKU_TRK_SPRING_MARGIN 20

typedef struct {
    int armed;
    int open;
    int x, y, w, h;
    int64_t entered;
} tiku_trk_springmenu_t;

void tiku_trk_springmenu_reset(tiku_trk_springmenu_t *m);
/** @brief The pointer came to rest over @p x, @p y, @p w, @p h at @p now. */
void tiku_trk_springmenu_enter(tiku_trk_springmenu_t *m, int x, int y, int w,
                               int h, int64_t now);
/**
 * @brief A tick: once the hold has lasted the spring delay the menu opens.
 *        @return 1 on the tick it opens.
 */
int tiku_trk_springmenu_pulse(tiku_trk_springmenu_t *m, int64_t now);
/** @brief Whether @p x, @p y is over the armed area, margin included. */
int tiku_trk_springmenu_inside(const tiku_trk_springmenu_t *m, int x, int y);
/** @brief The pointer moved to @p x, @p y; outside the area, the arm drops. */
void tiku_trk_springmenu_leave(tiku_trk_springmenu_t *m, int x, int y);
/**
 * @brief Whether a spring menu row can be chosen: a container unless its
 *        link is broken, a file only when something accepts its type.
 */
int tiku_trk_springmenu_item_enabled(int container, int accepts_type,
                                     int broken_link);

#endif
