/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_shortcuts.c - the action table.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_shortcuts.h"
#include "tiku_trk_input.h"
#include "tiku_trk_view_defs.h"

#include <string.h>

/**
 * @brief The unshifted command an item belongs to.
 *
 * An item currently showing its shifted face still has to be found and
 * updated, or it freezes in that face once the key comes back up.  Every
 * shifted command therefore maps back to the one the table is keyed on.
 */
static int
canonical(int command)
{
    switch (command) {
    case TIKU_TRK_CMD_CLEANUP_ALL:      return TIKU_TRK_CMD_CLEANUP;
    case TIKU_TRK_CMD_CLOSE_ALL:        return TIKU_TRK_CMD_CLOSE;
    case TIKU_TRK_CMD_COPY_MORE:        return TIKU_TRK_CMD_COPY;
    case TIKU_TRK_CMD_CUT_MORE:         return TIKU_TRK_CMD_CUT;
    case TIKU_TRK_CMD_PASTE_LINKS:      return TIKU_TRK_CMD_PASTE;
    case TIKU_TRK_CMD_CREATE_REL_LINK:  return TIKU_TRK_CMD_CREATE_LINK;
    case TIKU_TRK_CMD_REL_LINK_HERE:    return TIKU_TRK_CMD_LINK_HERE;
    case TIKU_TRK_CMD_DELETE:           return TIKU_TRK_CMD_MOVE_TO_TRASH;
    default:                            return command;
    }
}

static int
shifted(unsigned mods)
{
    return (mods & TIKU_TRK_MOD_SHIFT) != 0u;
}

