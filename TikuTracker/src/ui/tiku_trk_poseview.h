/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview.h - the engine.
 *
 * Tracker's BPoseView in shape: one view serving every mode, three pose lists
 * behind one accessor (all / filtered / selection), a selection pivot, and
 * type-ahead.  The rules implemented here are cited to FEATURES.md rows so a
 * behavior can be checked against the source that specified it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_POSEVIEW_H_
#define TIKU_TRK_POSEVIEW_H_

#include <stdint.h>

#include "tiku_attr.h"
#include "tiku_trk_input.h"
#include "tiku_trk_match.h"
#include "tiku_model.h"
#include "tiku_trk_nodeicon.h"
#include "tiku_notify.h"
#include "tiku_trk_view_defs.h"
#include "tiku_trk_repaint.h"

/* The layout clipboard is deliberately a second, typed channel.  File
 * references and a column layout must never overwrite one another: copying a
 * layout is a view operation, not a file operation. */
#define TIKU_TRK_LAYOUT_CLIPBOARD_TYPE "application/tracker-columns"
typedef struct {
    int  field;
    int  width;
    int  alignment;
    char title[24];
} tiku_trk_layout_column_t;

typedef struct {
    tiku_trk_layout_column_t column[TIKU_TRK_COLUMNS_MAX];
    int  count;
    int  sort_field;
    int  sort_secondary;
    int  reverse;
    int  serial;
} tiku_trk_layout_clip_t;

#define TIKU_TRK_TYPEAHEAD_MAX 64

/** @brief How many filter terms Shift+Space can accumulate. */
#define TIKU_TRK_FILTER_TERMS  8

/**
 * @brief Type-ahead lapses after this quiet period (PVK "restarts the
 * timeout").
 */
#define TIKU_TRK_TYPEAHEAD_US  1000000

/** @brief A saved position further out than this is nonsense (PVL: 50000). */
#define TIKU_TRK_SANE_LOCATION 50000

/** @brief One entry on screen: a Model plus where it is and how it looks. */
typedef struct {
    tiku_model_t model;
    /* The node's own typed descriptor, parsed once when the pose loads: it
     * is what turns a reading into "1.5 MiB" or "15h 57m" instead of a bare
     * integer, and what refuses a bad edit before it is sent. */
    tiku_desc_t  desc;
    /* MA-033: the value shown for a node, cut at the first newline.  The
     * cap is a DISPLAY limit; 255 rather than the source's 1023 because
     * poses are a fixed array and a kilobyte each costs eight megabytes a
     * window, which a browser that opens sixteen of them cannot spend. */
    char             value[256];    /* live value text for the Value column */
    int              value_valid;
    int              selected;
    int              index;         /* position in the current list         */
    /* Location is kept in UNSCALED 32 px space, as Tracker does, so a layout
     * arranged at one icon size reappears in the same relative arrangement
     * at another (PVL-012). */
    int              loc_x, loc_y;
    int              placed;        /* has a position at all                */
    /* Marked invisible in its saved position record: it exists, it is not
     * shown, and it is not a row waiting to be discovered again. */
    int              invisible;
    /* A link whose target has gone.  It keeps its row and its name and says
     * so, rather than vanishing as though the LINK had been deleted. */
    int              broken;
    /* Where the pose sat when the current drag began.  A drag must be
     * measured from there and not from the pose's live location: with
     * snapping on, each small step would round back into the cell it is
     * already in and the icon would never move at all. */
    int              drag_x0, drag_y0;
    int              auto_placed;   /* placed by us, not by the user        */
    /* Shown here while living somewhere else -- the Desktop's Trash and
     * its Disks icon.  A re-listing of the view's own directory will not
     * find it, and must not take it for a row that has gone. */
    int              adopted;
    /* When this pose last JOINED the selection.  The slow-second-click
     * rename refuses to arm on the click that selected the pose, and the
     * only way to tell those apart is the moment selection happened. */
    int64_t          sel_time;
    struct {
        int      field;
        int      width;
        uint64_t key;
        int      valid;
        char     text[160];
    } cell_cache[TIKU_TRK_COLUMNS_MAX];
} tiku_trk_pose_t;

typedef struct tiku_trk_poseview tiku_trk_poseview_t;

/** @brief Create a view over @p backend, showing @p path. */
tiku_trk_poseview_t *tiku_trk_poseview_new(tiku_backend_t *backend,
                                           const char *path);

/** @brief Free the view and everything it allocated. */
void tiku_trk_poseview_free(tiku_trk_poseview_t *v);

/** @brief Re-read the directory into poses (the AddPoses pass). */
int tiku_trk_poseview_reload(tiku_trk_poseview_t *v);

/*---------------------------------------------------------------------------*/
/* Loading a folder in batches (PVL-088, PVN-047, PVL-089, PVL-090)          */
/*---------------------------------------------------------------------------*/

/** @brief Rows published per batch (kMaxAddPosesChunk). */
#define TIKU_TRK_LOAD_CHUNK 50

/** @brief A batch is also flushed once this long has passed. */
#define TIKU_TRK_LOAD_CHUNK_US 300000

/** @brief The pole appears only if the load outlasts this (TS-038). */
#define TIKU_TRK_POLE_AFTER_US 500000

/**
 * @brief Begin loading the current folder.
 *
 * The entries are read now -- enumerating a namespace is fast -- and
 * PUBLISHED in batches, which is the contract the row is about: the window
 * never sits blank, rows appear as they arrive, and a load long enough to
 * notice shows a barber pole.
 *
 * @return Entries staged, or -1.
 */
int tiku_trk_poseview_load_begin(tiku_trk_poseview_t *v, int64_t now);

/**
 * @brief Publish the next batch.
 *
 * The first row is published on its own, immediately, as Tracker's zero
 * initial deadline makes it do -- a folder shows something at once and
 * settles into batches afterwards.
 *
 * @return 1 while more remain, 0 when the load is complete.
 */
int tiku_trk_poseview_load_step(tiku_trk_poseview_t *v, int64_t now);

/** @brief Whether a load is in flight. */
int tiku_trk_poseview_loading(const tiku_trk_poseview_t *v);

/**
 * @brief Whether the barber pole should be drawn (TS-038).
 *
 * Armed rather than shown: a load that finishes inside half a second never
 * flashes a pole at all.
 */
