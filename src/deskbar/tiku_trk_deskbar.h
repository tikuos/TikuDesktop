/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar.h - the Deskbar.
 *
 * Three sections stacked as the original has them: the leaf menu, the
 * running-application list whose windows nest underneath it in expando mode,
 * and the tray of replicants with the clock.  Behaviour is cited to the
 * DB-* rows in FEATURES.md, which were read from Deskbar's own source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_H_
#define TIKU_TRK_DESKBAR_H_

#include <time.h>
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_attr.h"
#include "tiku_trk_model.h"
#include "tiku_trk_state.h"

#define TIKU_TRK_DB_TEAMS_MAX   8
#define TIKU_TRK_DB_WINS_MAX   16
#define TIKU_TRK_DB_TRAY_MAX    8
#define TIKU_TRK_DB_LABEL_MAX  48
#define TIKU_TRK_DB_MAX_REPLICANT_H 12

/** @brief Where the bar is docked.  Expanded modes reserve an edge strip. */
typedef enum {
    TIKU_TRK_DB_RIGHT_EXPANDED = 0,   /* the BeOS default              */
    TIKU_TRK_DB_LEFT_EXPANDED,
    TIKU_TRK_DB_RIGHT_MINI,           /* collapsed to the leaf + tray  */
    TIKU_TRK_DB_LEFT_MINI,
    /* Along an edge rather than down one.  A horizontal bar is a
     * different shape, not a rotated one: the list runs across it and the
     * application buttons share the width between them (DB-033). */
    TIKU_TRK_DB_TOP,
    TIKU_TRK_DB_BOTTOM,
    TIKU_TRK_DB_DOCKS               /* how many there are              */
} tiku_trk_db_dock_t;

/** @brief A window's state, which its icon distinguishes (DB row). */
typedef enum {
    TIKU_TRK_DBW_SHOWN = 0,
    TIKU_TRK_DBW_MINIMIZED,
} tiku_trk_dbwin_state_t;

/** @brief One window under an application. */
typedef struct {
    char                   title[TIKU_TRK_DB_LABEL_MAX];  /* app name stripped */
    tiku_trk_dbwin_state_t state;
    int                    current;      /* the application's front window */
    void                  *tag;          /* the owner's window identity    */
} tiku_trk_dbwin_t;

/** @brief One running application. */
typedef struct {
    char             name[TIKU_TRK_DB_LABEL_MAX];
    int              expanded;           /* windows shown underneath       */
    int              active;             /* owns the focused window        */
    tiku_trk_dbwin_t wins[TIKU_TRK_DB_WINS_MAX];
    int              win_count;
    /* The types this application can open.  A drop on one that cannot
     * read the file would appear to work and do nothing (DB-035). */
    char             type[8][TIKU_TRK_TYPE_MAX];
    int              ntype;
    int              takes_anything;
} tiku_trk_team_t;

/** @brief A tray item: a namespace node rendered as label + value. */
typedef struct {
    char            label[16];
    char            path[TIKU_TRK_PATH_MAX];
    char            value[24];      /* raw, as the node reports it        */
    tiku_trk_desc_t desc;           /* resolved once, at subscribe time   */
    int             desc_known;
    int             valid;
    int             height;
} tiku_trk_replicant_t;

/** @brief How many rows the leaf menu holds, and its submenu. */
#define TIKU_TRK_DB_LEAF_MAX 24
#define TIKU_TRK_DB_SUB_MAX  50

/** @brief What one leaf-menu row IS, which decides what a click does. */
typedef enum {
    TIKU_TRK_DB_ITEM_SEP = 0,
    TIKU_TRK_DB_ITEM_ABOUT,
    TIKU_TRK_DB_ITEM_FIND,
    TIKU_TRK_DB_ITEM_PREFS,
    TIKU_TRK_DB_ITEM_SHOW_REPLICANTS,
    TIKU_TRK_DB_ITEM_QUIT,
    TIKU_TRK_DB_ITEM_PATH,      /* open it                              */
    TIKU_TRK_DB_ITEM_DIR,       /* expand it: its contents are a submenu */
    TIKU_TRK_DB_ITEM_RECENT,    /* one of the three recent lists         */
    TIKU_TRK_DB_ITEM_DISABLED   /* said, not chosen                      */
} tiku_trk_db_item_kind_t;

