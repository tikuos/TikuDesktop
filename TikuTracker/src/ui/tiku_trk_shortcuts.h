/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_shortcuts.h - the action table.
 *
 * Tracker keeps one class that answers, for every user action: what it is
 * called, what command it sends, what key invokes it, and whether it is
 * enabled right now.  Menus are built and updated THROUGH it, which is why
 * the File menu and the pose context menu can never disagree about whether
 * Duplicate is available.  Rebuilding that here is the point of porting the
 * brain rather than redrawing the body.
 *
 * Nine of these actions rename themselves while Shift is held, and each one
 * changes its command with its label -- an item reading "Delete" that still
 * carries the trash command is the bug this arrangement rules out.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SHORTCUTS_H_
#define TIKU_TRK_SHORTCUTS_H_

#include "tiku_menu.h"

/** @brief Commands.  Values are ours; the actions are Tracker's. */
enum {
    TIKU_TRK_CMD_NONE = 0,
    TIKU_TRK_CMD_FIND,
    TIKU_TRK_CMD_NEW_FOLDER,
    TIKU_TRK_CMD_OPEN,
    /* Between Open and Open with, where the original puts it: it is what
     * you do with a query instead of opening it (CW-033). */
    TIKU_TRK_CMD_EDIT_QUERY,
    TIKU_TRK_CMD_OPEN_WITH,
    TIKU_TRK_CMD_GET_INFO,
    TIKU_TRK_CMD_EDIT_NAME,
    TIKU_TRK_CMD_DUPLICATE,
    TIKU_TRK_CMD_MOVE_TO_TRASH,
    TIKU_TRK_CMD_DELETE,             /* what Move to Trash becomes on Shift */
    TIKU_TRK_CMD_RESTORE,
    TIKU_TRK_CMD_EMPTY_TRASH,
    TIKU_TRK_CMD_MOVE_TO,
    TIKU_TRK_CMD_COPY_TO,
    TIKU_TRK_CMD_CREATE_LINK,
    TIKU_TRK_CMD_CREATE_REL_LINK,    /* Shift                               */
    TIKU_TRK_CMD_CUT,
    TIKU_TRK_CMD_CUT_MORE,           /* Shift                               */
    TIKU_TRK_CMD_COPY,
    TIKU_TRK_CMD_COPY_MORE,          /* Shift                               */
    TIKU_TRK_CMD_PASTE,
    TIKU_TRK_CMD_PASTE_LINKS,        /* Shift                               */
    TIKU_TRK_CMD_COPY_LAYOUT,
    TIKU_TRK_CMD_PASTE_LAYOUT,
    TIKU_TRK_CMD_IDENTIFY,
    TIKU_TRK_CMD_ICON_VIEW,
    TIKU_TRK_CMD_MINI_ICON_VIEW,
    TIKU_TRK_CMD_LIST_VIEW,
    TIKU_TRK_CMD_RESIZE_TO_FIT,
    TIKU_TRK_CMD_ARRANGE_BY,
    TIKU_TRK_CMD_CLEANUP,
    TIKU_TRK_CMD_CLEANUP_ALL,        /* Shift                               */
    TIKU_TRK_CMD_REVERSE_ORDER,
    TIKU_TRK_CMD_SELECT_MATCHING,
    TIKU_TRK_CMD_SELECT_ALL,
    TIKU_TRK_CMD_INVERT_SELECTION,
    TIKU_TRK_CMD_OPEN_PARENT,
    TIKU_TRK_CMD_CLOSE,
    TIKU_TRK_CMD_CLOSE_ALL,          /* Shift                               */
    TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE,
    TIKU_TRK_CMD_PREFERENCES,
    TIKU_TRK_CMD_EDIT_TEMPLATES,
    TIKU_TRK_CMD_SORT_BY_NAME,
    TIKU_TRK_CMD_SORT_BY_KIND,
    TIKU_TRK_CMD_SORT_BY_SIZE,
    TIKU_TRK_CMD_SORT_BY_VALUE,
    /* Every rung of the ladder Cmd-+/- walks, so the menu can reach the
     * same sizes the keyboard can (PVL-002). */
    TIKU_TRK_CMD_ICON_SIZE_32,
    TIKU_TRK_CMD_ICON_SIZE_40,
    TIKU_TRK_CMD_ICON_SIZE_48,
    TIKU_TRK_CMD_ICON_SIZE_64,
    TIKU_TRK_CMD_ICON_SIZE_96,
    TIKU_TRK_CMD_ICON_SIZE_128,
    TIKU_TRK_CMD_SORT_BY_CAP,
    TIKU_TRK_CMD_COL_NAME,
    TIKU_TRK_CMD_COL_KIND,
    TIKU_TRK_CMD_COL_SIZE,
    TIKU_TRK_CMD_COL_VALUE,
    TIKU_TRK_CMD_COL_CAP,
    TIKU_TRK_CMD_COL_DATE,
    TIKU_TRK_CMD_COL_PATH,
    TIKU_TRK_CMD_COL_PERM,
    TIKU_TRK_CMD_COL_ORIGINAL,
    TIKU_TRK_CMD_ADD_ONS,
    TIKU_TRK_CMD_MOUNT,
    TIKU_TRK_CMD_UNMOUNT,
    TIKU_TRK_CMD_MOUNT_ALL,
    TIKU_TRK_CMD_MOUNT_SETTINGS,
    TIKU_TRK_CMD_OPEN_WITH_WINDOW,
    /* Drop menu */
    TIKU_TRK_CMD_MOVE_HERE,
    TIKU_TRK_CMD_COPY_HERE,
    TIKU_TRK_CMD_LINK_HERE,
    TIKU_TRK_CMD_REL_LINK_HERE,      /* Shift                               */
    /* The global bar's system menu */
        TIKU_TRK_CMD_ABOUT,
    TIKU_TRK_CMD_APP_ABOUT,          /* the focused program's, hoisted */
    TIKU_TRK_CMD_SCREENSHOT,         /* save the screen to a PNG */
    TIKU_TRK_CMD_SHUTDOWN,
    /* The Devices menu on the global bar */
    TIKU_TRK_CMD_DEVICE_HOST,        /* be current on this machine */
    TIKU_TRK_CMD_DEVICE_SCAN,        /* re-list the bus */
    TIKU_TRK_CMD_DEVICE_DISCONNECT,  /* let the current device go */
    /* Query-icon drop menu (Q-034) */
    TIKU_TRK_CMD_CREATE_QUERY,
    TIKU_TRK_CMD_CREATE_QUERY_TPL,
    TIKU_TRK_CMD_CANCEL
};

