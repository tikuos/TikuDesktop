/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_menu.h - menus.
 *
 * The geometry here is not invented: padding, item height, the reserved mark
 * gutter, the stroked check and submenu arrow, the two-line separator and the
 * one-pixel seam between items all come from the menu kit's own layout code,
 * so a menu drawn here has the proportions of the one it is modelled on.
 *
 * A menu is a value the caller BUILDS each time it opens, which is how the
 * original works too: item labels and enabled states are decided at open time
 * from the current selection, never cached across openings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_MENU_H_
#define TIKU_TRK_MENU_H_

#include "tiku_gfx.h"
#include "tiku_font.h"

#define TIKU_TRK_MENU_ITEMS_MAX  40
#define TIKU_TRK_MENU_LABEL_MAX  48
#define TIKU_TRK_MENU_DEPTH_MAX   4

/** @brief Modifier bits, matching the pose view's. */
#define TIKU_TRK_MENU_MOD_SHIFT  0x1u
#define TIKU_TRK_MENU_MOD_CMD    0x2u
#define TIKU_TRK_MENU_MOD_CTRL   0x4u

/** @brief One entry.  A separator carries no label and no command. */
typedef struct tiku_trk_mitem {
    char                  label[TIKU_TRK_MENU_LABEL_MAX];
    int                   command;      /* 0 for a separator or a submenu   */
    char                  shortcut;     /* 0 for none                       */
    unsigned              mods;         /* modifiers shown with the shortcut */
    int                   enabled;
    int                   marked;       /* draws the check                  */
    int                   separator;
    char                  icon[32];        /* optional file/MIME art key */
    struct tiku_trk_menu *submenu;      /* not owned                        */
} tiku_trk_mitem_t;

typedef struct tiku_trk_menu {
    char             title[TIKU_TRK_MENU_LABEL_MAX];
    tiku_trk_mitem_t item[TIKU_TRK_MENU_ITEMS_MAX];
    int              count;
    /* Filled by layout; valid after tiku_trk_menu_measure(). */
    int              width;
    int              height;
    int              open_index;        /* item whose submenu is showing    */
    int              hot;               /* item under the pointer, or -1    */
} tiku_trk_menu_t;

/** @brief Empty it, keeping the title. */
void tiku_trk_menu_clear(tiku_trk_menu_t *m);

/**
 * @brief Append an item.
 *
 * @param sc   Shortcut character, or 0.
 * @param mods Modifiers drawn with the shortcut.
 * @return Its index, or -1 when the menu is full.
 */
int tiku_trk_menu_add(tiku_trk_menu_t *m, const char *label, int command,
                      char sc, unsigned mods, int enabled);

/** @brief Append a separator: permanently disabled and never hit-tested. */
int tiku_trk_menu_add_separator(tiku_trk_menu_t *m);

/** @brief Append an item that opens @p sub instead of sending a command. */
int tiku_trk_menu_add_submenu(tiku_trk_menu_t *m, const char *label,
                              tiku_trk_menu_t *sub, int enabled);

/** @brief Insert an item at @p at, shifting the rest down. */
int tiku_trk_menu_insert(tiku_trk_menu_t *m, int at, const char *label,
                         int command, char sc, unsigned mods, int enabled);

/** @brief Remove the item at @p at. */
void tiku_trk_menu_remove(tiku_trk_menu_t *m, int at);

/** @brief Index of the item carrying @p command, or -1. */
int tiku_trk_menu_find(const tiku_trk_menu_t *m, int command);

/** @brief Set the check on the item carrying @p command. */
void tiku_trk_menu_mark(tiku_trk_menu_t *m, int command, int marked);

/** @brief Mark exactly one of a group, as a radio set does. */
void tiku_trk_menu_mark_radio(tiku_trk_menu_t *m, const int *commands, int n,
                              int chosen);

void tiku_trk_menu_enable(tiku_trk_menu_t *m, int command, int enabled);
void tiku_trk_menu_set_icon(tiku_trk_menu_t *m, int command, const char *icon);