int tiku_trk_poseview_pole(const tiku_trk_poseview_t *v, int64_t now);

/**
 * @brief The load's generation.
 *
 * A batch, or a change notice, carrying an older one belongs to a folder
 * this window has left, and applying it puts the previous directory's rows
 * into this one (PVL-089).
 */
int tiku_trk_poseview_generation(const tiku_trk_poseview_t *v);

/**
 * @brief Fill the view from a result set instead of a directory.
 *
 * A query window is a pose view over results that live all over the
 * namespace rather than in one place, which is why it shows each entry's
 * path: without it two results called "free" are indistinguishable.  The
 * view stops reloading from a directory once it holds results.
 */
int tiku_trk_poseview_set_results(tiku_trk_poseview_t *v,
                                  const tiku_model_t *m, int n);

/**
 * @brief Fill the view from a set the caller assembled (AW-018).
 *
 * The same as a result set without being a QUERY: the Desktop is a real
 * place whose contents come from several directories at once, so its poses
 * are opened and dropped on like any folder's, not treated as answers that
 * happen to have paths.
 */
int tiku_trk_poseview_set_models(tiku_trk_poseview_t *v,
                                 const tiku_model_t *m, int n);

/**
 * @brief Add one pose that does not live in this view's directory.
 *
 * The Desktop's Trash and its Disks icon are the reason: they are shown
 * ON the Desktop while living elsewhere, and a change notice cannot carry
 * them because a notice is addressed to the directory it happened in.
 * Left unplaced, so the next placement pass puts it in the next free slot.
 *
 * @return 0 when it was added.
 */
int tiku_trk_poseview_adopt(tiku_trk_poseview_t *v,
                            const tiku_model_t *m);

/** @brief Whether this view is showing query results. */
int tiku_trk_poseview_is_query(const tiku_trk_poseview_t *v);

/** @brief Document colour for this view, tinted when it is read-only. */

/** @brief Select the saved column/frame layout shared by this query type. */
void tiku_trk_poseview_set_query_type(tiku_trk_poseview_t *v,
                                      const char *type);

/** @brief Supply the sidecar store used by synthetic query-layout keys. */
void tiku_trk_poseview_set_layout_store(tiku_trk_poseview_t *v,
                                        tiku_store_t *store);

/** @brief State key for this view (folder path or per-query-type layout). */
const char *tiku_trk_poseview_layout_path(const tiku_trk_poseview_t *v);

/** @brief Install the four first-run query frame/column templates. */
int tiku_trk_poseview_install_query_defaults(tiku_backend_t *backend,
                                             tiku_store_t *store);

/** @brief Show @p path, keeping the same backend. */
int tiku_trk_poseview_switch_dir(tiku_trk_poseview_t *v, const char *path);

/** @brief Open the parent directory, if there is one. */
int tiku_trk_poseview_go_up(tiku_trk_poseview_t *v);

/** @brief The folder the view lists; the empty string for no view. */
const char *tiku_trk_poseview_path(const tiku_trk_poseview_t *v);

/**
 * @brief The list being shown: filtered while type-ahead is active, else all.
 *
 * Every iterator goes through this, which is what keeps filtering a one-line
 * concern rather than a test at each call site.
 */
int tiku_trk_poseview_count(const tiku_trk_poseview_t *v);
/**
 * @brief The pose at row @p i of what is shown, filtered or not.
 *        @return NULL off the end.
 */
tiku_trk_pose_t *tiku_trk_poseview_at(tiku_trk_poseview_t *v, int i);

/** @brief How many poses are selected, filtered out or not. */
int tiku_trk_poseview_selection_count(const tiku_trk_poseview_t *v);

/** @brief The pose the user acted on last, or NULL. */
tiku_trk_pose_t *tiku_trk_poseview_active(tiku_trk_poseview_t *v);

/*---------------------------------------------------------------------------*/
/* Selection (PVS-001..010)                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Whether more than one pose may be selected at a time (PVS-009).
 *
 * On by default.  Turned off for a picker asking for ONE thing, and the gate
 * belongs here rather than at the shells: every way of extending a selection
 * -- the modifier clicks, shift+arrow, a swept band, Select All -- has to
 * degrade to a plain single selection, and a rule enforced at four call
 * sites is a rule that will hold at three of them.
 */
void tiku_trk_poseview_set_multiple(tiku_trk_poseview_t *v, int on);
/** @brief Whether more than one pose may be selected. */
int tiku_trk_poseview_multiple(const tiku_trk_poseview_t *v);

/** @brief Apply a click at row @p index (-1 = empty space) with modifiers. */
void tiku_trk_poseview_click(tiku_trk_poseview_t *v, int index,
                             unsigned mods);

/**
 * @brief Apply a click whose view-space point is known (PVS-005).
 *
 * List-mode Shift selection is an index range.  In either icon mode it is a
 * spatial rectangle from the selection pivot to the point actually clicked,
 * so the engine needs the point rather than a row number alone.  The simpler
 * tiku_trk_poseview_click() remains the coordinate-free convenience entry.
 */
void tiku_trk_poseview_click_at(tiku_trk_poseview_t *v, int index, int x,
                                int y, unsigned mods);

/**
 * @brief Select whatever now sits at @p index, after a removal (FS-050).
 *
 * List mode only, and clamped to the end: deleting the last row leaves the
 * new last row selected rather than nothing.
 *
 * @return the index selected, or -1 when there was nothing to select.
 */
int tiku_trk_poseview_select_after_removal(tiku_trk_poseview_t *v, int index);

/** @brief Clear the selection; the pivot goes with it (PVS-008). */
void tiku_trk_poseview_select_none(tiku_trk_poseview_t *v);

/**
 * @brief Select every pose shown; in a single-selection view, the first.
 */
void tiku_trk_poseview_select_all(tiku_trk_poseview_t *v);

/**
 * @brief Select what was not selected and deselect what was (PVK-046).
 *
 * Both anchors are dropped first and only re-taken if the first row of the
 * list ends up selected: an inverted selection has no click behind it, so
 * a pivot left pointing into it would anchor a later shift-click on a row
 * the user never touched.
 */
void tiku_trk_poseview_invert_selection(tiku_trk_poseview_t *v);

