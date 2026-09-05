/*
 * TikuTracker -- the file manager for TikuOS.
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

#include "tiku_menu.h"
#include "tiku_model.h"
#include "tiku_trk_shortcuts.h"

/* One command per partition in the Mount submenu (CW-034). */
#define TIKU_TRK_CMD_MOUNT_PART_BASE 1600
/* The save panel's own errand: write the Find panel's query as a file. */
#define TIKU_TRK_CMD_SAVE_QUERY      1700
#define TIKU_TRK_CMD_ADD_SCOPE       1701
/* An application asked the shell to let the user pick a file.  Who
 * asked is not a folder like the other askers, so it rides in the
 * shell's pending_pick rather than in the panel's target. */
#define TIKU_TRK_CMD_APP_PICK        1702
/* The Move to / Copy to / Create link pickers: 16 destinations each
 * (CW-035).  (command - BASE) / 16 says which picker, % 16 which entry. */
#define TIKU_TRK_CMD_RECALC_SIZE     1702
#define TIKU_TRK_CMD_SET_LINK_TARGET 1703
/* The Sounds window asked for a file from anywhere for one moment. */
#define TIKU_TRK_CMD_CHOOSE_SOUND    1704
#define TIKU_TRK_CMD_DEST_BASE       1720
#define TIKU_TRK_CMD_DEST_MAX        16
#define TIKU_TRK_MOUNT_PART_MAX 32

typedef struct {
    tiku_menubar_t bar;
    tiku_menu_t    file;
    tiku_menu_t    new_menu;     /* New folder and discovered templates */
    tiku_menu_t    templates;    /* root of the template hierarchy       */
    tiku_menu_t    window;
    tiku_menu_t    attributes;
    tiku_menu_t    arrange;      /* submenu of Window                   */
    tiku_menu_t    icon_size;    /* submenu of Window                   */
    tiku_menu_t    addons;      /* add-on inventory                      */
    /* Mount >: every partition, mounted ones marked, plus Mount all and
     * Settings… (CW-034).  The names are kept beside the menu so a picked
     * command can be answered without a second scan racing the first. */
    tiku_menu_t    mount_menu;
    char               mount_part[TIKU_TRK_MOUNT_PART_MAX][32];
    int                mount_parts;
    /* The four context menus, kept apart rather than rebuilt into one, so
     * which one appeared is answerable afterwards. */
    tiku_menu_t    pose_ctx;
    tiku_menu_t    window_ctx;
    tiku_menu_t    volume_ctx;
    tiku_menu_t    trash_ctx;
    tiku_menu_t    drop_ctx;
    tiku_menu_t    view_ctx;    /* the gutter's view-mode popup     */
    /* The Info window icon's own copy of its context menu: the shared
     * pose_ctx is rebuilt by every folder draw, and the icon's extra
     * items (AW-076) must survive the frame. */
    tiku_menu_t    info_ctx;
    /* The applications that can open what is selected, as a submenu of the
     * "Open with" item.  Built by the shell, which is what knows the
     * registry, and attached here so both menus show the same one. */
    tiku_menu_t    open_with;
    /* Move to / Copy to / Create link as hierarchical pickers: Current
     * folder, Recent folders, Desktop, Home, volumes -- built by the
     * shell, which knows the recents and the mounts (CW-035, CW-058). */
    tiku_menu_t    picker[3];
    char               picker_path[3][TIKU_TRK_CMD_DEST_MAX][TIKU_PATH_MAX];
    int                picker_paths[3];
    /* One submenu per type the folder actually holds, each offering the
     * columns that type has to show (CW-026).  Filled by the shell, which
     * is what can see the listing. */
    tiku_menu_t    per_type[4];
    char               per_type_icon[4][32]; /* the type's art (IV-056)   */
    char               per_type_name[4][TIKU_TYPE_MAX];
    int                per_type_count;
    int                attributes_shown;
    char               templates_root[TIKU_PATH_MAX];
    struct {
        tiku_menu_t menu;
        char            path[TIKU_PATH_MAX];
    } template_node[64];
    int                template_node_count;
    struct {
        int  command;
        int  add_submenu;
        char path[TIKU_PATH_MAX];
    } template_entry[128];
    int                template_entry_count;
} tiku_trk_cwmenu_t;

/** @brief The command range a candidate application is picked with. */
/** @brief The command range an attribute column is toggled with. */
#define TIKU_TRK_CMD_COL_ATTR_BASE 800

#define TIKU_TRK_CMD_OPEN_WITH_BASE 900
#define TIKU_TRK_CMD_TEMPLATE_BASE 1200
#define TIKU_TRK_CMD_EDIT_TEMPLATES 1320
#define TIKU_TRK_CMD_ADD_TEMPLATE_SUBMENU_BASE 1400

/** @brief Empty menus, with the Templates folder under the home. */
void tiku_trk_cwmenu_init(tiku_trk_cwmenu_t *cw);

/** @brief Return the source path for a dynamic template command, if any. */
const char *tiku_trk_cwmenu_template_path(const tiku_trk_cwmenu_t *cw,
                                           int command);

/** @brief Return the parent directory for an Add new submenu command. */
const char *tiku_trk_cwmenu_template_add_path(const tiku_trk_cwmenu_t *cw,
                                               int command);

/** @brief The user's template directory, or NULL when none is configured. */
const char *tiku_trk_cwmenu_templates_root(const tiku_trk_cwmenu_t *cw);

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
tiku_menu_t *tiku_trk_cwmenu_context(tiku_trk_cwmenu_t *cw,
                                         const tiku_model_t *m,
                                         const tiku_trk_sc_ctx_t *c);

/** @brief The menu offered when items are dropped with the second button. */
tiku_menu_t *tiku_trk_cwmenu_drop(tiku_trk_cwmenu_t *cw,
                                      int dst_read_only, int src_read_only,
                                      unsigned mods);

/** @brief The Find icon's drop menu: query, template, cancel (Q-034). */
tiku_menu_t *tiku_trk_cwmenu_query_drop(tiku_trk_cwmenu_t *cw,
                                            int dst_read_only);

/**
 * @brief The view-mode popup the scroller gutter offers (NeXT UIG ch7:
 *        only controls that decide how the document is VIEWED may live in
 *        the scroller area -- and a view-mode popup is the named example).
 */
tiku_menu_t *tiku_trk_cwmenu_view_popup(tiku_trk_cwmenu_t *cw,
                                            const tiku_trk_sc_ctx_t *c);

#endif /* TIKU_TRK_CWMENU_H_ */
