/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_textview.h - many lines of editable text, with a caret in them.
 *
 * tiku_field is one line, a name being typed over; this is a document,
 * where Return makes a line rather than committing, the caret is a line
 * and a column, and the screen is a window onto something taller.  It
 * owns the text, the caret and the scroll and nothing else: the pixels
 * are the caller's, since a text view's look is what every application
 * wants differently.  Every edit leaves the caret on a real line at a
 * real column, the invariant hand-rolled editors get wrong first.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEXTVIEW_H_
#define TIKU_TEXTVIEW_H_

#include <stddef.h>

#define TIKU_TEXTVIEW_LINES_MAX 4000
#define TIKU_TEXTVIEW_LINE_MAX  1024
#define TIKU_TEXTVIEW_UNDO      256

typedef struct {
    char *text;                 /* NUL-terminated, owned */
    int   len;
} tiku_textview_line_t;

typedef struct {
    tiku_textview_line_t line[TIKU_TEXTVIEW_LINES_MAX];
    int nline;
    int cy, cx;                 /* the caret: line, then column */
    int ay, ax;                 /* the selection's anchor, line then col */
    int selecting;              /* a selection is being HELD from anchor */
    int top;                    /* first line shown             */
    /* The journal: every edit as an insert or a delete of text at a
     * place, runs of typing folded into one, so a word undoes in one
     * step.  "Modified" is this journal's DISTANCE from the mark a save
     * left, so undoing back to a save reads as unmodified. */
    struct tiku_textview_edit {
        int   kind;             /* 0 empty, 1 inserted, 2 deleted */
        int   line, col;        /* where the run began */
        char *text;             /* the characters, '\n' between lines */
        int   len;
        int   coalesce;         /* a like edit beside it may still fold in */
        int   group;            /* 0 solo; else a batch undone as one */
    } undo[TIKU_TEXTVIEW_UNDO];
    int undo_n;                 /* entries held (redo history is above at) */
    int undo_at;                /* the present: undo below it, redo above */
    int saved_at;               /* undo_at when the file was last saved */
    int recording;             /* 0 while undo/redo replays, so it is quiet */
    int group_open;             /* the batch id edits join now, 0 = none */
    int group_seq;              /* the last batch id handed out */
} tiku_textview_t;

/** @brief Start on one empty line, which is what an empty document is. */
void tiku_textview_init(tiku_textview_t *tv);

/** @brief Give back every line.  The view is empty after, not invalid. */
void tiku_textview_free(tiku_textview_t *tv);

/**
 * @brief Replace the whole text, splitting @p text on newlines.
 *
 * A trailing newline does NOT make a final empty line: a file that ends
 * as files do would otherwise grow a blank line every time it was opened
 * and saved.
 */
void tiku_textview_set(tiku_textview_t *tv, const char *text);

/** @brief How many lines there are (never fewer than one). */
int tiku_textview_lines(const tiku_textview_t *tv);

/** @brief Line @p i, or "" for one that does not exist. */
const char *tiku_textview_line(const tiku_textview_t *tv, int i);

/** @brief Length of line @p i. */
int tiku_textview_line_len(const tiku_textview_t *tv, int i);

/** @brief Whether it has been edited since the last set or save. */
int tiku_textview_modified(const tiku_textview_t *tv);

/** @brief Say it has been saved: the modified flag clears. */
void tiku_textview_saved(tiku_textview_t *tv);

/** @brief Where the caret is. */
void tiku_textview_caret(const tiku_textview_t *tv, int *line, int *col);

/** @brief The first line on screen. */
int tiku_textview_top(const tiku_textview_t *tv);

/** @brief Put the caret on @p line, @p col, clamped into the text. */
void tiku_textview_place(tiku_textview_t *tv, int line, int col);

/**
 * @brief Begin a selection at the caret, if one is not already held.
 *
 * Idempotent while a selection stands: the anchor is dropped ONCE, at
 * the caret it was called on, and every extending move after grows the
 * span from there.  A shift+move calls this before moving; a plain move
 * calls tiku_textview_select_none() instead.
 */
void tiku_textview_select_begin(tiku_textview_t *tv);

/** @brief Let a selection go; the caret stays where it is. */
void tiku_textview_select_none(tiku_textview_t *tv);

/** @brief Take the whole document: anchor at the top, caret at the end. */
void tiku_textview_select_all(tiku_textview_t *tv);

