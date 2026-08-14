/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_cwmenu.c - building a folder window's menus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_cwmenu.h"
#include "tiku_trk_view_defs.h"

#include <stdio.h>
#include <string.h>

#define ADD(m, cmd)  (void)tiku_trk_sc_add((m), (cmd), c)
#define SEP(m)       (void)tiku_trk_menu_add_separator(m)

void
tiku_trk_cwmenu_init(tiku_trk_cwmenu_t *cw)
{
    if (cw == NULL) {
        return;
    }
    memset(cw, 0, sizeof *cw);
    tiku_trk_menubar_init(&cw->bar);
    snprintf(cw->file.title, sizeof cw->file.title, "File");
    snprintf(cw->window.title, sizeof cw->window.title, "Window");
    snprintf(cw->attributes.title, sizeof cw->attributes.title, "Attributes");
    snprintf(cw->addons.title, sizeof cw->addons.title, "Add-ons");
    (void)tiku_trk_menu_add(&cw->addons, "No add-ons", TIKU_TRK_CMD_NONE,
                            0, 0u, 0);
    (void)tiku_trk_menubar_add(&cw->bar, &cw->file);
    (void)tiku_trk_menubar_add(&cw->bar, &cw->window);
}

/** @brief The icon-size submenu: fixed sizes, radio-marked to the current. */
static void
build_icon_size(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    /* Every rung, so a size the keyboard can reach is a size the menu can
     * reach; offering three of six left a 40 px view showing "48" marked. */
    static const int sizes[] = { TIKU_TRK_CMD_ICON_SIZE_32,
                                 TIKU_TRK_CMD_ICON_SIZE_40,
                                 TIKU_TRK_CMD_ICON_SIZE_48,
                                 TIKU_TRK_CMD_ICON_SIZE_64,
                                 TIKU_TRK_CMD_ICON_SIZE_96,
                                 TIKU_TRK_CMD_ICON_SIZE_128 };
    static const int px[] = { 32, 40, 48, 64, 96, 128 };
    int chosen = TIKU_TRK_CMD_NONE;
    int i, nsz = (int)(sizeof sizes / sizeof sizes[0]);

    tiku_trk_menu_clear(&cw->icon_size);
    for (i = 0; i < nsz; i++) {
        ADD(&cw->icon_size, sizes[i]);
    }
    /* The size is marked only while actually in icon mode; in list mode
     * nothing is marked, because no size is in force. */
    if (c->view_mode == TIKU_TRK_MODE_ICON) {
        for (i = 0; i < nsz; i++) {
            if (c->icon_size == px[i]) {
                chosen = sizes[i];
                break;
            }
        }
    }
    tiku_trk_menu_mark_radio(&cw->icon_size, sizes, nsz, chosen);
}

/** @brief Arrange by: the sort keys, then Reverse order and Clean up. */
static void
build_arrange(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    /* The same attributes the Attributes menu offers as COLUMNS: a
     * capability that can be sorted by a title click but not chosen here
     * would make the two menus disagree about what the listing has. */
    static const int keys[] = { TIKU_TRK_CMD_SORT_BY_NAME,
                                TIKU_TRK_CMD_SORT_BY_KIND,
                                TIKU_TRK_CMD_SORT_BY_SIZE,
                                TIKU_TRK_CMD_SORT_BY_VALUE,
                                TIKU_TRK_CMD_SORT_BY_CAP };
    int chosen = TIKU_TRK_CMD_SORT_BY_NAME;

    tiku_trk_menu_clear(&cw->arrange);
    ADD(&cw->arrange, TIKU_TRK_CMD_SORT_BY_NAME);
    ADD(&cw->arrange, TIKU_TRK_CMD_SORT_BY_KIND);
    ADD(&cw->arrange, TIKU_TRK_CMD_SORT_BY_SIZE);
    ADD(&cw->arrange, TIKU_TRK_CMD_SORT_BY_VALUE);
    ADD(&cw->arrange, TIKU_TRK_CMD_SORT_BY_CAP);
    SEP(&cw->arrange);
    ADD(&cw->arrange, TIKU_TRK_CMD_REVERSE_ORDER);
    SEP(&cw->arrange);
    ADD(&cw->arrange, TIKU_TRK_CMD_CLEANUP);

    switch (c->sort_field) {
    case TIKU_TRK_COL_KIND:  chosen = TIKU_TRK_CMD_SORT_BY_KIND;  break;
    case TIKU_TRK_COL_SIZE:  chosen = TIKU_TRK_CMD_SORT_BY_SIZE;  break;
    case TIKU_TRK_COL_VALUE: chosen = TIKU_TRK_CMD_SORT_BY_VALUE; break;
    case TIKU_TRK_COL_CAP:   chosen = TIKU_TRK_CMD_SORT_BY_CAP;   break;
    default: break;
    }
    /* Every key the menu OFFERS must be markable, or offering one more
     * makes the menu assert the wrong answer when it is chosen. */
    tiku_trk_menu_mark_radio(&cw->arrange, keys,
                             (int)(sizeof keys / sizeof keys[0]), chosen);
    tiku_trk_menu_mark(&cw->arrange, TIKU_TRK_CMD_REVERSE_ORDER,
                       c->reverse_sort);
}

