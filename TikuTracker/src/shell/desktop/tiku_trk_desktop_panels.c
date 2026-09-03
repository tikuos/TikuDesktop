/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_panels.c - the panels: Open/Save, Find and its results,
 * Select, Open-with, and the info window's own open-with list
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/** @brief Collect a directory's entries into @p out.  @return how many. */
struct collect_ctx { tiku_model_t *out; int n, max; };

void
build_open_with(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_menu_build_open_with(&app->folder_menu, fl);
    build_pickers(app, fl);
    if (fl != NULL && fl->view != NULL) {
        const char *types[TIKU_TRK_APP_TYPES_MAX];
        int n = tiku_trk_poseview_types_present(fl->view, types,
                                                TIKU_TRK_APP_TYPES_MAX);
        (void)tiku_trk_addons_build_menu(&app->addons, types, n,
                                         &app->menus.addons,
                                         TIKU_TRK_ADDON_COMMAND_BASE);
    } else {
        (void)tiku_trk_addons_build_menu(&app->addons, NULL, 0,
                                         &app->menus.addons,
                                         TIKU_TRK_ADDON_COMMAND_BASE);
    }
}

/**
 * @brief Hand @p paths to an application, and say what happened (AW-039).
 *
 * One place, because the Open-with menu, a double click on a document and
 * a drop on an application are the same act with three ways in -- and a
 * launch that reported differently depending on how it was asked for would
 * be three behaviours wearing one name.
 */
int
launch_paths(desktop_t *app, const tiku_trk_app_t *application,
             const char *const *paths, int count)
{
    return tiku_trk_launch_controller_run(&app->launcher, application, paths,
                                          count);
}

/** @brief The application a document should open in, or NULL (AW-030). */
static const tiku_trk_app_t *
handler_for(desktop_t *app, const tiku_model_t *model)
{
    return tiku_trk_launch_controller_handler(&app->launcher, model);
}

/** @brief Read every bound place, so the panel shows what IS. */
static void
panel_refresh(desktop_t *app)
{
    int i;

    for (i = 0; i < app->device_panel.form.nrow; i++) {
        tiku_form_row_t *row = &app->device_panel.form.row[i];

        if (row->bind[0] == '\0' || row->kind == TIKU_FORM_BUTTON) {
            continue;
        }
        if (tiku_backend_text(app->backend, row->bind, row->value,
                              sizeof row->value)) {
            /* Trailing blanks are how a device ends a line, not part of
             * what it said. */
            size_t n = strlen(row->value);

            while (n > 0u && (row->value[n - 1u] == '\n' ||
                              row->value[n - 1u] == '\r' ||
                              row->value[n - 1u] == ' ')) {
                row->value[--n] = '\0';
            }
            row->known = 1;
        } else {
            row->known = 0;     /* the place went quiet: say so, do not
                                 * keep showing what it last said */
        }
    }
}

static void
panel_draw(tiku_window_t *window, tiku_surface_t *surface,
           tiku_rect_t content, void *context)
{
    desktop_t *app = context;

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    tiku_form_draw(&app->device_panel.form, surface, content);
    if (app->device_panel.unknown > 0) {
        /* Said out loud rather than hidden: a panel from a device this
         * build is older than has rows it cannot draw, and a person
         * looking at a short panel deserves to know it is short. */
        const tiku_font_t *f = tiku_font_plain();
        char note[80];

        snprintf(note, sizeof note, "%d row(s) this build does not know",
                 app->device_panel.unknown);
        tiku_text(surface, f, content.x + 8,
                       content.y + content.h - 6, note,
                       tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL));
    }
}

static int
panel_event(tiku_window_t *window, const tiku_event_t *event, void *context)
{
    desktop_t *app = context;
    tiku_rect_t content = tiku_window_content(window);
    int row;

    if (event->type != TIKU_EVENT_POINTER_DOWN) {
        return 0;
    }
    row = tiku_form_at(&app->device_panel.form, content, event->x, event->y);
    if (row < 0) {
        return 0;
    }
    {
        const char *write = tiku_form_press(&app->device_panel.form, row);

        if (write != NULL) {
            /*
             * Working a control WRITES to the place it named.  That is
             * the whole of the interface: the device published where
             * its switch lives, and the desktop puts a value there.
             */
            char err[160];
            const char *bind = app->device_panel.form.row[row].bind;

            err[0] = '\0';
            if (app->backend != NULL && app->backend->ops != NULL &&
                app->backend->ops->write != NULL) {
                (void)app->backend->ops->write(app->backend, bind, write,
                                               strlen(write), err,
                                               sizeof err);
            }
            panel_refresh(app);
            app->dirty = 1;
        }
    }
    return 1;
}

