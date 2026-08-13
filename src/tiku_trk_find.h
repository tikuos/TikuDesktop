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
#include "tiku_trk_types.h"

struct tiku_trk_store;

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
    TIKU_TRK_FIND_HIT_TRASH,
    TIKU_TRK_FIND_HIT_TYPE,        /* the "of type" menu (Q-004)           */
    TIKU_TRK_FIND_HIT_SEARCH,
    TIKU_TRK_FIND_HIT_SAVE,
    TIKU_TRK_FIND_HIT_QUERIES      /* the templates and recent queries     */
} tiku_trk_find_hit_t;

typedef struct {
    tiku_trk_qfield_t field;
    tiku_trk_qop_t    op;
    char              value[TIKU_TRK_FIND_TEXT_MAX];
    tiku_trk_qjoin_t  join;        /* joins this row to the NEXT one       */
} tiku_trk_find_row_t;

/** @brief How many past runs the Queries menu offers (Q-032). */
#define TIKU_TRK_FIND_RECENT_MAX 10

/** @brief How many types the most-used list keeps at the top (Q-016). */
#define TIKU_TRK_FIND_MOST_USED 4
#define TIKU_TRK_FIND_TYPE_CHOICES TIKU_TRK_TYPES_MAX
#define TIKU_TRK_FIND_TYPE_SUPERS 8

typedef struct {
    tiku_trk_qmode_t      mode;
    /* The type every term is narrowed to, or "" for all.  Held as the type
     * STRING rather than as an index into the table, because it is what
     * goes into the query and what comes back out of a saved one. */
    char                  type[TIKU_TRK_TYPE_MAX];
    char                  name_text[TIKU_TRK_FIND_TEXT_MAX];
    tiku_trk_find_row_t   row[TIKU_TRK_FIND_ROWS_MAX];
    int                   row_count;
    char                  formula[TIKU_TRK_QUERY_PRED_MAX];
    /* Scope.  "Everywhere" and a set of places are mutually exclusive the
     * way "All disks" and individual volumes are. */
    char                  place[TIKU_TRK_FIND_PLACES_MAX][TIKU_TRK_NAME_MAX];
    uint64_t              place_dev[TIKU_TRK_FIND_PLACES_MAX];
    char                  place_volume[TIKU_TRK_FIND_PLACES_MAX]
                                      [TIKU_TRK_NAME_MAX];
    int                   place_count;
    unsigned              place_mask;
    int                   everywhere;
    int                   include_trash;
    tiku_trk_find_focus_t focus;
    int                   focus_row;
    /* A popup currently showing, and what it belongs to. */
    tiku_trk_menu_t       popup;
    tiku_trk_find_hit_t   popup_for;
    int                   popup_row;
    tiku_desk_rect_t      popup_frame;
    /* Type submenus are owned by the panel: menu items intentionally keep
     * non-owned pointers, so stack-built children would dangle on return. */
    tiku_trk_menu_t       type_sub[TIKU_TRK_FIND_TYPE_SUPERS];
    int                   type_sub_count;
    tiku_trk_menu_t      *popup_sub;
    tiku_desk_rect_t      popup_sub_frame;
    char                  type_choice[TIKU_TRK_FIND_TYPE_CHOICES]
                                     [TIKU_TRK_TYPE_MAX];
    int                   type_choice_count;
    /* What the last action through the Queries menu did, for the shell's
     * status line: a menu that loads a saved search without saying which
     * one leaves the user looking at a panel that changed by itself. */
    char                  note[80];
    /* The record the panel came from.  Templates normally remain sources;
     * edit_template is set only by the explicit in-place edit action. */
    char                  loaded_name[128];
    tiku_trk_qrec_t       loaded_kind;
    int                   edit_template;
    /* Where the most-used type list lives.  Held on the panel because the
     * menu is built where the click lands, and passing it through every
     * hit-testing call would put a store in the geometry's signature. */
    struct tiku_trk_store *store;
    const tiku_trk_types_t *types;
} tiku_trk_find_t;

/**
 * @brief Load the record @p name into the panel (Q-031, Q-032, Q-035).
 *
 * The same call whether it is a template or a query: what differs is what
 * the record SAYS it is, and a template loaded into the panel is exactly
 * a panel filled in and not run.
 *
 * @return 1 when it loaded whole, 0 when it loaded with something lost,
 *         -1 when there is no such record.
 */
