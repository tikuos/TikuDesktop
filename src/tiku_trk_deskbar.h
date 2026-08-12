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

#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_attr.h"
#include "tiku_trk_model.h"
#include "tiku_trk_win.h"

#define TIKU_TRK_DB_TEAMS_MAX   8
#define TIKU_TRK_DB_WINS_MAX   16
#define TIKU_TRK_DB_TRAY_MAX    8
#define TIKU_TRK_DB_LABEL_MAX  48

/** @brief Where the bar is docked.  Expanded modes reserve an edge strip. */
typedef enum {
    TIKU_TRK_DB_RIGHT_EXPANDED = 0,   /* the BeOS default              */
    TIKU_TRK_DB_LEFT_EXPANDED,
    TIKU_TRK_DB_RIGHT_MINI,           /* collapsed to the leaf + tray  */
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
} tiku_trk_team_t;

/** @brief A tray item: a namespace node rendered as label + value. */
typedef struct {
    char            label[16];
    char            path[TIKU_TRK_PATH_MAX];
    char            value[24];      /* raw, as the node reports it        */
    tiku_trk_desc_t desc;           /* resolved once, at subscribe time   */
    int             desc_known;
    int             valid;
} tiku_trk_replicant_t;

typedef struct tiku_trk_deskbar tiku_trk_deskbar_t;

/** @brief What a click on the Deskbar asked for. */
typedef enum {
    TIKU_TRK_DB_NONE = 0,
    TIKU_TRK_DB_ACTIVATE_WINDOW,  /* `tag` names the window            */
    TIKU_TRK_DB_MINIMIZE_WINDOW,
    TIKU_TRK_DB_CLOSE_WINDOW,
    TIKU_TRK_DB_OPEN_PATH,
    TIKU_TRK_DB_SHOW_ABOUT,
    TIKU_TRK_DB_FIND,
    TIKU_TRK_DB_HIDE_ALL,
    TIKU_TRK_DB_SHOW_ALL,
    TIKU_TRK_DB_CLOSE_ALL,
    TIKU_TRK_DB_QUIT,
} tiku_trk_db_action_t;

typedef struct {
    tiku_trk_db_action_t what;
    void                *tag;
    char                 path[TIKU_TRK_PATH_MAX];
} tiku_trk_db_pick_t;

tiku_trk_deskbar_t *tiku_trk_deskbar_new(tiku_trk_backend_t *backend);

void tiku_trk_deskbar_free(tiku_trk_deskbar_t *db);

void tiku_trk_deskbar_set_dock(tiku_trk_deskbar_t *db,
                               tiku_trk_db_dock_t dock);
tiku_trk_db_dock_t tiku_trk_deskbar_dock(const tiku_trk_deskbar_t *db);

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

/** @brief Re-read the tray's nodes.  @return values that changed. */
int tiku_trk_deskbar_refresh(tiku_trk_deskbar_t *db);

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

int tiku_trk_deskbar_menu_open(const tiku_trk_deskbar_t *db);
/** @brief Open the leaf menu, as the Menu key does (DB-011). */
void tiku_trk_deskbar_open_menu(tiku_trk_deskbar_t *db);

void tiku_trk_deskbar_close_menu(tiku_trk_deskbar_t *db);

/** @brief Whether the clock's calendar is showing. */
int tiku_trk_deskbar_calendar_open(const tiku_trk_deskbar_t *db);

#endif /* TIKU_TRK_DESKBAR_H_ */
