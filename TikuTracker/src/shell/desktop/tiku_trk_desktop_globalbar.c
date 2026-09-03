/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_globalbar.c - the global menu bar: the strip along the
 * top of the screen, the menus the focused window publishes into it, and the
 * system menu's own commands
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/** @brief The strip's rectangle, empty when the bar is off. */
tiku_rect_t
global_bar_rect(desktop_t *app)
{
    if (app->gbar_on != 1) {
        return (tiku_rect_t){ 0, 0, 0, 0 };
    }
    return (tiku_rect_t){ 0, 0, app->surf->w,
                               app->menus.bar.height };
}

/** @brief A published pick lands back in the shell's one command path. */
static void
folder_menu_pick(tiku_window_t *window, int command, void *context)
{
    desktop_t *app = context;
    folder_t *fl = folder_of(app, window);

    if (fl != NULL) {
        menu_invoke(app, fl, command);
    }
}

/** @brief Describe the focused folder's menus and publish them (phase 2). */
void
publish_folder_menus(desktop_t *app, folder_t *fl)
{
    tiku_menuset_t set;
    int m;

    if (fl == NULL || fl->win == NULL) {
        return;
    }
    memset(&set, 0, sizeof set);
    for (m = 0; m < app->menus.bar.count &&
                set.nmenu < TIKU_MENUSET_MENUS; m++) {
        const tiku_menu_t *from = app->menus.bar.menu[m];
        tiku_menu_list_t *to = &set.menu[set.nmenu];
        int i;

        snprintf(to->title, sizeof to->title, "%s", from->title);
        for (i = 0; i < from->count; i++) {
            const tiku_menu_item_t *fi = &from->item[i];
            tiku_menu_entry_t *ti;

            if (to->nitem >= TIKU_MENUSET_ITEMS) {
                break;
            }
            ti = &to->item[to->nitem++];
            snprintf(ti->label, sizeof ti->label, "%s", fi->label);
            ti->command = fi->command;
            ti->sc = fi->shortcut;
            ti->mods = fi->mods;
            ti->enabled = (unsigned char)!!fi->enabled;
            ti->marked = (unsigned char)!!fi->marked;
            ti->separator = (unsigned char)!!fi->separator;
            ti->level = 0;
            if (fi->submenu != NULL) {
                /* The submenu rides along as level-1 entries under its
                 * holder, which is all the flat description needs. */
                const tiku_menu_t *sub = fi->submenu;
                int k;

                for (k = 0; k < sub->count &&
                            to->nitem < TIKU_MENUSET_ITEMS; k++) {
                    const tiku_menu_item_t *si = &sub->item[k];
                    tiku_menu_entry_t *st = &to->item[to->nitem++];

                    snprintf(st->label, sizeof st->label, "%s", si->label);
                    st->command = si->command;
                    st->sc = si->shortcut;
                    st->mods = si->mods;
                    st->enabled = (unsigned char)!!si->enabled;
                    st->marked = (unsigned char)!!si->marked;
                    st->separator = (unsigned char)!!si->separator;
                    st->level = 1;
                }
            }
        }
        set.nmenu++;
    }
    tiku_window_publish_menus(fl->win, &set, folder_menu_pick, app);
}