const char *
tiku_trk_sc_label(int command, unsigned mods)
{
    int sh = shifted(mods);

    /* Delete is also the shifted form of Move to Trash, but a Trash menu
     * asks for Delete directly and must not need Shift to say what it does. */
    if (command == TIKU_TRK_CMD_DELETE) {
        return "Delete";
    }
    switch (canonical(command)) {
    /* The nine that rename themselves.  Capitalisation is the original's:
     * "Move to Trash" and "Empty Trash" capitalise Trash, the rest do not
     * capitalise their second word. */
    case TIKU_TRK_CMD_CLEANUP:        return sh ? "Clean up all" : "Clean up";
    case TIKU_TRK_CMD_CLOSE:          return sh ? "Close all" : "Close";
    case TIKU_TRK_CMD_COPY:           return sh ? "Copy more" : "Copy";
    case TIKU_TRK_CMD_CUT:            return sh ? "Cut more" : "Cut";
    case TIKU_TRK_CMD_PASTE:          return sh ? "Paste links" : "Paste";
    case TIKU_TRK_CMD_COPY_LAYOUT:    return "Copy layout";
    case TIKU_TRK_CMD_PASTE_LAYOUT:   return "Paste layout";
    case TIKU_TRK_CMD_CREATE_LINK:
        return sh ? "Create relative link" : "Create link";
    case TIKU_TRK_CMD_LINK_HERE:
        return sh ? "Create relative link here" : "Create link here";
    case TIKU_TRK_CMD_IDENTIFY:       return sh ? "Force identify"
                                                : "Identify";
    case TIKU_TRK_CMD_MOVE_TO_TRASH:  return sh ? "Delete" : "Move to Trash";

    case TIKU_TRK_CMD_FIND:              return "Find" "\xe2\x80\xa6";
    case TIKU_TRK_CMD_NEW_FOLDER:        return "New folder";
    case TIKU_TRK_CMD_OPEN:              return "Open";
    case TIKU_TRK_CMD_EDIT_QUERY:        return "Edit query";
    case TIKU_TRK_CMD_OPEN_WITH:         return "Open with" "\xe2\x80\xa6";
    case TIKU_TRK_CMD_GET_INFO:          return "Get info";
    case TIKU_TRK_CMD_EDIT_NAME:         return "Edit name";
    case TIKU_TRK_CMD_DUPLICATE:         return "Duplicate";
    case TIKU_TRK_CMD_RESTORE:           return "Restore";
    case TIKU_TRK_CMD_EMPTY_TRASH:       return "Empty Trash";
    case TIKU_TRK_CMD_MOVE_TO:           return "Move to";
    case TIKU_TRK_CMD_COPY_TO:           return "Copy to";
    case TIKU_TRK_CMD_ICON_VIEW:         return "Icon view";
    case TIKU_TRK_CMD_MINI_ICON_VIEW:    return "Mini icon view";
    case TIKU_TRK_CMD_LIST_VIEW:         return "List view";
    case TIKU_TRK_CMD_RESIZE_TO_FIT:     return "Resize to fit";
    case TIKU_TRK_CMD_ARRANGE_BY:        return "Arrange by";
    case TIKU_TRK_CMD_REVERSE_ORDER:     return "Reverse order";
    case TIKU_TRK_CMD_SELECT_MATCHING:   return "Select" "\xe2\x80\xa6";
    case TIKU_TRK_CMD_SELECT_ALL:        return "Select all";
    case TIKU_TRK_CMD_INVERT_SELECTION:  return "Invert selection";
    case TIKU_TRK_CMD_OPEN_PARENT:       return "Open parent";
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE: return "Close all in workspace";
    case TIKU_TRK_CMD_PREFERENCES:       return "Preferences" "\xe2\x80\xa6";
    case TIKU_TRK_CMD_EDIT_TEMPLATES:    return "Edit templates" "\xe2\x80\xa6";
    case TIKU_TRK_CMD_SORT_BY_NAME:      return "Name";
    case TIKU_TRK_CMD_SORT_BY_KIND:      return "Kind";
    case TIKU_TRK_CMD_SORT_BY_SIZE:      return "Size";
    case TIKU_TRK_CMD_SORT_BY_VALUE:     return "Value";
    case TIKU_TRK_CMD_ICON_SIZE_32:      return "32 x 32";
    case TIKU_TRK_CMD_ICON_SIZE_40:      return "40 x 40";
    case TIKU_TRK_CMD_ICON_SIZE_48:      return "48 x 48";
    case TIKU_TRK_CMD_ICON_SIZE_64:      return "64 x 64";
    case TIKU_TRK_CMD_ICON_SIZE_96:      return "96 x 96";
    case TIKU_TRK_CMD_ICON_SIZE_128:     return "128 x 128";
    case TIKU_TRK_CMD_SORT_BY_CAP:       return "Capability";
    case TIKU_TRK_CMD_COL_NAME:          return "Name";
    case TIKU_TRK_CMD_COL_KIND:          return "Kind";
    case TIKU_TRK_CMD_COL_SIZE:          return "Size";
    case TIKU_TRK_CMD_COL_VALUE:         return "Value";
    case TIKU_TRK_CMD_COL_DATE:          return "Modified";
    case TIKU_TRK_CMD_COL_PATH:          return "Location";
    case TIKU_TRK_CMD_COL_PERM:          return "Permissions";
    case TIKU_TRK_CMD_COL_ORIGINAL:      return "Original path";
    case TIKU_TRK_CMD_COL_CAP:           return "Capability";
    case TIKU_TRK_CMD_ADD_ONS:           return "Add-ons" "\xe2\x80\xa6";
    case TIKU_TRK_CMD_MOUNT:             return "Mount";
    case TIKU_TRK_CMD_UNMOUNT:           return "Unmount";
    case TIKU_TRK_CMD_MOUNT_ALL:         return "Mount all";
    case TIKU_TRK_CMD_MOUNT_SETTINGS:    return "Settings…";
    case TIKU_TRK_CMD_OPEN_WITH_WINDOW:  return "Open with…";
    case TIKU_TRK_CMD_MOVE_HERE:         return "Move here";
    case TIKU_TRK_CMD_COPY_HERE:         return "Copy here";
    case TIKU_TRK_CMD_CANCEL:            return "Cancel";
    default:                             return "";
    }
}

