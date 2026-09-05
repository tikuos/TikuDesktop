/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_list.h - a list of rows, and everything about it that is not
 * Which rows there are, which are selected, which the keyboard is on,
 * which is first shown, where a row sits and which row a click lands on,
 * and the rules turning a gesture into a change of all that.  It owns no
 * names, art or columns, since what a row looks like is the one thing
 * every list wants differently, and one index space: a row's index is
 * its place in the list and on the screen.  The selection rules are the
 * pose view's, lifted out with the icons and columns left behind; they
 * are stated in doc/lists.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LIST_H_
#define TIKU_LIST_H_

#include <stdint.h>

#include "tiku_event.h"
#include "tiku_gfx.h"

/**
 * @brief How many rows one list may hold.
 *
 * The flags are a byte a row and they live in the struct, so this is
 * what a list costs whether or not it is full.  A caller with more rows
 * than this is told so rather than quietly given a list that ends early
 * -- see tiku_list_set_count().
 */
#define TIKU_LIST_MAX 1024

/** @brief How long a type-ahead spelling lives before it starts over. */
#define TIKU_LIST_TYPEAHEAD_US 1000000

/** @brief What a row is called, asked for rather than stored. */
typedef const char *(*tiku_list_name_fn)(void *ctx, int row);

typedef struct {
    int  count;
    int  top;               /* the first row shown                     */
    int  cursor;            /* where the keyboard is, or -1            */
    int  pivot;             /* what a range reaches from, or -1        */
    int  last_added;        /* the far end of the last range, or -1    */
    int  single;            /* one row at a time                       */
    int  row_h;             /* 0 asks the face                         */
    /* The spelling a person is typing, and when they last typed into
     * it.  It lapses on its own so an old attempt cannot join the next
     * one -- which is what makes type-ahead feel like spelling a name
     * rather than accumulating keystrokes. */
    char    typed[32];
    int64_t typed_at;
    unsigned char sel[TIKU_LIST_MAX];
    /* Which rows are HEADINGS: furniture among the rows rather than
     * rows of their own.  A byte a row, beside the selection, because
     * the alternative is a second list of positions that drifts out of
     * step with this one the first time the rows change. */
    unsigned char head[TIKU_LIST_MAX];
} tiku_list_t;

/** @brief An empty list.  @p single nonzero holds one row at a time. */
void tiku_list_init(tiku_list_t *l, int single);

/**
 * @brief Say how many rows there are now.
 *
 * Anchors and the cursor are brought back inside the new count, and
 * rows that no longer exist stop being selected -- so a list that
 * shrinks under a selection cannot leave an anchor pointing at nothing.
 *
 * @return the count it holds, which is @p count clamped to
 *         TIKU_LIST_MAX -- a caller that cares compares them.
 */
int tiku_list_set_count(tiku_list_t *l, int count);

int tiku_list_count(const tiku_list_t *l);

/** @brief How tall a row is: the caller's, or the face's if none was set. */
int tiku_list_row_h(const tiku_list_t *l);
void tiku_list_set_row_h(tiku_list_t *l, int h);

/** @brief How many rows @p body has room for. */
int tiku_list_visible_rows(const tiku_list_t *l, tiku_rect_t body);

/**
 * @brief Where row @p row sits when the list is drawn in @p body.
 *
 * The rect comes back for a row that is not shown too -- above the top
 * or below the last -- so a caller can ask without checking first; it
 * is simply outside @p body, and clipping to the body is the caller's
 * job either way.  A row the list does not have is degenerate (zero
 * height) rather than off the end.
 */
tiku_rect_t tiku_list_row_rect(const tiku_list_t *l, tiku_rect_t body,
                               int row);

/** @brief Which row @p x, @p y lands on in @p body, or -1 for none. */
int tiku_list_at(const tiku_list_t *l, tiku_rect_t body, int x, int y);

/**
 * @brief Make @p row a heading, or an ordinary row again.
 *
 * A heading names the rows under it and is still a row of the one index
 * space, but it is not pickable: no click, range, select-all or
 * inversion selects it, the cursor steps over it, and spelling a name
 * passes it by, since a person travelling a list with the arrows is
 * looking for something to open.  Set headings after
 * tiku_list_set_count(): a shrinking list forgets the rows it lost.
 */