/** @brief Rebuild the strip's scratch menus from the PUBLISHED data. */
void
rebuild_global_bar(desktop_t *app)
{
    tiku_window_t *fw = tiku_workspace_focused(app->desk);
    const tiku_menuset_t *set =
        (fw != NULL) ? tiku_window_menus(fw) : NULL;
    int was_open = app->gbar_open;
    int m;

    tiku_menubar_init(&app->gbar);
    /*
     * The focused program's About, HOISTED: a program that published a
     * row called "About <itself>" meant the one thing every program
     * means by it, and that row belongs in the system menu beside the
     * system's own -- not wherever the program happened to file it.
     * The label is the protocol, which is what lets a program built
     * yesterday take part without a word of new wire.
     */
    app->about_hoist = 0;
    if (set != NULL) {
        for (m = 0; m < set->nmenu && app->about_hoist == 0; m++) {
            int i;

            for (i = 0; i < set->menu[m].nitem; i++) {
                if (strncmp(set->menu[m].item[i].label, "About ", 6)
                        == 0 &&
                    set->menu[m].item[i].level == 0) {
                    app->about_hoist = set->menu[m].item[i].command;
                    snprintf(app->about_label, sizeof app->about_label,
                             "%s", set->menu[m].item[i].label);
                    break;
                }
            }
        }
    }
    /*
     * The system menu first, where the corner ritual lives -- but WHOSE
     * corner depends on who is in front.  With a hosted program's
     * window focused, the desktop's own gestures -- Find, Preferences,
     * Shut down -- are not the program's to wear: the menu is the
     * PROGRAM's then, and says About it and no more.  The desktop's
     * whole menu is one click on its own ground away, and the Deskbar's
     * leaf carries Shut down whoever is in front.
     */
    tiku_menu_clear(&app->sysmenu);
    snprintf(app->sysmenu.title, sizeof app->sysmenu.title, "Tiku");
    app->sysmenu.mark = 1;      /* the system's menu wears its mark */
    {
        int program = (fw != NULL &&
                       (embed_win_of_window(app, fw) != NULL ||
                        tiku_remote_owner(&app->remote, fw) != NULL));

        if (app->about_hoist != 0) {
            (void)tiku_menu_add(&app->sysmenu, app->about_label,
                                    TIKU_TRK_CMD_APP_ABOUT, 0, 0u, 1);
            tiku_menu_set_icon(&app->sysmenu, TIKU_TRK_CMD_APP_ABOUT,
                                    MARK_ICON);
        } else {
            (void)tiku_menu_add(&app->sysmenu, tiku_str("cmd.about"),
                                    TIKU_TRK_CMD_ABOUT, 0, 0u, 1);
            tiku_menu_set_icon(&app->sysmenu, TIKU_TRK_CMD_ABOUT,
                                    MARK_ICON);
        }
        if (!program) {
            (void)tiku_menu_add_separator(&app->sysmenu);
            (void)tiku_menu_add(&app->sysmenu, tiku_str("cmd.find"),
                                    TIKU_TRK_CMD_FIND,
                                    'f', TIKU_TRK_MOD_CMD, 1);
            (void)tiku_menu_add(&app->sysmenu,
                                    tiku_str("cmd.preferences"),
                                    TIKU_TRK_CMD_PREFERENCES, 0, 0u, 1);
            (void)tiku_menu_add_separator(&app->sysmenu);
            (void)tiku_menu_add(&app->sysmenu, tiku_str("cmd.shutdown"),
                                    TIKU_TRK_CMD_SHUTDOWN, 0, 0u, 1);
        }
    }
    (void)tiku_menubar_add(&app->gbar, &app->sysmenu);
    app->gbar_from = (set != NULL) ? fw : NULL;
    for (m = 0; set != NULL && m < set->nmenu; m++) {
        const tiku_menu_list_t *from = &set->menu[m];
        tiku_menu_t *to = &app->gmenu[m];
        int i, nsub = 0;

        tiku_menu_clear(to);
        snprintf(to->title, sizeof to->title, "%s", from->title);
        for (i = 0; i < from->nitem; i++) {
            const tiku_menu_entry_t *fi = &from->item[i];

            if (fi->level == 1) {
                continue;           /* consumed with its holder below */
            }
            if (app->about_hoist != 0 &&
                fi->command == app->about_hoist &&
                strncmp(fi->label, "About ", 6) == 0) {
                continue;           /* hoisted into the system menu */
            }
            if (fi->separator) {
                /* Never as the FIRST row: the About above it may have
                 * been hoisted away, and a menu that opens with a rule
                 * is ruling off nothing. */
                if (to->count > 0) {
                    (void)tiku_menu_add_separator(to);
                }
            } else if (i + 1 < from->nitem &&
                       from->item[i + 1].level == 1 && nsub < 4) {
                /*
                 * Four submenus per published menu, which is what gsub
                 * holds.  A fifth would fall to the plain-item road
                 * below and its children would be skipped by the level
                 * test at the top of this loop -- a row that opens
                 * nothing.  No application publishes that many today;
                 * the day one does, the answer is to FLATTEN the extra
                 * submenu's items into their holder rather than to grow
                 * the array, because a menu five submenus deep in one
                 * strip is a menu that wants rethinking.
                 */
                tiku_menu_t *sub = &app->gsub[m][nsub++];
                int k = i + 1;

                tiku_menu_clear(sub);
                snprintf(sub->title, sizeof sub->title, "%s", fi->label);
                for (; k < from->nitem && from->item[k].level == 1; k++) {
                    const tiku_menu_entry_t *si = &from->item[k];

                    if (si->separator) {
                        (void)tiku_menu_add_separator(sub);
                    } else {
                        (void)tiku_menu_add(sub, si->label,
                            si->command, si->sc, si->mods, si->enabled);
                        if (si->marked) {
                            tiku_menu_mark(sub, si->command, 1);
                        }
                    }
                }
                (void)tiku_menu_add_submenu(to, fi->label, sub,
                                                fi->enabled);
                if (fi->marked && to->count > 0) {
                    to->item[to->count - 1].marked = 1;
                }
            } else {
                (void)tiku_menu_add(to, fi->label, fi->command,
                                        fi->sc, fi->mods, fi->enabled);
                if (fi->marked) {
                    tiku_menu_mark(to, fi->command, 1);
                }
            }
        }
        (void)tiku_menubar_add(&app->gbar, to);
    }
    app->gbar.open = was_open;
}

