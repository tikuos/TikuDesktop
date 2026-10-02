/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview.h - an editor document on the screen, its caret, and the
 * edits and searches made through it.
 *
 * A view shows each line of its document on a row, or with the wrap
 * option on as many rows as its width takes, and keeps every byte: a tab
 * reaches the next stop from the start of its line, a control character
 * is shown by name, and the caret may stop between any two characters.
 * Painting, hit testing and caret moves read that one layout.  Each view
 * keeps its own selection and scroll position.  Positions are byte
 * offsets; x and y are pixels on the surface the view is painted on, and
 * a line's own x starts at 0 where its first character begins.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CODEVIEW_H_
#define TIKU_CODEVIEW_H_

#include <stdint.h>

#include "tiku_edit_decorations.h"
#include "tiku_edit_search.h"
#include "tiku_editdoc.h"
#include "tiku_event.h"
#include "tiku_font.h"
#include "tiku_syntax.h"

/** @brief Columns between tab stops unless set; a column is the width of
 *         a space in the view's face. */
#define TIKU_CODEVIEW_TAB_DEFAULT 4
/** @brief The widest tab stop interval accepted, in columns. */
#define TIKU_CODEVIEW_TAB_MAX     32

typedef struct tiku_codeview tiku_codeview_t;

/** @brief What a view asks of whoever hosts it; any member may be NULL,
 *         and the view then goes without. */
typedef struct {
    void *ctx;
    /** @brief Put @p length bytes of @p text, NUL-terminated, on the
     *         clipboard.  @return 0 when the clipboard took them. */
    int (*clip_set)(void *ctx, const char *text, size_t length);
    /** @brief What the clipboard holds, setting @p length; NULL when it
     *         holds nothing.  Valid until the host is next called. */
    const char *(*clip_text)(void *ctx, size_t *length);
    /** @brief The document changed and the view should be painted again.
     *         Called while the document refuses changes. */
    void (*invalidate)(void *ctx, tiku_codeview_t *view);
} tiku_codeview_host_t;

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

/** @brief Colour the text as @p lang reads it, through the live theme;
 *         TIKU_SYNTAX_NONE, which a new view has, leaves it all in the
 *         document's ink.  A C line is coloured from the state the lines
 *         above it leave, which the view keeps and re-reads only from the
 *         first line a change could have altered. */
void tiku_codeview_set_language(tiku_codeview_t *view,
                                tiku_syntax_lang_t lang);
tiku_syntax_lang_t tiku_codeview_language(const tiku_codeview_t *view);

/* What a view shows besides the text; a new view shows none of them. */
#define TIKU_CODEVIEW_LINE_NUMBERS 0x1u  /* a gutter of line numbers    */
#define TIKU_CODEVIEW_INVISIBLES   0x2u  /* a mark in each space and tab */
#define TIKU_CODEVIEW_VSCROLL      0x4u  /* a scrollbar down the right  */
#define TIKU_CODEVIEW_HSCROLL      0x8u  /* a scrollbar along the foot  */
#define TIKU_CODEVIEW_BRACES       0x10u /* the bracket by the caret and
                                            its match marked            */
#define TIKU_CODEVIEW_WRAP         0x20u /* lines broken into rows at the
                                            text's width                */
#define TIKU_CODEVIEW_ANNOTATIONS  0x40u /* the document's annotations,
                                            rows under their lines      */
#define TIKU_CODEVIEW_FOLDS        0x80u /* a fold column in the gutter */

/**
 * @brief How a language is edited: how a line is commented, what opens
 *        and closes a level of indentation, and which brackets pair.
 *
 * Markers are compared without regard to ASCII case.  A closer typed as
 * the first character of a line, after its indentation, takes the
 * indentation of the line holding its opener.
 */
typedef struct {
    const char *comment;        /* a line comment's opening, inserted
                                   with a space after it; NULL: none    */
    const char *comment_alt;    /* also read as a comment's opening
                                   when removing one; NULL: none        */
    int numbered;               /* a line may start with a number, and
                                   its comment goes after it            */
    const char *opens;          /* a line whose code ends with one of
                                   these opens a level                  */
    const char *closes;         /* closers that end one                 */
    const char *brackets;       /* pairs, each opener then its closer   */
    const char *const *blocks;  /* words opening and closing a block, in
                                   pairs and then NULL; NULL: none      */
} tiku_codeview_rules_t;