/**
 * @brief Replace the selection with every pose matching @p expr (PVK-047).
 *
 * @param how     Which of the five rules to apply.
 * @param ignore_case  Fold case, the Select window's default.
 * @param invert  Select the poses that do NOT match.
 * @param err     Receives a regular-expression error; nothing is selected
 *                and the caller shows it rather than leaving the user with
 *                an empty selection and no reason.
 * @return Number of poses selected, or -1 when the expression is bad.
 */
int tiku_trk_poseview_select_matching(tiku_trk_poseview_t *v,
                                      const char *expr, tiku_trk_match_t how,
                                      int ignore_case, int invert,
                                      char *err, size_t errmax);

/**
 * @brief Normalize the selection for a context menu (PVS-018).
 *
 * On a pose that is not selected, that pose becomes the whole selection; on
 * a pose already in the selection the group is kept, so a menu command
 * still applies to everything the user gathered.  On empty space the
 * selection is cleared.
 *
 * @return 1 when the selection changed, so the owner can fire its hook.
 */
int tiku_trk_poseview_context_click(tiku_trk_poseview_t *v, int index);

/**
 * @brief Whether the selection is currently PAINTED (PVS-023).
 *
 * Membership and painting are two different things: an inactive window
 * still owns its selection, it just does not draw it.  Drawing asks this;
 * everything else asks whether the pose is selected.
 */
int tiku_trk_poseview_draws_selected(const tiku_trk_poseview_t *v,
                                     const tiku_trk_pose_t *p);

/**
 * @brief Window activation: hide or restore the painted selection.
 *
 * The pivot is stashed on the way out and restored on the way back, so a
 * shift-click after clicking back into the window still extends from where
 * the user last anchored rather than starting over.
 */
void tiku_trk_poseview_window_activated(tiku_trk_poseview_t *v, int active);

/** @brief Keep drawing the selection while the window is inactive. */
void tiku_trk_poseview_show_selection_when_inactive(tiku_trk_poseview_t *v,
                                                    int on);

/**
 * @brief A band is being swept, so labels paint selected even if inactive.
 *
 * PVS-031: without this the rows a band is sweeping over an inactive window
 * would give no feedback at all.
 */
void tiku_trk_poseview_set_band(tiku_trk_poseview_t *v, int on);
/**
 * @brief Mark row @p index as needing painting again.
 *
 * The damage a view accumulates is what the draw consumes, so anything
 * that learns something about ONE row says so here rather than asking for
 * a whole frame (PVL-075).
 */
void tiku_trk_poseview_damage_row(tiku_trk_poseview_t *v, int index);

/**
 * @brief Take the pending slide: the rows from *@p y down moved by the
 *        returned pixel count, and want blitting rather than drawing.
 */
int tiku_trk_poseview_repaint_take_slide(tiku_trk_poseview_t *v, int *y);

/** @brief Take the rectangles due a repaint, up to @p max. */
int tiku_trk_poseview_repaint_take(tiku_trk_poseview_t *v,
                                   tiku_trk_repaint_rect_t *out, int max);
/** @brief Whether a band is being drawn. */
int tiku_trk_poseview_band(const tiku_trk_poseview_t *v);

/*---------------------------------------------------------------------------*/
/* Keyboard (PVK-001..010)                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Arrow / Home / End / Page navigation.  @return 1 if handled. */
int tiku_trk_poseview_key_nav(tiku_trk_poseview_t *v, int key, unsigned mods);

/**
 * @brief Feed a typed character to type-ahead.
 *
 * Scoring is Tracker's: a name STARTING with the string always wins; else a
 * name containing it scores 1/offset so earlier occurrences win; a name not
 * containing it is not a candidate.
 */
void tiku_trk_poseview_type_ahead(tiku_trk_poseview_t *v, char ch);

/** @brief Delete one character; restarts the timeout. */
void tiku_trk_poseview_type_backspace(tiku_trk_poseview_t *v);

/** @brief Drop the type-ahead string and any filtering. */
void tiku_trk_poseview_type_clear(tiku_trk_poseview_t *v);

/** @brief The in-progress string, shown in the count view; "" when idle. */
const char *tiku_trk_poseview_type_string(const tiku_trk_poseview_t *v);

/**
 * @brief The distinct types the listing actually holds (CW-026, PVL-095).
 *
 * What the type-dependent menus are built from: a folder of text files
 * should not offer an image's columns, and the only way to know is to ask
 * what is in it.
 *
 * @return how many were written to @p out.
 */
int tiku_trk_poseview_types_present(const tiku_trk_poseview_t *v,
                                    const char **out, int max);

/** @brief Expire type-ahead if the quiet period has passed. */
void tiku_trk_poseview_type_tick(tiku_trk_poseview_t *v);

/** @brief Filter the listing to type-ahead matches instead of jumping. */
void tiku_trk_poseview_set_filtering(tiku_trk_poseview_t *v, int on);
/** @brief Whether a type-ahead filter is in force. */
int tiku_trk_poseview_is_filtering(const tiku_trk_poseview_t *v);

/**
 * @brief Begin an additional filter term (Shift+Space, PVK-031).
 *
 * A pose must match EVERY term, and each term may match a different column,
 * which is what makes two short words far more selective than one long one.
 * Refused while the current term is empty: an empty term matches everything
 * and would silently widen the filter instead of narrowing it.
 *
 * @return 1 when a term was started.
 */
int tiku_trk_poseview_filter_term(tiku_trk_poseview_t *v);

/** @brief How many filter terms are in force. */
int tiku_trk_poseview_filter_terms(const tiku_trk_poseview_t *v);

/**
 * @brief Return was pressed: prepare the selection, and say what to do.
 *
 * With a filter narrowing the listing and nothing selected, the top filtered
 * row is selected first so that type-a-few-letters-then-Enter opens what the
 * user is looking at (PVK-015).  Shift additionally dismisses the filter
 * once the selection is open, returning the window to the full listing
 * (PVK-016).
 *
 * @return 1 when the caller should also stop filtering afterwards.
 */
int tiku_trk_poseview_return_pressed(tiku_trk_poseview_t *v, int shift);