/*
 * The Devices menu's dynamic rows: one command per connected device
 * (PICK), one per available port (CONNECT).  High, fixed bases so the
 * index added to them never collides with the enum above or with a
 * folder command.
 */
#define TIKU_TRK_CMD_DEVICE_PICK_BASE    0x2000
#define TIKU_TRK_CMD_DEVICE_CONNECT_BASE 0x2100

/**
 * @brief What the enablement rules are asked about.
 *
 * These are the questions Tracker's own Update methods ask, one field per
 * question, so a rule here reads like the rule it came from.
 */
/**
 * @brief The up arrow, as a shortcut "key".
 *
 * Outside the printable range on purpose: a menu prints it as an arrow and
 * nothing can confuse it with a letter.
 */
#define TIKU_TRK_KEY_ARROW_UP 0x01

typedef struct {
    int      selection;          /* items selected                          */
    int      selection_ro;       /* selection lives somewhere unwritable    */
    int      target_ro;          /* this folder cannot be written           */
    int      is_root;
    int      is_trash;
    int      in_trash;
    int      is_query;
    int      is_virtual;         /* a namespace directory: fixed shape      */
    int      is_desktop;         /* the desktop background, not a folder    */
    int      selection_is_volume;
    int      selection_is_boot;      /* the boot volume cannot be unmounted */
    int      can_edit_name;
    int      can_open_parent;
    int      can_trash_or_dup;
    int      trash_full;
    int      clipboard_has_refs;
    int      clipboard_has_layout;
    int      text_focus;         /* a text field has focus                  */
    int      view_mode;          /* TIKU_TRK_MODE_*                         */
    int      icon_size;
    int      sort_field;
    int      reverse_sort;
    /* Which columns the view is showing, as a bit per TIKU_TRK_COL_*, so the
     * Attributes menu can check the ones that are really there. */
    unsigned columns;
    unsigned mods;               /* live modifiers, for the nine            */
} tiku_trk_sc_ctx_t;

/** @brief The label an action shows right now.  Never NULL. */
const char *tiku_trk_sc_label(int command, unsigned mods);

/** @brief The command an action sends right now, given the modifiers. */
int tiku_trk_sc_command(int command, unsigned mods);

/** @brief Whether an action is available in @p c. */
int tiku_trk_sc_enabled(int command, const tiku_trk_sc_ctx_t *c);

/** @brief The shortcut key for an action, or 0. */
char tiku_trk_sc_key(int command);

/**
 * @brief The command a bare Cmd+letter chord asks for, or 0 (PVK-042).
 *
 * The reverse of tiku_trk_sc_key, for the shells' key fallback: only the
 * vocabulary the standard shortcut set defines, with Shift and Option
 * picking the variants the menu labels pick.
 */
int tiku_trk_sc_command_for_key(unsigned key, unsigned mods);

/** @brief The modifier mask drawn with the shortcut, given live modifiers. */
unsigned tiku_trk_sc_mods(int command, unsigned mods);

/**
 * @brief Append an action to @p m, resolved against @p c.
 *
 * This is the only way menus should gain items: label, command, shortcut and
 * enabled state all come from one place and cannot drift apart.
 */
int tiku_trk_sc_add(tiku_menu_t *m, int command,
                    const tiku_trk_sc_ctx_t *c);

/**
 * @brief Re-resolve every item of @p m against @p c.
 *
 * Called when a menu opens, and again whenever the modifiers change while it
 * is open, which is what makes the nine rename themselves live.
 */
void tiku_trk_sc_update(tiku_menu_t *m, const tiku_trk_sc_ctx_t *c);

#endif /* TIKU_TRK_SHORTCUTS_H_ */