/** @brief Which recent list a RECENT row shows. */
typedef enum {
    TIKU_TRK_DB_RECENT_DOCS = 0,
    TIKU_TRK_DB_RECENT_FOLDERS,
    TIKU_TRK_DB_RECENT_APPS,
    TIKU_TRK_DB_RECENT_KINDS
} tiku_trk_db_recent_t;

/** @brief How many entries one recent list shows. */
#define TIKU_TRK_DB_RECENT_MAX 50

typedef struct {
    char                    label[TIKU_TRK_DB_LABEL_MAX];
    char                    path[TIKU_TRK_PATH_MAX];
    tiku_trk_db_item_kind_t kind;
    int                     which;   /* RECENT: which list              */
} tiku_trk_db_item_t;

typedef struct tiku_trk_deskbar tiku_trk_deskbar_t;
typedef struct tiku_trk_deskbar_settings tiku_trk_deskbar_settings_t;

/**
 * @brief Where the folder whose contents the leaf menu shows lives (DB-003).
 *
 * The user's own, with a fallback: a system copy is what a machine with no
 * per-user menu folder shows, rather than an empty menu.
 */
void tiku_trk_deskbar_set_menu_dir(tiku_trk_deskbar_t *db, const char *user,
                                   const char *fallback);

/** @brief The folder the menu is actually reading, or "" (DB-065). */
const char *tiku_trk_deskbar_menu_dir(const tiku_trk_deskbar_t *db);
/** @brief The user's editable folder, even while the fallback is shown. */
const char *tiku_trk_deskbar_user_menu_dir(const tiku_trk_deskbar_t *db);

/**
 * @brief Hand the bar one of the recent lists (DB-002, DB-074).
 *
 * Fed rather than read: the lists are the shell's, shared with the Open
 * panel, and a bar that read them itself would keep a second copy that
 * disagreed.  A list with nothing in it is not shown at all.
 */
void tiku_trk_deskbar_set_recent(tiku_trk_deskbar_t *db,
                                 tiku_trk_db_recent_t which,
                                 const char paths[][TIKU_TRK_PATH_MAX],
                                 int n);

/** @brief How many rows the leaf menu has, and what row @p i is. */
int tiku_trk_deskbar_leaf_count(const tiku_trk_deskbar_t *db);
const tiku_trk_db_item_t *tiku_trk_deskbar_leaf_item(
    const tiku_trk_deskbar_t *db, int i);

/** @brief Whether @p d lays the bar out down an edge rather than along one. */
int tiku_trk_deskbar_vertical(tiku_trk_db_dock_t d);

/** @brief Whether @p d is a collapsed mode: the leaf and the tray only. */
int tiku_trk_deskbar_mini(tiku_trk_db_dock_t d);

/**
 * @brief The dock a point on the screen asks for (DB-048).
 *
 * The screen is divided into zones the way the original's little monitor
 * is: the half the point is nearer decides the edge, and how far into the
 * corner it is decides expanded or mini.  A drag that ends in the middle
 * keeps the dock it started from, which is what makes a mis-drag harmless.
 */
tiku_trk_db_dock_t tiku_trk_deskbar_dock_at(int x, int y, int sw, int sh,
                                            tiku_trk_db_dock_t now);

/** @brief The width (or height, when horizontal) the user has set. */
#define TIKU_TRK_DB_MIN_W 134
#define TIKU_TRK_DB_MAX_W 320

/**
 * @brief Resize the bar's short axis (DB-050).
 *
 * Clamped: below the minimum a row's text is unreadable, and above the
 * maximum the bar is taking screen from the windows it exists to list.
 *
 * @return the width actually taken.
 */
int tiku_trk_deskbar_set_width(tiku_trk_deskbar_t *db, int w);
int tiku_trk_deskbar_width(const tiku_trk_deskbar_t *db);

/*---------------------------------------------------------------------------*/
/* Auto-hide and auto-raise (DB-051, DB-052)                                 */
/*---------------------------------------------------------------------------*/

/** @brief How wide the bar leaves itself when hidden. */
#define TIKU_TRK_DB_SLIVER 2

void tiku_trk_deskbar_set_autohide(tiku_trk_deskbar_t *db, int on);
void tiku_trk_deskbar_set_autoraise(tiku_trk_deskbar_t *db, int on);

/**
 * @brief Tell the bar where the pointer is (DB-036, DB-051, DB-052).
 *
 * The one input the bar never had: with auto-hide it decides whether to
 * show itself, with auto-raise whether to come forward, and an open menu
 * uses it to stay open while the pointer is still on its way back.
 *
 * @return 1 when something about the bar changed and it should be redrawn.
 */