/**
 * @brief Whether a pose passes every filter term (FilterPose).
 *
 * Matching is case-insensitive substring over the text of EVERY column, not
 * over the name: a filter typed into a listing of device nodes finds "gpio"
 * in the Kind column just as readily as in the name.
 */
int tiku_trk_poseview_filter_pose(tiku_trk_poseview_t *v,
                                  const tiku_trk_pose_t *p);

/**
 * @brief The text column @p field shows for @p p.
 *
 * The filter and the sort both need it, and so does the drawing code; one
 * implementation is what keeps "what you can filter on" and "what you can
 * see" the same set.
 */
const char *tiku_trk_poseview_cell_text(tiku_trk_poseview_t *v,
                            const tiku_trk_pose_t *p, int field,
                            char *buf, size_t max);
/** @brief How often a cell's text was answered from the cache, and not. */
void tiku_trk_poseview_cell_cache_stats(const tiku_trk_poseview_t *v,
                                        uint64_t *hits, uint64_t *misses);

/*---------------------------------------------------------------------------*/
/* Icon mode, layout and spatial memory (PVL-012..020)                       */
/*---------------------------------------------------------------------------*/

/** @brief The view's mode; list mode for a NULL view. */
int tiku_trk_poseview_mode(const tiku_trk_poseview_t *v);

/** @brief Switch mode; icon modes lay out, list mode drops back to rows. */
void tiku_trk_poseview_set_mode(tiku_trk_poseview_t *v, int mode);

/** @brief The icon mode last live -- what a list detour returns to. */
int tiku_trk_poseview_last_icon_mode(const tiku_trk_poseview_t *v);

/**
 * @brief Key this view's OWN records by @p key instead of its path.
 *
 * The Desktop's slot: its arrangement and a folder window showing the
 * same directory must never collide (MA-064).  The root's Disks slot is
 * applied automatically.
 */
void tiku_trk_poseview_set_state_slot(tiku_trk_poseview_t *v,
                                      const char *key);

/** @brief The Desktop's exemption: it scrolls for no key (PVK-013). */
void tiku_trk_poseview_set_no_autoscroll(tiku_trk_poseview_t *v, int on);

/** @brief The icon size in pixels; 32 for no view. */
int tiku_trk_poseview_icon_size(const tiku_trk_poseview_t *v);

/**
 * @brief Step the icon size one rung of the ladder (PVL-003).
 *
 * 32-40-48-64-96-128, stopping at the ends rather than wrapping, and only in
 * icon mode -- mini-icon and list draw at a fixed size, so a size command
 * there would change a number nothing on screen reads.
 *
 * @param up 1 to grow, 0 to shrink.
 * @return 1 when the size changed.
 */
int tiku_trk_poseview_step_icon_size(tiku_trk_poseview_t *v, int up);

/**
 * @brief The size icons are actually DRAWN at in the current mode.
 *
 * Icon mode uses the chosen size; mini-icon and list modes use the mini size
 * whatever that setting says, which is what makes them different modes.
 */
int tiku_trk_poseview_art_size(const tiku_trk_poseview_t *v);
/** @brief Icons at @p px, between 16 and 128; anything else is ignored. */
void tiku_trk_poseview_set_icon_size(tiku_trk_poseview_t *v, int px);

/**
 * @brief Tell the view the font it is being drawn in (PVL-007).
 *
 * Every height in an icon mode is a function of the label's height, not of
 * the icon size: a cell derived from the art alone leaves the name clipped
 * at one font and floating at another.  Supplied by the owner, as the label
 * MEASURE is, because only it owns a font.
 */
void tiku_trk_poseview_set_font_metrics(tiku_trk_poseview_t *v, int height,
                                        int ascent);

/** @brief The list-mode row height for the font in use (PVL-007). */
int tiku_trk_poseview_row_height(const tiku_trk_poseview_t *v);

/**
 * @brief Whether folders sort ahead of files (MA-039).
 *
 * On by default, as it is in the original: a folder is not just a big file.
 * It is a setting rather than a rule, so a listing can be asked for in
 * column order alone.
 */
void tiku_trk_poseview_set_folders_first(tiku_trk_poseview_t *v, int on);
/** @brief Hide dot-prefixed entries and reload the listing when changed. */
void tiku_trk_poseview_set_hide_dotfiles(tiku_trk_poseview_t *v, int on);
/** @brief Select the next/previous name in sort order, wrapping at edges. */
int tiku_trk_poseview_tab_next(tiku_trk_poseview_t *v, int backwards);

/**
 * @brief Tell the view the row height its owner DRAWS with (0 = derive).
 *
 * The derived height is Tracker's own formula; a shell that lays its list
 * out differently has to say so, or every rectangle the view answers with
 * is a row out of step with what is on screen.
 */
void tiku_trk_poseview_set_row_height(tiku_trk_poseview_t *v, int h);

/** @brief Grid cell and origin offset for the current mode (PVL-013). */
void tiku_trk_poseview_layout_info(const tiku_trk_poseview_t *v,
                                   int *grid_x, int *grid_y,
                                   int *off_x, int *off_y);

/** @brief Round a point to the nearest grid cell; a no-op with no grid. */
void tiku_trk_poseview_pin_to_grid(const tiku_trk_poseview_t *v,
                                   int *x, int *y);

/** @brief A pose's on-screen position, scaled from unscaled 32 px space. */
void tiku_trk_poseview_pose_location(const tiku_trk_poseview_t *v,
                                     const tiku_trk_pose_t *p,
                                     int *x, int *y);

/** @brief Move a pose to a screen point, storing it unscaled. */
void tiku_trk_poseview_move_pose(tiku_trk_poseview_t *v, tiku_trk_pose_t *p,
                                 int x, int y, int snap);

/**
 * @brief Auto-place every unplaced pose left-to-right, wrapping at the edge.
 *
 * @param view_w  Width available; the wrap point.
 */
int tiku_trk_poseview_place_unplaced(tiku_trk_poseview_t *v, int view_w);

/**
 * @brief Restart auto-placement at the top-left (PVL-017).
 *
 * Called by the things that make the old hint meaningless: a mode change, a
 * directory switch, a cleared listing.  Without it the next folder's first
 * icon continues from where the last folder's last one landed.
 */
void tiku_trk_poseview_reset_hint(tiku_trk_poseview_t *v);

