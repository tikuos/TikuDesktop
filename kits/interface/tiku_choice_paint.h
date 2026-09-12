#ifndef TIKU_CHOICE_PAINT_H_
#define TIKU_CHOICE_PAINT_H_
#include "tiku_choice.h"
#include "tiku_gfx.h"
/** @brief Paint the closed field. Optional label adds application-specific status. */
void tiku_choice_draw(const tiku_choice_t *c, tiku_surface_t *s,
                     tiku_rect_t anchor, const char *label, unsigned state);
/** @brief Paint the popup last, over the panel, using the behavior's geometry. */
void tiku_choice_draw_popup(const tiku_choice_t *c, tiku_surface_t *s,
                           tiku_rect_t anchor, tiku_rect_t screen);
#endif
