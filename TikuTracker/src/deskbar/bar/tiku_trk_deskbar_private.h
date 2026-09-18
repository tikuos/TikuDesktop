/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_private.h - what the tiku_trk_deskbar files share: the
 * includes, the types, the small accessors that read them, and the functions
 * one file defines for another.  Nothing outside this directory includes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_PRIVATE_H_
#define TIKU_TRK_DESKBAR_PRIVATE_H_

/* split: public TikuTracker/src/deskbar/tiku_trk_deskbar.h */

/* split: tag db */

#include "tiku_logo.h"
#include "tiku_trk_deskbar.h"
#include "tiku_mount.h"
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_iconpaint.h"
#include "tiku_strings.h"
#include "tiku_trk_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <locale.h>
#include <time.h>

/* Metrics read out of Deskbar's own source rather than estimated.  Rows do
 * not tile: each is drawn one pixel short and the seam between them is left
 * unpainted, so the pitch is the row plus SEAM. */
#define BAR_W        134     /* gMinimumWindowWidth at the 12 px face      */
/*
 * The leaf is the one control on the strip that is not a list item, and
 * it is the way in to the whole system -- so it is given the room to
 * look like it, rather than the twenty-six pixels a row of text happens
 * to need.  The mark inside it grows with it, and past twenty-two it
 * stops being drawn as a filled silhouette and becomes the wireframe
 * cube, which is the rendition worth having when there is room for it.
 */
#define LEAF_MIN      34
#define HLEAF_W 120     /* the leaf button, horizontal (DB-033) */
#define TEAM_MIN      20     /* kMinTeamItemHeight; pitch 21               */
#define WIN_MIN       19     /* pitch 20                                   */
#define SEAM           1     /* kGutter: the unpainted row between items   */
#define WIN_INDENT    22
#define TRAY_ROW      18     /* kGutter + fMaxReplicantHeight + kGutter    */
#define CLOCK_MIN     20
#define PAD            4
#define EXPANDER_W    13     /* ExpanderBounds() is 13 px, not 12          */
#define EXPANDER_ARM   5
#define EXPANDER_PEN   2
#define TRAY_PAD       3     /* fTrayPadding                               */
#define CLOCK_MARGIN  12     /* fClockMargin = fTrayPadding * 4            */
#define SWITCH_ROW    16     /* the theme switch's own strip of tray        */
#define SWITCH_W      26     /* the track */
#define SWITCH_H      12


struct tiku_trk_deskbar {
    tiku_backend_t  *backend;   /* the namespace the leaf menu lists */
    tiku_store_t     *store;    /* where the tray persists, or NULL  */
    tiku_trk_db_dock_t   dock;  /* which edge, expanded or mini      */
    /* The running applications, one row each; order[] holds indices
     * into teams[] in the order the rows are shown. */
    tiku_trk_team_t      teams[TIKU_TRK_DB_TEAMS_MAX];
    int                  order[TIKU_TRK_DB_TEAMS_MAX];  /* teams[] indices */
    int                  dragging;  /* a drag is passing over the bar */
    int                  drop_team; /* team row under the drag, or -1 */
    int                  team_count;    /* teams[] in use              */
    tiku_trk_replicant_t tray[TIKU_TRK_DB_TRAY_MAX];  /* the tray rows */
    int                  tray_count;    /* tray[] in use               */
    int                  next_replicant;   /* the ids handed out (DB-038) */
    int                  width;     /* the short axis, user-set        */
    /* Which of the three tables is in force: 0 light, 1 dark, 2 no hue.
     * The bar only DRAWS it; the shell owns the setting. */
    int                  theme;
    /* The applications the leaf menu offers, as installed. */
    struct {
        char label[TIKU_TRK_DB_LABEL_MAX];  /* the row's text        */
        char command[TIKU_PATH_MAX];        /* what launching it runs */
        char icon[24];          /* the icon set's name for it */
        int  group;             /* which leaf row lists it */
    }                    app[TIKU_TRK_DB_APPS_MAX];
    int                  app_count; /* app[] in use                    */
    int                  top_inset; /* the global bar owns this much   */
    tiku_trk_deskbar_settings_t settings;   /* the preferences in force */
    int                  autohide;  /* hidden until the pointer comes  */
    int                  autoraise; /* raised over windows on hover    */
    int                  show_replicants;   /* the tray is drawn at all */
    int                  pointer_on;  /* the pointer is over the bar    */
    int                  scroll;      /* first application row shown    */
    int                  overflow;    /* the list ran past the tray     */
    int                  menu_open;     /* the leaf menu is showing     */
    int                  calendar_open; /* the calendar popup is showing */
    int                  clock_menu_open;   /* the clock's menu is showing */
    int                  calendar_year;     /* the month the calendar    */
    int                  calendar_month;    /* shows, 0-based month      */
    time_t               fixed_now;     /* frozen clock for fixtures, or 0 */
    /* The hot row of each little menu, following the pointer; -1 when the
     * pointer is elsewhere.  The draw has always been able to highlight
     * one; this is the hand that moves it. */
    int                  leaf_hot;      /* into leaf[]                  */
    int                  sub_hot;       /* into sub[]                   */
    int                  sub_pending;   /* leaf awaiting hover dwell, -1 */
    tiku_rect_t          hover_damage;  /* what the last hover changed  */
    int64_t              sub_entered;   /* dwell start; -1 once attempted */
    tiku_trk_db_icon_fn  menu_icon;     /* injected by the shell, or NULL */
    void                *menu_icon_ctx; /* handed back to menu_icon     */
    int                  win_hot;       /* the window menu's row        */
    int                  clock_hot;     /* the clock menu's row         */
    int                  calendar_day;  /* the day picked, 1-based      */
    int                  win_menu_team;    /* -1 = none showing        */
    /* The expander a press is riding: the arrow turns down at once but
     * the state commits only on RELEASE over the same strip (DB-022). */
    int                  expander_press;   /* team index, or -1        */
    /* The row the pointer is resting on whose FULL name deserves saying:
     * a truncated label, or any application row while names are hidden
     * (DB-032). */
    char                 tip[TIKU_TRK_DB_LABEL_MAX];
    int                  tip_y;            /* the row's top, or -1     */
    /* The horizontal strip's layout, computed at draw time so the click
     * and the drawing read the same numbers (DB-033). */
    int                  hbutton_w;        /* one team button's width  */
    int                  hleft;            /* first button's left edge */
    int                  hoverflow;        /* the strip ran out of room */
    /* Mini mode's second icon: the running-apps popup (DB-037). */
    int                  apps_menu_open;   /* the popup is showing     */
    int                  apps_hot;         /* its row under the pointer */
    int                  screen_h;         /* the surface's, at last draw */
    /* The leaf menu, built from the namespace every time it opens rather
     * than compiled in: a file put in the Deskbar folder has to appear
     * without restarting anything (DB-005). */
    tiku_trk_db_item_t   leaf[TIKU_TRK_DB_LEAF_MAX];   /* its rows      */
    int                  leaf_count;       /* leaf[] in use            */
    int                  sub_of;           /* leaf row whose submenu is
                                            * open, or -1              */
    tiku_trk_db_item_t   sub[TIKU_TRK_DB_SUB_MAX];     /* that submenu  */
    int                  sub_count;        /* sub[] in use             */
    char                 menu_dir[TIKU_PATH_MAX];  /* the folder read  */
    /* The system's Deskbar folder, read when menu_dir has nothing. */
    char                 menu_dir_fallback[TIKU_PATH_MAX];
    /* Recent paths per kind, newest first (DB-074). */
    char                 recent[TIKU_TRK_DB_RECENT_KINDS]
                               [TIKU_TRK_DB_RECENT_MAX][TIKU_PATH_MAX];
    int                  recent_count[TIKU_TRK_DB_RECENT_KINDS]; /* in use */
    tiku_rect_t     frame;      /* the bar's rectangle on the screen  */
};