/**
 * @brief The system menu's own commands, which belong to no window.
 *
 * They are answered here rather than on the folder path, which returns
 * at once when no folder window is focused -- and the system menu is
 * reachable when none is.
 *
 * @return nonzero when the command was one of these.
 */
static int
system_invoke(desktop_t *app, int command)
{
    switch (command) {
    case TIKU_TRK_CMD_ABOUT:
        (void)embed_open_or_raise(app, &tiku_trk_about_app);
        return 1;

    case TIKU_TRK_CMD_FIND:
        open_panel(app);
        return 1;
    case TIKU_TRK_CMD_PREFERENCES:
        tiku_trk_prefs_window_open(&app->prefs_window);
        return 1;
    case TIKU_TRK_CMD_APP_ABOUT:
        /* The hoisted row: it looks like the system's but it belongs to
         * the program that published it, and goes back there. */
        if (app->about_hoist != 0 && app->gbar_from != NULL &&
            app->gbar_from->menu_pick != NULL) {
            app->gbar_from->menu_pick(app->gbar_from, app->about_hoist,
                                      app->gbar_from->menu_pick_context);
        }
        return 1;
    case TIKU_TRK_CMD_SHUTDOWN:
        /* The ellipsis is a promise: Shut down… asks before anything is
         * taken down (DB-006), the same question the leaf asks. */
        tiku_alert_open(alert_model(app), TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_SHUTDOWN,
                            tiku_str("alert.msg.shut_down"), tiku_str("alert.btn.shut_down"));
        return 1;
    default:
        break;
    }
    return 0;
}

/**
 * @brief Route a pick: a strip pick goes back to whoever PUBLISHED the
 *        menu, which is the other half of the protocol; everything else
 *        (and the system menu) takes the shell's own command path.
 */
void
global_or_invoke(desktop_t *app, folder_t *ffl, int command)
{
    if (app->gbar_open >= 0 && app->gbar_from != NULL &&
        app->popup != &app->sysmenu &&
        app->gbar_from->menu_pick != NULL) {
        app->gbar_from->menu_pick(app->gbar_from, command,
                                  app->gbar_from->menu_pick_context);
        return;
    }
    if (system_invoke(app, command)) {
        return;
    }
    menu_invoke(app, ffl, command);
}

/**
 * @brief The character a key event types, or 0.
 *
 * With no display there is no keyboard map to consult, so a Latin-1 keysym
 * stands for itself -- which is all a script needs in order to type.
 */
char
key_char(const tiku_event_t *event)
{
    return (event != NULL) ? event->text[0] : (char)0;
}

/** @brief Name a portable key for controllers whose API uses key labels. */
const char *
key_name(const tiku_event_t *event, char printable[2])
{
    if (event == NULL) { return ""; }
    switch (event->key) {
    case TIKU_KEY_UP:     return "Up";
    case TIKU_KEY_DOWN:   return "Down";
    case TIKU_KEY_LEFT:   return "Left";
    case TIKU_KEY_RIGHT:  return "Right";
    case TIKU_KEY_ESCAPE: return "Escape";
    case TIKU_KEY_RETURN: return "Return";
    case TIKU_KEY_TAB:    return "Tab";
    default:
        if (event->key == ' ') { return "space"; }
        if (event->key >= 0x20u && event->key < 0x7fu) {
            printable[0] = (char)event->key;
            printable[1] = '\0';
            return printable;
        }
        return "";
    }
}