/** @brief Where the next auto-placed icon would go (PVL-017). */
void tiku_trk_poseview_hint(const tiku_trk_poseview_t *v, int *x, int *y);

/**
 * @brief Put pose @p at back in sorted order if its key has moved (MA-055).
 *
 * A rename already goes through remove+insert; an edited VALUE did not, so
 * under a value sort the row kept its old slot until some later insert
 * happened to notice the array was unsorted.
 *
 * @return Where the pose ended up.
 */
int tiku_trk_poseview_recheck_order(tiku_trk_poseview_t *v, int at);

/**
 * @brief Re-lay out every pose from the top-left in sort order.
 *
 * What "Arrange by" does: the order changes, so every icon moves (PVL-024).
 */
void tiku_trk_poseview_cleanup(tiku_trk_poseview_t *v, int view_w);

/**
 * @brief Snap every icon to its nearest grid point, keeping the order.
 *
 * What "Clean up" does, and a different thing from arranging: the icons the
 * user placed stay where they roughly were, tidied -- an arrangement is not
 * a mess to be replaced, it is a mess to be straightened (PVL-021).
 *
 * @return how many actually moved.
 */
int tiku_trk_poseview_snap_to_grid(tiku_trk_poseview_t *v);

/** @brief Pose at a screen point in icon mode, or NULL. */
tiku_trk_pose_t *tiku_trk_poseview_pose_at(tiku_trk_poseview_t *v,
                                           int x, int y);

/**
 * @brief How wide a pose's label is, in pixels.
 *
 * Supplied by the owner because only it owns a font.  Without it every pose
 * is icon-wide and a long name is unclickable past the icon (PVL-065).
 */
typedef int (*tiku_trk_measure_fn)(const char *text, void *ctx);

/** @brief The text-measuring callback, with its context. */
void tiku_trk_poseview_set_measure(tiku_trk_poseview_t *v,
                                   tiku_trk_measure_fn fn, void *ctx);

/**
 * @brief The rectangle a pose occupies in an icon mode (PVL-065).
 *
 * Widened to the label when the label is wider than the icon: in icon mode
 * it is re-centred and grown both ways, in mini-icon mode it grows to the
 * right.  This is the rect that is hit-tested and swept, so a name is
 * clickable where it is drawn.
 */
void tiku_trk_poseview_pose_rect(tiku_trk_poseview_t *v,
                                 const tiku_trk_pose_t *p, int *x, int *y,
                                 int *w, int *h);

/**
 * @brief A pose's icon square and label box separately (PVS-019, IV-026).
 *
 * pose_rect is their union, which is what a band sweeps and what the extent
 * unions; these are what a CLICK is judged against, because the gap between
 * an icon and its name is not part of either.  Either pointer may be NULL.
 */
void tiku_trk_poseview_pose_parts(tiku_trk_poseview_t *v,
                                  const tiku_trk_pose_t *p,
                                  int *ix, int *iy, int *iw, int *ih,
                                  int *lx, int *ly, int *lw, int *lh);

/**
 * @brief The label a pose shows in an icon mode, elided to fit (IV-047).
 *
 * Thirty M's wide, whatever the icon size: a name is as long as it is, and
 * tying the allowance to the art shows a quarter of every name at 16 px.
 * The elision is in the middle, and both shells draw what this returns so
 * the text cannot overrun the box that selects it.
 *
 * @return Length written.
 */
int tiku_trk_poseview_fit_label(tiku_trk_poseview_t *v,
                                const tiku_trk_pose_t *p, char *out,
                                size_t max);

/**
 * @brief Whether a point lands on an icon's opaque PIXELS (PVS-019).
 *
 * Supplied rather than derived, for the same reason the label measure is:
 * the view owns no art.  It also keeps the two shells' different ways of
 * resolving art -- one through the node-icon tier, one straight from the
 * model -- out of the engine.  Without it a click is judged against the
 * icon's square, as it was before.
 *
 * @param px,py Offsets INSIDE the icon square.
 */
typedef int (*tiku_trk_hit_fn)(const tiku_model_t *m, int size, int px,
                               int py, void *ctx);

/** @brief The icon hit-test callback, with its context. */
void tiku_trk_poseview_set_icon_hit(tiku_trk_poseview_t *v,
                                    tiku_trk_hit_fn fn, void *ctx);

/*---------------------------------------------------------------------------*/
/* The viewport and the extent (PVL-049, PVL-050)                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Tell the view how big the window showing it is, in pixels.
 *
 * The view knew only a ROW COUNT before, which is why nothing in an icon
 * mode could scroll, clamp or page: none of those questions can be asked
 * without knowing how much is on screen.
 */
void tiku_trk_poseview_set_viewport(tiku_trk_poseview_t *v, int w, int h);

/** @brief The viewport's width and height, as last set. */
void tiku_trk_poseview_viewport(const tiku_trk_poseview_t *v, int *w,
                                int *h);

/**
 * @brief The scrollable area: what there is to look at (PVL-049).
 *
 * In list mode the columns' width by the row count; in an icon mode the
 * bounding box of every placed pose, outset by the layout offset so an icon
 * at the origin is not flush against the edge.
 */
void tiku_trk_poseview_extent(tiku_trk_poseview_t *v, int *x, int *y,
                              int *w, int *h);

/**
 * @brief The pixel origin of an icon mode, and how to move it.
 *
 * Kept per mode: switching to list and back returns to where the icons
 * were, which one shared origin cannot do (PVL-009).  Always clamped to
 * what there is to see.
 */
void tiku_trk_poseview_set_origin(tiku_trk_poseview_t *v, int x, int y);
/**
 * @brief Where the view's content starts, in pixels, whatever the mode: a
 *        list's scroll is answered in the same pixels an icon view's is.
 */
void tiku_trk_poseview_origin(const tiku_trk_poseview_t *v, int *x, int *y);

/**
 * @brief The first and last poses inside the viewport (PVK-011, PVL-062).
 *
 * In list mode the first and last visible ROWS; in an icon mode the poses
 * whose rects intersect the visible band.  Either pointer may be NULL.
 *
 * @return 1 when anything is visible at all.
 */
int tiku_trk_poseview_visible_range(tiku_trk_poseview_t *v, int *first,
                                    int *last);

