/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_paint_board.c - the desktop's drawing calls, each an op written
 * into the window's list.
 *
 * Only the outermost call is written down, as on the desktop: a
 * control's painter records its op and the fills it is made of are
 * suppressed beneath it, so the list says "button" and not the four
 * lines a button is drawn from.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gfx.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include "tiku_dl.h"
#include "tiku_clipboard.h"
#include "tiku_draw.h"
#include "tiku_metrics.h"
#include "tiku_measure.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
/* The surface and the recording rule                                       */
/*---------------------------------------------------------------------------*/

void
tiku_surface_on_window(tiku_surface_t *s, struct tiku_draw_window *win)
{
    if (s != NULL) {
        s->record = win;
        s->record_depth = 0;
    }
}

int
tiku_gfx_rec_enter(tiku_surface_t *s)
{
    if (s == NULL || s->record == NULL || s->record_depth > 0) {
        return 0;
    }
    s->record_depth++;
    return 1;
}

void
tiku_gfx_rec_leave(tiku_surface_t *s, int on)
{
    if (s != NULL && on) {
        s->record_depth--;
    }
}

/*---------------------------------------------------------------------------*/
/* Colour arithmetic, the desktop's own                                     */
/*---------------------------------------------------------------------------*/

tiku_rgb_t
tiku_tint(tiku_rgb_t c, float tint)
{
    int i, out[3];
    int ch[3];

    ch[0] = (int)((c >> 16) & 0xFFu);
    ch[1] = (int)((c >> 8) & 0xFFu);
    ch[2] = (int)(c & 0xFFu);
    for (i = 0; i < 3; i++) {
        float v;

        if (tint < 1.0f) {
            v = 255.0f - (255.0f - (float)ch[i]) * tint;
        } else {
            v = (float)ch[i] * (2.0f - tint);
        }
        if (v < 0.0f)   { v = 0.0f; }
        if (v > 255.0f) { v = 255.0f; }
        out[i] = (int)(v + 0.5f);
    }
    return TIKU_RGB(out[0], out[1], out[2]);
}

tiku_rgb_t
tiku_dim(tiku_rgb_t ink, tiku_rgb_t ground)
{
    return TIKU_RGB(
        (((ink >> 16) & 0xFFu) + ((ground >> 16) & 0xFFu)) / 2u,
        (((ink >> 8) & 0xFFu) + ((ground >> 8) & 0xFFu)) / 2u,
        ((ink & 0xFFu) + (ground & 0xFFu)) / 2u);
}

/**
 * @brief The shade a meaning is written in.
 *
 * kits/interface/tiku_gfx.c's answer with the default table's colours
 * in place of the theme's, since a board wears no theme.  FAINT and
 * PATH are derived here as they are there, so a file drawn on both ends
 * puts the same ink on the same word.
 */
/*
 * The nine meanings are numbered, not named, wherever they cross: a run
 * carries its ink as a byte.  Both ends pin the numbering to these
 * literals, so a tenth meaning added to one end and not the other is a
 * build that stops rather than a page drawn in the wrong colours.
 */
_Static_assert(TIKU_INK_PLAIN == 0 && TIKU_INK_FAINT == 1 &&
               TIKU_INK_WORD == 2 && TIKU_INK_NUM == 3 &&
               TIKU_INK_STR == 4 && TIKU_INK_REMARK == 5 &&
               TIKU_INK_WRONG == 6 && TIKU_INK_PATH == 7 &&
               TIKU_INK_LINK == 8,
               "the board's ink numbering is the desktop's");

tiku_rgb_t
tiku_ink(tiku_ink_t which)
{
    switch (which) {
    case TIKU_INK_FAINT:  return tiku_dim(TIKU_C_TEXT, TIKU_C_DOC);
    case TIKU_INK_WORD:   return TIKU_C_CODE_WORD;
    case TIKU_INK_NUM:    return TIKU_C_CODE_NUM;
    case TIKU_INK_STR:    return TIKU_C_CODE_STR;
    case TIKU_INK_REMARK: return TIKU_C_CODE_REM;
    case TIKU_INK_WRONG:  return TIKU_C_DANGER;
    case TIKU_INK_PATH:   return tiku_dim(TIKU_C_CODE_STR, TIKU_C_SELECT);
    case TIKU_INK_LINK:   return TIKU_C_SELECT;
    case TIKU_INK_PLAIN:
    default:              return TIKU_C_TEXT;
    }
}

tiku_rect_t
tiku_inset(tiku_rect_t r, int n)
{
    tiku_rect_t o;

    o.x = r.x + n;
    o.y = r.y + n;
    o.w = r.w - 2 * n;
    o.h = r.h - 2 * n;
    if (o.w < 0) { o.w = 0; }
    if (o.h < 0) { o.h = 0; }
    return o;
}

