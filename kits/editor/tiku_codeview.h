/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview.h - an editor document on the screen, and its caret.
 *
 * A view shows one row per line of its document and keeps every byte: a
 * tab reaches the next stop from the start of its line, a control
 * character is shown by name, and the caret may stop between any two
 * characters.  Painting, hit testing and caret moves read that one
 * layout.  Each view
 * keeps its own selection and scroll position.  Positions are byte
 * offsets; x and y are pixels on the surface the view is painted on, and
 * a line's own x starts at 0 where its first character begins.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CODEVIEW_H_
#define TIKU_CODEVIEW_H_

#include <stdint.h>

#include "tiku_editdoc.h"
#include "tiku_event.h"
#include "tiku_font.h"

/** @brief Columns between tab stops unless set; a column is the width of
 *         a space in the view's face. */
#define TIKU_CODEVIEW_TAB_DEFAULT 4
/** @brief The widest tab stop interval accepted, in columns. */
#define TIKU_CODEVIEW_TAB_MAX     32

typedef struct tiku_codeview tiku_codeview_t;

/** @brief A view of @p doc, with its own selection, an empty caret at 0.
 *         The view holds a reference to the document until it is
 *         destroyed. */
tiku_edit_status_t tiku_codeview_create(tiku_editdoc_t *doc,
                                        tiku_codeview_t **view);
/** @brief Forget the view's selection and drop its document reference. */
void tiku_codeview_destroy(tiku_codeview_t *view);

/** @brief The document shown. */
tiku_editdoc_t *tiku_codeview_doc(const tiku_codeview_t *view);
/** @brief The document's handle for this view's selection, for
 *         transactions that should restore it on undo. */
tiku_edit_view_t tiku_codeview_doc_view(const tiku_codeview_t *view);

/** @brief Where the view is painted; the text starts a small margin
 *         inside. */
void tiku_codeview_set_bounds(tiku_codeview_t *view, tiku_rect_t bounds);
tiku_rect_t tiku_codeview_bounds(const tiku_codeview_t *view);

/** @brief The face text is measured and drawn in; NULL follows the
 *         interface's monospaced face. */
void tiku_codeview_set_font(tiku_codeview_t *view, const tiku_font_t *font);
const tiku_font_t *tiku_codeview_font(const tiku_codeview_t *view);

/** @brief Columns between tab stops, from 1 to TIKU_CODEVIEW_TAB_MAX;
 *         anything else is refused and the setting kept. */
tiku_edit_status_t tiku_codeview_set_tab_width(tiku_codeview_t *view,
                                               int columns);
int tiku_codeview_tab_width(const tiku_codeview_t *view);

/* What a view shows besides the text; a new view shows none of them. */
#define TIKU_CODEVIEW_LINE_NUMBERS 0x1u  /* a gutter of line numbers    */
#define TIKU_CODEVIEW_INVISIBLES   0x2u  /* a mark in each space and tab */
#define TIKU_CODEVIEW_VSCROLL      0x4u  /* a scrollbar down the right  */
#define TIKU_CODEVIEW_HSCROLL      0x8u  /* a scrollbar along the foot  */

/** @brief Show what @p options names, and nothing else of them. */
void tiku_codeview_set_options(tiku_codeview_t *view, unsigned options);
unsigned tiku_codeview_options(const tiku_codeview_t *view);

/** @brief Whether the caret is drawn; the selection is drawn either
 *         way.  A new view is focused. */
void tiku_codeview_set_focus(tiku_codeview_t *view, int focused);
int tiku_codeview_focused(const tiku_codeview_t *view);

/** @brief Pixels from one row's top to the next. */
int tiku_codeview_row_height(const tiku_codeview_t *view);
/** @brief Whole rows the bounds show, at least 1. */
int64_t tiku_codeview_rows_visible(const tiku_codeview_t *view);

/** @brief The line shown in the first row, and how far the text is
 *         scrolled left, in pixels. */
int64_t tiku_codeview_top(const tiku_codeview_t *view);
int64_t tiku_codeview_left(const tiku_codeview_t *view);
/** @brief Scroll so @p top is the first row and the text is moved
 *         @p left pixels.  The top stops where the last line fills the
 *         last whole row, the text stops a little past the end of the
 *         widest line, and neither goes below 0. */
void tiku_codeview_scroll_to(tiku_codeview_t *view, int64_t top,
                             int64_t left);
/** @brief What there is to scroll, for an application drawing its own
 *         scrollbar: lines down, or pixels across when @p across, with
 *         @p total in all, @p visible of them shown and the first shown
 *         at @p value. */
