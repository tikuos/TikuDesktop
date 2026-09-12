/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_form.h - a panel that was DESCRIBED rather than programmed.
 *
 * A few lines of text say what controls there are, what each is called,
 * and which place in the namespace each one shows or changes, such as
 * "gauge Temperature /devices/board1/sys/temp 0 100".  The shell renders
 * that, tracks it, reads the bound places and writes to them when a
 * control is worked, so a device with no interface code of its own has
 * one.  It is not a layout language: no coordinates, sizes or nesting,
 * only rows in the order written, because the drawing end knows the
 * face, the theme and the room.  The thesis is in doc/forms.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FORM_H_
#define TIKU_FORM_H_

#include "tiku_gfx.h"

#define TIKU_FORM_ROWS   16
#define TIKU_FORM_LABEL  40
#define TIKU_FORM_BIND   256
#define TIKU_FORM_VALUE  64
#define TIKU_FORM_TITLE  48

/** @brief What a row IS. */
typedef enum {
    TIKU_FORM_TEXT = 0,     /* words, and nothing to work            */
    TIKU_FORM_GAUGE,        /* how full something is                 */
    TIKU_FORM_TOGGLE,       /* on or off, and it can be changed      */
    TIKU_FORM_FIELD,        /* what a place says, shown as words     */
    TIKU_FORM_BUTTON,       /* a press writes a value to the place   */
    TIKU_FORM_ENTRY,        /* words a person types and enters       */
    TIKU_FORM_SLIDER,       /* a number set by where it is pressed   */
    TIKU_FORM_CHOICE        /* one of a few words, pressed through   */
} tiku_form_kind_t;

typedef struct {
    unsigned char kind;
    char label[TIKU_FORM_LABEL];
    char bind[TIKU_FORM_BIND];      /* the place it shows or changes */
    char value[TIKU_FORM_VALUE];    /* what that place last said     */
    char options[TIKU_FORM_VALUE];  /* a choice's words, | between   */
    char edit[TIKU_FORM_VALUE];     /* an entry's words while typed  */
    int  lo, hi;                    /* a gauge's or slider's ends    */
    int  known;                     /* nonzero once value was read   */
    int  editing;                   /* an entry with the caret       */
} tiku_form_row_t;

typedef struct {
    char title[TIKU_FORM_TITLE];
    int  nrow;
    tiku_form_row_t row[TIKU_FORM_ROWS];
} tiku_form_t;

/**
 * @brief Read a description into @p f.
 *
 * A line the parser does not understand is SKIPPED rather than
 * refusing the whole description: a panel from a device built after
 * this desktop will have rows this desktop has never heard of, and
 * showing the rows it does understand is better than showing nothing.
 * The skipped ones are counted so a caller can say so.
 *
 * @return the number of rows read; @p unknown, when given, takes the
 *         number of lines that were passed over.
 */
int tiku_form_parse(tiku_form_t *f, const char *text, int *unknown);

/** @brief How tall the described panel wants to be, at this face. */
int tiku_form_height(const tiku_form_t *f);

/** @brief Where row @p i falls when the form is drawn in @p body. */
/** @brief The control half of row @p i: where a slider's track lies. */
tiku_rect_t tiku_form_control_rect(const tiku_form_t *f, tiku_rect_t body,
                                   int i);

/**
 * @brief A press at @p x on row @p i's control: a slider takes the value
 *        under the pointer.  @return what to write, or NULL.
 */
const char *tiku_form_press_at(tiku_form_t *f, tiku_rect_t body, int i,
                               int x);

/** @brief Give row @p i the caret, when it is an entry; -1 takes it away. */
void tiku_form_focus(tiku_form_t *f, int i);

/** @brief The row with the caret, or -1. */
int tiku_form_editing(const tiku_form_t *f);

/**
 * @brief A key while an entry has the caret.
 * @return 0 when no entry has it, 1 taken, 2 entered (write the row's
 *         value), 3 reverted with Escape.
 */
int tiku_form_key(tiku_form_t *f, unsigned key, char ch);
/** @brief Append UTF-8 to the focused entry without writing its value. */
int tiku_form_text(tiku_form_t *f, const char *text);

tiku_rect_t tiku_form_row_rect(const tiku_form_t *f, tiku_rect_t body,
                               int i);

/** @brief Which row @p x, @p y lands on, or -1. */
int tiku_form_at(const tiku_form_t *f, tiku_rect_t body, int x, int y);

/**
 * @brief Draw the form in @p body.
 *
 * Every row records itself as the control it is, so a described panel
 * narrates like any other window, and that is what it is for: a board's
 * interface is readable by a person, a test and an agent without the
 * board knowing any of them exist.
 */
void tiku_form_draw(const tiku_form_t *f, tiku_surface_t *s,
                    tiku_rect_t body);

/**
 * @brief What working row @p i should WRITE to its bound place, or NULL
 *        when working it writes nothing.
 *
 * A toggle answers the other of 0 and 1; a button answers what it was
 * described with.  The caller does the writing, because only the caller
 * knows the namespace -- this file knows what the gesture MEANT.
 */
const char *tiku_form_press(tiku_form_t *f, int i);

#endif /* TIKU_FORM_H_ */