/**
 * @brief Where the next created entry should land (PVL-025, PVL-098).
 *
 * Seeded from the click that invoked the command, or from the pointer, and
 * consumed by the next entry to appear -- the position has to exist before
 * the row does, or the icon lands wherever auto-placement puts it and jumps
 * afterwards.
 */
void tiku_trk_poseview_set_drop_point(tiku_trk_poseview_t *v, int x, int y);
/** @brief No drop point is marked any more. */
void tiku_trk_poseview_clear_drop_point(tiku_trk_poseview_t *v);

/** @brief How many landings a single drop can reserve. */
#define TIKU_TRK_LAND_MAX 32

/**
 * @brief Reserve where @p name will land when it appears (PVS-045, PVL-027).
 *
 * A drop is decided before the entries are in the folder, so the positions
 * have to be waiting for them: applied afterwards, the icons would be
 * auto-placed first and then jump into the arrangement they were dropped
 * in.  Unclaimed reservations are harmless -- they are consumed by name.
 */
void tiku_trk_poseview_drop_land(tiku_trk_poseview_t *v, const char *name,
                                 int x, int y);

/** @brief Forget every reservation (a drop that was refused). */
void tiku_trk_poseview_clear_landings(tiku_trk_poseview_t *v);

/** @brief Select every pose intersecting the rectangle (rubber band). */
int tiku_trk_poseview_select_rect(tiku_trk_poseview_t *v, int x0, int y0,
                                  int x1, int y1, unsigned mods);

/*---------------------------------------------------------------------------*/
/* Spatial memory -- positions persist on the objects (PVL-021, MA-064)      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Load saved positions for the current folder.  @return poses placed.
 */
int tiku_trk_poseview_restore_state(tiku_trk_poseview_t *v);

/** @brief Persist positions and the view state for the current folder. */
int tiku_trk_poseview_save_state(tiku_trk_poseview_t *v);

/*---------------------------------------------------------------------------*/
/* Sorting                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Sort by @p field; the same field again reverses (TS rows). */
void tiku_trk_poseview_sort_by(tiku_trk_poseview_t *v, int field);
/** @brief The column the listing is sorted by. */
int tiku_trk_poseview_sort_field(const tiku_trk_poseview_t *v);
/** @brief Whether the sort runs the other way. */
int tiku_trk_poseview_sort_reverse(const tiku_trk_poseview_t *v);

/**
 * @brief A click on a column title, with or without Shift (TS-003, TS-004).
 *
 * Shift on the primary column swaps primary and secondary -- but only when
 * there IS a secondary, otherwise it falls through to reversing, so Shift
 * never becomes a click that does nothing.  Shift elsewhere sets the
 * secondary sort, or clears it when it is already that column.
 */
void tiku_trk_poseview_sort_click(tiku_trk_poseview_t *v, int field,
                                  int shift);

/** @brief The tie-break column, or -1 when there is none. */
int tiku_trk_poseview_sort_secondary(const tiku_trk_poseview_t *v);
/**
 * @brief Sort within equal keys by @p field; a field with no column, or
 *        the primary itself, means no secondary.
 */
void tiku_trk_poseview_set_secondary(tiku_trk_poseview_t *v, int field);

/*---------------------------------------------------------------------------*/
/* Liveness                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief What the view is telling its owner after a notice. */
#define TIKU_TRK_NOTIFY_NOTHING  0x0u
#define TIKU_TRK_NOTIFY_REDRAW   0x1u
#define TIKU_TRK_NOTIFY_GONE     0x2u   /* the folder itself was removed  */
#define TIKU_TRK_NOTIFY_RETITLE  0x4u   /* it was renamed                 */

/**
 * @brief Whether this view is showing something on the volume @p dev.
 *
 * Asked by IDENTITY rather than by path: a window on a mount point stays
 * stat-able and listable after the volume under it goes, so the deletion
 * test that closes an ordinary vanished folder cannot see an unmount at
 * all (PVN-043).
 */
int tiku_trk_poseview_on_device(const tiku_trk_poseview_t *v, uint64_t dev);

/**
 * @brief Drop every row that lived on the volume @p dev.
 *
 * For the windows that are NOT showing that volume: an unmount takes rows
 * out of other folders too, and leaving them is a listing that offers
 * entries which are no longer there.
 *
 * @return how many rows went.
 */
int tiku_trk_poseview_device_gone(tiku_trk_poseview_t *v, uint64_t dev);

/**
 * @brief Give the view the icon cache to keep in step (IV-038, IV-041).
 *
 * Optional, and shared: passing the same cache to every view is the point,
 * since a file shown in two windows has one icon.  Held here rather than
 * dropped at the call sites because the notice handler is where a change
 * is already known to have happened, and a second place that has to
 * remember to evict is a place that will not.
 */
void tiku_trk_poseview_set_icons(tiku_trk_poseview_t *v,
                                 tiku_trk_nodeicon_t *c);
/** @brief Generation changed whenever type-dependent menus must rebuild. */
uint64_t tiku_trk_poseview_menu_generation(const tiku_trk_poseview_t *v);
/** @brief Defer a post-mount broken-link repair by half a second (PVN-042). */
void tiku_trk_poseview_schedule_broken_repair(tiku_trk_poseview_t *v,
                                              int64_t now);
/**
 * @brief Whether the repair of broken rows fell due by @p now; answering
 *        yes clears it, so it fires once.
 */
int tiku_trk_poseview_broken_repair_due(tiku_trk_poseview_t *v,
                                        int64_t now);

/**
 * @brief Apply one change notice.
 *
 * The single entry point every notice goes through, whichever source it came
 * from -- a derived diff today, a pushed event when the device can carry
 * one.  Every rule about what a change does to a view lives behind here
 * rather than at the call sites, so a query window and a folder window
 * cannot drift apart.
 *
 * @return TIKU_TRK_NOTIFY_* flags for the owner to act on.
 */
unsigned tiku_trk_poseview_notify(tiku_trk_poseview_t *v,
                                  const tiku_node_event_t *e);

/**
 * @brief Update a query view from a new result set, through the handler.
 *
 * Diffs against what the view holds and applies the difference, so a result
 * that is still there keeps its row and its selection -- a query window
 * updates by the same path a folder window does.
 */