/** @brief The clipboard block, omitted where the idea has no meaning. */
static int
wants_clipboard(const tiku_trk_sc_ctx_t *c)
{
    return !(c->is_root || c->is_trash || c->in_trash);
}

/**
 * @brief Open with: a SUBMENU of applications when there are any (AW-048).
 *
 * The plain item stays when the list is empty -- disabled, because there is
 * nothing behind it -- rather than disappearing: an item that comes and
 * goes is harder to find than one that is greyed.
 */
static void
add_open_with(tiku_trk_cwmenu_t *cw, tiku_trk_menu_t *m,
              const tiku_trk_sc_ctx_t *c)
{
    if (cw->open_with.count > 0) {
        (void)tiku_trk_menu_add_submenu(m, "Open with", &cw->open_with,
                                        c->selection > 0);
    } else {
        ADD(m, TIKU_TRK_CMD_OPEN_WITH);
        tiku_trk_menu_enable(m, TIKU_TRK_CMD_OPEN_WITH, 0);
    }
}

static void
build_file(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    tiku_trk_menu_t *m = &cw->file;

    tiku_trk_menu_clear(m);
    /* The Trash window leads with Empty Trash and its own separator. */
    if (c->is_trash) {
        ADD(m, TIKU_TRK_CMD_EMPTY_TRASH);
        SEP(m);
    }
    ADD(m, TIKU_TRK_CMD_FIND);
    if (!c->is_query) {
        ADD(m, TIKU_TRK_CMD_NEW_FOLDER);
    }
    SEP(m);
    ADD(m, TIKU_TRK_CMD_OPEN);
    /* Between Open and Open with: a query is edited rather than opened,
     * and the item is present only when there is one (CW-033, CW-042). */
    ADD(m, TIKU_TRK_CMD_EDIT_QUERY);
    add_open_with(cw, m, c);
    ADD(m, TIKU_TRK_CMD_GET_INFO);
    ADD(m, TIKU_TRK_CMD_EDIT_NAME);
    if (c->selection_is_volume) {
        ADD(m, TIKU_TRK_CMD_UNMOUNT);
    }
    /* In the Trash, Delete and Restore stand where Duplicate and Move to
     * Trash would be: there is nowhere further to throw something away. */
    if (c->is_trash || c->in_trash) {
        ADD(m, TIKU_TRK_CMD_DELETE);
        ADD(m, TIKU_TRK_CMD_RESTORE);
    } else if (!c->is_root) {
        ADD(m, TIKU_TRK_CMD_DUPLICATE);
        ADD(m, TIKU_TRK_CMD_MOVE_TO_TRASH);
    }
    if (wants_clipboard(c)) {
        SEP(m);
        ADD(m, TIKU_TRK_CMD_MOVE_TO);
        ADD(m, TIKU_TRK_CMD_COPY_TO);
        ADD(m, TIKU_TRK_CMD_CREATE_LINK);
        ADD(m, TIKU_TRK_CMD_CUT);
        ADD(m, TIKU_TRK_CMD_COPY);
        if (!c->is_query) {
            ADD(m, TIKU_TRK_CMD_PASTE);
        }
        SEP(m);
        ADD(m, TIKU_TRK_CMD_IDENTIFY);
    }
    (void)tiku_trk_menu_add_submenu(m, "Add-ons", &cw->addons, 0);
}

