/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_private.h - what the code view's files share: the view
 * itself and the one walk over a line that painting, hit testing and
 * measuring all use.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CODEVIEW_PRIVATE_H_
#define TIKU_CODEVIEW_PRIVATE_H_

#include "tiku_codeview.h"
#include "tiku_text_input.h"

/* Pixels between the bounds' left edge and a line's x 0, and between the
 * bounds' top and the first row. */
#define TIKU_CV_PAD_X 5
#define TIKU_CV_PAD_Y 3
/* Pixels added below the face's height to make a row. */
#define TIKU_CV_ROW_GAP 2
/* Pixels each side of a control character's name, inside its cell. */
#define TIKU_CV_NAME_PAD 2
/* The line-number gutter: its pixels besides the digits, its separator
 * included, and the gap from it to a line's x 0. */
#define TIKU_CV_GUTTER_PAD 12
#define TIKU_CV_GUTTER_GAP 4
/* A scrollbar's thickness. */
#define TIKU_CV_BAR 15
/* Pixels past the widest line the text may scroll, room for the caret. */
#define TIKU_CV_SLACK 12

struct tiku_codeview {
    tiku_editdoc_t *doc;            /* referenced while the view lives  */
    tiku_edit_view_t view;          /* the document's selection for it  */
    tiku_edit_listener_t listener;  /* told of every change             */
    const tiku_font_t *font;        /* NULL: the monospaced face        */
    int tab_width;                  /* columns between tab stops        */
    int focused;                    /* draw the caret                   */
    tiku_rect_t bounds;             /* on the surface                   */
    int64_t top;                    /* line in the first row            */
    int64_t left;                   /* pixels scrolled left             */
    int64_t goal;                   /* x vertical moves keep; -1 none   */
    int dragging;                   /* button 1 is down on the view     */
    int unit;                       /* a drag selects 1 characters,
                                       2 words, 3 lines                 */
    tiku_edit_range_t pressed;      /* the word or line first selected  */
    int clicks;                     /* presses counted as one click     */
    int64_t click_at;               /* the last press's time, in us     */
    int click_x, click_y;           /* and where it was                 */
    unsigned options;               /* TIKU_CODEVIEW_LINE_NUMBERS ...   */
    int held_bar;                   /* thumb held: 1 vertical, 2 across */
    int grab;                       /* where in the thumb it was taken  */
    int arrow_bar;                  /* arrow held: 1 vertical, 2 across */
    int arrow_dir;                  /* -1 back, +1 on                   */
    int arrow_pulses;               /* ticks since it was pressed       */
    int64_t *widths;                /* each line's width, -1 unknown    */
    int64_t nwidths, widths_cap;    /* entries in use and allocated     */
    int64_t widest;                 /* -1 until found again             */
    int widths_ok;                  /* the table follows the document   */
    uint32_t widths_key;            /* the metrics it was measured in   */
    tiku_codeview_host_t host;      /* clipboard and invalidation       */
    char newline[3];                /* what Return inserts              */
    tiku_edit_status_t status;      /* the last event's edit            */
    tiku_text_input_t input;        /* a text-input commit arriving     */
};

/* What a character of a line is on the screen. */
typedef enum {
    TIKU_CV_GLYPH = 0,              /* drawn from the face              */
    TIKU_CV_TAB,                    /* space to the next stop           */
    TIKU_CV_CONTROL                 /* a control character, by name     */
} tiku_cv_kind_t;

typedef struct {
    tiku_edit_pos_t pos;            /* its first byte                   */
    int len;                        /* its bytes, 1 to 4                */
    unsigned cp;                    /* its code point                   */
    tiku_cv_kind_t kind;
    int64_t x;                      /* its left edge, from x 0          */
    int adv;                        /* its width                        */
    char bytes[5];                  /* its UTF-8 and a NUL              */
} tiku_cv_cell_t;

/* Called for each character in order; nonzero stops the walk. */
typedef int (*tiku_cv_cell_fn)(void *ctx, const tiku_cv_cell_t *cell);

/* The face in force. */
const tiku_font_t *tiku_cv_font(const tiku_codeview_t *v);
/* Pixels between tab stops in the face in force, at least 1. */
int tiku_cv_tab_px(const tiku_codeview_t *v);
/* The name a control character is shown by, in @p name (at least 5
 * bytes); 0 when @p cp is not a control character. */
int tiku_cv_control_name(unsigned cp, char *name);

/* Visit @p line's characters from its start, @p fn may be NULL.  Returns
 * the x where the walk ended: the left edge of the character @p fn
 * stopped at, or the line's width; -1 when there is no such line. */
int64_t tiku_cv_walk(tiku_codeview_t *v, int64_t line, tiku_cv_cell_fn fn,
                     void *ctx);
/* The x of boundary @p pos on @p line, which must hold it. */
int64_t tiku_cv_x_of(tiku_codeview_t *v, int64_t line, tiku_edit_pos_t pos);
/* The caret position on @p line nearest the line's own @p x. */
tiku_edit_pos_t tiku_cv_pos_at(tiku_codeview_t *v, int64_t line, int64_t x);
/* The first byte of the character on @p line under the line's own @p x,
 * or -1 past the line's end. */
tiku_edit_pos_t tiku_cv_char_at(tiku_codeview_t *v, int64_t line, int64_t x);

/* Where line x 0 and the first row's top are on the surface. */
int tiku_cv_text_x(const tiku_codeview_t *v);
int tiku_cv_text_y(const tiku_codeview_t *v);
/* The line-number gutter's width, 0 when the numbers are off. */
int tiku_cv_gutter(const tiku_codeview_t *v);
/* The text's ground: the bounds less the gutter and the scrollbars. */
tiku_rect_t tiku_cv_area(const tiku_codeview_t *v);
/* Pixels of a line the text area shows across. */
int64_t tiku_cv_text_w(const tiku_codeview_t *v);
/* A scrollbar's place, empty when the bar is off or the bounds are too
 * small to hold it. */
tiku_rect_t tiku_cv_bar(const tiku_codeview_t *v, int horizontal);
/* Where a bar's thumb is, in thousandths of its travel, and the share of
 * the bar it fills, in thousandths and at least 50. */
void tiku_cv_bar_place(tiku_codeview_t *v, int horizontal, int *pos,
                       int *frac);
/* Bring the first row back inside the document and the horizontal
 * position to 0 or more.  The second also stops the text TIKU_CV_SLACK
 * pixels past the end of the widest line, measuring every line not yet
 * measured. */
void tiku_cv_clamp(tiku_codeview_t *v);
void tiku_cv_clamp_across(tiku_codeview_t *v);

/* The widest line's width, measuring every line not yet measured; with
 * no room for the table, the widest of the rows shown. */
int64_t tiku_cv_widest(tiku_codeview_t *v);
/* Follow a change in the width table: lines @p first to @p last_old
 * became @p first to @p last_new, @p lines in all. */
void tiku_cv_widths_change(tiku_codeview_t *v, int64_t first,
                           int64_t last_old, int64_t last_new,
                           int64_t lines);
/* Free the width table. */
void tiku_cv_widths_free(tiku_codeview_t *v);

/* An editing key: 1 when the view took it. */
int tiku_cv_edit_key(tiku_codeview_t *v, const tiku_event_t *e);
/* A text-input event, given what staging made of it. */
int tiku_cv_edit_commit(tiku_codeview_t *v, int commit);

#endif /* TIKU_CODEVIEW_PRIVATE_H_ */