/** @brief The rules of @p lang: C comments with "//" and opens a level
 *         after "{"; BASIC comments with "REM" after a line's number and
 *         has FOR, WHILE, REPEAT, SUB and THEN blocks; prose has no
 *         comments.  All of them pair (), [] and {}. */
const tiku_codeview_rules_t *tiku_codeview_rules_for(tiku_syntax_lang_t lang);
/** @brief Edit by @p rules, copied, whose strings must outlive the view;
 *         NULL follows the view's language, as a new view does. */
void tiku_codeview_set_rules(tiku_codeview_t *view,
                             const tiku_codeview_rules_t *rules);
/** @brief The rules the view edits by. */
const tiku_codeview_rules_t *tiku_codeview_rules(const tiku_codeview_t *view);
/** @brief Indent by @p spaces spaces a level, from 1 to
 *         TIKU_CODEVIEW_TAB_MAX, or by a tab with 0, as a new view does;
 *         anything else is refused and the setting kept. */
tiku_edit_status_t tiku_codeview_set_indent(tiku_codeview_t *view,
                                            int spaces);
/** @brief Spaces a level of indentation, 0 for a tab. */
int tiku_codeview_indent_spaces(const tiku_codeview_t *view);

/** @brief Use @p host's clipboard and invalidation; NULL uses none.  The
 *         members are copied. */
void tiku_codeview_set_host(tiku_codeview_t *view,
                            const tiku_codeview_host_t *host);

/** @brief The line break Return inserts: "\n", "\r\n" or "\r"; anything
 *         else is refused with TIKU_EDIT_INVALID_TEXT.  "\n" unless set. */
tiku_edit_status_t tiku_codeview_set_newline(tiku_codeview_t *view,
                                             const char *newline);

/** @brief Show what @p options names, and nothing else of them. */
void tiku_codeview_set_options(tiku_codeview_t *view, unsigned options);
unsigned tiku_codeview_options(const tiku_codeview_t *view);

/**
 * @brief Whether lines that wrap are still to be counted.
 *
 * With TIKU_CODEVIEW_WRAP a row breaks after the last blank that fits, or
 * else before the character that would pass the text's width, and keeps
 * at least one character; blanks past the width stay on their row.  The
 * rows shown are counted at once, and up to 256 KB of the other lines
 * each time the view is laid out; a tick event counts more.  A line not
 * yet counted takes its last count, or one row, in the scroll range.
 */
int tiku_codeview_wrapping(tiku_codeview_t *view);

/** @brief Whether the caret is drawn; the selection is drawn either
 *         way.  A new view is focused. */
void tiku_codeview_set_focus(tiku_codeview_t *view, int focused);
int tiku_codeview_focused(const tiku_codeview_t *view);

/** @brief Pixels from one row's top to the next. */
int tiku_codeview_row_height(const tiku_codeview_t *view);
/** @brief Whole rows the bounds show, at least 1. */
int64_t tiku_codeview_rows_visible(const tiku_codeview_t *view);

/** @brief The first row shown, which is a line's index unless lines
 *         wrap, and how far the text is scrolled left, in pixels. */
int64_t tiku_codeview_top(const tiku_codeview_t *view);
int64_t tiku_codeview_left(const tiku_codeview_t *view);
/** @brief Scroll so row @p top is the first shown and the text is moved
 *         @p left pixels.  The top stops where the last row fills the
 *         last whole row, the text stops a little past the end of the
 *         widest line, and neither goes below 0.  Wrapped lines are never
 *         scrolled across. */
void tiku_codeview_scroll_to(tiku_codeview_t *view, int64_t top,
                             int64_t left);
/** @brief What there is to scroll, for an application drawing its own
 *         scrollbar: rows down, or pixels across when @p across, with
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
 *         the row under @p y, clamped to the first and last rows, before
 *         a character whose left half holds @p x and after one whose
 *         right half does; past the row's end, its end. */
tiku_edit_pos_t tiku_codeview_hit(tiku_codeview_t *view, int x, int y);