/**
 * @brief Open the panel a file DESCRIBES.
 *
 * The file is read through the namespace like anything else, so a
 * description sitting on a board reached over a serial line opens the
 * same way as one in a folder here.
 */
static int
open_described_panel(desktop_t *app, const tiku_model_t *model)
{
    char text[4096];
    tiku_rect_t frame;
    int rows;

    if (!tiku_backend_text(app->backend, model->path, text, sizeof text)) {
        return 0;
    }
    rows = tiku_form_parse(&app->device_panel.form, text,
                           &app->device_panel.unknown);
    if (rows == 0 && app->device_panel.unknown == 0) {
        return 0;               /* not a panel after all */
    }
    snprintf(app->device_panel.path, sizeof app->device_panel.path, "%s",
             model->path);
    panel_refresh(app);
    frame.w = 320;
    frame.h = tiku_form_height(&app->device_panel.form) +
              tiku_window_tab_h() + 2 * TIKU_WINDOW_BORDER + 14;
    frame.x = (app->surf->w - frame.w) / 2;
    frame.y = (app->surf->h - frame.h) / 3;
    if (app->device_panel.window != NULL &&
        app->device_panel.window->open) {
        tiku_workspace_close(app->desk, app->device_panel.window);
    }
    app->device_panel.window = tiku_workspace_open(app->desk,
        app->device_panel.form.title[0] != '\0'
            ? app->device_panel.form.title : model->name,
        frame, panel_draw, app, &app->device_panel);
    if (app->device_panel.window != NULL) {
        tiku_window_set_handlers(app->device_panel.window, panel_event,
                                 NULL, NULL);
    }
    app->dirty = 1;
    return 1;
}

int
open_one(desktop_t *app, const tiku_model_t *model)
{
    tiku_trk_app_t itself;
    const char *one[1];

    if (model == NULL) {
        return 0;
    }
    if (model->name[0] != '\0') {
        const char *dot = strrchr(model->name, '.');

        if (dot != NULL && strcmp(dot, ".panel") == 0 &&
            open_described_panel(app, model)) {
            return 1;
        }
    }
    if (tiku_trk_launch_self(model, &itself)) {
        /* No documents: it is being started, not being given something
         * to open.  launch_run drops an application's own path anyway. */
        return launch_paths(app, &itself, NULL, 0);
    }
    one[0] = model->path;
    return launch_paths(app, handler_for(app, model), one, 1);
}

/**
 * @brief Fill an Info window's "Opens with" row from the registry (AW-071).
 *
 * The user's preferred handler when one is set, the best candidate when
 * not, and "Default application" when the choice is the ranking's own.
 */
void
info_opens_with_fill(desktop_t *app, tiku_trk_info_t *info)
{
    const char *pref;

    info->opens_with[0] = '\0';
    if (info == NULL || tiku_model_is_container(&info->model) ||
        (info->model.facts.perm & TIKU_P_EXEC) != 0u) {
        return;
    }
    char prefbuf[48];

    pref = tiku_trk_launch_preferred(app->store, info->model.type, prefbuf,
                                     sizeof prefbuf);
    if (pref != NULL && pref[0] != '\0') {
        snprintf(info->opens_with, sizeof info->opens_with, "%s", pref);
    } else {
        snprintf(info->opens_with, sizeof info->opens_with,
                 "Default application");
    }
}