/** @brief Collects one directory entry into the menu being built. */
struct menu_walk {
    tiku_trk_deskbar_t *db;     /* whose menu is being built          */
    int                 n;      /* rows written into into[] so far    */
    tiku_trk_db_item_t *into;   /* the rows, or NULL to only count    */
    int                 max;    /* into[] capacity                    */
};

/** @brief Hand one row's item to the shell's painter. */
struct menu_icon_relay {
    tiku_trk_deskbar_t       *db;       /* holds the painter and its ctx */
    const tiku_trk_db_item_t *items;    /* the menu's rows, by index     */
};

/* Shared across the family: defined in one file, used in another. */

/* tiku_trk_deskbar.c */
int db_clock_row(void);
int db_leaf_row(void);
int db_team_row(void);
int db_win_row(void);

/* tiku_trk_deskbar_draw.c */
tiku_rect_t db_leaf_menu_rect(const tiku_trk_deskbar_t *db, int rowh, int rows);
tiku_rect_t db_sub_menu_rect(const tiku_trk_deskbar_t *db, tiku_rect_t m,
                             int rowh);
void db_open_sub(tiku_trk_deskbar_t *db, int at);

/* tiku_trk_deskbar_layout.c */
int db_list_rows(const tiku_trk_deskbar_t *db);
int db_list_y(const tiku_trk_deskbar_t *db);
int db_tray_y(const tiku_trk_deskbar_t *db);
void db_draw_switch(const tiku_trk_deskbar_t *db, tiku_surface_t *s);

/* tiku_trk_deskbar_leaf.c */
const char *db_leaf_name(const char *path);
int db_calendar_days(int year, int month);
int db_menu_entry(const tiku_model_t *m, void *ctx);
void db_build_leaf(tiku_trk_deskbar_t *db);
void db_calendar_shift(tiku_trk_deskbar_t *db, int months, int years);

/* tiku_trk_deskbar_winmenu.c */
int db_win_menu_items(const tiku_trk_deskbar_t *db, int team,
                      const char **items, int *enabled);
tiku_rect_t db_calendar_rect(const tiku_trk_deskbar_t *db);
tiku_rect_t db_win_menu_rect(const tiku_trk_deskbar_t *db, int rows, int rowh);
void db_reset_menu_hots(tiku_trk_deskbar_t *db);
void db_team_action(tiku_trk_deskbar_t *db, int team, unsigned mods, int button,
                    int clicks, tiku_trk_db_pick_t *pick);

/* End of the shared declarations. */

#endif /* TIKU_TRK_DESKBAR_PRIVATE_H_ */