/** @brief The view's selection; equal ends are an empty caret. */
tiku_edit_status_t tiku_codeview_selection(const tiku_codeview_t *view,
                                           tiku_edit_pos_t *anchor,
                                           tiku_edit_pos_t *caret);
/** @brief Select from @p anchor to @p caret, both caret positions, the
 *         view's only selection. */
tiku_edit_status_t tiku_codeview_select(tiku_codeview_t *view,
                                        tiku_edit_pos_t anchor,
                                        tiku_edit_pos_t caret);
/** @brief The view's selections in order: how many, up to @p cap of them
 *         in @p out, and the main one's place in @p main.  The main one
 *         is the one tiku_codeview_selection() reads. */
size_t tiku_codeview_selections(const tiku_codeview_t *view,
                                tiku_edit_selection_t *out, size_t cap,
                                size_t *main);
/** @brief Set @p count selections, the one at @p main the main one,
 *         merged as tiku_editdoc_view_select_many() merges them. */
tiku_edit_status_t tiku_codeview_select_many(
    tiku_codeview_t *view, const tiku_edit_selection_t *selections,
    size_t count, size_t main);
/** @brief Add a selection and make it the main one; one it meets
 *         joins it. */
tiku_edit_status_t tiku_codeview_add_selection(tiku_codeview_t *view,
                                               tiku_edit_pos_t anchor,
                                               tiku_edit_pos_t caret);
/** @brief Keep only the main selection; TIKU_EDIT_NOTHING when it is
 *         the only one. */
tiku_edit_status_t tiku_codeview_keep_main(tiku_codeview_t *view);
/** @brief A caret at the end of each line the main selection touches, a
 *         selection ending at a line's start leaving that line out; the
 *         last line's caret is the main one. */
tiku_edit_status_t tiku_codeview_carets_to_line_ends(tiku_codeview_t *view);

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
    TIKU_CODEVIEW_LINE_START,       /* the row's start, then the line's */
    TIKU_CODEVIEW_LINE_END,         /* the row's end, then the line's,
                                       before its line break            */
    TIKU_CODEVIEW_DOC_START,
    TIKU_CODEVIEW_DOC_END
} tiku_codeview_move_t;

/**
 * @brief Move every caret, then scroll as little as shows the main one.
 *
 * With @p extend each anchor stays and its selection follows the caret;
 * without it each becomes its caret, a character move over a selection
 * stopping at its edge.  Rows up and down keep each caret's x from its
 * row's start until another move or a change.  A caret where a row breaks
 * shows at the next row's start, unless End, a row move or a press past
 * the row's end put it at that end.
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
 * @brief Replace the selection, or each of several, with @p length bytes
 *        of @p text as one undo step, a caret after each, and show the
 *        main caret.
 *
 * With @p typing the step may join the typing before it, going on where
 * that stopped at every caret.  A caret that would fall between a CR and
 * its LF stands before the CR.  Every failure leaves the document as it
 * was.
 */
tiku_edit_status_t tiku_codeview_insert(tiku_codeview_t *view,
                                        const char *text, size_t length,
                                        int typing);
/** @brief Delete the selection, or else the character before the caret,
 *         or after it when @p forward, a CRLF as one, at every selection
 *         as one step; presses in a row join into one undo step.  With no
 *         character on that side nothing changes and TIKU_EDIT_NOTHING is
 *         returned. */
tiku_edit_status_t tiku_codeview_delete(tiku_codeview_t *view, int forward);
/**
 * @brief Replace each selection with the view's line break and the
 *        indentation of its line, up to it, one level more after a line
 *        whose code ends with an opener of the rules.
 *
 * A closer that followed a selection goes to a line of its own at the old
 * indentation, unless another selection stands before it.  One undo step
 * joining the typing before it, a caret at the end of each indentation.
 */
tiku_edit_status_t tiku_codeview_newline(tiku_codeview_t *view);
/**
 * @brief Indent the lines the selection touches by one level, or with
 *        @p out unindent them: one tab or up to a level of spaces off
 *        the front of each.
 *
 * A selection that ends at the start of a line leaves that line out.
 * Over several lines, empty lines are left as they are and the whole
 * lines end selected.  TIKU_EDIT_NOTHING when nothing would change.
 */