/**
 * @brief How a menu paints a named icon; @p dimmed for a disabled item.
 *
 * A hook rather than a dependency: the menu layer stays linkable without
 * the rasteriser, and a build without one keeps the plain slot (IV-054).
 *
 * @return Nonzero when the art was painted.
 */
typedef int (*tiku_trk_menu_icon_fn)(tiku_surface_t *s,
    const char *icon, int x, int y, int size, int dimmed);
void tiku_trk_menu_set_icon_painter(tiku_trk_menu_icon_fn fn);

/** @brief Replace an item's label and command together, as a live menu does. */
void tiku_trk_menu_relabel(tiku_trk_menu_t *m, int at, const char *label,
                           int command, char sc, unsigned mods);

/** @brief Compute width and height into the menu.  Idempotent. */
void tiku_trk_menu_measure(tiku_trk_menu_t *m);

/** @brief Height of the item at @p i, once measured. */
int tiku_trk_menu_item_height(const tiku_trk_menu_t *m, int i);

/** @brief Top of the item at @p i, relative to the menu's own top. */
int tiku_trk_menu_item_top(const tiku_trk_menu_t *m, int i);

/**
 * @brief The item at a point inside the menu, or -1.
 *
 * Separators and the one-pixel seams between items are misses, exactly as
 * they are in the original.
 */
int tiku_trk_menu_item_at(const tiku_trk_menu_t *m, tiku_rect_t frame,
                          int x, int y);

/**
 * @brief Let the hot row follow the pointer over an open menu.
 *
 * The draw has always highlighted m->hot; this is the hand that moves it.
 * Outside the frame the hot row clears, so a menu never keeps a stale
 * highlight for a pointer that left.
 *
 * @return Nonzero when the hot row CHANGED, so the caller knows to paint.
 */
int tiku_trk_menu_track(tiku_trk_menu_t *m, tiku_rect_t frame,
                        int x, int y);

/** @brief Where the menu should sit so it stays on @p screen. */
tiku_rect_t tiku_trk_menu_place(tiku_trk_menu_t *m, int x, int y,
                                     tiku_rect_t screen);

/**
 * @brief Where a submenu of item @p i sits: off the item's right edge, or
 *        flipped to the left when it would leave @p screen.
 */
tiku_rect_t tiku_trk_menu_place_sub(tiku_trk_menu_t *m, int i,
                                         tiku_rect_t frame,
                                         tiku_rect_t screen);

void tiku_trk_menu_draw(tiku_trk_menu_t *m, tiku_surface_t *s,
                        tiku_rect_t frame);

/*---------------------------------------------------------------------------*/
/* The menu bar                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_TRK_MENUBAR_MAX 6

typedef struct {
    tiku_trk_menu_t *menu[TIKU_TRK_MENUBAR_MAX];
    int              count;
    int              open;               /* index of the open menu, or -1   */
    int              height;
} tiku_trk_menubar_t;

void tiku_trk_menubar_init(tiku_trk_menubar_t *b);

int tiku_trk_menubar_add(tiku_trk_menubar_t *b, tiku_trk_menu_t *m);

/** @brief Insert at @p at, which is how the Attributes menu comes and goes. */
int tiku_trk_menubar_insert(tiku_trk_menubar_t *b, int at,
                            tiku_trk_menu_t *m);

void tiku_trk_menubar_remove(tiku_trk_menubar_t *b, tiku_trk_menu_t *m);

/** @brief Title index at a point on the bar, or -1. */
int tiku_trk_menubar_title_at(const tiku_trk_menubar_t *b,
                              tiku_rect_t frame, int x, int y);

/** @brief Where the open menu's panel sits, under its title. */
tiku_rect_t tiku_trk_menubar_panel(tiku_trk_menubar_t *b,
                                        tiku_rect_t frame,
                                        tiku_rect_t screen);

void tiku_trk_menubar_draw(tiku_trk_menubar_t *b, tiku_surface_t *s,
                           tiku_rect_t frame);

#endif /* TIKU_TRK_MENU_H_ */
