/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_commands.c - what a folder window's commands come to:
 * opening, renaming, editing, drops, the spring-loaded folders, the Trash
 * union, add-ons, and the script's command file
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/* Used before they are defined. */
int desktop_cmd_new_folder(desktop_t *app, folder_t *fl, int command,
                           int seeded, int seed_x, int seed_y);
int desktop_cmd_unmount(desktop_t *app, folder_t *fl, int command);
int desktop_cmd_open_with(desktop_t *app, folder_t *fl, int command);
int desktop_cmd_mount(desktop_t *app, folder_t *fl, int command);
int desktop_cmd_layout(desktop_t *app, folder_t *fl, int command);
int desktop_cmd_template(desktop_t *app, folder_t *fl, int command,
                         const char *template_path,
                         const char **template_parent, int *seeded, int *seed_x,
                         int *seed_y);
int desktop_cmd_destination(desktop_t *app, folder_t *fl, int command,
                            const char **template_path);
int desktop_cmd_query(desktop_t *app, folder_t *fl, int command);
int desktop_cmd_system(desktop_t *app, folder_t *fl, int command);
/* End of the forward declarations. */

/** @brief The directory above @p path into @p out; "/" for the root. */
void
parent_path(const char *path, char *out, size_t size)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL || slash == path) {
        snprintf(out, size, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n >= size) {
        n = size - 1u;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

/** @brief Let the launch controller hand Tracker-owned paths to the shell. */
void
launch_open_path(void *context, const char *path)
{
    (void)open_folder(context, path);
}

/** @brief Ensure the editable local menu exists before showing it. */
void
deskbar_edit_path(void *context, const char *path)
{
    desktop_t *app = context;

    {
        /*
         * Making the folder is reaching PAST the backend to the local
         * filesystem, so the question is whether local files are what
         * answers for this path -- not what the shell's backend happens
         * to be called.  With a namespace in the way that outer name is
         * "mount" for everything, and asking it here meant the folder
         * was never made and the window opened on nothing.
         */
        char local[TIKU_PATH_MAX];
        tiku_backend_t *serving =
            tiku_backend_serving(app->backend, path, local, sizeof local);

        if (serving != NULL && serving->ops != NULL &&
            strcmp(serving->ops->name, "posix") == 0) {
            char parent[TIKU_PATH_MAX];
            char *slash;

            snprintf(parent, sizeof parent, "%s", local);
            slash = strrchr(parent, '/');
            if (slash != NULL && slash != parent) {
                *slash = '\0';
                (void)mkdir(parent, 0700);
            }
            (void)mkdir(local, 0700);
        }
    }
    (void)open_folder(app, path);
}

/** @brief Close invalidated spatial windows on behalf of file actions. */
void
file_actions_close_under(void *context, const char *path)
{
    close_windows_under(context, path);
}

/**
 * @brief Offer Get Info for a link that leads nowhere (AW-031).
 *
 * Instead of a bare error: the window that can SAY what is wrong -- and
 * re-point it -- is one button away.
 */
void
offer_broken_link(desktop_t *app, const char *path)
{
    snprintf(app->broken_open_path, sizeof app->broken_open_path, "%s",
             path);
    tiku_sound_play(TIKU_SOUND_BEEP);
    tiku_alert_open(alert_model(app), TIKU_ALERT_WARN,
                        TIKU_TRK_ALERT_TAG_BROKEN_OPEN,
                        tiku_str("alert.msg.broken_open"),
                        tiku_str("alert.btn.get_info"));
}

/**
 * @brief Select what an operation made, once it shows up (AW-033).
 *
 * Retried on its period until the pose exists or the loop's timeout gives
 * up: the entry may still be arriving through the listing.
 */
int
select_created(void *ctx)
{
    select_created_t *sc = ctx;
    char parent[TIKU_PATH_MAX];
    char *slash;
    int i;

    snprintf(parent, sizeof parent, "%s", sc->path);
    slash = strrchr(parent, '/');
    if (slash == NULL) {
        return 1;
    }
    if (slash == parent) { parent[1] = '\0'; } else { *slash = '\0'; }
    for (i = 0; i < sc->app->folder_count; i++) {
        folder_t *fl = &sc->app->folders[i];

        if (fl->win == NULL || !fl->win->open || fl->view == NULL ||
            strcmp(fl->path, parent) != 0) {
            continue;
        }
        slash = strrchr(sc->path, '/');
        if (select_named(sc->app, fl,
                         slash != NULL ? slash + 1 : sc->path) >= 0) {
            sc->app->dirty = 1;
            return 1;           /* found and selected: done */
        }
        return 0;               /* the window is there; the entry not yet */
    }
    return 1;                   /* no window shows the parent: nothing owed */
}

/**
 * @brief Keep the add-on inventory true to the directory and the user's
 *        shortcut file (AW-022, AW-023, CW-038).
 */
void
sync_addons(desktop_t *app, int force)
{
    struct stat st;
    int64_t now = tiku_trk_now();

    if (!force && now - app->addons_checked < 500000) {
        return;
    }
    app->addons_checked = now;
    if (app->addons_dir[0] != '\0') {
        time_t mtime = (stat(app->addons_dir, &st) == 0) ? st.st_mtime : 0;

        if (force || mtime != app->addons_mtime) {
            app->addons_mtime = mtime;
            (void)tiku_trk_addons_discover(&app->addons, app->addons_dir);
            app->dirty = 1;
        }
    }
    if (app->addons_keys[0] != '\0') {
        time_t mtime = (stat(app->addons_keys, &st) == 0) ? st.st_mtime : 0;

        if ((force || mtime != app->keys_mtime) && mtime != 0) {
            FILE *f = fopen(app->addons_keys, "r");

            app->keys_mtime = mtime;
            if (f != NULL) {
                char buf[2048];
                size_t got = fread(buf, 1u, sizeof buf - 1u, f);

                buf[got] = '\0';
                (void)fclose(f);
                (void)tiku_trk_addons_rebind(&app->addons, buf);
                app->dirty = 1;
            }
        }
    }
}

/**
 * @brief Run one scripting line against the shell (PVK-052).
 *
 * The suite resolves what a verb acts on and NAMES it; the shell is the
 * caller that acts -- execute opens exactly as a double-click would, and
 * delete moves to the Trash.  Replies go beside the command file, in-band
 * (PVK-054's rule: reported, never thrown).
 */
static void
script_command_line(desktop_t *app, const char *line, FILE *reply)
{
    char store[8][TIKU_PATH_MAX];
    tiku_trk_req_t req;
    tiku_trk_rep_t rep;
    folder_t *fl = NULL;
    int i;

    {
        tiku_window_t *fw = tiku_workspace_focused(app->desk);

        fl = folder_of(app, fw);
        if (fl == NULL && app->folder_count > 0) {
            fl = &app->folders[0];
        }
    }
    if (fl == NULL || fl->view == NULL ||
        tiku_trk_script_parse(line, &req, store) != 0) {
        if (reply != NULL) {
            fprintf(reply, "error unhandled\n");
        }
        return;
    }
    memset(&rep, 0, sizeof rep);
    tiku_trk_script_run(fl->view, &req, &rep);
    if (rep.error != TIKU_TRK_ERR_OK) {
        if (reply != NULL) {
            fprintf(reply, "error %d\n", rep.error);
        }
        return;
    }
    if (req.verb == TIKU_TRK_VERB_EXECUTE) {
        for (i = 0; i < rep.nresult; i++) {
            tiku_model_t m;

            if (app->backend->ops->stat(app->backend, rep.result[i],
                                        &m) != 0) {
                continue;
            }
            if (tiku_model_is_container(&m)) {
                (void)open_folder(app, rep.result[i]);
            } else {
                (void)open_one(app, &m);
            }
        }
    } else if (req.verb == TIKU_TRK_VERB_DELETE) {
        const char *sel[64];
        int n = 0;

        for (i = 0; i < rep.nresult && n < 64; i++) {
            sel[n++] = rep.result[i];
        }
        if (n > 0) {
            (void)tiku_fs_trash(app->fs, sel, n);
        }
    }
    if (reply != NULL) {
        fprintf(reply, "ok %d\n", rep.nresult);
        for (i = 0; i < rep.nresult; i++) {
            fprintf(reply, "%s\n", rep.result[i]);
        }
    }
    app->dirty = 1;
}

/** @brief The command FILE is the transport (PVK-052): read, act, reply. */
void
script_command_file(desktop_t *app, const char *path)
{
    FILE *f = fopen(path, "r");
    FILE *reply = NULL;
    char rpath[TIKU_PATH_MAX];
    char line[512];

    if (f == NULL) {
        return;
    }
    snprintf(rpath, sizeof rpath, "%s.reply", path);
    reply = fopen(rpath, "w");
    while (fgets(line, sizeof line, f) != NULL) {
        char *nl = strchr(line, '\n');

        if (nl != NULL) {
            *nl = '\0';
        }
        if (line[0] != '\0' && line[0] != '#') {
            script_command_line(app, line, reply);
        }
    }
    (void)fclose(f);
    if (reply != NULL) {
        (void)fclose(reply);
    }
}

/**
 * @brief The union every volume's Trash makes (PVN-027).
 *
 * Each Trash on the machine contributes its entries; the models keep
 * their REAL paths, so restore and delete act on the file wherever its
 * volume keeps it.
 */
static int
trash_union_models(desktop_t *app, tiku_model_t *out, int max)
{
    char dirs[8][TIKU_PATH_MAX];
    int ndirs = tiku_fs_trash_dirs(app->fs, dirs, 8);
    int i, n = 0;

    for (i = 0; i < ndirs; i++) {
        n += list_children(app->backend, dirs[i], out + n, max - n);
    }
    return n;
}

/** @brief Re-derive a union Trash window from its contributors. */
int
trash_union_refresh(desktop_t *app, folder_t *fl)
{
    tiku_model_t *models;
    unsigned nf;
    int n;

    models = calloc(256, sizeof *models);
    if (models == NULL) {
        return 0;
    }
    n = trash_union_models(app, models, 256);
    nf = tiku_trk_poseview_apply_results(fl->view, models, n,
                                         tiku_trk_now());
    free(models);
    return nf != TIKU_TRK_NOTIFY_NOTHING;
}

/** @brief Re-apply the spring menu's enabled states from the live drag. */
void
spring_nav_states(desktop_t *app)
{
    const char *types[TIKU_TRK_DRAG_TYPES];
    int i;

    for (i = 0; i < app->drag.ntype; i++) {
        types[i] = app->drag.type[i];
    }
    tiku_trk_nav_spring_states(&app->nav,
        (const struct tiku_trk_apps *)&app->apps, types, app->drag.ntype);
}

/**
 * @brief The spring: a drag resting on a folder opens a NAVIGABLE menu of
 *        its contents rather than a window (PVS-051).
 */
void
spring_open_nav(void *context, const char *path)
{
    desktop_t *app = context;

    tiku_trk_nav_open(&app->nav, app->backend, path);
    app->nav.hide_dotfiles = tiku_trk_prefs_get(prefs_model(app),
                                                "HideDotFiles");
    app->nav.folders_first = tiku_trk_prefs_get(prefs_model(app),
                                                "SortFolderNamesFirst");
    app->spring_nav_on = 1;
    app->spring_nav_x = app->drag_target.spring.x +
                        app->drag_target.spring.w;
    app->spring_nav_y = app->drag_target.spring.y;
    app->dirty = 1;
}

/** @brief Close every spring level and put the walk away. */
void
spring_nav_close(desktop_t *app)
{
    while (tiku_trk_nav_leave(&app->nav)) {
    }
    app->nav.depth = 0;
    app->spring_nav_on = 0;
    app->dirty = 1;
}

/**
 * @brief The frame of spring level @p k, chained the way the draw chains.
 */
static tiku_rect_t
spring_nav_frame(desktop_t *app, int k)
{
    int px = app->spring_nav_x;
    int py = app->spring_nav_y;
    tiku_rect_t r = { 0, 0, 0, 0 };
    int j;

    for (j = 0; j <= k && j < app->nav.depth; j++) {
        tiku_trk_nav_level_t *lv = &app->nav.level[j];

        tiku_trk_nav_place(&app->nav, j, px, py, app->surf->w,
                           app->surf->h, &r.x, &r.y, &r.w, &r.h);
        px = r.x + r.w - 4;
        py = r.y + 2 + ((lv->sel >= 0) ? lv->sel : 0) * TIKU_TRK_NAV_ROW;
    }
    return r;
}

/**
 * @brief Follow the pointer over the spring levels (PVS-051, PVS-054).
 *
 * Hovering a row selects it; a container row opens the next level in
 * place; leaving every level (and the sprung pose) by more than the
 * margin closes the whole thing.
 */
void
spring_nav_track(desktop_t *app, int x, int y)
{
    int hit_level = -1;
    int hit_item = -1;
    int inside_margin = 0;
    int k;

    for (k = 0; k < app->nav.depth; k++) {
        tiku_rect_t r = spring_nav_frame(app, k);
        int at;

        if (x >= r.x - TIKU_TRK_SPRING_MARGIN &&
            x < r.x + r.w + TIKU_TRK_SPRING_MARGIN &&
            y >= r.y - TIKU_TRK_SPRING_MARGIN &&
            y < r.y + r.h + TIKU_TRK_SPRING_MARGIN) {
            inside_margin = 1;
        }
        at = tiku_trk_nav_item_at(&app->nav, k, r.x, r.y, x, y);
        if (at >= 0) {
            hit_level = k;
            hit_item = at;
        }
    }
    if (hit_level >= 0) {
        tiku_trk_nav_level_t *lv = &app->nav.level[hit_level];

        if (lv->sel != hit_item) {
            /* A new row: anything deeper hung off the OLD row. */
            while (app->nav.depth > hit_level + 1) {
                (void)tiku_trk_nav_leave(&app->nav);
            }
            lv->sel = hit_item;
            if (lv->item[hit_item].is_container &&
                lv->item[hit_item].enabled) {
                (void)tiku_trk_nav_enter(&app->nav, hit_item);
            }
            app->dirty = 1;
        }
        return;
    }
    /* Off every level: the sprung pose itself keeps it alive too, so the
     * pointer can travel from the pose into the menu (PVS-054). */
    if (!inside_margin &&
        !tiku_trk_springmenu_inside(&app->drag_target.spring, x, y)) {
        spring_nav_close(app);
    }
}

/**
 * @brief What releasing the drag at @p x,@p y over the spring means.
 *
 * @return 1 with @p dest set for an enabled row, 2 for a disabled row
 *         (the release is swallowed), 0 when the menu was not hit.
 */
int
spring_nav_hit(desktop_t *app, int x, int y, char *dest, size_t max)
{
    int k;

    for (k = app->nav.depth - 1; k >= 0; k--) {
        tiku_rect_t r = spring_nav_frame(app, k);
        int at = tiku_trk_nav_item_at(&app->nav, k, r.x, r.y, x, y);

        if (at >= 0) {
            tiku_trk_nav_level_t *lv = &app->nav.level[k];

            if (!lv->item[at].enabled) {
                return 2;
            }
            snprintf(dest, max, "%s", lv->item[at].path);
            return 1;
        }
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return 2;               /* in the menu, between rows */
        }
    }
    return 0;
}

/** @brief A drop on an application pose is the same one act (PVS-044). */
int
drop_launch(void *context, const struct tiku_trk_app *application,
            const char *const *paths, int count)
{
    desktop_t *app = context;

    return launch_paths(app, (const tiku_trk_app_t *)application, paths,
                        count);
}

/** @brief Fill the Attributes menu's per-type submenus. */
void
build_per_type(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_menu_build_per_type(&app->folder_menu, fl);
}

/**
 * @brief Finish an edit: check the value, then write it.
 *
 * The descriptor is consulted BEFORE the device is: a refusal that names the
 * range or the expected spelling is worth more than the write error the
 * device would return, and it costs the device nothing.
 */
void
commit_edit(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_edit_commit_value(&app->folder_edit, fl);
}

/**
 * @brief Write a folder window's frame back, if it moved (CW-001, CW-002).
 *
 * A folder that has GONE keeps no frame, for the same reason it keeps no
 * arrangement: there is nothing left for the position to be the position of.
 */
void
close_window(desktop_t *app, tiku_window_t *w)
{
    folder_t *fl;
    int info_slot;

    if (w == NULL) {
        return;
    }
    if (w == tiku_trk_panel_window_handle(&app->file_panel)) {
        tiku_trk_panel_window_close(&app->file_panel);
        return;
    }
    {
        struct embed_win *ew = embed_win_of_window(app, w);

        if (ew != NULL) {
            embed_drop_window(ew, 1);
            return;
        }
    }
    if (tiku_remote_owner(&app->remote, w) != NULL) {
        tiku_remote_window_closed(&app->remote, w);
        tiku_workspace_close(app->desk, w);
        return;
    }
    if (w == tiku_trk_find_window_handle(&app->find_window)) {
        tiku_trk_find_window_close(&app->find_window);
        return;
    }
    if (w == tiku_trk_deskbar_prefs_window_handle(
                 &app->deskbar_prefs_window)) {
        tiku_trk_deskbar_prefs_window_close(&app->deskbar_prefs_window);
        return;
    }
    if (w == tiku_trk_desktop_prefs_window_handle(
                 &app->desktop_prefs_window)) {
        tiku_trk_desktop_prefs_window_close(&app->desktop_prefs_window);
        return;
    }
    if (w == tiku_trk_fonts_window_handle(&app->fonts_window)) {
        tiku_trk_fonts_window_close(&app->fonts_window);
        return;
    }
    if (w == tiku_trk_help_window_handle(&app->help_window)) {
        tiku_trk_help_window_close(&app->help_window);
        return;
    }
    if (w == tiku_trk_image_window_handle(app->image_window)) {
        if (app->prefs_track == w) app->prefs_track = NULL;
        tiku_trk_image_window_close(app->image_window);
        return;
    }
    if (w == tiku_trk_help_window_handle(&app->reader_window)) {
        tiku_trk_help_window_close(&app->reader_window);
        return;
    }
    if (w == desktop_deviceprefs_handle(app)) {
        desktop_deviceprefs_close(app);
        return;
    }
    if (desktop_utility_owns(app, w)) {
        desktop_utility_close(app, w);
        return;
    }
    if (w == desktop_startup_handle(app)) {
        desktop_startup_close(app);
        return;
    }
    if (w == desktop_forcequit_handle(app)) {
        desktop_forcequit_close(app);
        return;
    }
    if (w == desktop_associations_handle(app)) {
        desktop_associations_close(app);
        return;
    }
    if (w == tiku_trk_sounds_window_handle(&app->sounds_window)) {
        tiku_trk_sounds_window_close(&app->sounds_window);
        return;
    }
    info_slot = tiku_trk_info_windows_slot(&app->info_windows, w);
    if (info_slot >= 0) {
        tiku_trk_info_windows_close(&app->info_windows, info_slot);
        return;
    }
    fl = folder_of(app, w);
    if (fl != NULL) {
        if (app->bar_fl == fl) {
            app->bar_fl = NULL;
            app->bar_open = -1;
            app->menus.bar.open = -1;
            app->popup = NULL;
            app->popup_sub = NULL;
        }
        tiku_trk_folder_lifecycle_close(&app->folder_lifecycle, fl);
        return;
    }
    tiku_workspace_close(app->desk, w);
    app->dirty = 1;
}

/**
 * @brief A drop landing anywhere on the file panel (AW-059).
 *
 * Exactly one carried item navigates the panel; any other count is
 * swallowed, exactly as the original's MessageDropFilter swallows it.
 * @return nonzero when the drop was the panel's, handled or swallowed.
 */
int
drop_on_panel(desktop_t *app, int x, int y)
{
    tiku_window_t *w = NULL;

    (void)tiku_workspace_hit(app->desk, x, y, &w);
    if (w == NULL || w != tiku_trk_panel_window_handle(&app->file_panel)) {
        return 0;
    }
    if (app->drag.count == 1) {
        tiku_trk_panel_window_drop(&app->file_panel, app->drag.path[0]);
    }
    app->dirty = 1;
    return 1;
}

/**
 * @brief The Info window icon under the pointer that would take a drop.
 *
 * The icon stands for the window's own item, so it is a drop target
 * exactly when that item is a container the store can write (AW-075).
 * @return the slot, or -1.
 */
int
info_icon_drop_slot(desktop_t *app, int x, int y)
{
    tiku_window_t *w = NULL;
    tiku_trk_info_t *info;
    tiku_model_t m;
    int slot;

    (void)tiku_workspace_hit(app->desk, x, y, &w);
    slot = tiku_trk_info_windows_slot(&app->info_windows, w);
    if (slot < 0 ||
        tiku_trk_info_windows_hit(&app->info_windows, slot, x, y, NULL) !=
            TIKU_TRK_INFO_HIT_ICON) {
        return -1;
    }
    info = tiku_trk_info_windows_model(&app->info_windows, slot);
    if (app->backend->ops->stat(app->backend, info->path, &m) != 0 ||
        !tiku_model_is_mutable_container(&m)) {
        return -1;
    }
    return slot;
}

/** @brief Drop the drag on an Info window's icon.  @return handled. */
int
drop_on_info_icon(desktop_t *app, int x, int y, unsigned modifiers)
{
    const char *paths[TIKU_TRK_DRAG_MAX];
    tiku_trk_info_t *info;
    unsigned drag_modifiers = 0u;
    tiku_trk_drop_t action;
    int slot = info_icon_drop_slot(app, x, y);
    int same_store, i, count = 0;

    if (slot < 0 || app->drag.count == 0) {
        return 0;
    }
    info = tiku_trk_info_windows_model(&app->info_windows, slot);
    if ((modifiers & TIKU_MOD_OPTION) != 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_COPY;
    }
    if ((modifiers & TIKU_MOD_CMD) != 0u) {
        drag_modifiers |= TIKU_TRK_DRAG_MOD_LINK;
    }
    same_store = tiku_fs_drop_action(app->fs, app->drag.path[0],
                                         info->path, 0) == TIKU_OP_MOVE;
    action = tiku_trk_drag_action(&app->drag, info->path, drag_modifiers,
                                  same_store);
    if (action == TIKU_TRK_DROP_NOTHING) {
        return 1;               /* over the icon, but nothing to do */
    }
    for (i = 0; i < app->drag.count; i++) {
        paths[count++] = app->drag.path[i];
    }
    snprintf(app->prog_dest, sizeof app->prog_dest, "%s", info->path);
    switch (action) {
    case TIKU_TRK_DROP_COPY:
        (void)tiku_fs_copy(app->fs, paths, count, info->path,
                               TIKU_CONFLICT_ASK);
        break;
    case TIKU_TRK_DROP_LINK:
        (void)tiku_fs_link(app->fs, paths, count, info->path,
                               (modifiers & TIKU_MOD_SHIFT) != 0u);
        break;
    default:
        (void)tiku_fs_move(app->fs, paths, count, info->path,
                               TIKU_CONFLICT_ASK);
        break;
    }
    snprintf(app->status, sizeof app->status,
             "dropped %d item%s on %s", count, count == 1 ? "" : "s",
             info->model.name);
    return 1;
}

/**
 * @brief Resolve and perform the drop the pointer is over.
 *
 * The destination is the highlighted pose when there is one and the hovered
 * window's own folder otherwise -- which is also what a drop onto one of the
 * dragged poses means, since such a pose never highlights (PVS-042).
 */
void
do_drop(desktop_t *app, int x, int y, unsigned state, unsigned button)
{
    tiku_trk_drop_controller_execute(&app->drop_controller, x, y, state,
                                     button);
}

/**
 * @brief Close the window on @p path and every window on a folder
 *        inside it; a sibling sharing the prefix is left open.
 */
void
close_windows_under(desktop_t *app, const char *path)
{
    size_t n;
    int i;

    if (app == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    n = strlen(path);
    /* Backwards, because closing compacts the array. */
    for (i = app->folder_count - 1; i >= 0; i--) {
        const char *p = app->folders[i].path;

        /* The folder itself, or a folder INSIDE it -- matched on a whole
         * component so a sibling sharing the same prefix is left alone. */
        if (strncmp(p, path, n) == 0 && (p[n] == '\0' || p[n] == '/')) {
            close_window(app, app->folders[i].win);
        }
    }
}

/**
 * @brief Start the inline rename the slow-second-click timer armed.
 *
 * A namespace node has no name of its own to change; refused here rather
 * than letting an editor open over something the device will not rename.
 */
void
begin_rename(desktop_t *app, folder_t *fl, int row)
{
    tiku_trk_folder_edit_begin_rename(&app->folder_edit, fl, row);
}

/** @brief Finish the rename, keeping or discarding what was typed. */
void
commit_rename(desktop_t *app, int commit)
{
    tiku_trk_folder_edit_finish_rename(&app->folder_edit, commit);
}

/** @brief Begin editing the active pose's value, if it has one to edit. */
void
begin_edit(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_edit_begin_value(&app->folder_edit, fl);
}

/**
 * @brief The selected rows' paths, for an operation that acts on them.
 *
 * @return How many were collected.
 */
int
folder_selection(desktop_t *app, folder_t *fl,
                 char paths[][TIKU_PATH_MAX], const char **out, int max)
{
    (void)app;
    return tiku_trk_folder_file_actions_selection(fl, paths, out, max);
}

/**
 * @brief Run @p addon on the focused folder's selection: an external one
 *        in a process of its own, a built-in one through its command.
 *        @return 1 when it ran.
 */
static int
invoke_addon(const tiku_trk_addon_t *addon, void *context)
{
    desktop_t *app = context;

    if (addon == NULL || app == NULL) {
        return 0;
    }
    if (addon->external) {
        /* Run against the selection, in a process of its own -- the
         * port's separate thread (CW-038). */
        char paths[64][TIKU_PATH_MAX];
        const char *sel[64];
        folder_t *fl = folder_of(app,
                                 tiku_workspace_focused(app->desk));
        int n;

        if (fl == NULL) {
            fl = desktop_folder(app);
        }
        n = tiku_trk_folder_file_actions_selection(fl, paths, sel, 64);
        if (tiku_trk_addons_run_external(addon, sel, n) == 0) {
            snprintf(app->status, sizeof app->status,
                     "running %.48s on %d item%s", addon->name, n,
                     (n == 1) ? "" : "s");
        } else {
            snprintf(app->status, sizeof app->status,
                     "could not run %.60s", addon->name);
        }
        return 1;
    }
    snprintf(app->status, sizeof app->status, "add-on requested: %.80s",
             addon->name);
    return 1;
}

/**
 * @brief Point a new entry at where it was asked for (PVL-025, PVL-098).
 *
 * The menu's own corner is where it was opened, which is the click that
 * asked for the folder; a keyboard shortcut has no menu, so the pointer
 * answers instead, and only while it is over the window.
 */
static int
seed_new_entry(desktop_t *app, folder_t *fl, int *vx, int *vy)
{
    tiku_rect_t c;
    int ox = 0, oy = 0, x, y;

    if (fl == NULL || fl->view == NULL ||
        tiku_trk_poseview_mode(fl->view) == TIKU_TRK_MODE_LIST) {
        return 0;
    }
    if (app->popup != NULL) {
        x = app->popup_frame.x;
        y = app->popup_frame.y;
    } else {
        x = app->ptr_x;
        y = app->ptr_y;
    }
    c = tiku_trk_folder_window_content(fl);
    if (x < c.x || x >= c.x + c.w || y < c.y || y >= c.y + c.h) {
        return 0;
    }
    tiku_trk_poseview_origin(fl->view, &ox, &oy);
    *vx = x - (c.x + 2) + ox;
    *vy = y - (c.y + 2) + oy;
    return 1;
}

/** @brief Give the entry just made the place it was asked for. */
static void
land_new_entry(desktop_t *app, folder_t *fl, const char *made, int seeded,
               int vx, int vy)
{
    const char *slash = (made != NULL) ? strrchr(made, '/') : NULL;

    if (!seeded || fl == NULL || fl->view == NULL) {
        return;
    }
    (void)app;
    tiku_trk_poseview_drop_land(fl->view,
                                (slash != NULL) ? slash + 1 : made, vx, vy);
}

/** @brief Carry out a menu choice on the focused folder. */
void
menu_invoke(desktop_t *app, folder_t *fl, int command)
{
    const char *template_path;
    const char *template_parent;
    int seeded = 0, seed_x = 0, seed_y = 0;

    if (fl == NULL) {
        return;
    }
    {
        /* The one enablement the menus use gates EVERY route here: a
         * chord for a command the menu would show grey answers out loud
         * instead of being silently swallowed (NeXT UIG ch6). */
        tiku_trk_sc_ctx_t gc = menu_ctx(app, fl, 0u);

        /* Three commands are exempt because their handlers carry WIDER
         * semantics than the menu rule and say their own refusals:
         * Unmount acts on the volume being SHOWN when nothing is
         * selected (AW-096), and the trash/delete pair routes a selected
         * VOLUME to an unmount with its own boot-volume refusal
         * (FS-041). */
        if (command != TIKU_TRK_CMD_UNMOUNT &&
            command != TIKU_TRK_CMD_MOVE_TO_TRASH &&
            command != TIKU_TRK_CMD_DELETE &&
            !tiku_trk_sc_enabled(command, &gc)) {
            snprintf(app->status, sizeof app->status,
                     "%s is not available here",
                     tiku_trk_sc_label(command, 0u));
            *&app->dirty = 1;
            return;
        }
    }
    if (desktop_cmd_system(app, fl, command)) {
        return;
    }
    if (desktop_cmd_query(app, fl, command)) {
        return;
    }
    if (desktop_cmd_destination(app, fl, command, &template_path)) {
        return;
    }
    if (desktop_cmd_template(app, fl, command, template_path, &template_parent,
        &seeded, &seed_x, &seed_y)) {
        return;
    }
    if (tiku_trk_addons_invoke(&app->addons, command,
                               TIKU_TRK_ADDON_COMMAND_BASE,
                               invoke_addon, app)) {
        app->dirty = 1;
        return;
    }
    if (desktop_cmd_layout(app, fl, command)) {
        return;
    }
    if (desktop_cmd_mount(app, fl, command)) {
        return;
    }
    if (desktop_cmd_open_with(app, fl, command)) {
        return;
    }
    if (desktop_cmd_unmount(app, fl, command)) {
        return;
    }
    if (desktop_cmd_new_folder(app, fl, command, seeded, seed_x, seed_y)) {
        return;
    }
    if (tiku_trk_folder_actions_invoke(app->desk, fl, command)) {
        app->dirty = 1;
        return;
    }
    if (tiku_trk_folder_file_actions_invoke(&app->file_actions, fl,
                                             command)) {
        app->dirty = 1;
        return;
    }
    switch (command) {
    case TIKU_TRK_CMD_OPEN: {
        tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

        if (p != NULL && p->model.facts.link_broken) {
            offer_broken_link(app, p->model.path);
        } else if (p != NULL && tiku_model_is_container(&p->model)) {
            (void)open_folder(app, p->model.path);
        }
        break;
    }
    case TIKU_TRK_CMD_OPEN_PARENT: {
        char parent[TIKU_PATH_MAX];
        char *slash;

        snprintf(parent, sizeof parent, "%s", fl->path);
        slash = strrchr(parent, '/');
        if (slash != NULL) {
            if (slash == parent) { parent[1] = '\0'; } else { *slash = '\0'; }
            (void)open_folder(app, parent);
        }
        break;
    }
    case TIKU_TRK_CMD_GET_INFO: {
        tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

        /* The selection if there is one, else the folder itself: asking
         * about "this window" with nothing picked is asking about the
         * folder it is showing. */
        {
            int islot = tiku_trk_info_windows_open(&app->info_windows,
                (p != NULL && p->selected) ? p->model.path : fl->path);

            if (islot >= 0) {
                info_opens_with_fill(app, &app->info_windows.model[islot]);
            }
        }
        break;
    }
    case TIKU_TRK_CMD_EDIT_QUERY: {
        /* Re-open the panel ON the query rather than running it again:
         * what the user wants from a result set is usually one more
         * condition, not the same search a second time (Q-045). */
        int whole = (fl->qname[0] != '\0')
                        ? tiku_trk_find_load_named(find_model(app), fl->qname)
                        : -1;

        if (whole < 0) {
            snprintf(app->status, sizeof app->status,
                     "that query cannot be opened");
            break;
        }
        if (!whole) {
            /* Said rather than swallowed: a scope this panel cannot offer
             * means the restored search is not the saved one. */
            snprintf(app->status, sizeof app->status,
                     "one of its places is no longer here");
        }
        open_panel(app);
        break;
    }
    case TIKU_TRK_CMD_MOVE_TO:
    case TIKU_TRK_CMD_COPY_TO:
        /* Both ask the same question -- WHERE -- so both open the same
         * picker and it is the command that decides what happens with the
         * answer. */
        fpanel_open(app, fl, command);
        break;
    case TIKU_TRK_CMD_SELECT_MATCHING:
        /* The same window the keyboard opens: an item that named it and
         * did nothing was the menu disagreeing with the shortcut. */
        tiku_trk_select_window_open(&app->select_window, fl, 520, 300);
        break;
    case TIKU_TRK_CMD_CLOSE:
        close_window(app, fl->win);
        break;
    case TIKU_TRK_CMD_CLOSE_ALL:
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE: {
        int i;

        for (i = 0; i < app->folder_count; i++) {
            if (app->folders[i].win != NULL) {
                close_window(app, app->folders[i].win);
            }
        }
        break;
    }
    default:
        if (command >= TIKU_TRK_CMD_COL_ATTR_BASE &&
            command < TIKU_TRK_CMD_OPEN_WITH_BASE) {
            (void)tiku_trk_poseview_toggle_column(fl->view,
                      command - TIKU_TRK_CMD_COL_ATTR_BASE);
            break;
        }
        if (command >= TIKU_TRK_CMD_OPEN_WITH_BASE) {
            open_with_pick(app, fl, command - TIKU_TRK_CMD_OPEN_WITH_BASE);
            break;
        }
        /* Everything else is built and enabled correctly but has no
         * implementation behind it yet; it must do nothing rather than
         * something approximate. */
        break;
    }
    app->dirty = 1;
}

/**
 * @brief Open a window on @p path through the folder lifecycle.  A Trash
 *        folder becomes a result set over every volume's trash (PVN-027).
 *        @return the window, or NULL when none could be opened.
 */
folder_t *
open_folder(desktop_t *app, const char *path)
{
    folder_t *fl = tiku_trk_folder_lifecycle_open(&app->folder_lifecycle,
                                                  path);

    if (fl != NULL && fl->view != NULL && !fl->trash_union &&
        path != NULL && tiku_fs_in_trash(app->fs, path)) {
        /* A Trash window shows the UNION of every volume's trash
         * (PVN-027): the view becomes a result set over the
         * contributors, refreshed on the query beat, each entry keeping
         * its real path so restore and delete land on the right
         * volume. */
        tiku_model_t m;

        if (app->backend->ops->stat(app->backend, path, &m) == 0 &&
            m.kind == TIKU_KIND_TRASH) {
            tiku_model_t *models = calloc(256, sizeof *models);

            if (models != NULL) {
                int n = trash_union_models(app, models, 256);
                (void)tiku_trk_poseview_set_results(fl->view, models, n);
                free(models);
                fl->trash_union = 1;
            }
        }
    }
    return fl;
}

/**
 * @brief One step of the file work under way, and the progress row it moves.
 */
void
desktop_progress_tick(desktop_t *app)
{
    int busy = (app->fs != NULL) && tiku_fs_busy(app->fs);
    int over = app->fs_was_busy && !busy && !app->fs_job;

    app->fs_was_busy = busy;
    if (app->fs != NULL) {
        tiku_fs_moved_pump(app->fs);
    }
    if (busy && !progress_model(app)->row[0].paused) {
        const tiku_progress_t *pr = tiku_fs_progress(app->fs);

        /* The stepping is a worker's, one job for the whole operation;
         * it stops when the engine asks, and is posted again once the
         * question is answered. */
        if (!app->fs_job && !pr->conflict && !pr->error_ask) {
            desktop_wire_fs_run(app);
        }
    }
    if (over || (busy && !progress_model(app)->row[0].paused)) {
        const tiku_progress_t *pr;

        if (over) {
            /* The operation just finished: what it MADE gets selected
             * where it lives, once it shows up (AW-033). */
            char made[8][TIKU_PATH_MAX];
            int nmade = tiku_fs_last_created(app->fs, made, 8);
            int mi;

            for (mi = 0; mi < nmade; mi++) {
                select_created_t *scx = malloc(sizeof *scx);

                if (scx == NULL) {
                    break;
                }
                scx->app = app;
                snprintf(scx->path, sizeof scx->path, "%s", made[mi]);
                (void)tiku_trk_run_periodic(app->loop, select_created,
                                            scx, free, 200000,
                                            5000000);
            }
        }
        /* A mixed paste left its second half waiting for the engine to
         * go idle; this is where it gets to start (FS-055). */
        (void)tiku_trk_folder_file_actions_pump(&app->file_actions);
        pr = tiku_fs_progress(app->fs);
        if (pr->conflict) {
            /* Asked, not assumed.  The engine worked out which form
             * the question takes; this puts it up and waits. */
            if (!alert_model(app)->open) {
                tiku_trk_alert_for_conflict(alert_model(app), pr);
            }
        } else if (pr->error_ask) {
            if (!alert_model(app)->open) {
                tiku_trk_alert_for_error(alert_model(app), pr);
            }
        } else {
            snprintf(app->status, sizeof app->status, "%d of %d%s%s",
                     pr->done + pr->skipped, pr->total,
                     pr->skipped ? " (some skipped)" : "",
                     pr->failed ? " -- some failed" : "");
        }
        if (tiku_trk_progress_wanted(pr->op, pr->total)) {
            /* Where it is going, which is the other half of "what is
             * happening": a file name alone does not say whether this
             * is the copy the user meant (FS-069). */
            tiku_trk_progress_update(progress_model(app), 0, pr, 1,
                                     pr->to[0] != '\0' ? pr->to
                                     : app->prog_dest[0] != '\0'
                                         ? app->prog_dest : NULL);
            tiku_trk_progress_sample(progress_model(app), 0, pr->bytes,
                                     tiku_trk_now());
        }
        app->dirty = 1;
        if (tiku_trk_status_overlay_progress_opened(
                &app->status_overlay) == 0) {
            tiku_trk_status_overlay_progress_start(&app->status_overlay,
                                                   tiku_trk_now());
            /* Whoever was in front when the job began gets the focus
             * back when it ends: an operation is not a reason to be
             * left looking at something else (FS-073). */
            app->prog_was_active = tiku_workspace_focused(app->desk);
        }
    } else if (progress_model(app)->open) {
        tiku_trk_progress_update(progress_model(app), 0, NULL, 0, NULL);
        tiku_trk_status_overlay_progress_finish(&app->status_overlay);
        if (app->prog_was_active != NULL &&
            app->prog_was_active->open) {
            tiku_workspace_activate(app->desk, app->prog_was_active);
        }
        app->prog_was_active = NULL;
        app->dirty = 1;
    }
}

/**
 * @brief Tracker's Pulse: a rename armed by a slow second click starts when its
 * interval is up.
 */
void
desktop_rename_pulse(desktop_t *app)
{
    int i;

    /* Tracker's Pulse: a rename armed by a slow second click starts
     * here rather than on the click itself (MA-043). */
    for (i = 0; i < app->folder_count; i++) {
        folder_t *fl = &app->folders[i];
        int due;

        if (fl->win == NULL || !fl->win->open) {
            continue;
        }
        due = tiku_trk_poseview_edit_due(fl->view, tiku_trk_now());
        if (due >= 0 && !app->folder_edit.editing) {
            begin_rename(app, fl, due);
            app->dirty = 1;
        }
    }
}

/**
 * @brief A button held still for the click interval opens the context menu, on
 * the bar or on a pose (PVS-017).
 */
void
desktop_press_hold(desktop_t *app)
{
    /* PVS-017: a button held still for the click interval opens the
     * context menu without waiting for the release. */
    if (app->deskbar_press &&
        tiku_trk_now() - app->press_at > 2 * TIKU_TRK_CLICK_SPEED_US) {
        tiku_trk_db_pick_t dbpick;

        /* A still primary press is the Deskbar's long-press equivalent
         * of a secondary click.  It replaces the calendar with clock
         * options without waiting for release. */
        tiku_trk_deskbar_close_calendar(app->deskbar);
        (void)tiku_trk_deskbar_click(app->deskbar, app->press_x,
                                     app->press_y, 0u, 3, 1, &dbpick);
        app->deskbar_press = 0;
        app->dirty = 1;
    }
    if (app->press_down && !app->press_menu && app->popup == NULL &&
        app->press_fl != NULL &&
        tiku_trk_now() - app->press_at > TIKU_TRK_CLICK_SPEED_US) {
        folder_t *pfl = app->press_fl;
        tiku_trk_pose_t *p = (app->press_row >= 0)
            ? tiku_trk_poseview_at(pfl->view, app->press_row) : NULL;
        tiku_trk_sc_ctx_t c;

        tiku_trk_poseview_cancel_edit_wait(pfl->view);
        commit_rename(app, 1);            /* CW-029: mouse-opened */
        (void)tiku_trk_poseview_context_click(pfl->view, app->press_row);
        c = menu_ctx(app, pfl, 0u);
        build_open_with(app, pfl);
        build_per_type(app, pfl);
        app->popup = tiku_trk_cwmenu_context(&app->menus,
                        (p != NULL) ? &p->model : NULL, &c);
        app->popup_sub = NULL;
        app->popup_frame = tiku_menu_place(app->popup, app->press_x,
            app->press_y, (tiku_rect_t){ 0, 0, app->surf->w,
                                             app->surf->h });
        app->press_menu = 1;
        app->press_down = 0;
        app->dirty = 1;
    }
}

/**
 * @brief The spring-loaded and navigation menus take a chunk of their folder
 * per turn, and ask the entries not yet asked (CW-055).
 */
void
desktop_spring_tick(desktop_t *app)
{
    int i;

    if ((app->nav_on && app->popup != NULL) || app->spring_nav_on) {
        /* A chunk per turn: the menu answers the mouse while a large
         * folder is still arriving (CW-055). */
        if (tiku_trk_nav_fill(&app->nav, app->nav.depth - 1)) {
            if (app->spring_nav_on) {
                /* What the drag can land on is per entry, so fresh
                 * entries get their states as they arrive (PVS-052). */
                spring_nav_states(app);
            }
            app->dirty = 1;
        }
    } else if (app->nav_on && app->popup == NULL) {
        app->nav_on = 0;         /* the menu went; so does the walk */
    }
    {   /* Ask the entries that have not been asked whether they have
         * a picture of their own -- a bounded batch per turn, for the
         * same reason the thumbnails are made in one: a folder of a
         * thousand images must not open a thousand files inside one
         * tick (IV-005, IV-037). */
        int budget = HINTS_PER_TURN;

        for (i = 0; i < app->folder_count && budget > 0; i++) {
            folder_t *fl = &app->folders[i];
            int k, n;

            if (fl->win == NULL || !fl->win->open) {
                continue;
            }
            n = tiku_trk_poseview_count(fl->view);
            for (k = 0; k < n && budget > 0; k++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, k);

                if (p == NULL || p->model.facts.icon_hint !=
                                     TIKU_ICONHINT_UNASKED) {
                    continue;
                }
                (void)tiku_trk_nodeicon_hint(app->store, &app->thumbs,
                                             &p->model);
                budget--;
                /* One pose learned what picture it has, so one row is
                 * what needs painting: a bare dirty here repainted the
                 * whole listing and undid the damage the notice had
                 * just recorded (PVL-075). */
                tiku_trk_poseview_damage_row(fl->view, k);
                app->dirty = 1;
            }
        }
    }
}

/**
 * @brief a new folder, named and renamed in place.  @return 1 when this was the
 * command
 */
int
desktop_cmd_new_folder(desktop_t *app, folder_t *fl, int command, int seeded,
                       int seed_x, int seed_y)
{
    if (command == TIKU_TRK_CMD_NEW_FOLDER) {
        char made[TIKU_PATH_MAX], error[200];
        const char *slash;
        int row;

        error[0] = '\0';
        if (app->fs != NULL && tiku_fs_new_folder(
                app->fs, fl->path, made, sizeof made, error,
                sizeof error) == 0) {
            slash = strrchr(made, '/');
            snprintf(app->status, sizeof app->status, "created '%.150s'",
                     slash != NULL ? slash + 1 : made);
            land_new_entry(app, fl, made, seeded, seed_x, seed_y);
            (void)tiku_trk_poseview_reload(fl->view);
            row = select_named(app, fl,
                               slash != NULL ? slash + 1 : made);
            if (row >= 0) {
                begin_rename(app, fl, row);
                /* Opening the editor must not hide the operation result;
                 * scripts and users should still see what was made. */
                snprintf(app->status, sizeof app->status,
                         "created '%.150s'",
                         slash != NULL ? slash + 1 : made);
            }
        } else {
            snprintf(app->status, sizeof app->status, "%.180s",
                     error[0] != '\0' ? error : "could not create a new folder");
        }
        app->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief unmounting the folder's volume, refused out loud when it cannot be.
 * @return 1 when this was the command
 */
int
desktop_cmd_unmount(desktop_t *app, folder_t *fl, int command)
{
    if (command == TIKU_TRK_CMD_UNMOUNT) {
        /* The selection names the volume; with none, the volume ROOT the
         * window is showing is the one the gesture means (CW-034).
         * Everything open on it saves itself first (AW-096), and a
         * refusal or failure is said out loud. */
        char upaths[1][TIKU_PATH_MAX];
        const char *usel[1];
        const char *upath =
            (tiku_trk_folder_file_actions_selection(fl, upaths, usel, 1) > 0)
                ? usel[0] : fl->path;
        tiku_volumes_t uvols;
        char uwhy[200];

        uwhy[0] = '\0';
        if (tiku_volumes_scan(&uvols) > 0 &&
            tiku_volume_is_root(&uvols, upath)) {
                        char umount_at[TIKU_PATH_MAX], uname[128];

            /* Asked and saved here; the umount itself, which flushes
             * what is dirty, goes to a worker and reports back. */
            if (tiku_volume_unmount_prepare(&uvols, upath,
                                            save_before_unmount, app,
                                            umount_at, sizeof umount_at,
                                            uname, sizeof uname,
                                            uwhy, sizeof uwhy) == 0) {
                (void)desktop_wire_unmount(app, umount_at, uname);
            } else if (uwhy[0] != '\0') {
                tiku_sound_play(TIKU_SOUND_BEEP);
                tiku_alert_open(alert_model(app), TIKU_ALERT_STOP,
                                    TIKU_TRK_ALERT_TAG_REFUSAL, uwhy,
                                    "Cancel");
            }
        } else {
            /* Nothing here IS a volume: said, not swallowed (NeXT UIG
             * ch6). */
            snprintf(app->status, sizeof app->status,
                     "Unmount is not available here");
        }
        app->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief the Open With window.  @return 1 when this was the command
 */
int
desktop_cmd_open_with(desktop_t *app, folder_t *fl, int command)
{
    if (command == TIKU_TRK_CMD_OPEN_WITH_WINDOW) {
        char wpaths[TIKU_TRK_OW_PATHS][TIKU_PATH_MAX];
        const char *wsel[TIKU_TRK_OW_PATHS];
        const char *wtypes[TIKU_TRK_APP_TYPES_MAX];
        int wn, wt = 0, wi, wk, wcount;

        wn = tiku_trk_folder_file_actions_selection(fl, wpaths, wsel,
                                                    TIKU_TRK_OW_PATHS);
        if (wn > 0) {
            wcount = tiku_trk_poseview_count(fl->view);
            for (wi = 0; wi < wcount && wt < TIKU_TRK_APP_TYPES_MAX; wi++) {
                tiku_trk_pose_t *wp = tiku_trk_poseview_at(fl->view, wi);

                if (wp == NULL || !wp->selected) {
                    continue;
                }
                for (wk = 0; wk < wt; wk++) {
                    if (strcmp(wtypes[wk], wp->model.type) == 0) {
                        break;
                    }
                }
                if (wk == wt) {
                    wtypes[wt++] = wp->model.type;
                }
            }
            tiku_trk_open_with_window_open(&app->open_with_window,
                &app->apps, wtypes, wt, wsel, wn);
        }
        app->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief the mount settings and mounting everything.  @return 1 when this was
 * the command
 */
int
desktop_cmd_mount(desktop_t *app, folder_t *fl, int command)
{
    (void)fl;
    if (command == TIKU_TRK_CMD_MOUNT_SETTINGS) {
        /* Straight to the Disk mount page: the one caller the jump-to-page
         * request has (AW-101, CW-034). */
        app->prefs_window.page = TIKU_TRK_PAGE_MOUNT;
        tiku_trk_prefs_window_open(&app->prefs_window);
        app->dirty = 1;
        return 1;
    }
    if (command == TIKU_TRK_CMD_MOUNT_ALL ||
        (command >= TIKU_TRK_CMD_MOUNT_PART_BASE &&
         command < TIKU_TRK_CMD_MOUNT_PART_BASE +
                       TIKU_TRK_MOUNT_PART_MAX)) {
        /* Asked of the system, and the answer -- on this host, that
         * mounting needs its leave -- is said out loud (CW-034). */
        char why[200];
        int failed = 0, tried = 0;

        why[0] = '\0';
        if (command == TIKU_TRK_CMD_MOUNT_ALL) {
            tiku_partition_t part[TIKU_TRK_MOUNT_PART_MAX];
            int pn = tiku_partitions_scan(part,
                                              TIKU_TRK_MOUNT_PART_MAX);
            int k;

            for (k = 0; k < pn; k++) {
                if (part[k].mounted) {
                    continue;
                }
                tried++;
                if (tiku_volume_mount(part[k].name, why,
                                          sizeof why) != 0) {
                    failed++;
                    break;
                }
            }
        } else {
            int idx = command - TIKU_TRK_CMD_MOUNT_PART_BASE;

            if (idx < app->menus.mount_parts) {
                tried++;
                failed = tiku_volume_mount(app->menus.mount_part[idx],
                                               why, sizeof why) != 0;
            }
        }
        if (failed && why[0] != '\0') {
            tiku_sound_play(TIKU_SOUND_BEEP);
            tiku_alert_open(alert_model(app), TIKU_ALERT_STOP,
                                TIKU_TRK_ALERT_TAG_REFUSAL, why, "Cancel");
        } else if (tried == 0) {
            snprintf(app->status, sizeof app->status,
                     "every partition is already mounted");
        }
        app->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief copying and pasting a window layout.  @return 1 when this was the
 * command
 */
int
desktop_cmd_layout(desktop_t *app, folder_t *fl, int command)
{
    if (command == TIKU_TRK_CMD_COPY_LAYOUT) {
        (void)tiku_trk_poseview_copy_layout(fl->view);
        app->dirty = 1;
        return 1;
    }
    if (command == TIKU_TRK_CMD_PASTE_LAYOUT) {
        if (tiku_trk_poseview_paste_layout(fl->view) > 0) {
            (void)tiku_trk_poseview_save_columns(fl->view);
            snprintf(app->status, sizeof app->status, "pasted layout");
        }
        app->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief a new entry from a template, and editing the templates.  @return 1
 * when this was the command
 */
int
desktop_cmd_template(desktop_t *app, folder_t *fl, int command,
                     const char *template_path, const char **template_parent,
                     int *seeded, int *seed_x, int *seed_y)
{
    /* Where the user asked for it, worked out BEFORE the entry exists: the
     * menu that makes it is still up, and its corner is the click that
     * opened it (PVL-025, PVL-098). */
    if (template_path != NULL || command == TIKU_TRK_CMD_NEW_FOLDER) {
        (*seeded) = seed_new_entry(app, fl, &(*seed_x), &(*seed_y));
    }
    if (template_path != NULL) {
        char made[TIKU_PATH_MAX], error[200];
        const char *slash;
        int row;

        if (tiku_fs_new_from_template(app->fs, template_path, fl->path,
                                          made, sizeof made, error,
                                          sizeof error) == 0) {
            slash = strrchr(made, '/');
            land_new_entry(app, fl, made, (*seeded), (*seed_x), (*seed_y));
            (void)tiku_trk_poseview_reload(fl->view);
            row = select_named(app, fl,
                               slash != NULL ? slash + 1 : made);
            if (row >= 0) {
                begin_rename(app, fl, row);
            }
            snprintf(app->status, sizeof app->status, "created '%.150s'",
                     slash != NULL ? slash + 1 : made);
        } else {
            snprintf(app->status, sizeof app->status, "%.180s",
                     error[0] != '\0' ? error : "could not create from template");
        }
        app->dirty = 1;
        return 1;
    }
    (*template_parent) = tiku_trk_cwmenu_template_add_path(&app->menus,
                                                           command);
    if ((*template_parent) != NULL) {
        char made[TIKU_PATH_MAX], error[200];

        if (tiku_fs_new_folder(app->fs, (*template_parent), made,
                                   sizeof made, error, sizeof error) == 0) {
            const char *root = tiku_trk_cwmenu_templates_root(&app->menus);

            snprintf(app->status, sizeof app->status,
                     "created template submenu '%.130s'", strrchr(made, '/')
                     != NULL ? strrchr(made, '/') + 1 : made);
            if (root != NULL) {
                folder_t *templates = open_folder(app, (*template_parent));
                const char *slash = strrchr(made, '/');

                if (templates != NULL && slash != NULL) {
                    (void)select_named(app, templates, slash + 1);
                }
            }
        } else {
            snprintf(app->status, sizeof app->status, "%.180s",
                     error[0] != '\0' ? error :
                     "could not create template submenu");
        }
        app->dirty = 1;
        return 1;
    }
    if (command == TIKU_TRK_CMD_EDIT_TEMPLATES) {
        const char *root = tiku_trk_cwmenu_templates_root(&app->menus);

        if (root != NULL) {
            (void)open_folder(app, root);
        }
        app->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief Move to and Copy to, the destination chosen from the menu.  @return 1
 * when this was the command
 */
int
desktop_cmd_destination(desktop_t *app, folder_t *fl, int command,
                        const char **template_path)
{
    if (command >= TIKU_TRK_CMD_DEST_BASE &&
        command < TIKU_TRK_CMD_DEST_BASE + 3 * TIKU_TRK_CMD_DEST_MAX) {
        /* A picker row IS the destination (CW-035): the same act the
         * panel commits, aimed where the row points. */
        int k = (command - TIKU_TRK_CMD_DEST_BASE) / TIKU_TRK_CMD_DEST_MAX;
        int at = (command - TIKU_TRK_CMD_DEST_BASE) % TIKU_TRK_CMD_DEST_MAX;
        const char *dest = app->menus.picker_path[k][at];

        if (at >= app->menus.picker_paths[k]) {
            return 1;
        }
        if (k == 2) {
            char paths[16][TIKU_PATH_MAX];
            const char *selection[16];
            int count = folder_selection(app, fl, paths, selection, 16);

            if (count > 0) {
                (void)tiku_fs_link(app->fs, selection, count, dest, 0);
                snprintf(app->status, sizeof app->status,
                         "linking %d item%s", count, count == 1 ? "" : "s");
                tiku_trk_recent_add(app->store, TIKU_TRK_RECENT_FOLDERS,
                                    dest);
            }
        } else {
            fpanel_commit(app, fl, (k == 0) ? TIKU_TRK_CMD_MOVE_TO
                                            : TIKU_TRK_CMD_COPY_TO, dest);
        }
        return 1;
    }
    (*template_path) = tiku_trk_cwmenu_template_path(&app->menus, command);
    return 0;
}

/**
 * @brief creating and editing a query.  @return 1 when this was the command
 */
int
desktop_cmd_query(desktop_t *app, folder_t *fl, int command)
{
    if (command == TIKU_TRK_CMD_CREATE_QUERY ||
        command == TIKU_TRK_CMD_CREATE_QUERY_TPL) {
        /* The Find icon's drop chose (Q-034): the current search becomes
         * a record in the folder it was dropped on, under the first free
         * "New Query" name, as a query or as a template. */
        char dest[TIKU_PATH_MAX];
        tiku_model_t probe;
        int k;

        if (app->qdrop_dest[0] == '\0') {
            return 1;
        }
        tiku_trk_find_build(&app->find_window.model, &app->pending_query);
        app->pending_query.kind =
            (command == TIKU_TRK_CMD_CREATE_QUERY_TPL)
                ? TIKU_QREC_TEMPLATE : TIKU_QREC_QUERY;
        for (k = 0; k < 100; k++) {
            if (k == 0) {
                snprintf(dest, sizeof dest, "%s/New Query",
                         app->qdrop_dest);
            } else {
                snprintf(dest, sizeof dest, "%s/New Query %d",
                         app->qdrop_dest, k + 1);
            }
            if (app->backend->ops->stat(app->backend, dest, &probe) != 0) {
                break;
            }
        }
        fpanel_commit(app, fl, TIKU_TRK_CMD_SAVE_QUERY, dest);
        app->qdrop_dest[0] = '\0';
        return 1;
    }
    return 0;
}

/**
 * @brief About and Shut down.  @return 1 when this was the command
 */
int
desktop_cmd_system(desktop_t *app, folder_t *fl, int command)
{
    (void)fl;
    if (command == TIKU_TRK_CMD_ABOUT) {
        (void)embed_open_or_raise(app, &tiku_trk_about_app);
        return 1;
    }
    if (command == TIKU_TRK_CMD_SHUTDOWN) {
        /* The system menu's corner ritual, by the same question the
         * Deskbar asks (DB-006). */
        tiku_sound_play(TIKU_SOUND_BEEP);
        tiku_alert_open(alert_model(app), TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_SHUTDOWN,
                            tiku_str("alert.msg.shut_down"), tiku_str("alert.btn.shut_down"));
        return 1;
    }
    return 0;
}