tiku_edit_status_t tiku_codeview_indent(tiku_codeview_t *view, int out);
/**
 * @brief Comment the lines the selection touches, or uncomment them when
 *        every one holding text is commented already.
 *
 * The comment goes before the text at the least indentation among the
 * lines, or after a line's number by numbered rules; blank lines are left
 * as they are.  TIKU_EDIT_NOTHING when the rules have no comment or no
 * line holds text.
 */
tiku_edit_status_t tiku_codeview_toggle_comment(tiku_codeview_t *view);
/** @brief Copy the lines the selection touches below them, and select
 *         the copy of what was selected. */
tiku_edit_status_t tiku_codeview_duplicate_lines(tiku_codeview_t *view);
/** @brief Swap the lines the selection touches with the line above, or
 *         with @p down the line below; the selection moves with them and
 *         a last line keeps no line break.  TIKU_EDIT_NOTHING at the
 *         document's first or last line. */
tiku_edit_status_t tiku_codeview_move_lines(tiku_codeview_t *view,
                                            int down);
/** @brief Delete the lines the selection touches with their line breaks,
 *         or the last lines with the break before them; the caret goes
 *         to where they began. */
tiku_edit_status_t tiku_codeview_delete_lines(tiku_codeview_t *view);
/** @brief Delete the selection, or else back to where a word move left
 *         would go, or with @p forward on to where a word move right
 *         would, at every selection, as one undo step. */
tiku_edit_status_t tiku_codeview_delete_word(tiku_codeview_t *view,
                                             int forward);
/**
 * @brief The bracket of the view's rules next to the caret, the one
 *        before it first, and in @p match the bracket it pairs with.
 *
 * Brackets inside quotes or remarks are not counted, and the search for
 * the match stops TIKU_CODEVIEW_BRACE_SCAN bytes away.  @return the
 * bracket's position, or -1 when there is none or the caret holds a
 * selection; @p match is -1 when no bracket pairs with it, and -2 when
 * the search stopped before finding out.
 */
tiku_edit_pos_t tiku_codeview_bracket(tiku_codeview_t *view,
                                      tiku_edit_pos_t *match);
/** @brief Put the caret on the other side of the matching bracket, or
 *         with @p extend select from the bracket through its match.
 *         TIKU_EDIT_NOTHING when the caret is by no paired bracket. */
tiku_edit_status_t tiku_codeview_goto_bracket(tiku_codeview_t *view,
                                              int extend);
/** @brief Bytes the bracket search reads before giving up. */
#define TIKU_CODEVIEW_BRACE_SCAN ((tiku_edit_pos_t)1 << 20)

/** @brief Step the document's history back or forward and show the
 *         caret. */
tiku_edit_status_t tiku_codeview_undo(tiku_codeview_t *view);
tiku_edit_status_t tiku_codeview_redo(tiku_codeview_t *view);
/** @brief Put the selection on the host's clipboard, several in order
 *         with the view's line break between each two.  TIKU_EDIT_NOTHING
 *         with nothing selected, TIKU_EDIT_INVALID with no clipboard,
 *         TIKU_EDIT_LIMIT when the clipboard refused the text. */
tiku_edit_status_t tiku_codeview_copy(tiku_codeview_t *view);
/** @brief Copy, then delete what is selected only when the clipboard
 *         took it; carets among several selections stay. */
tiku_edit_status_t tiku_codeview_cut(tiku_codeview_t *view);
/** @brief Replace the selection with the clipboard's text, whole or not
 *         at all; several selections take one line each when it has as
 *         many lines, else all of it.  TIKU_EDIT_NOTHING when the
 *         clipboard is empty. */
tiku_edit_status_t tiku_codeview_paste(tiku_codeview_t *view);
/** @brief Why the edit the last event asked for was not made, or
 *         TIKU_EDIT_OK when it was made or the event asked for none. */
tiku_edit_status_t tiku_codeview_status(const tiku_codeview_t *view);

/** @brief What a search did. */
typedef struct {
    size_t replaced;            /* matches replaced                     */
    int found;                  /* a match is selected now              */
    int wrapped;                /* it started again from the other end  */
} tiku_codeview_found_t;