int
tiku_trk_sc_command(int command, unsigned mods)
{
    int sh = shifted(mods);

    if (command == TIKU_TRK_CMD_DELETE) {
        return TIKU_TRK_CMD_DELETE;
    }
    switch (canonical(command)) {
    case TIKU_TRK_CMD_CLEANUP:
        return sh ? TIKU_TRK_CMD_CLEANUP_ALL : TIKU_TRK_CMD_CLEANUP;
    case TIKU_TRK_CMD_CLOSE:
        return sh ? TIKU_TRK_CMD_CLOSE_ALL : TIKU_TRK_CMD_CLOSE;
    case TIKU_TRK_CMD_COPY:
        return sh ? TIKU_TRK_CMD_COPY_MORE : TIKU_TRK_CMD_COPY;
    case TIKU_TRK_CMD_CUT:
        return sh ? TIKU_TRK_CMD_CUT_MORE : TIKU_TRK_CMD_CUT;
    case TIKU_TRK_CMD_PASTE:
        return sh ? TIKU_TRK_CMD_PASTE_LINKS : TIKU_TRK_CMD_PASTE;
    case TIKU_TRK_CMD_CREATE_LINK:
        return sh ? TIKU_TRK_CMD_CREATE_REL_LINK : TIKU_TRK_CMD_CREATE_LINK;
    case TIKU_TRK_CMD_LINK_HERE:
        return sh ? TIKU_TRK_CMD_REL_LINK_HERE : TIKU_TRK_CMD_LINK_HERE;
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
        return sh ? TIKU_TRK_CMD_DELETE : TIKU_TRK_CMD_MOVE_TO_TRASH;
    /* Identify is the exception: the label changes but the command does NOT.
     * The shift travels with the message as a "force" flag instead. */
    case TIKU_TRK_CMD_IDENTIFY:
        return TIKU_TRK_CMD_IDENTIFY;
    default:
        return canonical(command);
    }
}

char
tiku_trk_sc_key(int command)
{
    switch (canonical(command)) {
    case TIKU_TRK_CMD_FIND:              return 'F';
    case TIKU_TRK_CMD_NEW_FOLDER:        return 'N';
    case TIKU_TRK_CMD_OPEN:              return 'O';
    case TIKU_TRK_CMD_EDIT_QUERY:        return 'G';
    case TIKU_TRK_CMD_OPEN_WITH:         return 'O';   /* with Ctrl added   */
    case TIKU_TRK_CMD_GET_INFO:          return 'I';
    case TIKU_TRK_CMD_EDIT_NAME:         return 'E';
    case TIKU_TRK_CMD_DUPLICATE:         return 'D';
    case TIKU_TRK_CMD_CUT:               return 'X';
    case TIKU_TRK_CMD_COPY:              return 'C';
    case TIKU_TRK_CMD_PASTE:             return 'V';
    case TIKU_TRK_CMD_CLEANUP:           return 'K';
    case TIKU_TRK_CMD_RESIZE_TO_FIT:     return 'Y';
    case TIKU_TRK_CMD_SELECT_MATCHING:   return 'A';   /* with Shift        */
    case TIKU_TRK_CMD_SELECT_ALL:        return 'A';
    case TIKU_TRK_CMD_INVERT_SELECTION:  return 'S';
    case TIKU_TRK_CMD_CLOSE:             return 'W';
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE: return 'Q';
    case TIKU_TRK_CMD_PREFERENCES:       return ',';
    case TIKU_TRK_CMD_ICON_VIEW:         return '1';
    /* Open parent is Cmd+Up.  The menu shows no accelerator for it: this
     * drawing floor's font has one glyph per byte and no arrow among them,
     * and a blank column advertises a chord the user cannot read. */
    case TIKU_TRK_CMD_MINI_ICON_VIEW:    return '2';
    case TIKU_TRK_CMD_LIST_VIEW:         return '3';
    /* Move to Trash is the one action bound WITHOUT the command key: it is
     * plain Del, so that pressing Del in a list does the obvious thing. */
    case TIKU_TRK_CMD_MOVE_TO_TRASH:     return 0x7F;
    default:                             return 0;
    }
}