/** @brief Walk "Opens with" to the next candidate and keep it (AW-071). */
void
info_opens_with_next(desktop_t *app, tiku_trk_info_t *info)
{
    const char *types[1];
    tiku_trk_cand_t cand[16];
    const char *current;
    int count, i, at = -1;

    types[0] = info->model.type;
    count = tiku_trk_launch_candidates(&app->apps, types, 1, app->store,
                                       cand, 16);
    if (count == 0) {
        snprintf(app->status, sizeof app->status,
                 "no application claims %s", info->model.type);
        return;
    }
    char curbuf[48];

    current = tiku_trk_launch_preferred(app->store, info->model.type,
                                        curbuf, sizeof curbuf);
    for (i = 0; i < count; i++) {
        if (current != NULL && strcmp(cand[i].app->name, current) == 0) {
            at = i;
        }
    }
    if (at + 1 < count) {
        (void)tiku_trk_launch_set_preferred(app->store, info->model.type,
                                            cand[at + 1].app->name);
        snprintf(app->status, sizeof app->status, "%s opens with %s",
                 info->model.name, cand[at + 1].app->name);
    } else {
        /* Past the last candidate the choice returns to the ranking's
         * own: "Default application". */
        (void)tiku_trk_launch_set_preferred(app->store, info->model.type,
                                            "");
        snprintf(app->status, sizeof app->status,
                 "%s opens with the default application",
                 info->model.name);
    }
    (void)tiku_store_flush(app->store);
    info_opens_with_fill(app, info);
}

/** @brief The Open With window's launch: the same one act (AW-039). */
void
open_with_window_launch(void *context, const tiku_trk_app_t *application,
                        const char *const *paths, int count)
{
    desktop_t *app = context;

    (void)launch_paths(app, application, paths, count);
}

/** @brief Open the selection with the candidate at @p at (AW-040). */
void
open_with_pick(desktop_t *app, folder_t *fl, int at)
{
    tiku_trk_launch_controller_pick(&app->launcher, fl, at);
}

static int
collect_one(const tiku_model_t *m, void *ctx)
{
    struct collect_ctx *c = ctx;

    if (c->n >= c->max) {
        return 1;
    }
    c->out[c->n++] = *m;
    return 0;
}

int
list_children(tiku_backend_t *b, const char *path, tiku_model_t *out,
              int max)
{
    struct collect_ctx c = { out, 0, max };

    if (b == NULL || b->ops->list == NULL) {
        return 0;
    }
    (void)b->ops->list(b, path, collect_one, &c);
    return c.n;
}

/**
 * @brief Adopt a result set into a window, live.
 *
 * The run is handed over rather than copied from and dropped: its lifetime is
 * the subscription's lifetime.
 */
static folder_t *
open_results(desktop_t *app, const char *title, tiku_qrun_t *run)
{
    return tiku_trk_folder_lifecycle_open_results(&app->folder_lifecycle,
                                                   title, run);
}

/** @brief Re-run a live window's query and apply what changed. */
int
refresh_results(desktop_t *app, folder_t *fl)
{
    return tiku_trk_folder_lifecycle_refresh_results(
        &app->folder_lifecycle, fl);
}

/** @brief Apply a Select controller expression to its originating folder. */
tiku_window_t *
apply_select_model(void *context, void *target,
                   const tiku_trk_select_t *selection)
{
    desktop_t *app = context;
    folder_t *folder = target;
    char error[96];
    int count;

    if (folder == NULL || folder->win == NULL || !folder->win->open) {
        snprintf(app->status, sizeof app->status,
                 "the window this was opened from has gone");
        return NULL;
    }
    count = tiku_trk_poseview_select_matching(folder->view,
        selection->expr.text, tiku_trk_select_rule(selection->rule),
        selection->ignore_case, selection->invert, error, sizeof error);
    if (count < 0) {
        /* A bad expression is a stop alert, closeable with Escape, and it
         * selects nothing (PVK-047). */
        tiku_alert_open(alert_model(app), TIKU_ALERT_STOP,
                            TIKU_TRK_ALERT_TAG_REFUSAL, error, "Cancel");
        return NULL;
    }
    snprintf(app->status, sizeof app->status, "selected %d item%s", count,
             count == 1 ? "" : "s");
    return folder->win;
}

/** @brief Open the Find panel, or bring it forward. */
void
open_panel(desktop_t *app)
{
    tiku_trk_find_window_open(&app->find_window);
}