/**
 * @brief Select the first match at or after the caret, or with
 *        @p backward the last one before the selection's start, starting
 *        again from the other end when there is none that way.
 *
 * The match is selected from its start to its end and shown.
 * TIKU_EDIT_NOTHING when there is no match anywhere.  @p out may be NULL.
 */
tiku_edit_status_t tiku_codeview_find(tiku_codeview_t *view,
                                      const tiku_edit_query_t *q,
                                      int backward,
                                      tiku_codeview_found_t *out);
/** @brief Replace the selection with @p length bytes of @p with when it
 *         is exactly one match, as one undo step, then find the next
 *         match. */
tiku_edit_status_t tiku_codeview_replace(tiku_codeview_t *view,
                                         const tiku_edit_query_t *q,
                                         const char *with, size_t length,
                                         tiku_codeview_found_t *out);
/** @brief Replace every match in the document as one undo step; each
 *         view's selection moves with the text it was on. */
tiku_edit_status_t tiku_codeview_replace_all(tiku_codeview_t *view,
                                             const tiku_edit_query_t *q,
                                             const char *with,
                                             size_t length,
                                             tiku_codeview_found_t *out);

/**
 * @brief Take a key, text, pointer, wheel or tick event.
 *
 * Moves: the arrows, Home, End and the page keys, with Shift extending;
 * Command with Home or End, Option or Command with Left or Right.  Edits:
 * typed text, Return, Tab, Shift Tab, Backspace, Delete and text-input
 * commits; Command with C, X, V, A, Z, Shift Z and Y, and the line
 * commands' keys: / D, Shift K, Shift Up, Shift Down, M, Shift M, and
 * Backspace or Delete for a word.
 *
 * @note Button 1 in the text puts the caret, Shift extends, two clicks
 *       select a word and three a line, and dragging extends by the same
 *       unit; on a scrollbar it steps, pages or drags.  The wheel
 *       scrolls.
 * @return 1 when the view used the event, 0 when it is the
 *         application's; deactivation ends a drag and still returns 0.
 */
int tiku_codeview_event(tiku_codeview_t *view, const tiku_event_t *e);

/** @brief How the document's indicators of one kind are drawn. */
typedef enum {
    TIKU_CODEVIEW_MARK_NONE = 0,    /* not drawn, as in a new view      */
    TIKU_CODEVIEW_MARK_SQUIGGLE,    /* a wavy line under the text       */
    TIKU_CODEVIEW_MARK_LINE,        /* a straight line under it         */
    TIKU_CODEVIEW_MARK_BOX          /* a frame round it                 */
} tiku_codeview_mark_t;

/**
 * @brief Draw indicators of @p kind, from 0 to TIKU_EDIT_INDICATOR_KINDS
 *        - 1, as @p style in @p color, and that kind's annotations on a
 *        ground of @p color pulled halfway to the page's.
 *
 * Indicators are drawn under the text's ink; an annotation of a kind
 * with no style set has the panel's ground.
 */
tiku_edit_status_t tiku_codeview_set_mark(tiku_codeview_t *view,
                                          unsigned kind,
                                          tiku_codeview_mark_t style,
                                          tiku_rgb_t color);
/** @brief Select the next indicator of @p kinds after the selection's
 *         start, or with @p backward the one before it, starting again
 *         from the other end when there is none that way, and show it.
 *         TIKU_EDIT_NOTHING when there is none at all. */
tiku_edit_status_t tiku_codeview_goto_indicator(tiku_codeview_t *view,
                                                uint32_t kinds,
                                                int backward);

/**
 * @brief The last line of the region @p line heads, or -1 for none.
 *
 * A region is told by the rules: a line leaving an opener of a level open
 * heads the lines up to the one before its closer's line; with blocks, a
 * line opening a block heads them up to the one before its closer; with
 * neither, a line heads the lines after it indented further.
 */
int64_t tiku_codeview_fold_end(tiku_codeview_t *view, int64_t line);
/**
 * @brief Fold the region @p line heads in this view, or with @p open
 *        unfold it, showing its lines but a folded region's inside it.
 *
 * A selection reaching into what is folded becomes the caret at the end
 * of @p line; a line in a folded region unfolds unseen, its lines shown
 * with that region's.  TIKU_EDIT_NOTHING when there is nothing to do.
 */