/*---------------------------------------------------------------------------*/
/* Primitives                                                               */
/*---------------------------------------------------------------------------*/

/** @brief One op for a primitive, when this is the outermost call. */
#define PRIMITIVE(s, call)                                                  \
    do {                                                                    \
        int rec_ = tiku_gfx_rec_enter(s);                                   \
        if (rec_) {                                                         \
            (void)(call);                                                   \
        }                                                                   \
        tiku_gfx_rec_leave(s, rec_);                                        \
    } while (0)

void
tiku_fill(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c)
{
    PRIMITIVE(s, tiku_draw_fill(s->record, r.x, r.y, r.w, r.h, c));
}

void
tiku_frame(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c)
{
    PRIMITIVE(s, tiku_draw_frame(s->record, r.x, r.y, r.w, r.h, c));
}

void
tiku_hline(tiku_surface_t *s, int x, int y, int len, tiku_rgb_t c)
{
    PRIMITIVE(s, tiku_draw_hline(s->record, x, y, len, c));
}

void
tiku_vline(tiku_surface_t *s, int x, int y, int len, tiku_rgb_t c)
{
    PRIMITIVE(s, tiku_draw_vline(s->record, x, y, len, c));
}

void
tiku_bevel(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t light,
           tiku_rgb_t shadow)
{
    PRIMITIVE(s, tiku_draw_bevel(s->record, r.x, r.y, r.w, r.h, light,
                                 shadow));
}

void
tiku_clip_set(tiku_surface_t *s, tiku_rect_t r)
{
    PRIMITIVE(s, tiku_draw_clip_set(s->record, r.x, r.y, r.w, r.h));
}

void
tiku_clip_reset(tiku_surface_t *s)
{
    PRIMITIVE(s, tiku_draw_clip_reset(s->record));
}

/*---------------------------------------------------------------------------*/
/* Faces                                                                    */
/*---------------------------------------------------------------------------*/

static const tiku_font_t plain = { 0, 0, TIKU_DRAW_PLAIN };
static const tiku_font_t bold = { 0, 0, TIKU_DRAW_BOLD };
static const tiku_font_t mono = { 0, 0, TIKU_DRAW_MONO };
static tiku_font_t faces[3];

/** @brief The face @p f, its metrics filled in from the table. */
static const tiku_font_t *
face(const tiku_font_t *f)
{
    tiku_font_t *out = &faces[f->face];

    out->face = f->face;
    out->height = tiku_draw_text_height((tiku_draw_face_t)f->face);
    out->ascent = tiku_draw_text_ascent((tiku_draw_face_t)f->face);
    return out;
}

const tiku_font_t *
tiku_font_plain(void)
{
    return face(&plain);
}

const tiku_font_t *
tiku_font_bold(void)
{
    return face(&bold);
}

const tiku_font_t *
tiku_font_mono(int bold_face)
{
    (void)bold_face;
    return face(&mono);
}

int
tiku_text_width(const tiku_font_t *f, const char *text)
{
    return tiku_draw_text_width((tiku_draw_face_t)f->face, text);
}

int
tiku_text_height(const tiku_font_t *f)
{
    return f->height;
}

void
tiku_text(tiku_surface_t *s, const tiku_font_t *f, int x, int y,
          const char *text, tiku_rgb_t c)
{
    PRIMITIVE(s, tiku_draw_text(s->record, x, y, c,
                                (tiku_draw_face_t)f->face, text));
}

int
tiku_text_centered(tiku_surface_t *s, const tiku_font_t *f, tiku_rect_t r,
                   const char *text, tiku_rgb_t c)
{
    PRIMITIVE(s, tiku_draw_text_centered(s->record, r.x, r.y, r.w, r.h, c,
                                         (tiku_draw_face_t)f->face, text));
    return tiku_text_width(f, text);
}

