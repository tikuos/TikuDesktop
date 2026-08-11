/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_find.h - the Find panel.
 *
 * Three modes, as the original has: by name, by attribute, by formula.  The
 * attribute mode is a grid of rows, each an attribute, an operator and a
 * value, with the and/or that joins a row to the NEXT one sitting on the row
 * above -- and the fold is strictly left-to-right with no precedence, so
 * "a or b and c" means ((a or b) and c) and not what arithmetic would say.
 *
 * Switching into formula mode pre-fills the field with the predicate the
 * controls would have produced, which is the panel's best feature: it is how
 * the query language is discovered rather than documented.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FIND_H_
#define TIKU_TRK_FIND_H_

#include "tiku_trk_menu.h"
#include "tiku_trk_query.h"

#define TIKU_TRK_FIND_ROWS_MAX   4
#define TIKU_TRK_FIND_PLACES_MAX 8
#define TIKU_TRK_FIND_TEXT_MAX   64

/** @brief Which control has the caret. */
typedef enum {
    TIKU_TRK_FIND_FOCUS_NONE = 0,
    TIKU_TRK_FIND_FOCUS_NAME,
    TIKU_TRK_FIND_FOCUS_ROW,       /* focus_row says which                 */
    TIKU_TRK_FIND_FOCUS_FORMULA
} tiku_trk_find_focus_t;

/** @brief What a click landed on. */
typedef enum {
    TIKU_TRK_FIND_HIT_NONE = 0,
    TIKU_TRK_FIND_HIT_MODE,
    TIKU_TRK_FIND_HIT_FIELD,       /* the attribute menu of a row          */
    TIKU_TRK_FIND_HIT_OP,          /* its operator menu                    */
    TIKU_TRK_FIND_HIT_VALUE,       /* its text field                       */
    TIKU_TRK_FIND_HIT_JOIN,        /* the and/or between this row and next */
    TIKU_TRK_FIND_HIT_ADD,
    TIKU_TRK_FIND_HIT_REMOVE,
    TIKU_TRK_FIND_HIT_SCOPE,
    TIKU_TRK_FIND_HIT_SEARCH,
    TIKU_TRK_FIND_HIT_SAVE
} tiku_trk_find_hit_t;

typedef struct {
    tiku_trk_qfield_t field;
    tiku_trk_qop_t    op;
    char              value[TIKU_TRK_FIND_TEXT_MAX];
    tiku_trk_qjoin_t  join;        /* joins this row to the NEXT one       */
} tiku_trk_find_row_t;

typedef struct {
    tiku_trk_qmode_t      mode;
    char                  name_text[TIKU_TRK_FIND_TEXT_MAX];
    tiku_trk_find_row_t   row[TIKU_TRK_FIND_ROWS_MAX];
    int                   row_count;
    char                  formula[TIKU_TRK_QUERY_PRED_MAX];
    /* Scope.  "Everywhere" and a set of places are mutually exclusive the
     * way "All disks" and individual volumes are. */
    char                  place[TIKU_TRK_FIND_PLACES_MAX][TIKU_TRK_NAME_MAX];
    int                   place_count;
    unsigned              place_mask;
    int                   everywhere;
    tiku_trk_find_focus_t focus;
    int                   focus_row;
    /* A popup currently showing, and what it belongs to. */
    tiku_trk_menu_t       popup;
    tiku_trk_find_hit_t   popup_for;
    int                   popup_row;
    tiku_desk_rect_t      popup_frame;
} tiku_trk_find_t;

/** @brief Start a panel with one attribute row and no scope restriction. */
void tiku_trk_find_init(tiku_trk_find_t *f);

/** @brief Offer @p name as a place the search can be scoped to. */
int tiku_trk_find_add_place(tiku_trk_find_t *f, const char *name);

/**
 * @brief Change mode.
 *
 * Entering formula mode fills the field from the controls being left, so the
 * predicate is visible and editable rather than hidden.
 */
void tiku_trk_find_set_mode(tiku_trk_find_t *f, tiku_trk_qmode_t mode);

/** @brief Add a row.  @return 1 when one was added. */
int tiku_trk_find_add_row(tiku_trk_find_t *f);

/** @brief Remove the last row; refused while only one remains. */
int tiku_trk_find_remove_row(tiku_trk_find_t *f);

/** @brief Build the query the panel is describing. */
void tiku_trk_find_build(const tiku_trk_find_t *f, tiku_trk_query_t *q);

/**
 * @brief The name a saved query would get.
 *
 * A description of the criteria, then " - " and a timestamp, sanitised of
 * anything a path cannot carry.
 */
int tiku_trk_find_default_name(const tiku_trk_find_t *f, long stamp,
                               char *out, size_t max);

/** @brief The scope summary shown on the button: a name, "N places", or all. */
int tiku_trk_find_scope_label(const tiku_trk_find_t *f, char *out,
                              size_t max);

/** @brief Natural height for @p width. */
int tiku_trk_find_height(const tiku_trk_find_t *f);

void tiku_trk_find_draw(tiku_trk_find_t *f, tiku_desk_surface_t *s,
                        tiku_desk_rect_t frame);

/**
 * @brief What is under a point.
 *
 * @param row Receives the row index for row-owned parts.
 */
tiku_trk_find_hit_t tiku_trk_find_hit(const tiku_trk_find_t *f,
                                      tiku_desk_rect_t frame, int x, int y,
                                      int *row);

/**
 * @brief Handle a click.
 *
 * @return The hit, so the caller can act on Search and Save; everything else
 *         is handled inside the panel.
 */
tiku_trk_find_hit_t tiku_trk_find_click(tiku_trk_find_t *f,
                                        tiku_desk_rect_t frame, int x, int y);

/** @brief Feed a character to whichever field has the caret. */
void tiku_trk_find_key(tiku_trk_find_t *f, char ch);

void tiku_trk_find_backspace(tiku_trk_find_t *f);

#endif /* TIKU_TRK_FIND_H_ */