static void
build_window(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    tiku_trk_menu_t *m = &cw->window;
    static const int modes[] = { TIKU_TRK_CMD_ICON_VIEW,
                                 TIKU_TRK_CMD_MINI_ICON_VIEW,
                                 TIKU_TRK_CMD_LIST_VIEW };
    int chosen = TIKU_TRK_CMD_LIST_VIEW;

    tiku_trk_menu_clear(m);
    if (c->is_query) {
        /* A results window is a set of answers, not a place: there is no
         * arrangement to keep, nothing above it to open, and a view mode
         * would be a mode for a folder that does not exist (CW-023). */
        ADD(m, TIKU_TRK_CMD_RESIZE_TO_FIT);
        ADD(m, TIKU_TRK_CMD_SELECT_MATCHING);
        ADD(m, TIKU_TRK_CMD_SELECT_ALL);
        ADD(m, TIKU_TRK_CMD_INVERT_SELECTION);
        ADD(m, TIKU_TRK_CMD_CLOSE);
        ADD(m, TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE);
        return;
    }
    (void)tiku_trk_menu_add_submenu(m, "Icon view", &cw->icon_size, 1);
    ADD(m, TIKU_TRK_CMD_MINI_ICON_VIEW);
    ADD(m, TIKU_TRK_CMD_LIST_VIEW);
    SEP(m);
    ADD(m, TIKU_TRK_CMD_RESIZE_TO_FIT);
    (void)tiku_trk_menu_add_submenu(m, "Arrange by", &cw->arrange,
                                    tiku_trk_sc_enabled(
                                        TIKU_TRK_CMD_ARRANGE_BY, c));
    ADD(m, TIKU_TRK_CMD_SELECT_MATCHING);
    ADD(m, TIKU_TRK_CMD_SELECT_ALL);
    ADD(m, TIKU_TRK_CMD_INVERT_SELECTION);
    ADD(m, TIKU_TRK_CMD_OPEN_PARENT);
    ADD(m, TIKU_TRK_CMD_CLOSE);
    ADD(m, TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE);
    SEP(m);
    ADD(m, TIKU_TRK_CMD_PREFERENCES);

    /* The Icon view item carries the icon mode itself, so its mark has to be
     * set on the holder rather than through the action table. */
    switch (c->view_mode) {
    case TIKU_TRK_MODE_ICON: chosen = TIKU_TRK_CMD_ICON_VIEW;      break;
    case TIKU_TRK_MODE_MINI: chosen = TIKU_TRK_CMD_MINI_ICON_VIEW; break;
    default:                 chosen = TIKU_TRK_CMD_LIST_VIEW;      break;
    }
    tiku_trk_menu_mark_radio(m, modes, 3, chosen);
    m->item[0].marked = (c->view_mode == TIKU_TRK_MODE_ICON);
}

static void
build_attributes(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    tiku_trk_menu_t *m = &cw->attributes;

    tiku_trk_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_COL_NAME);
    ADD(m, TIKU_TRK_CMD_COL_KIND);
    ADD(m, TIKU_TRK_CMD_COL_VALUE);
    ADD(m, TIKU_TRK_CMD_COL_SIZE);
    ADD(m, TIKU_TRK_CMD_COL_CAP);
    ADD(m, TIKU_TRK_CMD_COL_DATE);
    ADD(m, TIKU_TRK_CMD_COL_PATH);
    ADD(m, TIKU_TRK_CMD_COL_PERM);
    ADD(m, TIKU_TRK_CMD_COL_ORIGINAL);
    /* Name is always a column and cannot be turned off, so it is marked and
     * disabled rather than absent. */
    tiku_trk_menu_mark(m, TIKU_TRK_CMD_COL_NAME, 1);
    tiku_trk_menu_enable(m, TIKU_TRK_CMD_COL_NAME, 0);
    /* Every other item is checked from the columns the view really has, so
     * the menu reports the view rather than remembering its own idea. */
    tiku_trk_menu_mark(m, TIKU_TRK_CMD_COL_KIND,
                       (c->columns & (1u << TIKU_TRK_COL_KIND)) != 0u);
    tiku_trk_menu_mark(m, TIKU_TRK_CMD_COL_VALUE,
                       (c->columns & (1u << TIKU_TRK_COL_VALUE)) != 0u);
    tiku_trk_menu_mark(m, TIKU_TRK_CMD_COL_SIZE,
                       (c->columns & (1u << TIKU_TRK_COL_SIZE)) != 0u);
    tiku_trk_menu_mark(m, TIKU_TRK_CMD_COL_CAP,
                       (c->columns & (1u << TIKU_TRK_COL_CAP)) != 0u);
    tiku_trk_menu_mark(m, TIKU_TRK_CMD_COL_DATE,
                       (c->columns & (1u << TIKU_TRK_COL_DATE)) != 0u);
    tiku_trk_menu_mark(m, TIKU_TRK_CMD_COL_PATH,
                       (c->columns & (1u << TIKU_TRK_COL_PATH)) != 0u);
    tiku_trk_menu_mark(m, TIKU_TRK_CMD_COL_PERM,
                       (c->columns & (1u << TIKU_TRK_COL_PERM)) != 0u);
    tiku_trk_menu_mark(m, TIKU_TRK_CMD_COL_ORIGINAL,
                       (c->columns & (1u << TIKU_TRK_COL_ORIGINAL)) != 0u);
    if (cw->per_type_count > 0) {
        int k;

        /* The types the folder really holds, each with what it has to
         * show: a folder of text files does not offer an image's columns
         * (CW-026). */
        SEP(m);
        for (k = 0; k < cw->per_type_count; k++) {
            (void)tiku_trk_menu_add_submenu(m, cw->per_type_name[k],
                                            &cw->per_type[k], 1);
        }
    }
}