/** @brief Run what the panel is asking and show the results. */
int
panel_search(void *context, tiku_trk_find_t *panel)
{
    desktop_t *app = context;
    tiku_query_t q;
    tiku_qrun_t *run;
    char title[TIKU_PATH_MAX];
    char scope[64];
    char record_name[TIKU_NAME_MAX];
    int replacing;

    tiku_trk_find_build(panel, &q);
    if (q.invalid) {
        snprintf(app->status, sizeof app->status,
                 "invalid query formula");
        app->dirty = 1;
        return 0;
    }
    /* The checkbox stores the choice; the actual Trash path is resolved at
     * run time because it belongs to this machine and this volume, not to a
     * portable saved-query record (Q-036). */
    (void)tiku_fs_trash_dir(app->fs, desktop_folder(app)->path,
                                q.trash_path,
                                sizeof q.trash_path);
    tiku_query_bind_scopes(&q, app->backend);
    run = tiku_query_start(app->backend, &q);
    if (run == NULL) {
        return 0;
    }
    while (!tiku_query_done(run)) {
        if (tiku_query_step(run, 16) < 0) {
            break;
        }
    }
    (void)tiku_trk_find_scope_label(panel, scope, sizeof scope);
    replacing = panel->loaded_kind == TIKU_QREC_QUERY &&
                panel->loaded_name[0] != '\0';
    record_name[0] = '\0';
    {
        /* Every run leaves a record, marked TEMPORARY: a search is a thing
         * that can be re-opened and edited, not only a window that appears
         * -- and one the user never named is swept up later rather than
         * accumulating for ever (Q-023, MA-053). */
        tiku_query_t previous;
        int i;

        q.temporary = 1;
        if (replacing) {
            snprintf(record_name, sizeof record_name, "%s",
                     panel->loaded_name);
            if (tiku_query_load(&previous, record_name, app->store) ==
                    0) {
                q.temporary = previous.temporary;
            }
            /* The old window owns the layout being inherited.  Close it
             * first so its frame and columns reach the store before the
             * replacement reads them (Q-025). */
            for (i = 0; i < app->folder_count; i++) {
                if (app->folders[i].win != NULL &&
                    app->folders[i].win->open &&
                    strcmp(app->folders[i].qname, record_name) == 0) {
                    close_window(app, app->folders[i].win);
                }
            }
        }
        /* The WALL clock, not the running one: how old a record is has to
         * mean the same thing after a restart, and it is also the date its
         * name carries. */
        q.saved_at = (int64_t)time(NULL);
        if (record_name[0] != '\0' ||
            tiku_trk_find_default_name(panel, (long)q.saved_at,
                                       record_name,
                                       sizeof record_name) > 0) {
            (void)tiku_query_save(&q, record_name, app->store);
            snprintf(app->last_query, sizeof app->last_query, "%s",
                     record_name);
        }
    }
    snprintf(title, sizeof title, "find #%d", ++app->find_serial);
    {
        folder_t *fl = open_results(app, title, run);

        if (fl != NULL) {
            snprintf(fl->qname, sizeof fl->qname, "%s", app->last_query);
            snprintf(app->status, sizeof app->status,
                     "%d result%s in %.30s -- live",
                     tiku_query_count(fl->run),
                     (tiku_query_count(fl->run) == 1) ? "" : "s", scope);
        } else {
            return 0;
        }
    }
    app->dirty = 1;
    return 1;
}

/**
 * @brief Run the typed search and open its results in their own window.
 *
 * The scope is the folder the search started from, which is what makes a
 * search from /dev mean "in /dev" without a target menu to set it in.
 */