void tiku_codeview_extent(tiku_codeview_t *view, int across,
                          int64_t *total, int64_t *visible,
                          int64_t *value);

/** @brief The width of @p line's text, its line break left out; -1 for
 *         a line that is not there. */
int64_t tiku_codeview_line_width(tiku_codeview_t *view, int64_t line);
/** @brief Where a caret at @p pos is drawn: its left edge and its row's
 *         top on the surface, whether or not that is inside the bounds.
 *         A position outside the text or inside a character is refused
 *         with TIKU_EDIT_INVALID_RANGE. */
tiku_edit_status_t tiku_codeview_point(tiku_codeview_t *view,
                                       tiku_edit_pos_t pos, int64_t *x,
                                       int64_t *y);
/** @brief The caret position nearest the surface point @p x, @p y: on
 *         the row under @p y, clamped to the first and last lines, before
 *         a character whose left half holds @p x and after one whose
 *         right half does; past the line's end, its end. */
tiku_edit_pos_t tiku_codeview_hit(tiku_codeview_t *view, int x, int y);

/** @brief The view's selection; equal ends are an empty caret. */
tiku_edit_status_t tiku_codeview_selection(const tiku_codeview_t *view,
                                           tiku_edit_pos_t *anchor,
                                           tiku_edit_pos_t *caret);
/** @brief Select from @p anchor to @p caret, both caret positions. */
tiku_edit_status_t tiku_codeview_select(tiku_codeview_t *view,
                                        tiku_edit_pos_t anchor,
                                        tiku_edit_pos_t caret);

/** @brief Where a caret move goes. */
typedef enum {
    TIKU_CODEVIEW_CHAR_LEFT = 0,    /* one character back               */
    TIKU_CODEVIEW_CHAR_RIGHT,       /* one character on                 */
    TIKU_CODEVIEW_WORD_LEFT,        /* back over spaces, then one run   */
    TIKU_CODEVIEW_WORD_RIGHT,       /* on over one run, then spaces     */
    TIKU_CODEVIEW_LINE_UP,          /* the row above, at the same x     */
    TIKU_CODEVIEW_LINE_DOWN,        /* the row below, at the same x     */
    TIKU_CODEVIEW_PAGE_UP,          /* the visible rows up              */
    TIKU_CODEVIEW_PAGE_DOWN,        /* the visible rows down            */
    TIKU_CODEVIEW_LINE_START,
    TIKU_CODEVIEW_LINE_END,         /* before the line break            */
    TIKU_CODEVIEW_DOC_START,
    TIKU_CODEVIEW_DOC_END
} tiku_codeview_move_t;

/**
 * @brief Move the caret, then scroll as little as shows it.
 *
 * With @p extend the anchor stays and the selection follows the caret;
 * without it the selection becomes the caret alone, and a character move
 * over a selection stops at the selection's edge.  The rows up and down
 * keep the x the first of them started from until another move or a
 * change to the document.
 */
tiku_edit_status_t tiku_codeview_move(tiku_codeview_t *view,
                                      tiku_codeview_move_t move, int extend);
/** @brief Select the whole document, the caret at its end, and show it. */
tiku_edit_status_t tiku_codeview_select_all(tiku_codeview_t *view);
/** @brief Scroll as little as shows the caret on a whole row and at
 *         least 4 pixels inside the right edge; scrolling right leaves
 *         it 8 pixels inside. */
void tiku_codeview_reveal(tiku_codeview_t *view);

/**
 * @brief Take a key, pointer, wheel or tick event.
 *
 * Moves: the arrows, Home, End, Page Up and Page Down, extending with
 * Shift; Command with Home or End for the document's ends; Option or
 * Command with Left or Right for words.
 *
 * @note Button 1 in the text puts the caret, Shift extends, two clicks
 *       select a word and three a line, and dragging extends by the same
 *       unit; on a scrollbar it steps, pages or drags.  The wheel
 *       scrolls.
 * @return 1 when the view used the event, 0 when it is the
 *         application's; deactivation ends a drag and still returns 0.
 */
int tiku_codeview_event(tiku_codeview_t *view, const tiku_event_t *e);

/** @brief Paint the view into its bounds on @p s, within the clip @p s
 *         already has, which is restored afterwards.  Everything drawn
 *         is recorded when @p s records. */
void tiku_codeview_paint(tiku_codeview_t *view, tiku_surface_t *s);

#endif /* TIKU_CODEVIEW_H_ */