tiku_edit_status_t tiku_codeview_fold(tiku_codeview_t *view, int64_t line,
                                      int open);
/** @brief Fold the region the caret's line heads or is in, the innermost,
 *         or with @p open unfold the caret's line. */
tiku_edit_status_t tiku_codeview_fold_at(tiku_codeview_t *view, int open);
/** @brief Fold every region, the inner ones first, or with @p open
 *         unfold them all. */
tiku_edit_status_t tiku_codeview_fold_all(tiku_codeview_t *view, int open);
/** @brief Whether @p line heads a folded region, and whether it is shown,
 *         in no folded region. */
int tiku_codeview_folded(const tiku_codeview_t *view, int64_t line);
int tiku_codeview_shown(const tiku_codeview_t *view, int64_t line);

/** @brief Fill the rows of lines marked @p kind with @p color, behind
 *         their text and selection; a higher kind's fill covers a lower
 *         one's.  A new view fills no kind. */
tiku_edit_status_t tiku_codeview_set_marker_fill(tiku_codeview_t *view,
                                                 unsigned kind,
                                                 tiku_rgb_t color);
/** @brief Stop filling the rows of lines marked @p kind. */
void tiku_codeview_clear_marker_fill(tiku_codeview_t *view, unsigned kind);
/** @brief Fill the row of the document's execution line with @p color,
 *         over any marker's fill, while the line is current; a new view
 *         fills it light green. */
void tiku_codeview_set_execution_fill(tiku_codeview_t *view,
                                      tiku_rgb_t color);

/** @brief Words offered for a range of the text. */
typedef struct {
    tiku_edit_range_t range;    /* what the chosen word replaces        */
    const char *const *items;   /* count words, copied when shown       */
    size_t count;
    size_t current;             /* the word chosen first                */
    int inserts;                /* keys and clicks put the chosen word
                                   in; 0: a click only chooses one      */
} tiku_codeview_list_t;

/**
 * @brief Show @p list in a box under the row of its range's start, or
 *        over it when only there is room, five words at a time, instead
 *        of any list shown before.
 *
 * The range follows edits.  While the list is shown, Escape hides it and
 * any tip, and a press on a word chooses it.  A list that inserts also
 * takes Up and Down, which move the choice round, and Return and Tab,
 * which put the chosen word in; with no words it still takes them and
 * changes nothing.
 */
tiku_edit_status_t tiku_codeview_list_show(tiku_codeview_t *view,
                                           const tiku_codeview_list_t *list);
/** @brief Hide the list. */
void tiku_codeview_list_hide(tiku_codeview_t *view);
/** @brief Whether a list is shown, with the chosen word's index in
 *         @p current, which may be NULL. */
int tiku_codeview_list_shown(const tiku_codeview_t *view, size_t *current);
/** @brief Replace the list's range with the chosen word as one undo step,
 *         the caret after it, then hide the list.  TIKU_EDIT_NOTHING when
 *         there is no list or no word. */
tiku_edit_status_t tiku_codeview_list_insert(tiku_codeview_t *view);

/**
 * @brief Show @p text in a tip by the row of @p pos, beside a shown list
 *        or else under or over the row, instead of any tip shown before.
 *
 * Lines break at each LF and wrap at 320 pixels.  The tip goes on
 * Escape, on a load, and when an event leaves the caret before @p pos or
 * on another line; a press on it does nothing else.
 */
tiku_edit_status_t tiku_codeview_tip_show(tiku_codeview_t *view,
                                          tiku_edit_pos_t pos,
                                          const char *text);
/** @brief Hide the tip. */
void tiku_codeview_tip_hide(tiku_codeview_t *view);
/** @brief Whether a tip is shown. */
int tiku_codeview_tip_shown(const tiku_codeview_t *view);

/** @brief Paint the view into its bounds on @p s, within the clip @p s
 *         already has, which is restored afterwards.  Everything drawn
 *         is recorded when @p s records. */
void tiku_codeview_paint(tiku_codeview_t *view, tiku_surface_t *s);

#endif /* TIKU_CODEVIEW_H_ */
