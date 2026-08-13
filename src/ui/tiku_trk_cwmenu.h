/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_cwmenu.h - a folder window's menus.
 *
 * The inventories are the container window's own: File and Window always,
 * Attributes only in list view, and four different context menus chosen by
 * what the pointer is over.  Every item is added through the action table, so
 * the File menu and the pose context menu cannot disagree about whether an
 * action is available.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_CWMENU_H_
#define TIKU_TRK_CWMENU_H_

#include "tiku_trk_menu.h"
#include "tiku_trk_model.h"
#include "tiku_trk_shortcuts.h"

typedef struct {
    tiku_trk_menubar_t bar;
    tiku_trk_menu_t    file;
    tiku_trk_menu_t    window;
    tiku_trk_menu_t    attributes;
    tiku_trk_menu_t    arrange;      /* submenu of Window                   */
    tiku_trk_menu_t    icon_size;    /* submenu of Window                   */
    /* The four context menus, kept apart rather than rebuilt into one, so
     * which one appeared is answerable afterwards. */
    tiku_trk_menu_t    pose_ctx;
    tiku_trk_menu_t    window_ctx;
    tiku_trk_menu_t    volume_ctx;
    tiku_trk_menu_t    trash_ctx;
    tiku_trk_menu_t    drop_ctx;
    /* The applications that can open what is selected, as a submenu of the
     * "Open with" item.  Built by the shell, which is what knows the
     * registry, and attached here so both menus show the same one. */
    tiku_trk_menu_t    open_with;
    /* One submenu per type the folder actually holds, each offering the
     * columns that type has to show (CW-026).  Filled by the shell, which
     * is what can see the listing. */
    tiku_trk_menu_t    per_type[4];
    char               per_type_name[4][TIKU_TRK_TYPE_MAX];
    int                per_type_count;
    int                attributes_shown;
} tiku_trk_cwmenu_t;

/** @brief The command range a candidate application is picked with. */
/** @brief The command range an attribute column is toggled with. */
#define TIKU_TRK_CMD_COL_ATTR_BASE 800

#define TIKU_TRK_CMD_OPEN_WITH_BASE 900

void tiku_trk_cwmenu_init(tiku_trk_cwmenu_t *cw);

/**
 * @brief Rebuild every menu against @p c.
 *
 * Called when a menu is about to open and whenever the modifiers change while
 * one is open: labels and enabled states are decided now, never cached.
 */
void tiku_trk_cwmenu_rebuild(tiku_trk_cwmenu_t *cw,
                             const tiku_trk_sc_ctx_t *c);

/**
 * @brief Which context menu belongs to what was clicked.
 *
 * @param m NULL when the click was on empty window background.
 */
tiku_trk_menu_t *tiku_trk_cwmenu_context(tiku_trk_cwmenu_t *cw,
                                         const tiku_trk_model_t *m,
                                         const tiku_trk_sc_ctx_t *c);

/** @brief The menu offered when items are dropped with the second button. */
tiku_trk_menu_t *tiku_trk_cwmenu_drop(tiku_trk_cwmenu_t *cw,
                                      int dst_read_only, int src_read_only,
                                      unsigned mods);

#endif /* TIKU_TRK_CWMENU_H_ */
