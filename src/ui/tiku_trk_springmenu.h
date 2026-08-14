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
void tiku_trk_springmenu_enter(tiku_trk_springmenu_t *m, int x, int y, int w,
                               int h, int64_t now);
int tiku_trk_springmenu_pulse(tiku_trk_springmenu_t *m, int64_t now);
int tiku_trk_springmenu_inside(const tiku_trk_springmenu_t *m, int x, int y);
void tiku_trk_springmenu_leave(tiku_trk_springmenu_t *m, int x, int y);
int tiku_trk_springmenu_item_enabled(int container, int accepts_type,
                                     int broken_link);

#endif