unsigned tiku_trk_poseview_apply_results(tiku_trk_poseview_t *v,
                                         const tiku_model_t *set, int n,
                                         int64_t now);

/** @brief Apply a batch in arrival order.  @return the OR of the flags. */
unsigned tiku_trk_poseview_notify_all(tiku_trk_poseview_t *v,
                                      const tiku_evlist_t *l);

/**
 * @brief Re-list and apply what changed, without rebuilding the view.
 *
 * Rows that are still there keep their selection, their place and their
 * saved position; only what actually moved is touched.
 *
 * @return TIKU_TRK_NOTIFY_* flags.
 */
/**
 * @brief Re-list and apply what changed, without rebuilding the view.
 *
 * @param ch       Changes the OWNER drained this tick, or NULL when it could
 *                 not ask.  They are passed in rather than drained here
 *                 because draining is destructive: a view that drained for
 *                 itself would consume records belonging to every other
 *                 view, and those views would be told nothing happened.
 * @param nch      How many, or -1 when the backend cannot tell.
 * @param complete 0 when the set is known to be partial, so the view
 *                 re-reads instead of trusting it.
 */
unsigned tiku_trk_poseview_poll(tiku_trk_poseview_t *v, int64_t now,
                                const tiku_model_change_t *ch, int nch,
                                int complete);

/** @brief Notices held for rows that have not appeared yet. */
int tiku_trk_poseview_parked(const tiku_trk_poseview_t *v);

/** @brief Entries seen but not readable, held rather than dropped. */
int tiku_trk_poseview_zombies(const tiku_trk_poseview_t *v);

/**
 * @brief Service the backend and refresh what changed.
 *
 * @return Number of poses whose displayed value changed.
 */
int tiku_trk_poseview_pump(tiku_trk_poseview_t *v, int timeout_ms);

/** @brief Read the value column for visible rows (device nodes). */
int tiku_trk_poseview_refresh_values(tiku_trk_poseview_t *v, int first,
                                     int count);

/*---------------------------------------------------------------------------*/
/* Drawing                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Columns currently shown. */
int tiku_trk_poseview_columns(const tiku_trk_poseview_t *v,
                              const tiku_trk_column_t **out);

/** @brief Whether @p field has a column. */
int tiku_trk_poseview_has_column(const tiku_trk_poseview_t *v, int field);

/**
 * @brief The view-space rectangle the open animation grows out of (PVS-013).
 *
 * List mode: the row's slice of the MEASURED first column.  Icon modes: the
 * clicked pose's picture cell.  @return 0 when @p row has no pose.
 */
int tiku_trk_poseview_open_anim_rect(const tiku_trk_poseview_t *v, int row,
                                     int row_h, int *x, int *y, int *w,
                                     int *h);

/**
 * @brief Where the first column's data starts.
 *
 * The strip to its left belongs to the row's icon, not to any column, which
 * is why a column's offset is an absolute x and not an accumulated width.
 */
int tiku_trk_poseview_start_offset(const tiku_trk_poseview_t *v);

/**
 * @brief Re-derive every column's offset from the start, left to right.
 *
 * The one place positions come from.  Add, remove, resize and reorder all
 * end here rather than each adjusting its neighbours, which is what keeps a
 * column from drifting a pixel per operation.
 */
void tiku_trk_poseview_reflow_columns(tiku_trk_poseview_t *v);

/**
 * @brief Set a column's width (TS-010).
 *
 * @param width Clamped up to TIKU_TRK_COL_MIN_WIDTH: a column narrower than
 *              that cannot be grabbed again to widen it.
 * @return 1 when the width changed.
 */
int tiku_trk_poseview_resize_column(tiku_trk_poseview_t *v, int field,
                                    int width);

/**
 * @brief Move the column @p field to where @p before now sits (PVL-038).
 *
 * The destination index is sampled BEFORE the source is lifted out, which is
 * what makes dragging right land after the target rather than before it.
 *
 * @return 1 when the order changed.
 */
int tiku_trk_poseview_move_column(tiku_trk_poseview_t *v, int field,
                                  int before);

/** @brief Move a column to an explicit index. */
int tiku_trk_poseview_move_column_to(tiku_trk_poseview_t *v, int field,
                                     int to);

/**
 * @brief Put @p field immediately AFTER @p after (TS-009).
 *
 * A column dragged back into the strip lands after the header it was
 * dropped on, not before it -- dropping on the first header would otherwise
 * be the only way to reach position zero, and dropping on the last could
 * never reach the end.
 */
int tiku_trk_poseview_column_after(tiku_trk_poseview_t *v, int field,
                                   int after);

/**
 * @brief The width that would fit the widest value in @p field (PVL-037).
 *
 * Measured over the list being SHOWN, so fitting a filtered listing fits
 * what is on screen; floored at TIKU_TRK_COL_MIN_WIDTH.
 *
 * @return The width, or 0 with no measure function.
 */
int tiku_trk_poseview_column_widest(tiku_trk_poseview_t *v, int field);

/**
 * @brief Size a column to its widest value.
 *
 * Refused -- and the caller falls through to a drag -- when the answer would
 * neither exceed the floor nor narrow the column, which is Tracker's test
 * for "there is nothing to fit here".
 *
 * @return 1 when it resized.
 */
int tiku_trk_poseview_fit_column(tiku_trk_poseview_t *v, int field);

/** @brief Horizontal scroll, in pixels; titles and rows share it (TS-020). */
int tiku_trk_poseview_hscroll(const tiku_trk_poseview_t *v);
/**
 * @brief Scroll the columns sideways to @p px, clamped so the last column
 *        ends at the view's edge.
 */
void tiku_trk_poseview_set_hscroll(tiku_trk_poseview_t *v, int px);

/** @brief Total width the columns occupy, for the scroll range. */
int tiku_trk_poseview_columns_width(const tiku_trk_poseview_t *v);

/**
 * @brief Persist the column layout, and read it back.
 *
 * Order, width and alignment per column.  Positions are NOT persisted as
 * positions: they are re-derived on load, and the saved offsets survive only
 * long enough to sort the columns into their old order (PVL-033).
 *
 * @return Columns written / restored.
 */