unsigned
tiku_trk_sc_mods(int command, unsigned mods)
{
    unsigned live = mods & TIKU_TRK_MOD_SHIFT;

    switch (canonical(command)) {
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
        /* No command key; Shift joins it to mean Delete. */
        return live;
    case TIKU_TRK_CMD_SELECT_MATCHING:
        return TIKU_TRK_MENU_MOD_CMD | TIKU_TRK_MENU_MOD_SHIFT;
    case TIKU_TRK_CMD_OPEN_WITH:
        return TIKU_TRK_MENU_MOD_CMD | TIKU_TRK_MENU_MOD_CTRL;
    /* The five that carry the live shift into their own shortcut, so the
     * key shown is the key that will actually fire the shifted command. */
    case TIKU_TRK_CMD_CLEANUP:
    case TIKU_TRK_CMD_CLOSE:
    case TIKU_TRK_CMD_COPY:
    case TIKU_TRK_CMD_CUT:
    case TIKU_TRK_CMD_PASTE:
        return TIKU_TRK_MENU_MOD_CMD | live;
    default:
        return (tiku_trk_sc_key(command) != 0) ? TIKU_TRK_MENU_MOD_CMD : 0u;
    }
}

int
tiku_trk_sc_enabled(int command, const tiku_trk_sc_ctx_t *c)
{
    if (c == NULL) {
        return 0;
    }
    switch (canonical(command)) {
    /* Always available. */
    case TIKU_TRK_CMD_FIND:
    case TIKU_TRK_CMD_GET_INFO:            /* enabled even with no selection */
    case TIKU_TRK_CMD_RESIZE_TO_FIT:
    case TIKU_TRK_CMD_SELECT_MATCHING:
    case TIKU_TRK_CMD_SELECT_ALL:
    case TIKU_TRK_CMD_INVERT_SELECTION:
    case TIKU_TRK_CMD_CLOSE:
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE:
    case TIKU_TRK_CMD_PREFERENCES:
    case TIKU_TRK_CMD_REVERSE_ORDER:
    case TIKU_TRK_CMD_ICON_VIEW:
    case TIKU_TRK_CMD_MINI_ICON_VIEW:
    case TIKU_TRK_CMD_LIST_VIEW:
    case TIKU_TRK_CMD_CANCEL:
        return 1;

    /* Something has to be selected. */
    case TIKU_TRK_CMD_OPEN:
    case TIKU_TRK_CMD_OPEN_WITH:
    case TIKU_TRK_CMD_IDENTIFY:
    case TIKU_TRK_CMD_RESTORE:
    case TIKU_TRK_CMD_COPY_TO:
        return c->selection > 0;

    /* Selected, and where it lives can be written. */
    case TIKU_TRK_CMD_MOVE_TO:
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
        return c->selection > 0 && !c->selection_ro;

    /* Only where there is a query to edit: the item clicked is one, or
     * the window itself is a results window (CW-033). */
    case TIKU_TRK_CMD_EDIT_QUERY:
        return c->is_query;

    case TIKU_TRK_CMD_DUPLICATE:
        return c->can_trash_or_dup;
    case TIKU_TRK_CMD_EDIT_NAME:
        return c->can_edit_name;
    case TIKU_TRK_CMD_OPEN_PARENT:
        return c->can_open_parent;
    case TIKU_TRK_CMD_EMPTY_TRASH:
        return c->trash_full;

    /* Cut is refused outright in the places where the idea makes no sense,
     * and Cut/Copy/Paste are all available whenever a text field has focus,
     * because then they mean the text-editing verbs instead. */
    case TIKU_TRK_CMD_CUT:
        if (c->text_focus) {
            return 1;
        }
        if (c->is_root || c->is_trash || c->is_virtual) {
            return 0;
        }
        return c->selection > 0 && !c->target_ro;
    case TIKU_TRK_CMD_COPY:
        return c->text_focus || c->selection > 0;
    case TIKU_TRK_CMD_PASTE:
        if (c->text_focus) {
            return 1;
        }
        return c->clipboard_has_refs && !c->target_ro && !c->is_trash &&
               !c->is_query && !c->is_virtual;
    case TIKU_TRK_CMD_COPY_LAYOUT:
        return c->view_mode == TIKU_TRK_MODE_LIST;
    case TIKU_TRK_CMD_PASTE_LAYOUT:
        return c->view_mode == TIKU_TRK_MODE_LIST &&
               c->clipboard_has_layout;

    case TIKU_TRK_CMD_NEW_FOLDER:
        return !(c->is_root || c->is_trash || c->in_trash || c->is_query ||
                 c->is_virtual) && !c->target_ro;

    case TIKU_TRK_CMD_CREATE_LINK:
    case TIKU_TRK_CMD_LINK_HERE:
        return c->selection > 0;

    case TIKU_TRK_CMD_ADD_ONS:
    case TIKU_TRK_CMD_MOUNT:
        return 0;

    /* Live now, through the volume model; the boot volume's entry is
     * disabled so it cannot be unmounted (CW-034). */
    case TIKU_TRK_CMD_UNMOUNT:
        return c->selection_is_volume && !c->selection_is_boot;
    case TIKU_TRK_CMD_MOUNT_ALL:
    case TIKU_TRK_CMD_MOUNT_SETTINGS:
        return 1;
    case TIKU_TRK_CMD_OPEN_WITH_WINDOW:
        return c->selection > 0;

    /* Arrange by is meaningless in list view, where the column titles sort. */
    case TIKU_TRK_CMD_ARRANGE_BY:
        return c->view_mode != TIKU_TRK_MODE_LIST;
    case TIKU_TRK_CMD_CLEANUP:
        return c->view_mode != TIKU_TRK_MODE_LIST;

    default:
        return 1;
    }
}

