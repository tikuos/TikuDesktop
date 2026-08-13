/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_edit.h - the in-place text editor a rename types into.
 *
 * Tracker's BTextWidget hands editing to a BTextView placed over the row.
 * The behaviours that matter are not the text view's: the editor opens with
 * everything selected so the first keystroke replaces the old name, Escape
 * abandons and Return commits, and while it is up the view's own keys are
 * off -- an arrow moves the caret, not the selection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_EDIT_H_
#define TIKU_TRK_EDIT_H_

#include <stddef.h>

/**
 * @brief The editor's ceiling.
 *
 * Not smaller than the longest value a pose can hold: an editor that opens
 * already truncated commits the truncation, silently shortening a value the
 * user never touched.
 */
#define TIKU_TRK_EDIT_MAX 256

/** @brief Keys the editor understands beyond ordinary characters. */
typedef enum {
    TIKU_TRK_EDIT_CHAR = 0,
    TIKU_TRK_EDIT_LEFT,
    TIKU_TRK_EDIT_RIGHT,
    TIKU_TRK_EDIT_HOME,
    TIKU_TRK_EDIT_END,
    TIKU_TRK_EDIT_BACKSPACE,
    TIKU_TRK_EDIT_DELETE,
    TIKU_TRK_EDIT_SELECT_ALL,
    TIKU_TRK_EDIT_COMMIT,
    TIKU_TRK_EDIT_CANCEL
} tiku_trk_edit_key_t;

/** @brief What the caller should do after a key. */
typedef enum {
    TIKU_TRK_EDIT_IGNORED = 0,
    TIKU_TRK_EDIT_HANDLED,
    TIKU_TRK_EDIT_COMMITTED,
    TIKU_TRK_EDIT_CANCELLED
} tiku_trk_edit_result_t;

/** @brief An open editor: text, caret and the selected range. */
typedef struct {
    char text[TIKU_TRK_EDIT_MAX];
    int  len;
    int  caret;
    int  sel_a, sel_b;              /* equal means nothing is selected     */
    int  active;
    /* Characters this field will not accept at all.  A name's illegal set
     * belongs to the STORE -- a namespace path and a file name forbid
     * different things -- so it is supplied rather than hardcoded. */
    const char *deny;
} tiku_trk_edit_t;

/**
 * @brief Open the editor over @p text, with all of it selected.
 *
 * Selected because the common rename is a REPLACEMENT: typing over an
 * inherited name is the case, and having to clear it first would make every
 * rename two operations.
 */
void tiku_trk_edit_start(tiku_trk_edit_t *e, const char *text);

/**
 * @brief Refuse @p deny's characters at the keystroke (MA-046).
 *
 * Set after starting.  NULL accepts anything printable.
 */
void tiku_trk_edit_deny(tiku_trk_edit_t *e, const char *deny);

/** @brief Feed one key.  @p ch is used only for TIKU_TRK_EDIT_CHAR. */
tiku_trk_edit_result_t tiku_trk_edit_key(tiku_trk_edit_t *e,
                                         tiku_trk_edit_key_t key,
                                         int shift, char ch);

/** @brief Close without deciding; the text is left as it stands. */
void tiku_trk_edit_stop(tiku_trk_edit_t *e);

/** @brief The selected range, ordered.  @return 1 when there is one. */
int tiku_trk_edit_selection(const tiku_trk_edit_t *e, int *from, int *to);

#endif /* TIKU_TRK_EDIT_H_ */