static void
build_contexts(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    tiku_trk_menu_t *m;

    /* On a pose. */
    m = &cw->pose_ctx;
    tiku_trk_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_OPEN);
    /* Between Open and Open with: a query is edited rather than opened,
     * and the item is present only when there is one (CW-033, CW-042). */
    ADD(m, TIKU_TRK_CMD_EDIT_QUERY);
    add_open_with(cw, m, c);
    ADD(m, TIKU_TRK_CMD_GET_INFO);
    ADD(m, TIKU_TRK_CMD_EDIT_NAME);
    if (c->is_trash || c->in_trash) {
        ADD(m, TIKU_TRK_CMD_DELETE);
        ADD(m, TIKU_TRK_CMD_RESTORE);
    } else {
        ADD(m, TIKU_TRK_CMD_DUPLICATE);
        ADD(m, TIKU_TRK_CMD_MOVE_TO_TRASH);
    }
    if (wants_clipboard(c)) {
        SEP(m);
        ADD(m, TIKU_TRK_CMD_MOVE_TO);
        ADD(m, TIKU_TRK_CMD_COPY_TO);
        ADD(m, TIKU_TRK_CMD_CREATE_LINK);
        ADD(m, TIKU_TRK_CMD_CUT);
        ADD(m, TIKU_TRK_CMD_COPY);
        ADD(m, TIKU_TRK_CMD_PASTE);
        SEP(m);
        ADD(m, TIKU_TRK_CMD_IDENTIFY);
    }

    (void)tiku_trk_menu_add_submenu(m, "Add-ons", &cw->addons, 0);

    /* On empty window background. */
    m = &cw->window_ctx;
    tiku_trk_menu_clear(m);
    if (c->is_query) {
        ADD(m, TIKU_TRK_CMD_RESIZE_TO_FIT);
        ADD(m, TIKU_TRK_CMD_SELECT_MATCHING);
        ADD(m, TIKU_TRK_CMD_SELECT_ALL);
        ADD(m, TIKU_TRK_CMD_CLOSE);
    } else {
        if (c->is_trash) {
            ADD(m, TIKU_TRK_CMD_EMPTY_TRASH);
            SEP(m);
        }
        if (!(c->is_root || c->is_trash || c->in_trash)) {
            ADD(m, TIKU_TRK_CMD_NEW_FOLDER);
        }
        if (c->view_mode != TIKU_TRK_MODE_LIST) {
            (void)tiku_trk_menu_add_submenu(m, "Arrange by", &cw->arrange,
                                            1);
        }
        if (c->is_desktop) {
            (void)tiku_trk_menu_add_submenu(m, "Icon size", &cw->icon_size,
                                            1);
        }
        if (wants_clipboard(c)) {
            ADD(m, TIKU_TRK_CMD_PASTE);
        }
        ADD(m, TIKU_TRK_CMD_SELECT_MATCHING);
        ADD(m, TIKU_TRK_CMD_SELECT_ALL);
        ADD(m, TIKU_TRK_CMD_OPEN_PARENT);
        (void)tiku_trk_menu_add_submenu(m, "Add-ons", &cw->addons, 0);
    }

    /* On a volume or the root: no clipboard, no trashing. */
    m = &cw->volume_ctx;
    tiku_trk_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_OPEN);
    /* Between Open and Open with: a query is edited rather than opened,
     * and the item is present only when there is one (CW-033, CW-042). */
    ADD(m, TIKU_TRK_CMD_EDIT_QUERY);
    ADD(m, TIKU_TRK_CMD_GET_INFO);
    ADD(m, TIKU_TRK_CMD_EDIT_NAME);
    ADD(m, c->selection_is_volume ? TIKU_TRK_CMD_UNMOUNT
                                  : TIKU_TRK_CMD_MOUNT);
    (void)tiku_trk_menu_add_submenu(m, "Add-ons", &cw->addons, 0);

    /* On the Trash itself: three items, and Empty is the first. */
    m = &cw->trash_ctx;
    tiku_trk_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_EMPTY_TRASH);
    ADD(m, TIKU_TRK_CMD_OPEN);
    /* Between Open and Open with: a query is edited rather than opened,
     * and the item is present only when there is one (CW-033, CW-042). */
    ADD(m, TIKU_TRK_CMD_EDIT_QUERY);
    ADD(m, TIKU_TRK_CMD_GET_INFO);
    (void)tiku_trk_menu_add_submenu(m, "Add-ons", &cw->addons, 0);
}