void
run_find(desktop_t *app, folder_t *from)
{
    tiku_query_t q;
    tiku_qrun_t *run;
    char title[TIKU_PATH_MAX];
    folder_t *fl;

    tiku_query_init(&q);
    tiku_query_set_name(&q, app->find_buf);
    if (from != NULL) {
        (void)tiku_query_add_scope(&q, from->path);
    }
    (void)tiku_fs_trash_dir(app->fs,
                                (from != NULL) ? from->path
                                               : desktop_folder(app)->path,
                                q.trash_path, sizeof q.trash_path);
    run = tiku_query_start(app->backend, &q);
    if (run == NULL) {
        return;
    }
    while (!tiku_query_done(run)) {
        if (tiku_query_step(run, 8) < 0) {
            break;
        }
    }
    /* The window is named for the search, not for a place: two searches for
     * different things must not collapse onto one window. */
    snprintf(title, sizeof title, "find:%.40s#%d", app->find_buf,
             ++app->find_serial);
    fl = open_folder(app, title);
    if (fl != NULL) {
        int i, n = tiku_query_count(run);
        tiku_model_t *set = calloc((size_t)(n > 0 ? n : 1), sizeof *set);

        if (set != NULL) {
            for (i = 0; i < n; i++) {
                set[i] = *tiku_query_at(run, i);
            }
            (void)tiku_trk_poseview_set_results(fl->view, set, n);
            free(set);
        }
        snprintf(app->status, sizeof app->status,
                 "%d result%s for \"%.30s\" in %.40s", n, (n == 1) ? "" : "s",
                 app->find_buf, (from != NULL) ? from->path : "/");
    }
    tiku_query_stop(run);
    app->dirty = 1;
}

/**
 * @brief Tell whoever asked what the person chose, and forget the ask.
 *
 * Both hosts, one function: an embedded application is called through
 * its descriptor, a remote one is told over its own session.  The note
 * is cleared either way, including when there is nobody left to tell --
 * an application that went away while the panel was up must not leave
 * the shell believing it is still asking.
 */
static void
deliver_pick(desktop_t *app, const char *path)
{
    int kind = app->pending_pick.kind;
    struct desktop_embed *slot = app->pending_pick.slot;
    int session = app->pending_pick.session;
    uint32_t window = app->pending_pick.window;

    app->pending_pick.kind = 0;
    app->pending_pick.slot = NULL;
    if (path == NULL) {
        return;
    }
    if (kind == 1 && slot != NULL && slot->running &&
        slot->descriptor != NULL && slot->descriptor->picked != NULL) {
        slot->descriptor->picked(slot->state, window, path);
        app->dirty = 1;
    } else if (kind == 2) {
        tiku_msg_t *m = tiku_msg_new(TIKU_MSG_PICKED);

        if (m != NULL) {
            (void)tiku_msg_add_int32(m, "window", (int32_t)window);
            (void)tiku_msg_add_string(m, "path", path);
            (void)tiku_remote_tell(&app->remote, session, m);
            tiku_msg_free(m);
        }
    }
}

void
fpanel_commit(void *context, void *target, int command,
              const char *destination)
{
    desktop_t *app = context;
    folder_t *folder = target;
    char paths[16][TIKU_PATH_MAX];
    const char *selection[16];
    int count;

    if (command == TIKU_TRK_CMD_APP_PICK) {
        /*
         * An application's ask, answered.  Which application is not the
         * panel's business -- it carried an opaque target and this is
         * where the shell reads its own note about who was asking.
         */
        deliver_pick(app, destination);
        return;
    }
    if (command == TIKU_TRK_CMD_ADD_SCOPE && destination != NULL) {
        /* The chosen folder becomes a place and the search is scoped to
         * it at once (Q-019). */
        if (tiku_trk_find_scope_to_folder(&app->find_window.model,
                                          destination)) {
            snprintf(app->status, sizeof app->status,
                     "scoped to %.100s", destination);
        }
        return;
    }
    if (command == TIKU_TRK_CMD_SAVE_QUERY && destination != NULL) {
        /* The panel already checked the destination (AW-052); what is
         * left is the writing: the query's saved text as a real file,
         * and the same record under the file's name so the Queries menu
         * knows it too. */
        char text[TIKU_QUERY_PRED_MAX * 2];
        char err[160];
        const char *base = strrchr(destination, '/');
        int n = tiku_query_serialise(&app->pending_query, text,
                                         sizeof text);

        if (n < 0 || app->backend->ops->write == NULL ||
            app->backend->ops->write(app->backend, destination, text,
                                     (size_t)n, err, sizeof err) != 0) {
            snprintf(app->status, sizeof app->status,
                     "could not save the query");
            return;
        }
        (void)tiku_query_save(&app->pending_query,
                                  base != NULL ? base + 1 : destination,
                                  app->store);
        (void)tiku_store_flush(app->store);
        snprintf(app->status, sizeof app->status, "query saved");
        return;
    }
    if (folder == NULL || destination == NULL) {
        return;
    }
    count = folder_selection(app, folder, paths, selection, 16);
    if (count > 0) {
        if (command == TIKU_TRK_CMD_COPY_TO) {
            (void)tiku_fs_copy(app->fs, selection, count, destination,
                                   TIKU_CONFLICT_ASK);
            snprintf(app->status, sizeof app->status,
                     "copying %d item%s", count, count == 1 ? "" : "s");
        } else {
            (void)tiku_fs_move(app->fs, selection, count, destination,
                                   TIKU_CONFLICT_ASK);
            snprintf(app->status, sizeof app->status,
                     "moving %d item%s", count, count == 1 ? "" : "s");
        }
        tiku_trk_recent_add(app->store, TIKU_TRK_RECENT_FOLDERS,
                            destination);
    }
}