int tiku_trk_deskbar_pointer(tiku_trk_deskbar_t *db, int x, int y);
/**
 * @brief The screen point of the open leaf menu's row named @p label.
 *
 * The submenu is searched first, because its rows are drawn over the
 * leaf's.  Zero when no menu is open or nothing is named that.
 */
int tiku_trk_deskbar_menu_point(const tiku_trk_deskbar_t *db,
                                const char *label, int *x, int *y);

/** @brief The open leaf menu's row labels, or its submenu's, "a|b|" style. */
int tiku_trk_deskbar_menu_labels(const tiku_trk_deskbar_t *db, int sub,
                                 char *out, size_t cap);

/** Whether a pointer location is still within the open menu's tracking margin. */
int tiku_trk_deskbar_menu_keeps_open(const tiku_trk_deskbar_t *db, int x, int y);
/** @brief Deskbar's close policy in the single-process port (DB-053). */
int tiku_trk_deskbar_quit_policy(int prefs_open, int shortcut_quit);

/** @brief Whether the bar is currently showing more than its sliver. */
int tiku_trk_deskbar_shown(const tiku_trk_deskbar_t *db);

/**
 * @brief Scroll the application list (DB-034).
 *
 * The list scrolls only when there is more of it than fits: arrows appear
 * as soon as it runs past the tray, and until then the wheel over the bar
 * does nothing rather than sliding a list that is entirely visible.
 *
 * @param step Rows, positive downwards.
 * @return 1 when the first shown row changed.
 */
int tiku_trk_deskbar_scroll(tiku_trk_deskbar_t *db, int step);

/** @brief The first application row shown, and whether any are hidden. */
int tiku_trk_deskbar_scroll_at(const tiku_trk_deskbar_t *db);
int tiku_trk_deskbar_scrollable(const tiku_trk_deskbar_t *db);

/**
 * @brief Whether the bar should be drawn ABOVE the windows.
 *
 * Without auto-raise it always is, which is what it has always done.  With
 * auto-raise on it only is while the pointer is on it -- so a window can
 * cover the bar until the user reaches for it (DB-052).
 */
int tiku_trk_deskbar_raised(const tiku_trk_deskbar_t *db);


/** @brief What a click on the Deskbar asked for. */
typedef enum {
    TIKU_TRK_DB_NONE = 0,
    TIKU_TRK_DB_ACTIVATE_WINDOW,  /* `tag` names the window            */
    TIKU_TRK_DB_MINIMIZE_WINDOW,
    TIKU_TRK_DB_CLOSE_WINDOW,
    TIKU_TRK_DB_OPEN_PATH,
    TIKU_TRK_DB_SHOW_ABOUT,
    TIKU_TRK_DB_FIND,
    TIKU_TRK_DB_PREFERENCES,
    TIKU_TRK_DB_HIDE_ALL,
    TIKU_TRK_DB_SHOW_ALL,
    TIKU_TRK_DB_CLOSE_ALL,
    TIKU_TRK_DB_QUIT,
    TIKU_TRK_DB_CLOCK_HIDE,
    TIKU_TRK_DB_CLOCK_SHOW,
    TIKU_TRK_DB_CLOCK_SECONDS,
    TIKU_TRK_DB_CLOCK_DAY,
    TIKU_TRK_DB_CLOCK_ZONE,
} tiku_trk_db_action_t;

typedef struct {
    tiku_trk_db_action_t what;
    void                *tag;
    char                 path[TIKU_TRK_PATH_MAX];
} tiku_trk_db_pick_t;

tiku_trk_deskbar_t *tiku_trk_deskbar_new(tiku_trk_backend_t *backend);

/**
 * @brief Freeze the clock at @p at; 0 returns it to the wall.
 *
 * A scripted run is a fixture, and its clock must be one too: a minute
 * rolling over between an incremental frame and its reference redraw
 * showed up as a fistful of differing clock pixels in nothing else.
 */
void tiku_trk_deskbar_set_clock(tiku_trk_deskbar_t *db, time_t at);

/**
 * @brief The fields the clock's rendering varies with, as one string.
 *
 * The pulse compares this instead of dirtying blindly, so a repaint is
 * owed only when the displayed time actually turned over (DB-044).
 *
 * @return Nonzero while the clock is shown.
 */