void tiku_list_set_heading(tiku_list_t *l, int row, int heading);

int tiku_list_is_heading(const tiku_list_t *l, int row);

int tiku_list_selected(const tiku_list_t *l, int row);
int tiku_list_selection_count(const tiku_list_t *l);

/** @brief The first selected row, or -1.  What a single-selection
 *         caller means by "the selection". */
int tiku_list_chosen(const tiku_list_t *l);

/** @brief Select exactly @p row and anchor on it. */
void tiku_list_select_only(tiku_list_t *l, int row);

/** @brief Every row (or, holding one at a time, just the first). */
void tiku_list_select_all(tiku_list_t *l);

/** @brief No row, and no anchors either. */
void tiku_list_select_none(tiku_list_t *l);

/**
 * @brief Flip every row.
 *
 * The anchors go first and are re-taken only if row 0 ends up selected:
 * an inverted selection has no click behind it, so there is nowhere
 * honest for a range to reach from.
 */
void tiku_list_invert(tiku_list_t *l);

/**
 * @brief A click on @p row (-1 for the empty space below the rows).
 *
 * @p modifiers is the event's mask.  TIKU_MOD_CMD toggles the one row;
 * TIKU_MOD_SHIFT reaches from the anchor to @p row; the two together
 * give the whole range whatever state @p row did NOT have, so reaching
 * out to add to a selection cannot clear it.  Plain, on a row that is
 * already part of a selection, does nothing at all -- which is what
 * lets a multi-row drag survive the press that starts it.
 */
void tiku_list_click(tiku_list_t *l, int row, unsigned modifiers);

/**
 * @brief Offer @p key to the list.
 *
 * Up and Down move by one, Page by a bodyful, Home and End to the ends;
 * with TIKU_MOD_SHIFT each reaches from the anchor instead of starting
 * over.  Cmd-A selects everything.  The top follows, so a caller that
 * draws from tiku_list_top() never scrolls by hand.  Return is not
 * handled, deliberately: what opening a row means is the caller's, and
 * a list that swallowed it would make every caller work around it.
 *
 * @return nonzero when the list took the key.
 */
int tiku_list_key(tiku_list_t *l, unsigned key, unsigned modifiers,
                  tiku_rect_t body);

/**
 * @brief Spell a name to jump to it.
 *
 * @p ch is the character typed and @p now the clock in microseconds.
 * Letters typed close together spell one word; after a second of quiet
 * the next letter starts a new one.  The first row whose name begins
 * with the spelling (ignoring case) is selected and revealed.
 *
 * @return the row jumped to, or -1 when nothing begins that way -- and
 *         nothing moves in that case, because a spelling that matches
 *         nothing is a spelling still being typed.
 */
int tiku_list_typeahead(tiku_list_t *l, char ch, int64_t now,
                        tiku_rect_t body, tiku_list_name_fn name,
                        void *ctx);

/**
 * @brief Bring the cursor's row into @p body.
 * @return nonzero when the top moved.
 */
int tiku_list_reveal(tiku_list_t *l, tiku_rect_t body);

/**
 * @brief Show @p top first, clamped so the list cannot be scrolled off.
 * @return nonzero when the top moved.
 */
int tiku_list_scroll_to(tiku_list_t *l, int top, tiku_rect_t body);

/**
 * @brief Draw the rows in @p body as plain named rows.
 *
 * The simple look: one line of text a row, selected rows in the
 * selection colours, through the kit's row painter, so each row crosses
 * the wire as the row it is and a reader is told its name and whether
 * it is picked.  A caller that wants columns, art or a second line draws
 * from tiku_list_row_rect() instead.  A heading crosses as words rather
 * than as a row: a reader counting rows is not told about furniture.
 */
void tiku_list_draw(const tiku_list_t *l, tiku_surface_t *s,
                    tiku_rect_t body, tiku_list_name_fn name, void *ctx);

#endif /* TIKU_LIST_H_ */
