/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_text_layout_paint.c - laid-out text, drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_text_layout_paint.h"
#include "tiku_font.h"

static const tiku_font_t *font(int face)
{
    return face == TIKU_MEASURE_BOLD ? tiku_font_bold() :
           face == TIKU_MEASURE_MONO ? tiku_font_mono(0) : tiku_font_plain();
}

/* Desktop intersects/restores the current clip. Recording-side helpers
 * reset clipping afterwards, like existing kit painters; see controls.md. */
static tiku_rect_t clip(tiku_surface_t *s, tiku_rect_t r)
{
#ifdef TIKU_DISPLAYLESS
    tiku_rect_t old = r;
#else
    tiku_rect_t old = s->clip;
    int right = r.x + r.w < old.x + old.w ? r.x + r.w : old.x + old.w;
    int bottom = r.y + r.h < old.y + old.h ? r.y + r.h : old.y + old.h;
    if (r.x < old.x) r.x = old.x;
    if (r.y < old.y) r.y = old.y;
    r.w = right > r.x ? right - r.x : 0;
    r.h = bottom > r.y ? bottom - r.y : 0;
#endif
    tiku_clip_set(s, r);
    return old;
}
static void restore(tiku_surface_t *s, tiku_rect_t old)
{
#ifdef TIKU_DISPLAYLESS
    (void)old;
    tiku_clip_reset(s);
#else
    tiku_clip_set(s, old);
#endif
}
void tiku_text_line_draw(tiku_surface_t *s, tiku_rect_t r, int face,
                         const char *text, tiku_rgb_t ink)
{
    char line[TIKU_TEXT_LINE_MAX];
    if (!s || r.w <= 0 || r.h <= 0) return;
    tiku_text_elide(face, text, r.w, line, sizeof line);
    tiku_rect_t old = clip(s, r);
    const tiku_font_t *f = font(face);
    tiku_text(s, f, r.x, r.y + (r.h - f->height) / 2 + f->ascent, line, ink);
    restore(s, old);
}
void tiku_text_block_draw(tiku_surface_t *s, tiku_rect_t r, int face,
                          const char *text, tiku_rgb_t ink, int gap)
{
    char line[TIKU_TEXT_LINE_MAX];
    const tiku_font_t *f = font(face);
    if (!s || r.w <= 0 || r.h < f->height) return;
    if (gap < 0) gap = 0;
    int rows = (r.h + gap) / (f->height + gap);
    tiku_rect_t old = clip(s, r);
    for (int i = 0; i < rows; i++) {
        if (!tiku_text_wrap_next(face, &text, r.w, line, sizeof line,
                                i == rows - 1)) break;
        tiku_text(s, f, r.x, r.y + i * (f->height + gap) + f->ascent, line, ink);
    }
    restore(s, old);
}