int tiku_trk_find_load_named(tiku_trk_find_t *f, const char *name);

/**
 * @brief Save what the panel holds as a TEMPLATE under @p name (Q-028).
 *
 * A template is not run when it is opened -- it fills the panel in -- so
 * this is the second save action the Save button could never be.
 *
 * @return 0 when written.
 */
int tiku_trk_find_save_template(tiku_trk_find_t *f, const char *name,
                                int64_t now);

/** @brief Load a template specifically for replacement in place. */
int tiku_trk_find_edit_template(tiku_trk_find_t *f, const char *name);

/** @brief Save back to the explicitly attached query or template. */
int tiku_trk_find_save_current(tiku_trk_find_t *f, int64_t now);

/** @brief Load (or create) the unattached template used by a fresh panel. */
int tiku_trk_find_open_default(tiku_trk_find_t *f, int64_t now);

/** @brief Whether the separate Save action has an attached destination. */
int tiku_trk_find_can_save_current(const tiku_trk_find_t *f);

/** @brief Whether the panel is the special Edit Query template window. */
int tiku_trk_find_editing_template(const tiku_trk_find_t *f);

/** @brief Whether the default action saves the attached template only. */
int tiku_trk_find_primary_is_save(const tiku_trk_find_t *f);

/**
 * @brief Whether one of the panel's popups is showing.
 *
 * A popup hangs BELOW the panel's window, over whatever is behind it, so
 * the shell has to send it the click wherever it lands -- otherwise the
 * menu is drawn over the Desktop and clicked on the Desktop.
 */
int tiku_trk_find_popup_open(const tiku_trk_find_t *f);

/** @brief What the last Queries-menu action did, for a status line. */
const char *tiku_trk_find_note(const tiku_trk_find_t *f);

/** @brief Forget it, once it has been said. */
void tiku_trk_find_clear_note(tiku_trk_find_t *f);

/** @brief Tell the panel where its most-used list lives (Q-016). */
void tiku_trk_find_set_store(tiku_trk_find_t *f, struct tiku_trk_store *s);

/** @brief Supply the installed type declarations used by both type menus. */
void tiku_trk_find_set_types(tiku_trk_find_t *f,
                             const tiku_trk_types_t *types);

/** @brief Start a panel with one attribute row and no scope restriction. */
void tiku_trk_find_init(tiku_trk_find_t *f);

/**
 * @brief Whether the type menu is shown at all (Q-012).
 *
 * Hidden in formula mode, where the formula says what it asks for and a
 * control that silently ANDed something onto it would make the text a lie.
 */
int tiku_trk_find_type_shown(const tiku_trk_find_t *f);

/** @brief The label the type control shows. */
const char *tiku_trk_find_type_label(const tiku_trk_find_t *f);

/**
 * @brief Choose a type, and promote it in the most-used list (Q-016).
 *
 * @param store Where the list lives, or NULL not to keep one.
 */
void tiku_trk_find_set_type(tiku_trk_find_t *f, const char *type,
                            struct tiku_trk_store *store);

/**
 * @brief The types the menu offers, most-used first (Q-015).
 *
 * "All files and folders" is not in this list: it is the fixed first item,
 * which is a different thing from a type.
 *
 * @return how many were written.
 */
int tiku_trk_find_type_menu(struct tiku_trk_store *store,
                            const char **out, int max);

/** @brief Offer @p name as a place the search can be scoped to. */
int tiku_trk_find_add_place(tiku_trk_find_t *f, const char *name);

/** @brief Offer a place together with the volume identity it belongs to. */
int tiku_trk_find_add_place_identity(tiku_trk_find_t *f, const char *name,
                                     uint64_t dev, const char *volume);

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
 * @brief Fill the panel FROM a query, for Edit query (Q-026).
 *
 * The inverse of _build, and the reason a saved query is a thing you can
 * open rather than only a thing you can run: the mode comes back, the rows
 * come back with their joins, the name text comes back out of the single
 * term a by-name query is, and a scope that names a place the panel offers
 * ticks that place.  A scope naming somewhere the panel does not offer
 * leaves it on "everywhere" rather than silently narrowing the search to
 * nothing.
 *
 * @return 1 when every scope in the query was recognised.
 */
int tiku_trk_find_from_query(tiku_trk_find_t *f, const tiku_trk_query_t *q);

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
