/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ui.h - the desktop's control set, as a board writes it down.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_UI_BOARD_
#define TIKU_UI_BOARD_

#include "tiku_gfx.h"

#define TIKU_S_DISABLED  0x01u
#define TIKU_S_FOCUS     0x02u
#define TIKU_S_DEFAULT   0x04u
#define TIKU_S_ON        0x10u
#define TIKU_S_PRESSED   0x20u

int tiku_ui_row_h(void);
int tiku_ui_button_h(void);
int tiku_ui_button_w(const char *label, int least);
int tiku_ui_step_w(void);
void tiku_ui_panel(tiku_surface_t *s, tiku_rect_t r);
void tiku_ui_raised(tiku_surface_t *s, tiku_rect_t r);
void tiku_ui_sunken(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t face);
void tiku_ui_button(tiku_surface_t *s, tiku_rect_t r, const char *label,
                    unsigned state);
void tiku_ui_checkbox(tiku_surface_t *s, tiku_rect_t r, const char *label,
                      unsigned state);
void tiku_ui_radio(tiku_surface_t *s, tiku_rect_t r, const char *label,
                   unsigned state);
void tiku_ui_menufield(tiku_surface_t *s, tiku_rect_t r, const char *label,
                       unsigned state);
void tiku_ui_textfield(tiku_surface_t *s, tiku_rect_t r, const char *text,
                       int caret, unsigned state);
void tiku_ui_gauge(tiku_surface_t *s, tiku_rect_t r, float fraction);
void tiku_ui_tip(tiku_surface_t *s, tiku_rect_t r, const char *text);
void tiku_ui_scrollbar(tiku_surface_t *s, tiku_rect_t r, float pos,
                       float frac, int horiz);
void tiku_ui_list_row(tiku_surface_t *s, tiku_rect_t r, const char *text,
                      int selected);
void tiku_ui_arrow(tiku_surface_t *s, tiku_rect_t r, int dir, int enabled);

#endif /* TIKU_UI_BOARD_ */
