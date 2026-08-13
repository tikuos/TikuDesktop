/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_menu.c - menu geometry, and the action table's rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_menu.h"
#include "tiku_trk_shortcuts.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_cwmenu.h"

static int fails;

static void
check(int cond, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %s\n", what);
        fails++;
    } else {
        printf("  [PASS] %s\n", what);
    }
}

static tiku_trk_sc_ctx_t
base_ctx(void)
{
    tiku_trk_sc_ctx_t c;

    memset(&c, 0, sizeof c);
    c.view_mode = TIKU_TRK_MODE_LIST;
    return c;
}

int
main(void)
{
    printf("menu geometry\n");
    {
        tiku_trk_menu_t m;
        int h0, h1;

        tiku_trk_menu_clear(&m);
        (void)tiku_trk_menu_add(&m, "Open", TIKU_TRK_CMD_OPEN, 'O',
                                TIKU_TRK_MENU_MOD_CMD, 1);
        (void)tiku_trk_menu_add_separator(&m);
        (void)tiku_trk_menu_add(&m, "Get info", TIKU_TRK_CMD_GET_INFO, 'I',
                                TIKU_TRK_MENU_MOD_CMD, 1);
        tiku_trk_menu_measure(&m);

        h0 = tiku_trk_menu_item_height(&m, 0);
        h1 = tiku_trk_menu_item_height(&m, 1);
        check(h1 == 8, "a separator is 8 px tall, not a full row");
        check(h0 > h1, "an item is taller than a separator");
        /* Items do not tile: there is a one-pixel seam under each. */
        check(tiku_trk_menu_item_top(&m, 1) == h0 + 1,
              "each item leaves a one-pixel seam below it");
        check(tiku_trk_menu_item_top(&m, 2) == h0 + 1 + h1 + 1,
              "the seam accumulates down the menu");
        check(m.width > 0 && m.height > 0, "the menu measures non-empty");
    }

    printf("\nhit-testing\n");
    {
        tiku_trk_menu_t m;
        tiku_desk_rect_t f = { 100, 50, 0, 0 };
        int seam_y;

        tiku_trk_menu_clear(&m);
        (void)tiku_trk_menu_add(&m, "Open", TIKU_TRK_CMD_OPEN, 0, 0u, 1);
        (void)tiku_trk_menu_add_separator(&m);
        (void)tiku_trk_menu_add(&m, "Close", TIKU_TRK_CMD_CLOSE, 0, 0u, 1);
        tiku_trk_menu_measure(&m);
        f.w = m.width;
        f.h = m.height;

        check(tiku_trk_menu_item_at(&m, f, 110, 50) == 0,
              "the first item is hit at the top of the frame");
        check(tiku_trk_menu_item_at(&m, f, 110,
                                    50 + tiku_trk_menu_item_top(&m, 1) + 3)
              == -1, "a separator is never hit");
        seam_y = 50 + tiku_trk_menu_item_height(&m, 0);
        check(tiku_trk_menu_item_at(&m, f, 110, seam_y) == -1,
              "the seam between items belongs to neither");
        check(tiku_trk_menu_item_at(&m, f, 5, 50) == -1,
              "a point outside the frame misses");
    }

    printf("\nlive relabelling: the nine that change with Shift\n");
    {
        static const struct { int cmd; const char *plain, *shift; } nine[] = {
            { TIKU_TRK_CMD_CLEANUP,       "Clean up",      "Clean up all" },
            { TIKU_TRK_CMD_CLOSE,         "Close",         "Close all" },
            { TIKU_TRK_CMD_COPY,          "Copy",          "Copy more" },
            { TIKU_TRK_CMD_CUT,           "Cut",           "Cut more" },
            { TIKU_TRK_CMD_PASTE,         "Paste",         "Paste links" },
            { TIKU_TRK_CMD_CREATE_LINK,   "Create link",
              "Create relative link" },
            { TIKU_TRK_CMD_LINK_HERE,     "Create link here",
              "Create relative link here" },
            { TIKU_TRK_CMD_IDENTIFY,      "Identify",      "Force identify" },
            { TIKU_TRK_CMD_MOVE_TO_TRASH, "Move to Trash", "Delete" },
        };
        size_t i;
        int changed_cmd = 0;

        for (i = 0; i < sizeof nine / sizeof *nine; i++) {
            const char *p = tiku_trk_sc_label(nine[i].cmd, 0u);
            const char *s = tiku_trk_sc_label(nine[i].cmd,
                                              TIKU_TRK_MOD_SHIFT);
            char what[96];

            snprintf(what, sizeof what, "\"%s\" becomes \"%s\" on Shift",
                     nine[i].plain, nine[i].shift);
            check(strcmp(p, nine[i].plain) == 0 &&
                  strcmp(s, nine[i].shift) == 0, what);
            if (tiku_trk_sc_command(nine[i].cmd, TIKU_TRK_MOD_SHIFT) !=
                tiku_trk_sc_command(nine[i].cmd, 0u)) {
                changed_cmd++;
            }
        }
        /* Eight of the nine also change command; Identify keeps its command
         * and carries the shift as a flag instead. */
        check(changed_cmd == 8,
              "eight of the nine change command with their label");
        check(tiku_trk_sc_command(TIKU_TRK_CMD_IDENTIFY, TIKU_TRK_MOD_SHIFT)
              == TIKU_TRK_CMD_IDENTIFY,
              "Force identify keeps the Identify command");
    }

    printf("\nan item showing its shifted face still updates back\n");
    {
        tiku_trk_menu_t m;
        tiku_trk_sc_ctx_t c = base_ctx();
        int i;

        c.mods = TIKU_TRK_MOD_SHIFT;
        c.selection = 1;
        tiku_trk_menu_clear(&m);
        (void)tiku_trk_sc_add(&m, TIKU_TRK_CMD_MOVE_TO_TRASH, &c);
        i = 0;
        check(strcmp(m.item[i].label, "Delete") == 0 &&
              m.item[i].command == TIKU_TRK_CMD_DELETE,
              "built with Shift held, it reads Delete and sends Delete");

        /* Now the key comes up.  The item currently carries the SHIFTED
         * command, so an update keyed only on that command would not find
         * it and it would be stuck reading "Delete" for ever. */
        c.mods = 0u;
        tiku_trk_sc_update(&m, &c);
        check(strcmp(m.item[i].label, "Move to Trash") == 0 &&
              m.item[i].command == TIKU_TRK_CMD_MOVE_TO_TRASH,
              "releasing Shift restores it, from the shifted command");
    }

    printf("\nenablement rules\n");
    {
        tiku_trk_sc_ctx_t c = base_ctx();

        check(!tiku_trk_sc_enabled(TIKU_TRK_CMD_OPEN, &c),
              "Open needs a selection");
        c.selection = 1;
        check(tiku_trk_sc_enabled(TIKU_TRK_CMD_OPEN, &c),
              "Open is available once something is selected");
        check(tiku_trk_sc_enabled(TIKU_TRK_CMD_GET_INFO, &c) &&
              (c.selection = 0, tiku_trk_sc_enabled(TIKU_TRK_CMD_GET_INFO,
                                                    &c)),
              "Get info is available even with nothing selected");

        c = base_ctx();
        c.selection = 1;
        c.is_virtual = 1;
        check(!tiku_trk_sc_enabled(TIKU_TRK_CMD_CUT, &c),
              "Cut is refused in a namespace directory");
        c.text_focus = 1;
        check(tiku_trk_sc_enabled(TIKU_TRK_CMD_CUT, &c),
              "Cut is available anyway while a text field has focus");

        c = base_ctx();
        check(!tiku_trk_sc_enabled(TIKU_TRK_CMD_PASTE, &c),
              "Paste needs something on the clipboard");
        c.clipboard_has_refs = 1;
        check(tiku_trk_sc_enabled(TIKU_TRK_CMD_PASTE, &c),
              "Paste is available with a writable target");
        c.target_ro = 1;
        check(!tiku_trk_sc_enabled(TIKU_TRK_CMD_PASTE, &c),
              "Paste is refused into an unwritable folder");
        c.target_ro = 0;
        c.is_trash = 1;
        check(!tiku_trk_sc_enabled(TIKU_TRK_CMD_PASTE, &c),
              "Paste is refused into the Trash");

        c = base_ctx();
        check(tiku_trk_sc_enabled(TIKU_TRK_CMD_NEW_FOLDER, &c),
              "New folder is available in an ordinary folder");
        c.is_root = 1;
        check(!tiku_trk_sc_enabled(TIKU_TRK_CMD_NEW_FOLDER, &c),
              "New folder is refused at the root");
        c = base_ctx();
        c.target_ro = 1;
        check(!tiku_trk_sc_enabled(TIKU_TRK_CMD_NEW_FOLDER, &c),
              "New folder is refused where nothing can be written");

        c = base_ctx();
        check(!tiku_trk_sc_enabled(TIKU_TRK_CMD_ARRANGE_BY, &c),
              "Arrange by is disabled in list view, where titles sort");
        c.view_mode = TIKU_TRK_MODE_ICON;
        check(tiku_trk_sc_enabled(TIKU_TRK_CMD_ARRANGE_BY, &c),
              "Arrange by is available in icon view");

        c = base_ctx();
        c.selection = 1;
        c.selection_ro = 1;
        check(!tiku_trk_sc_enabled(TIKU_TRK_CMD_MOVE_TO_TRASH, &c),
              "Move to Trash is refused for a read-only selection");
    }

    printf("\nshortcut keys\n");
    {
        check(tiku_trk_sc_key(TIKU_TRK_CMD_MOVE_TO_TRASH) == 0x7F,
              "Move to Trash is bound to Del");
        check((tiku_trk_sc_mods(TIKU_TRK_CMD_MOVE_TO_TRASH, 0u) &
               TIKU_TRK_MENU_MOD_CMD) == 0u,
              "and without the command key, unlike every other action");
        check((tiku_trk_sc_mods(TIKU_TRK_CMD_SELECT_MATCHING, 0u) &
               TIKU_TRK_MENU_MOD_SHIFT) != 0u,
              "Select... carries Shift in its own shortcut");
        check((tiku_trk_sc_mods(TIKU_TRK_CMD_COPY, TIKU_TRK_MOD_SHIFT) &
               TIKU_TRK_MENU_MOD_SHIFT) != 0u,
              "Copy's shown shortcut gains Shift while Shift is held");
    }

    printf("\nmarking and the menu bar\n");
    {
        tiku_trk_menu_t win, attr;
        tiku_trk_menubar_t bar;
        static const int modes[] = { TIKU_TRK_CMD_ICON_VIEW,
                                     TIKU_TRK_CMD_MINI_ICON_VIEW,
                                     TIKU_TRK_CMD_LIST_VIEW };
        tiku_trk_sc_ctx_t c = base_ctx();

        tiku_trk_menu_clear(&win);
        snprintf(win.title, sizeof win.title, "Window");
        (void)tiku_trk_sc_add(&win, TIKU_TRK_CMD_ICON_VIEW, &c);
        (void)tiku_trk_sc_add(&win, TIKU_TRK_CMD_MINI_ICON_VIEW, &c);
        (void)tiku_trk_sc_add(&win, TIKU_TRK_CMD_LIST_VIEW, &c);
        tiku_trk_menu_mark_radio(&win, modes, 3, TIKU_TRK_CMD_LIST_VIEW);
        check(win.item[2].marked && !win.item[0].marked &&
              !win.item[1].marked, "exactly one view mode is marked");
        tiku_trk_menu_mark_radio(&win, modes, 3, TIKU_TRK_CMD_ICON_VIEW);
        check(win.item[0].marked && !win.item[2].marked,
              "marking another unmarks the first");

        tiku_trk_menu_clear(&attr);
        snprintf(attr.title, sizeof attr.title, "Attributes");
        tiku_trk_menubar_init(&bar);
        (void)tiku_trk_menubar_add(&bar, &win);
        check(bar.count == 1, "the bar takes a menu");
        (void)tiku_trk_menubar_insert(&bar, 1, &attr);
        check(bar.count == 2 && bar.menu[1] == &attr,
              "Attributes inserts at its own index");
        tiku_trk_menubar_remove(&bar, &attr);
        check(bar.count == 1 && bar.menu[0] == &win,
              "and leaves again when the view mode changes");
    }

    printf("\nsubmenu placement\n");
    {
        tiku_trk_menu_t m, sub;
        tiku_desk_rect_t screen = { 0, 0, 640, 480 };
        tiku_desk_rect_t frame, r;
        tiku_trk_sc_ctx_t c = base_ctx();

        tiku_trk_menu_clear(&sub);
        (void)tiku_trk_sc_add(&sub, TIKU_TRK_CMD_SORT_BY_NAME, &c);
        tiku_trk_menu_clear(&m);
        (void)tiku_trk_menu_add_submenu(&m, "Arrange by", &sub, 1);
        tiku_trk_menu_measure(&m);

        frame = (tiku_desk_rect_t){ 40, 40, m.width, m.height };
        r = tiku_trk_menu_place_sub(&m, 0, frame, screen);
        check(r.x >= frame.x + frame.w,
              "a submenu opens off its parent's right edge");

        /* Against the right edge it flips to the LEFT rather than being
         * clamped, which would cover the parent it came from. */
        frame.x = screen.w - m.width - 4;
        r = tiku_trk_menu_place_sub(&m, 0, frame, screen);
        check(r.x + r.w <= frame.x + 1,
              "at the screen edge it flips to the left instead");
        check(r.x >= 0, "and stays on the screen");
    }

    printf("\nthe container window's menus\n");
    {
        tiku_trk_cwmenu_t cw;
        tiku_trk_sc_ctx_t c = base_ctx();
        tiku_trk_model_t vol, file;

        tiku_trk_cwmenu_init(&cw);
        tiku_trk_cwmenu_rebuild(&cw, &c);
        check(cw.bar.count == 3 && cw.bar.menu[2] == &cw.attributes,
              "Attributes joins the bar in list view, after Window");
        c.view_mode = TIKU_TRK_MODE_ICON;
        tiku_trk_cwmenu_rebuild(&cw, &c);
        check(cw.bar.count == 2,
              "and leaves it again in icon view");

        c = base_ctx();
        tiku_trk_cwmenu_rebuild(&cw, &c);
        {
            tiku_trk_sc_ctx_t q = base_ctx();

            q.is_query = 1;
            tiku_trk_cwmenu_rebuild(&cw, &q);
            check(tiku_trk_menu_find(&cw.window,
                                     TIKU_TRK_CMD_LIST_VIEW) < 0 &&
                  tiku_trk_menu_find(&cw.window,
                                     TIKU_TRK_CMD_OPEN_PARENT) < 0,
                  "CW-023: a results window has no view modes and nothing "
                  "to open above it -- it is a set of answers, not a "
                  "place");
            check(tiku_trk_menu_find(&cw.window,
                                     TIKU_TRK_CMD_SELECT_ALL) >= 0 &&
                  tiku_trk_menu_find(&cw.window,
                                     TIKU_TRK_CMD_RESIZE_TO_FIT) >= 0,
                  "CW-023: but it can still be sized and its answers "
                  "picked out");
            tiku_trk_cwmenu_rebuild(&cw, &c);
            check(tiku_trk_menu_find(&cw.window,
                                     TIKU_TRK_CMD_LIST_VIEW) >= 0,
                  "CW-023: and an ordinary folder keeps the whole menu");
        }
        check(tiku_trk_menu_find(&cw.attributes, TIKU_TRK_CMD_COL_DATE) >= 0
              && tiku_trk_menu_find(&cw.attributes,
                                    TIKU_TRK_CMD_COL_PATH) >= 0 &&
              tiku_trk_menu_find(&cw.attributes,
                                 TIKU_TRK_CMD_COL_PERM) >= 0,
              "CW-025: the Attributes menu offers Modified, Location and "
              "Permissions as well -- the vocabulary was five columns wide "
              "and could show none of them");
        {
            int before = cw.attributes.count;

            /* The types the folder really holds each grow a submenu of the
             * columns that mean something for them. */
            snprintf(cw.per_type_name[0], sizeof cw.per_type_name[0],
                     "text/plain");
            snprintf(cw.per_type_name[1], sizeof cw.per_type_name[1],
                     "tiku-gpio");
            cw.per_type_count = 2;
            tiku_trk_menu_clear(&cw.per_type[0]);
            (void)tiku_trk_menu_add(&cw.per_type[0], "Size",
                                    TIKU_TRK_CMD_COL_SIZE, 0, 0u, 1);
            tiku_trk_menu_clear(&cw.per_type[1]);
            (void)tiku_trk_menu_add(&cw.per_type[1], "Value",
                                    TIKU_TRK_CMD_COL_VALUE, 0, 0u, 1);
            tiku_trk_cwmenu_rebuild(&cw, &c);
            check(cw.attributes.count == before + 3,
                  "CW-026: a type present in the folder grows a submenu of its own "
                  "columns, with a rule above them");
            check(cw.attributes.item[cw.attributes.count - 2].submenu ==
                  &cw.per_type[0] &&
                  cw.attributes.item[cw.attributes.count - 1].submenu ==
                  &cw.per_type[1],
                  "CW-026: and each carries that type's columns rather than one "
                  "shared list");
            cw.per_type_count = 0;
            tiku_trk_cwmenu_rebuild(&cw, &c);
            check(cw.attributes.count == before,
                  "CW-026: a folder holding none of them is offered none: the menu "
                  "reports the listing rather than the vocabulary");
        }
        check(tiku_trk_menu_find(&cw.file, TIKU_TRK_CMD_PASTE) >= 0,
              "an ordinary folder's File menu has the clipboard block");
        c.is_trash = 1;
        tiku_trk_cwmenu_rebuild(&cw, &c);
        check(tiku_trk_menu_find(&cw.file, TIKU_TRK_CMD_PASTE) < 0,
              "the Trash window has no clipboard block");
        check(tiku_trk_menu_find(&cw.file, TIKU_TRK_CMD_EMPTY_TRASH) == 0,
              "and leads with Empty Trash");
        check(tiku_trk_menu_find(&cw.file, TIKU_TRK_CMD_RESTORE) >= 0 &&
              tiku_trk_menu_find(&cw.file, TIKU_TRK_CMD_DUPLICATE) < 0,
              "Restore stands where Duplicate would be");

        memset(&vol, 0, sizeof vol);
        vol.kind = TIKU_TRK_KIND_VOLUME;
        memset(&file, 0, sizeof file);
        file.kind = TIKU_TRK_KIND_FILE;
        c = base_ctx();
        check(tiku_trk_cwmenu_context(&cw, NULL, &c) == &cw.window_ctx,
              "right-clicking the background gets the window menu");
        check(tiku_trk_cwmenu_context(&cw, &file, &c) == &cw.pose_ctx,
              "right-clicking a file gets the pose menu");
        check(tiku_trk_cwmenu_context(&cw, &vol, &c) == &cw.volume_ctx,
              "right-clicking a volume gets its own menu");
        check(tiku_trk_menu_find(&cw.volume_ctx, TIKU_TRK_CMD_CUT) < 0,
              "the volume menu has no clipboard items");
    }

    printf("\nthe drop menu\n");
    {
        tiku_trk_cwmenu_t cw;
        tiku_trk_menu_t *d;
        int i;

        tiku_trk_cwmenu_init(&cw);
        d = tiku_trk_cwmenu_drop(&cw, 0, 0, 0u);
        check(tiku_trk_menu_find(d, TIKU_TRK_CMD_MOVE_HERE) >= 0 &&
              tiku_trk_menu_find(d, TIKU_TRK_CMD_CANCEL) >= 0,
              "a drop offers Move/Copy/Link and a way out");

        d = tiku_trk_cwmenu_drop(&cw, 1, 0, 0u);
        i = tiku_trk_menu_find(d, TIKU_TRK_CMD_MOVE_HERE);
        check(!d->item[i].enabled,
              "an unwritable destination disables every verb");
        check(d->item[tiku_trk_menu_find(d, TIKU_TRK_CMD_CANCEL)].enabled,
              "but never Cancel");

        d = tiku_trk_cwmenu_drop(&cw, 0, 1, 0u);
        check(!d->item[tiku_trk_menu_find(d, TIKU_TRK_CMD_MOVE_HERE)].enabled,
              "an unwritable source disables Move");
        check(d->item[tiku_trk_menu_find(d, TIKU_TRK_CMD_COPY_HERE)].enabled,
              "but copying out of it is still allowed");

        d = tiku_trk_cwmenu_drop(&cw, 0, 0, TIKU_TRK_MOD_SHIFT);
        check(tiku_trk_menu_find(d, TIKU_TRK_CMD_REL_LINK_HERE) >= 0,
              "Shift turns the link verb relative");
    }

    printf("\n%s\n", fails ? "FAILURES" : "every menu rule holds");
    return fails ? 1 : 0;
}