/** @brief Find's 'Select folders...': the panel chooses a scope (Q-019). */
void
find_select_folder(void *context)
{
    desktop_t *app = context;

    tiku_trk_panel_window_open(&app->file_panel, app,
                               TIKU_TRK_CMD_ADD_SCOPE, "/");
}

/** @brief Find's Save with nowhere attached: ask WHERE with the panel. */
void
find_save_as(void *context)
{
    desktop_t *app = context;
    const char *home = getenv("HOME");

    tiku_trk_find_build(&app->find_window.model, &app->pending_query);
    tiku_trk_panel_window_open_save(&app->file_panel, app,
                                    TIKU_TRK_CMD_SAVE_QUERY,
                                    (home != NULL && home[0] != '\0')
                                        ? home : "/",
                                    "query");
}

/** @brief Ask the reusable panel where an operation should put its items. */
void
fpanel_open(desktop_t *app, folder_t *folder, int command)
{
    if (folder != NULL) {
        tiku_trk_panel_window_open(&app->file_panel, folder, command,
                                   folder->path);
    }
}

/**
 * @brief Select the row called @p name in @p fl, and scroll it into view.
 *
 * What "open the parent WITH the item selected" means: landing on the
 * folder with nothing picked would leave the user to find it again.
 */
int
select_named(desktop_t *app, folder_t *fl, const char *name)
{
    int i, n;

    if (fl == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    n = tiku_trk_poseview_count(fl->view);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, i);

        if (p != NULL && strcmp(p->model.name, name) == 0) {
            tiku_trk_poseview_click(fl->view, i, 0u);
            /* Into view, minimally: the application changed the selection,
             * so it keeps it visible (NeXT UIG ch3) -- but a row already
             * on screen must not jump to the top. */
            tiku_trk_poseview_scroll_into_view(fl->view, i,
                tiku_trk_poseview_visible_rows(fl->view));
            app->dirty = 1;
            return i;
        }
    }
    return -1;
}

/**
 * @brief Where art dropped by hand is taken from, and the Desktop.
 *
 * Both live under the user's own configuration, both are made if they
 * are not there: a directory nobody can find is a directory nobody
 * fills, and an empty Desktop is still a Desktop.
 */
void
prepare_places(desktop_t *app)
{
    const char *home = getenv("HOME");
    char path[TIKU_PATH_MAX];
    const char *icons = getenv("TIKU_ICONS");

    (void)app;
    if (icons == NULL || icons[0] == '\0') {
        if (home == NULL || home[0] == '\0') {
            return;
        }
        snprintf(path, sizeof path, "%s/.config/tracker/icons", home);
        icons = path;
    }
    (void)tiku_state_mkparents(icons);
    (void)mkdir(icons, 0755);
    /* Whatever is in there wins over what was baked in, by name, so a
     * device can say what its own folders and programs look like. */
    (void)tiku_icons_load_dir(icons);

    {
        /* This Tracker's own Desktop, not the login session's: what a
         * device shows is the device's, and a host desktop full of
         * somebody's work is not it. */
        char desktop[TIKU_PATH_MAX];

        (void)tiku_desktop_dir(desktop, sizeof desktop);
        (void)tiku_state_mkparents(desktop);
        (void)mkdir(desktop, 0755);
    }
}