int
tiku_trk_sc_add(tiku_trk_menu_t *m, int command, const tiku_trk_sc_ctx_t *c)
{
    unsigned mods = (c != NULL) ? c->mods : 0u;

    return tiku_trk_menu_add(m, tiku_trk_sc_label(command, mods),
                             tiku_trk_sc_command(command, mods),
                             tiku_trk_sc_key(command),
                             tiku_trk_sc_mods(command, mods),
                             tiku_trk_sc_enabled(command, c));
}

void
tiku_trk_sc_update(tiku_trk_menu_t *m, const tiku_trk_sc_ctx_t *c)
{
    unsigned mods = (c != NULL) ? c->mods : 0u;
    int i;

    if (m == NULL) {
        return;
    }
    for (i = 0; i < m->count; i++) {
        tiku_trk_mitem_t *it = &m->item[i];
        int base;

        if (it->separator) {
            continue;
        }
        if (it->submenu != NULL) {
            /* A submenu holder carries no command of its own, so its enabled
             * state is asked for by the label it was built with; its contents
             * are updated in their own right. */
            tiku_trk_sc_update(it->submenu, c);
            continue;
        }
        /* In a Trash inventory Delete is the base action.  Elsewhere the
         * same command value is the shifted face of Move to Trash and must
         * turn back when Shift is released. */
        base = (it->command == TIKU_TRK_CMD_DELETE && c != NULL &&
                (c->is_trash || c->in_trash))
                   ? TIKU_TRK_CMD_DELETE : canonical(it->command);
        tiku_trk_menu_relabel(m, i, tiku_trk_sc_label(base, mods),
                              tiku_trk_sc_command(base, mods),
                              tiku_trk_sc_key(base),
                              tiku_trk_sc_mods(base, mods));
        it->enabled = tiku_trk_sc_enabled(base, c);
    }
    /* Marks are the caller's business: they come from view state, not from
     * the action table. */
}