/*---------------------------------------------------------------------------*/
/* The control set                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_ui_row_h(void)
{
    return tiku_measure_row_h();
}

int
tiku_ui_button_h(void)
{
    return tiku_draw_text_height(TIKU_DRAW_PLAIN) + 11;
}

int
tiku_ui_button_w(const char *label, int least)
{
    int want = tiku_draw_text_width(TIKU_DRAW_PLAIN, label) +
               2 * tiku_draw_text_height(TIKU_DRAW_PLAIN);

    return (want > least) ? want : least;
}

int
tiku_ui_step_w(void)
{
    return tiku_ui_button_h();
}

void
tiku_ui_panel(tiku_surface_t *s, tiku_rect_t r)
{
    PRIMITIVE(s, tiku_draw_panel(s->record, r.x, r.y, r.w, r.h));
}

void
tiku_ui_raised(tiku_surface_t *s, tiku_rect_t r)
{
    PRIMITIVE(s, tiku_draw_raised(s->record, r.x, r.y, r.w, r.h));
}

void
tiku_ui_sunken(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t face_colour)
{
    PRIMITIVE(s, tiku_draw_sunken(s->record, r.x, r.y, r.w, r.h,
                                  face_colour));
}

void
tiku_ui_button(tiku_surface_t *s, tiku_rect_t r, const char *label,
               unsigned state)
{
    PRIMITIVE(s, tiku_draw_button(s->record, r.x, r.y, r.w, r.h, state,
                                  label));
}

void
tiku_ui_checkbox(tiku_surface_t *s, tiku_rect_t r, const char *label,
                 unsigned state)
{
    PRIMITIVE(s, tiku_draw_checkbox(s->record, r.x, r.y, r.w, r.h, state,
                                    label));
}

void
tiku_ui_radio(tiku_surface_t *s, tiku_rect_t r, const char *label,
              unsigned state)
{
    PRIMITIVE(s, tiku_draw_radio(s->record, r.x, r.y, r.w, r.h, state,
                                 label));
}

void
tiku_ui_menufield(tiku_surface_t *s, tiku_rect_t r, const char *label,
                  unsigned state)
{
    PRIMITIVE(s, tiku_draw_menufield(s->record, r.x, r.y, r.w, r.h, state,
                                     label));
}

void
tiku_ui_textfield(tiku_surface_t *s, tiku_rect_t r, const char *text,
                  int caret, unsigned state)
{
    (void)caret;
    PRIMITIVE(s, tiku_draw_textfield(s->record, r.x, r.y, r.w, r.h, state,
                                     text));
}

void
tiku_ui_gauge(tiku_surface_t *s, tiku_rect_t r, float fraction)
{
    int pm = (int)(fraction * 1000.0f + 0.5f);

    PRIMITIVE(s, tiku_draw_gauge(s->record, r.x, r.y, r.w, r.h,
                                 pm < 0 ? 0 : pm > 1000 ? 1000 : pm));
}

void
tiku_ui_tip(tiku_surface_t *s, tiku_rect_t r, const char *text)
{
    PRIMITIVE(s, tiku_draw_tip(s->record, r.x, r.y, r.w, r.h, 0u, text));
}

void
tiku_ui_scrollbar(tiku_surface_t *s, tiku_rect_t r, float pos, float frac,
                  int horiz)
{
    PRIMITIVE(s, tiku_draw_scrollbar(s->record, r.x, r.y, r.w, r.h,
                                     (int)(pos * 1000.0f + 0.5f),
                                     (int)(frac * 1000.0f + 0.5f), horiz));
}

void
tiku_ui_list_row(tiku_surface_t *s, tiku_rect_t r, const char *text,
                 int selected)
{
    PRIMITIVE(s, tiku_draw_list_row(s->record, r.x, r.y, r.w, r.h,
                                    selected ? 1u : 0u, text));
}

void
tiku_ui_arrow(tiku_surface_t *s, tiku_rect_t r, int dir, int enabled)
{
    /* Part of a stepper's face on the desktop, drawn inside its record;
     * on its own here it has no op of its own and draws nothing. */
    (void)s;
    (void)r;
    (void)dir;
    (void)enabled;
}

/*---------------------------------------------------------------------------*/
/* The list's own ops, named by the paint files                             */
/*---------------------------------------------------------------------------*/

int
tiku_dl_slider(tiku_dl_t *dl, tiku_rect_t r, int min, int max, int value)
{
    return tiku_draw_slider(dl, r.x, r.y, r.w, r.h, min, max, value);
}

int
tiku_dl_tabs(tiku_dl_t *dl, tiku_rect_t r, int count, int current,
             const char *labels, size_t labels_len)
{
    return tiku_draw_tabs(dl, r.x, r.y, r.w, r.h, count, current, labels,
                          labels_len);
}

int
tiku_dl_gauge(tiku_dl_t *dl, tiku_rect_t r, int per_mille)
{
    return tiku_draw_gauge(dl, r.x, r.y, r.w, r.h, per_mille);
}

int
tiku_dl_list_row(tiku_dl_t *dl, tiku_rect_t r, const char *text,
                 int selected)
{
    return tiku_draw_list_row(dl, r.x, r.y, r.w, r.h, selected ? 1u : 0u,
                              text);
}

/*---------------------------------------------------------------------------*/
/* The clipboard, one line the board keeps                                  */
/*---------------------------------------------------------------------------*/

static char clip[128];
static unsigned clip_stamp;

void
tiku_clipboard_set(const char *text)
{
    size_t n = text ? strlen(text) : 0u;

    if (n >= sizeof clip) {
        n = sizeof clip - 1u;
    }
    memcpy(clip, text ? text : "", n);
    clip[n] = '\0';
    clip_stamp++;
}

const char *
tiku_clipboard_text(void)
{
    return clip;
}

unsigned
tiku_clipboard_stamp(void)
{
    return clip_stamp;
}