int tiku_trk_deskbar_clock_stamp(const tiku_trk_deskbar_t *db, char *out,
                                 size_t max);

/** @brief Attach the durable store used for tray subscriptions. */
void tiku_trk_deskbar_set_store(tiku_trk_deskbar_t *db,
                                tiku_trk_store_t *store);
/** @brief Load saved tray paths, pruning entries that no longer resolve. */
int tiku_trk_deskbar_load_replicants(tiku_trk_deskbar_t *db);
/** @brief Remove a tray subscription whose source disappeared. */
int tiku_trk_deskbar_remove_replicant(tiku_trk_deskbar_t *db,
                                      const char *path);
/** @brief Update a tray source after the backing node was moved. */
int tiku_trk_deskbar_repoint_replicant(tiku_trk_deskbar_t *db,
                                       const char *old_path,
                                       const char *new_path);

void tiku_trk_deskbar_free(tiku_trk_deskbar_t *db);

void tiku_trk_deskbar_set_dock(tiku_trk_deskbar_t *db,
                               tiku_trk_db_dock_t dock);
tiku_trk_db_dock_t tiku_trk_deskbar_dock(const tiku_trk_deskbar_t *db);

/** @brief Apply all live preference effects to the bar (DB-061, DB-073). */
void tiku_trk_deskbar_apply_settings(tiku_trk_deskbar_t *db,
    const tiku_trk_deskbar_settings_t *settings);

/**
 * @brief Register an application in the list.
 *
 * An application appears once however many instances it has, and disappears
 * when its last one quits (DB-014, DB-015).
 *
 * @return Its index, or -1.
 */
int tiku_trk_deskbar_add_team(tiku_trk_deskbar_t *db, const char *name);

/** @brief Replace an application's window list, keeping its expanded state. */
void tiku_trk_deskbar_set_windows(tiku_trk_deskbar_t *db, int team,
                                  const tiku_trk_dbwin_t *wins, int n,
                                  int active);

/** @brief Add a tray replicant watching @p path. */
int tiku_trk_deskbar_add_replicant(tiku_trk_deskbar_t *db, const char *label,
                                   const char *path);
int tiku_trk_deskbar_set_replicant_height(tiku_trk_deskbar_t *db,
                                          const char *path, int height);
int tiku_trk_deskbar_replicant_height(const tiku_trk_deskbar_t *db,
                                      const char *path);
void tiku_trk_deskbar_set_show_replicants(tiku_trk_deskbar_t *db, int show);
int tiku_trk_deskbar_show_replicants(const tiku_trk_deskbar_t *db);

/** @brief Re-read the tray's nodes.  @return values that changed. */
int tiku_trk_deskbar_refresh(tiku_trk_deskbar_t *db);

/** @brief How many applications the bar is tracking. */
int tiku_trk_deskbar_team_count(const tiku_trk_deskbar_t *db);

/** @brief The name of team @p i, or NULL. */
const char *tiku_trk_deskbar_team_name(const tiku_trk_deskbar_t *db, int i);

/**
 * @brief Strip the owning application's name from a window title.
 *
 * "Tracker: /sys" and "/sys - Tracker" both become "/sys", which is why the
 * list reads as places rather than as the same word repeated.
 */
void tiku_trk_deskbar_strip_title(const char *app, const char *title,
                                  char *out, size_t max);

/** @brief The strip the bar occupies; windows keep out of it. */
tiku_desk_rect_t tiku_trk_deskbar_frame(const tiku_trk_deskbar_t *db);

void tiku_trk_deskbar_draw(tiku_trk_deskbar_t *db, tiku_desk_surface_t *s);

int tiku_trk_deskbar_hit(const tiku_trk_deskbar_t *db, int x, int y);

/**
 * @brief Handle a click.
 *
 * @param mods  TIKU_TRK_MOD_* -- Ctrl minimises rather than raises, as the
 *              original's Ctrl+click does.
 * @return 1 when the click was consumed.
 */
int tiku_trk_deskbar_click(tiku_trk_deskbar_t *db, int x, int y,
                           unsigned mods, int secondary,
                           tiku_trk_db_pick_t *pick);

/*---------------------------------------------------------------------------*/
/* Drops (DB-008, DB-009, DB-035, DB-066)                                    */
/*---------------------------------------------------------------------------*/