void
tiku_trk_cwmenu_rebuild(tiku_trk_cwmenu_t *cw, const tiku_trk_sc_ctx_t *c)
{
    if (cw == NULL || c == NULL) {
        return;
    }
    build_icon_size(cw, c);
    build_arrange(cw, c);
    build_file(cw, c);
    build_window(cw, c);
    build_attributes(cw, c);
    build_contexts(cw, c);

    /* Attributes belongs to list view only, and sits after Window; it comes
     * and goes as the view mode changes rather than being disabled. */
    if (c->view_mode == TIKU_TRK_MODE_LIST) {
        if (!cw->attributes_shown) {
            (void)tiku_trk_menubar_insert(&cw->bar, 2, &cw->attributes);
            cw->attributes_shown = 1;
        }
    } else if (cw->attributes_shown) {
        tiku_trk_menubar_remove(&cw->bar, &cw->attributes);
        cw->attributes_shown = 0;
    }
}

tiku_trk_menu_t *
tiku_trk_cwmenu_context(tiku_trk_cwmenu_t *cw, const tiku_trk_model_t *m,
                        const tiku_trk_sc_ctx_t *c)
{
    if (cw == NULL) {
        return NULL;
    }
    tiku_trk_cwmenu_rebuild(cw, c);
    if (m == NULL) {
        return &cw->window_ctx;      /* empty background                    */
    }
    if (m->kind == TIKU_TRK_KIND_TRASH) {
        return &cw->trash_ctx;
    }
    if (m->kind == TIKU_TRK_KIND_VOLUME ||
        m->kind == TIKU_TRK_KIND_ROOT) {
        return &cw->volume_ctx;
    }
    return &cw->pose_ctx;
}

tiku_trk_menu_t *
tiku_trk_cwmenu_drop(tiku_trk_cwmenu_t *cw, int dst_read_only,
                     int src_read_only, unsigned mods)
{
    tiku_trk_sc_ctx_t ctx;
    const tiku_trk_sc_ctx_t *c = &ctx;
    tiku_trk_menu_t *m;

    if (cw == NULL) {
        return NULL;
    }
    memset(&ctx, 0, sizeof ctx);
    ctx.mods = mods;
    /* This menu exists only while a drag payload is pending.  The shared
     * enablement table quite correctly requires a selection for Create
     * link; represent the payload as that selection instead of leaving the
     * link item grey in an otherwise actionable drop menu (PVS-039). */
    ctx.selection = 1;
    m = &cw->drop_ctx;
    tiku_trk_menu_clear(m);
    ADD(m, TIKU_TRK_CMD_MOVE_HERE);
    ADD(m, TIKU_TRK_CMD_COPY_HERE);
    ADD(m, TIKU_TRK_CMD_LINK_HERE);
    SEP(m);
    ADD(m, TIKU_TRK_CMD_CANCEL);

    /* An unwritable destination disables every verb; an unwritable SOURCE
     * disables only Move, since copying from it is still possible.  Cancel
     * is never disabled -- there has to be a way out. */
    if (dst_read_only) {
        tiku_trk_menu_enable(m, TIKU_TRK_CMD_MOVE_HERE, 0);
        tiku_trk_menu_enable(m, TIKU_TRK_CMD_COPY_HERE, 0);
        tiku_trk_menu_enable(m, tiku_trk_sc_command(TIKU_TRK_CMD_LINK_HERE,
                                                    mods), 0);
    } else if (src_read_only) {
        tiku_trk_menu_enable(m, TIKU_TRK_CMD_MOVE_HERE, 0);
    }
    return m;
}