/**
 * @brief The selected span, NORMALISED so start is not past end.
 *
 * @return 1 and fills the four coordinates when a non-empty selection
 * is held; 0 (and touches nothing) when there is none.  An insert, a
 * newline, a backspace or a delete first REMOVES this span, so typing
 * replaces what is selected -- the behaviour lives here, once, for
 * every editor that uses the view.
 */
int tiku_textview_selection(const tiku_textview_t *tv,
                            int *y0, int *x0, int *y1, int *x1);

/**
 * @brief The selected text, newlines between its lines, NUL-closed.
 *
 * @return the length written (not counting the NUL); 0 when nothing is
 * selected or the buffer cannot hold the first character.
 */
size_t tiku_textview_selected(const tiku_textview_t *tv, char *out,
                              size_t max);

/** @brief The bytes tiku_textview_selected() needs for the whole
 *         selection, its NUL apart; 0 when nothing is selected. */
size_t tiku_textview_selected_size(const tiku_textview_t *tv);

/**
 * @brief Type @p text at the caret, newlines breaking lines.
 *
 * The first character removes a held selection exactly as a typed key
 * does -- pasting over a selection replaces it -- and the rest lands
 * after it. Text bytes, tabs and line endings are preserved, including
 * UTF-8 bytes, so whole-document edits and undo do not strip file data.
 */
void tiku_textview_insert_text(tiku_textview_t *tv, const char *text);
/** @brief Insert one validated UTF-8 commit as one undo group.
 * Refuses invalid text or line/count overflow before changing the selection.
 * Public caret columns remain byte offsets, snapped to codepoint boundaries.
 */
int tiku_textview_commit(tiku_textview_t *tv, const char *text);

/** @brief Take back the last edit; @return 1 if there was one. */
int tiku_textview_undo(tiku_textview_t *tv);

/** @brief Do again the last undone edit; @return 1 if there was one. */
int tiku_textview_redo(tiku_textview_t *tv);

/** @brief Whether there is an edit to undo / a undone edit to redo. */
int tiku_textview_can_undo(const tiku_textview_t *tv);
int tiku_textview_can_redo(const tiku_textview_t *tv);

/**
 * @brief Bracket a batch of edits so undo takes ALL of them at once.
 *
 * A renumber rewrites every line; without this it would be a string of
 * undos to walk back.  Between begin and end the edits still happen
 * one by one -- and coalesce as usual -- but each is stamped with one
 * batch id, and undo/redo carry the whole run of a batch together.
 */
void tiku_textview_group_begin(tiku_textview_t *tv);
void tiku_textview_group_end(tiku_textview_t *tv);

/** @brief Type one character at the caret. */
void tiku_textview_insert(tiku_textview_t *tv, char ch);

/** @brief Break the line at the caret; the tail becomes the next line. */
void tiku_textview_newline(tiku_textview_t *tv);

/**
 * @brief Rub out backwards.
 *
 * At the head of a line this joins it to the one above, which is where
 * the caret lands -- at the seam, not at the start.
 */
void tiku_textview_backspace(tiku_textview_t *tv);

/** @brief Rub out forwards, joining the next line at a line's end. */
void tiku_textview_delete(tiku_textview_t *tv);

/** @brief Which way the caret is being moved. */
typedef enum {
    TIKU_TEXTVIEW_LEFT = 0,
    TIKU_TEXTVIEW_RIGHT,
    TIKU_TEXTVIEW_UP,
    TIKU_TEXTVIEW_DOWN,
    TIKU_TEXTVIEW_HOME,
    TIKU_TEXTVIEW_END
} tiku_textview_move_t;

/**
 * @brief Move the caret.
 *
 * Left at the head of a line goes to the END of the one above, and right
 * at the end goes to the head of the one below: the caret walks the text
 * as one run, the way it reads, rather than stopping at line ends.
 */
void tiku_textview_move(tiku_textview_t *tv, tiku_textview_move_t how);

/**
 * @brief Scroll so the caret is among the @p rows lines on screen.
 *
 * @return nonzero when the first line shown changed.
 */
int tiku_textview_reveal(tiku_textview_t *tv, int rows);

/** @brief Scroll to @p top directly, clamped to the text. */
int tiku_textview_scroll_to(tiku_textview_t *tv, int top, int rows);

/**
 * @brief Write the whole text into @p out, lines joined by newlines.
 *
 * @return the length written, or the length it would need when @p out is
 *         too small -- so a caller can size a buffer by asking twice.
 */
size_t tiku_textview_text(const tiku_textview_t *tv, char *out, size_t max);

#endif /* TIKU_TEXTVIEW_H_ */