/** @brief What sits under a drag hovering the Deskbar. */
typedef enum {
    TIKU_TRK_DB_DROP_NONE = 0,
    TIKU_TRK_DB_DROP_LEAF,      /* the logo: links go in the Deskbar folder */
    TIKU_TRK_DB_DROP_TEAM,      /* an application: hand the files to it     */
    TIKU_TRK_DB_DROP_MENU_DIR,  /* a folder in the menu: links go there     */
    TIKU_TRK_DB_DROP_MENU_FILE, /* a file in the menu: launch it with them  */
    TIKU_TRK_DB_DROP_BAR        /* the strip itself: launch the files       */
} tiku_trk_db_drop_t;

/**
 * @brief What a drag hovering @p x, @p y would drop onto.
 *
 * @param which Receives the team index for DROP_TEAM, or -1.
 */
tiku_trk_db_drop_t tiku_trk_deskbar_drop_at(const tiku_trk_deskbar_t *db,
                                            int x, int y, int *which);

/**
 * @brief Tell the bar a drag is in flight, so its menu can answer for it.
 *
 * While one is, the fixed leaf-menu items are disabled: About and Power off
 * are not drop targets, and leaving them live invites a drop that does
 * something unrelated to dropping (DB-008).
 */
void tiku_trk_deskbar_set_dragging(tiku_trk_deskbar_t *db, int dragging);

/**
 * @brief What a drag over the OPEN leaf menu is on (DB-009).
 *
 * A folder entry takes the links; a fixed entry takes nothing, since
 * those are disabled while a drag is in flight.
 *
 * @param path Receives the folder's path for DROP_MENU_DIR.
 */
tiku_trk_db_drop_t tiku_trk_deskbar_menu_drop_at(const tiku_trk_deskbar_t *db,
                                                 int x, int y, char *path,
                                                 size_t max);

/**
 * @brief Light the application a hovering drag would be handed to.
 *
 * Pass -1 for none.  Held on the bar rather than in the shell because the
 * bar is what draws it, and a highlight the drawing has to be told about
 * separately is one that can disagree with the drop (DB-035).
 */
void tiku_trk_deskbar_set_drop_team(tiku_trk_deskbar_t *db, int i);

/** @brief Which application is lit, or -1. */
int tiku_trk_deskbar_drop_team(const tiku_trk_deskbar_t *db);

/** @brief Whether a fixed leaf-menu item may be chosen right now. */
int tiku_trk_deskbar_item_enabled(const tiku_trk_deskbar_t *db, int fixed);

/**
 * @brief Whether team @p i can be handed files of these types (DB-035).
 *
 * Unknown types are refused unless @p force -- Ctrl is the override, and
 * without it a drop on an application that cannot read the file is a drop
 * that appears to work and does nothing.  Tracker itself takes anything.
 */
int tiku_trk_deskbar_team_accepts(const tiku_trk_deskbar_t *db, int i,
                                  const char *const *types, int ntypes,
                                  int force);

/** @brief Give team @p i a type it can open. */
int tiku_trk_deskbar_team_type(tiku_trk_deskbar_t *db, int i,
                               const char *type);

/**
 * @brief Whether @p path should be launched by a Deskbar drop (DB-066).
 *
 * A directory is silently ignored: launching one has no meaning, and
 * refusing out loud for something the user very likely did by accident
 * would be noise.
 */
int tiku_trk_deskbar_launchable(const tiku_trk_model_t *m);

int tiku_trk_deskbar_menu_open(const tiku_trk_deskbar_t *db);
/** @brief Open the leaf menu, as the Menu key does (DB-011). */
void tiku_trk_deskbar_open_menu(tiku_trk_deskbar_t *db);

void tiku_trk_deskbar_close_menu(tiku_trk_deskbar_t *db);

/** @brief Whether the clock's calendar is showing. */
int tiku_trk_deskbar_calendar_open(const tiku_trk_deskbar_t *db);
/** @brief Calendar's currently displayed month (1..12) and year/day. */
void tiku_trk_deskbar_calendar_date(const tiku_trk_deskbar_t *db,
                                    int *year, int *month, int *day);
int tiku_trk_deskbar_clock_visible(const tiku_trk_deskbar_t *db);
void tiku_trk_deskbar_set_clock_visible(tiku_trk_deskbar_t *db, int visible);
void tiku_trk_deskbar_close_calendar(tiku_trk_deskbar_t *db);

#endif /* TIKU_TRK_DESKBAR_H_ */