int tiku_trk_poseview_save_columns(tiku_trk_poseview_t *v);
/**
 * @brief The saved column layout for this folder or query, read back from
 *        its store, byte order and attribute numbering repaired.
 *        @return how many columns were restored; 0 when nothing usable was
 *        saved.
 */
int tiku_trk_poseview_restore_columns(tiku_trk_poseview_t *v);

/**
 * @brief Take @p from's column layout, for a first opening (MA-070).
 *
 * Not marked dirty: an inherited layout persists only once the user
 * changes something, the same as a default would.
 */
void tiku_trk_poseview_copy_columns(tiku_trk_poseview_t *v,
                                    const tiku_trk_poseview_t *from);

/** Copy/paste the complete column and sort layout through the typed channel. */
int tiku_trk_poseview_copy_layout(const tiku_trk_poseview_t *v);
/**
 * @brief Take the copied layout: its columns, widths, sort and order, minus
 *        any attribute column this view has not registered.
 *        @return how many columns were taken; 0 when there was no name
 *        column or nothing copied.
 */
int tiku_trk_poseview_paste_layout(tiku_trk_poseview_t *v);
/** @brief Whether a layout has been copied. */
int tiku_trk_poseview_has_layout_clipboard(void);
/** @brief The copied layout, or NULL when none has been. */
const tiku_trk_layout_clip_t *tiku_trk_poseview_layout_clipboard(void);

/**
 * @brief The point size the owner draws at (TS-031).
 *
 * Widths are stored in units of TIKU_TRK_FONT_REF and scaled on the way in
 * and out, so a layout arranged at one font size comes back proportional at
 * another rather than clipping or leaving a gap.
 */
#define TIKU_TRK_FONT_REF 12
void tiku_trk_poseview_set_font_size(tiku_trk_poseview_t *v, int px);

/**
 * @brief Add or remove a column, as checking an Attributes item does.
 *
 * The name column cannot be removed: there would be nothing left to identify
 * a row by.  Removing the column a view is sorted on drops it back to sorting
 * by name rather than sorting on something invisible.
 *
 * @return 1 when the set changed.
 */
int tiku_trk_poseview_toggle_column(tiku_trk_poseview_t *v, int field);

/**
 * @brief Whether removing @p field would leave the listing with nothing.
 *
 * The name column can never go, so a listing always has one; asked so the
 * caller can SAY why rather than have the removal quietly refused
 * (TS-023).
 */
int tiku_trk_poseview_last_column(const tiku_trk_poseview_t *v, int field);

/**
 * @brief Register a column showing the attribute @p attr (CW-026).
 *
 * Registering is not showing: it gives the attribute a field id the rest
 * of the column machinery can carry, and the column is added or removed
 * with the same toggle as every other one.
 *
 * @return the field id, or -1.
 */
int tiku_trk_poseview_attr_column(tiku_trk_poseview_t *v, const char *attr,
                                  const char *title, int width);

/** @brief The attribute a registered column shows, or NULL. */
const char *tiku_trk_poseview_attr_of(const tiku_trk_poseview_t *v,
                                      int field);

/**
 * @brief Tell the view how many rows its window can show.
 *
 * The view cannot see its own window, and several rules are stated in terms
 * of what is VISIBLE rather than what exists -- paging, and the scroll
 * correction when the last visible row goes.  Without this they are guessed
 * at with a constant.
 */
void tiku_trk_poseview_set_visible_rows(tiku_trk_poseview_t *v, int rows);

/**
 * @brief Mark the row at @p index as shown or not.
 *
 * The bit rides on the entry's saved record, so it survives a reload and a
 * restart: an entry hidden here is hidden the next time the folder opens,
 * not merely for this sitting.  Takes effect on the next restore.
 *
 * @return 1 when the flag changed.
 */
int tiku_trk_poseview_set_invisible(tiku_trk_poseview_t *v, int index,
                                    int hidden);

/** @brief How many rows fit the viewport, as last set. */
int tiku_trk_poseview_visible_rows(const tiku_trk_poseview_t *v);

/*---------------------------------------------------------------------------*/
/* Inline rename, armed on the slow second click (PVS-014, PVS-015, MA-043)  */
/*---------------------------------------------------------------------------*/

/** @brief BeOS's double-click interval; the arming delay is the same value. */
#define TIKU_TRK_CLICK_SPEED_US 500000

/**
 * @brief A click landed on the NAME of row @p index (TextWidget::MouseUp).
 *
 * Arming is refused when the pose is not selected, when it was selected less
 * than one click-speed ago -- the click that selected it does not count --
 * and on the second of a pair, so a double-click opens rather than renames.
 *
 * @return 1 when a rename is now armed.
 */
int tiku_trk_poseview_name_clicked(tiku_trk_poseview_t *v, int index,
                                   int64_t now);

/** @brief Disarm: another click, a right-click, empty space, or a drag. */
void tiku_trk_poseview_cancel_edit_wait(tiku_trk_poseview_t *v);

/**
 * @brief Whether the armed rename is now due (CheckExpiration).
 *
 * @return The ROW to edit, or -1.  Disarms itself either way, so the
 *         same expiry cannot start two editors.
 */
int tiku_trk_poseview_edit_due(tiku_trk_poseview_t *v, int64_t now);

/** @brief Whether a rename is armed and waiting. */
int tiku_trk_poseview_edit_armed(const tiku_trk_poseview_t *v);

/**
 * @brief Refuse in-place editing in this view (Open With, file panels).
 *
 * fAllowPoseEditing: a panel that is choosing a file must not let a click
 * pair turn into a rename.
 */
void tiku_trk_poseview_allow_editing(tiku_trk_poseview_t *v, int on);

/** @brief Scroll offset in rows, and the row count that fits @p height. */
int tiku_trk_poseview_scroll(const tiku_trk_poseview_t *v);
/** @brief Row @p row at the top, clamped to the rows there are. */
void tiku_trk_poseview_scroll_to(tiku_trk_poseview_t *v, int row);
/**
 * @brief Scroll just enough for row @p index to show among @p visible_rows,
 *        unless auto-scroll is off.
 */
void tiku_trk_poseview_scroll_into_view(tiku_trk_poseview_t *v, int index,
                                        int visible_rows);

#endif /* TIKU_TRK_POSEVIEW_H_ */
