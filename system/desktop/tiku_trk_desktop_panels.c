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

/**
 * @brief Rebuild the Open With rows, the pickers and the add-ons menu
 *        for @p fl's selection (for none when @p fl is NULL).
 */
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

/** @brief The bind of @p row as a path of this namespace. */
const char *
bind_path(const tiku_trk_described_t *p, const tiku_form_row_t *row,
          char *buf, size_t max)
{
    if (p->root[0] != '\0' && strncmp(row->bind, "/gui/", 5) == 0) {
        snprintf(buf, max, "%.*s%s", (int)(max / 2), p->root, row->bind);
        return buf;
    }
    return row->bind;
}

/** @brief Read every bound place, so the panel shows what IS. */
static void
panel_refresh(tiku_trk_described_t *p)
{
    desktop_t *app = p->app;
    char path[TIKU_PATH_MAX];
    int i, asked = 0, answered = 0;

    for (i = 0; i < p->form.nrow; i++) {
        tiku_form_row_t *row = &p->form.row[i];

        if (row->bind[0] == '\0' || row->kind == TIKU_FORM_BUTTON) {
            continue;
        }
        asked++;
        if (tiku_backend_text(app->backend,
                              bind_path(p, row, path, sizeof path),
                              row->value, sizeof row->value)) {
            answered++;
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
    /* Every place quiet at once is the board, not a row: said in the
     * status line, and the rows are drawn empty rather than stale. */
        panel_refreshed(p, asked, answered);
}

/**
 * @brief The end of a refresh, however the values came: every place
 *        quiet at once is the board, not a row -- said in the status
 *        line, and the rows are drawn empty rather than stale.
 */
void
panel_refreshed(tiku_trk_described_t *p, int asked, int answered)
{
    p->quiet = (asked > 0 && answered == 0);
    if (p->quiet) {
        snprintf(p->status, sizeof p->status, "%s", "not answering");
    } else if (strcmp(p->status, "not answering") == 0) {
        p->status[0] = '\0';
    }
}

/**
 * @brief Paint a described panel: its form, the last refusal, and a note
 *        when the device sent rows this build cannot draw.
 */
static void
panel_draw(tiku_window_t *window, tiku_surface_t *surface,
           tiku_rect_t content, void *context)
{
    tiku_trk_described_t *p = context;
    const tiku_font_t *f = tiku_font_plain();
    char note[112];

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    tiku_form_draw(&p->form, surface, content);
    note[0] = '\0';
    if (p->status[0] != '\0') {
        snprintf(note, sizeof note, "%s", p->status);
    } else if (p->unknown > 0) {
        /* Said out loud rather than hidden: a panel from a device this
         * build is older than has rows it cannot draw, and a person
         * looking at a short panel deserves to know it is short. */
        snprintf(note, sizeof note, "%d row(s) this build does not know",
                 p->unknown);
    }
    if (note[0] != '\0') {
        tiku_text(surface, f, content.x + 8, content.y + content.h - 6,
                  note, tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL));
    }
}

/* Used before it is defined. */
static int panel_track(tiku_trk_described_t *p, tiku_window_t *window,
                       const tiku_event_t *event);
/* End of the forward declarations. */

/**
 * @brief A press on a described panel: the row under it is pressed and
 *        what it writes goes to the device; a refusal is shown, not
 *        dropped.  @return 1 when a row took it.
 */
static int
panel_event(tiku_window_t *window, const tiku_event_t *event, void *context)
{
    tiku_trk_described_t *p = context;
    desktop_t *app = p->app;
    tiku_rect_t content = tiku_window_content(window);
    const char *write;
    int row;

    if (p->tracking) {
        return panel_track(p, window, event);
    }
    if (event->type != TIKU_EVENT_POINTER_DOWN) {
        return 0;
    }
    row = tiku_form_at(&p->form, content, event->x, event->y);
    if (row < 0) {
        return 0;
    }
    if (p->form.row[row].kind == TIKU_FORM_ENTRY) {
        /* The caret, and nothing written until Return. */
        tiku_form_focus(&p->form, row);
        desktop_damage_all(app);
        return 1;
    }
    if (tiku_form_editing(&p->form) >= 0) {
        tiku_form_focus(&p->form, -1);      /* a press elsewhere lets go */
    }
    if (p->form.row[row].kind == TIKU_FORM_SLIDER) {
        /* The knob is taken: it follows the pointer, and the value is
         * written once, when the hand lets go. */
        (void)tiku_form_press_at(&p->form, content, row, event->x);
        p->tracking = 1;
        p->pending = row;
        desktop_damage_all(app);
        return 1;
    }
    write = tiku_form_press(&p->form, row);
    if (write != NULL) {
        /*
         * Working a control WRITES to the place it named.  That is the
         * whole of the interface: the device published where its switch
         * lives, and the desktop puts a value there -- on the next idle
         * turn rather than under the pointer, so a slow cable never holds
         * the press.
         */
        p->pending = row;
        desktop_damage_all(app);
    }
    return 1;
}

/** @brief The slider's knob follows a move; the release writes it. */
static int
panel_track(tiku_trk_described_t *p, tiku_window_t *window,
            const tiku_event_t *event)
{
    tiku_rect_t content = tiku_window_content(window);
    int row = p->pending;

    if (row < 0 || row >= p->form.nrow) {
        p->tracking = 0;
        return 0;
    }
    (void)tiku_form_press_at(&p->form, content, row, event->x);
    if (event->type == TIKU_EVENT_POINTER_UP) {
        p->tracking = 0;        /* pending stays: the pump writes it */
    }
    desktop_damage_all(p->app);
    return 1;
}

/**
 * @brief Write a row's value to the place it is bound to, and read the
 *        panel back -- on a worker, since a board's write has a ceiling
 *        of seconds and the loop must not stand for it.
 */
static void
panel_write(tiku_trk_described_t *p, int row)
{
    if (!desktop_wire_panel_write(p->app, p, row)) {
        snprintf(p->status, sizeof p->status, "%s: %s",
                 p->form.row[row].label, "nothing serves it");
        tiku_sound_play(TIKU_SOUND_BEEP);
        desktop_damage_all(p->app);
    }
}

int
panel_key(desktop_t *app, unsigned key, char ch)
{
    tiku_trk_described_t *p =
        panel_of_window(app, tiku_workspace_focused(app->desk));
    int row, took;

    if (p == NULL) {
        return 0;
    }
    row = tiku_form_editing(&p->form);
    if (row < 0) {
        return 0;
    }
    took = tiku_form_key(&p->form, key, ch);
    if (took == 2) {
        p->pending = row;
    }
    desktop_damage_all(app);
    return took != 0;
}

void
panels_pump(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        tiku_trk_described_t *p = &app->panels[i];

        if (p->window != NULL && p->window->open && p->pending >= 0 &&
            !p->tracking) {
            int row = p->pending;

            p->pending = -1;
            panel_write(p, row);
            return;             /* one write a turn keeps the loop answering */
        }
    }
}

tiku_trk_described_t *
panel_tracking(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        if (app->panels[i].tracking && app->panels[i].window != NULL &&
            app->panels[i].window->open) {
            return &app->panels[i];
        }
    }
    return NULL;
}

tiku_trk_described_t *
panel_of_window(desktop_t *app, const tiku_window_t *w)
{
    int i;

    for (i = 0; w != NULL && i < TIKU_TRK_PANELS_MAX; i++) {
        if (app->panels[i].window == w && w->open) {
            return &app->panels[i];
        }
    }
    return NULL;
}

/** @brief The open panel describing @p path, or NULL. */
static tiku_trk_described_t *
panel_of_path(desktop_t *app, const char *path)
{
    int i;

    for (i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        tiku_trk_described_t *p = &app->panels[i];

        if (p->window != NULL && p->window->open &&
            strcmp(p->path, path) == 0) {
            return p;
        }
    }
    return NULL;
}

/** @brief A free slot, else the first: one panel gives way for another. */
static tiku_trk_described_t *
panel_slot(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        tiku_trk_described_t *p = &app->panels[i];

        if (p->window == NULL || !p->window->open) {
            return p;
        }
    }
    return &app->panels[0];
}

/**
 * @brief Where a board's binds hang off: for .../gui/<form>/panel the
 *        part before /gui/, else "" (a .panel file binds absolutely).
 */
static void
root_of(const char *path, char *root, size_t max)
{
    const char *gui = strstr(path, "/gui/");
    size_t n = strlen(path);

    root[0] = '\0';
    if (gui != NULL && n >= 6 && strcmp(path + n - 6, "/panel") == 0) {
        snprintf(root, max, "%.*s", (int)(gui - path), path);
    }
}

int
open_panel_path(desktop_t *app, const char *path)
{
    char text[4096];
    tiku_rect_t frame;
    tiku_trk_described_t *p;
    const char *name;
    int rows, unknown;
    tiku_form_t form;

    if (path == NULL ||
        !tiku_backend_text(app->backend, path, text, sizeof text)) {
        return 0;
    }
    rows = tiku_form_parse(&form, text, &unknown);
    if (rows == 0 && unknown == 0) {
        return 0;               /* not a panel after all */
    }
    p = panel_of_path(app, path);
    if (p != NULL) {
        /* Open already: it is brought forward with what it says now. */
        p->form = form;
        p->unknown = unknown;
        panel_refresh(p);
        tiku_workspace_activate(app->desk, p->window);
        desktop_damage_all(app);
        return 1;
    }
    p = panel_slot(app);
    if (p->window != NULL && p->window->open) {
        tiku_workspace_close(app->desk, p->window);
    }
    memset(p, 0, sizeof *p);
    p->app = app;
    p->form = form;
    p->unknown = unknown;
    p->pending = -1;
    snprintf(p->path, sizeof p->path, "%s", path);
    root_of(path, p->root, sizeof p->root);
    panel_refresh(p);
    name = strrchr(path, '/');
    name = (name != NULL) ? name + 1 : path;
    frame.w = 320;
    frame.h = tiku_form_height(&p->form) + tiku_window_tab_h() +
              2 * TIKU_WINDOW_BORDER + 14;
    frame.x = (app->surf->w - frame.w) / 2 +
              (int)(p - app->panels) * 24;
    frame.y = (app->surf->h - frame.h) / 3 + (int)(p - app->panels) * 24;
    p->window = tiku_workspace_open(app->desk,
        p->form.title[0] != '\0' ? p->form.title : name, frame,
        panel_draw, p, p);
    if (p->window != NULL) {
        tiku_window_set_handlers(p->window, panel_event, NULL, NULL);
    }
    desktop_damage_all(app);
    return p->window != NULL;
}

/** @brief One listing callback: open the panel of each form under gui/. */
static int
open_form_entry(const tiku_model_t *m, void *ctx)
{
    desktop_t *app = ctx;
    char path[TIKU_PATH_MAX];

    if (m == NULL || m->name[0] == '.' || !tiku_model_is_container(m)) {
        return 0;
    }
    snprintf(path, sizeof path, "%.*s/panel", (int)sizeof path - 8, m->path);
    (void)open_panel_path(app, path);
    return 0;
}

/**
 * @brief Keep one record per mount, with the identity of its gui/ node
 *        looked up once; a mount that is not a board is marked so.
 */
static void
boards_refresh(desktop_t *app)
{
    int slots = tiku_mount_slots(app->backend), i;

    for (i = 0; i < slots && i < TIKU_MOUNT_MAX; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        tiku_model_t m;
        char dir[TIKU_PATH_MAX];

        if (prefix == NULL || (prefix[0] == '/' && prefix[1] == '\0')) {
            app->boards[i].root[0] = '\0';
            continue;
        }
        if (strcmp(app->boards[i].root, prefix) != 0) {
            memset(&app->boards[i], 0, sizeof app->boards[i]);
            snprintf(app->boards[i].root, TIKU_PATH_MAX, "%s", prefix);
        }
        if (app->boards[i].gui_id != 0 || app->backend->ops->stat == NULL) {
            continue;
        }
        snprintf(dir, sizeof dir, "%s/gui", prefix);
        app->boards[i].gui_id =
            (app->backend->ops->stat(app->backend, dir, &m) == 0 &&
             m.node_id != 0) ? m.node_id : ~(uint64_t)0;
    }
}

/**
 * @brief Board @p b rang its gui/: re-read its open panels, or open its
 *        forms the first time one is seen.  @return how many changed.
 */
static int
panels_apply_board(desktop_t *app, int b)
{
    const char *root = app->boards[b].root;
    int changed = 0, any = 0, i;

    if (root[0] == '\0') {
        return 0;
    }
    for (i = 0; i < TIKU_TRK_PANELS_MAX; i++) {
        tiku_trk_described_t *p = &app->panels[i];

        if (p->window != NULL && p->window->open &&
            strcmp(p->root, root) == 0) {
            panel_refresh(p);
            any = 1;
            changed++;
        }
    }
    if (!any && !app->boards[b].announced &&
        app->backend->ops->list != NULL) {
        char dir[TIKU_PATH_MAX];

        app->boards[b].announced = 1;
        snprintf(dir, sizeof dir, "%s/gui", root);
        (void)app->backend->ops->list(app->backend, dir, open_form_entry,
                                      app);
        changed++;
    }
    return changed;
}

/** @brief Apply a record whose id names a board's gui/ node. */
static int
panels_apply(desktop_t *app, const tiku_model_change_t *c)
{
    int b;

    for (b = 0; b < TIKU_MOUNT_MAX; b++) {
        if (app->boards[b].root[0] != '\0' &&
            app->boards[b].gui_id == c->id) {
            return panels_apply_board(app, b);
        }
    }
    return 0;
}

int
panels_poll(desktop_t *app, const tiku_model_change_t *ch, int nch)
{
    int changed = 0, k, b;

    boards_refresh(app);
    for (k = 0; k < nch; k++) {
        changed += panels_apply(app, &ch[k]);
    }
    if (nch >= 0) {
        return changed;
    }
    /*
     * The namespace as a whole could not say (a root with no ring beside
     * a board that has one), so nothing above drained the board: its
     * ring is asked here, board by board, and the records are the
     * panels' alone since every view is polling.
     */
    for (b = 0; b < TIKU_MOUNT_MAX; b++) {
        /* The router could not tell: each board's own answer, as the
         * worker last brought it, says whether its panels moved. */
        if (app->boards[b].root[0] == '\0' ||
            app->boards[b].gui_id == ~(uint64_t)0) {
            continue;
        }
        if (desktop_wire_changed(app, b)) {
            changed += panels_apply_board(app, b);
        }
    }
    return changed;
}

/**
 * @brief Open the panel a file DESCRIBES: a .panel file, or a board's
 *        gui/<form>/panel node.
 */
static int
open_described_panel(desktop_t *app, const tiku_model_t *model)
{
    const char *dot = strrchr(model->name, '.');

    if (dot != NULL && strcmp(dot, ".panel") == 0) {
        return open_panel_path(app, model->path);
    }
    if (strcmp(model->name, "panel") == 0 &&
        strstr(model->path, "/gui/") != NULL) {
        return open_panel_path(app, model->path);
    }
    return 0;
}

/**
 * @brief Open @p model: a .panel file as a described panel, anything
 *        else through the launcher.  @return 1 when something opened.
 */
int
open_one(desktop_t *app, const tiku_model_t *model)
{
    tiku_trk_app_t itself;
    const char *one[1];

    if (model == NULL) {
        return 0;
    }
    if (model->name[0] != '\0' && open_described_panel(app, model)) {
        return 1;
    }
    one[0] = model->path;
    {
        /* A document opens for editing even with its executable bit set:
         * the type database says which types are documents. */
        const tiku_trk_typerec_t *rec = tiku_trk_types_find(&app->types,
                                                            model->type);

        if (rec != NULL && rec->document) {
            return launch_paths(app, handler_for(app, model), one, 1);
        }
    }
    {
        /* An installed application, or a link to one: the runner starts
         * it, as the Deskbar's row would; its module is not a program
         * of its own to exec. */
        const tiku_trk_app_t *installed = desktop_app_at(app, model->path);

        if (installed != NULL) {
            return tiku_trk_launch_run(installed, NULL, 0) ==
                   TIKU_TRK_LAUNCH_OK;
        }
    }
    if (tiku_trk_launch_self(model, &itself)) {
        /* No documents: it is being started, not being given something
         * to open.  launch_run drops an application's own path anyway. */
        return launch_paths(app, &itself, NULL, 0);
    }
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
    /* The ranking's own order, with no store to read the current choice
     * from: sorted with the choice, the chosen application leads the list
     * and the walk alternates between the first two, reaching neither the
     * rest of them nor the default again. */
    count = tiku_trk_launch_candidates(&app->apps, types, 1, NULL,
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

/** @brief One listed entry copied into the collector; full stops the list. */
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

/**
 * @brief The entries of @p path from backend @p b, at most @p max, into
 *        @p out.  @return how many.
 */
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
        tiku_sound_play(TIKU_SOUND_BEEP);
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
        desktop_damage_all(app);
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
    /* Not walked here.  A search whose scope is a volume is minutes of
     * work on a real disk, and this is the thread that draws: the window
     * opens on a run that has found nothing yet and fills as the turn
     * steps it (tiku_trk_folder_lifecycle_step_results). */
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
            snprintf(fl->qscope, sizeof fl->qscope, "%s", scope);
            /* What is true at this instant: nothing has been looked at
             * yet.  The turn writes this line again as the walk finds
             * things and once more when it ends. */
            snprintf(app->status, sizeof app->status,
                     "searching %.30s -- 0 results so far", scope);
        } else {
            return 0;
        }
    }
    desktop_damage_all(app);
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
    desktop_damage_all(app);
}

/** @brief Map a host directory into the desktop namespace, never a device. */
static int
local_picker_start(desktop_t *app, const char *path, char *out, size_t max)
{
    if (!path || path[0] != '/') path = "/";
    if (!tiku_mount_is(app->backend)) {
        if (!app->backend || !app->backend->ops ||
            strcmp(app->backend->ops->name, "posix")) return 0;
        return snprintf(out, max, "%s", path) < (int)max;
    }
    for (int i = 0; i < tiku_mount_slots(app->backend); i++) {
        tiku_backend_t *b = tiku_mount_at(app->backend, i);
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        if (!b || !b->ops || strcmp(b->ops->name, "posix") || !prefix)
            continue;
        return snprintf(out, max, "%s%s",
            !strcmp(prefix, "/") ? "" : prefix, path) < (int)max;
    }
    return 0;
}

/** @brief Whether the question up is one @p window of @p session put. */
static int
pick_is_from(const desktop_t *app, int session, uint32_t window)
{
    return app->pending_pick.kind == 2 &&
           app->pending_pick.session == session &&
           app->pending_pick.generation ==
               app->remote.session[session].generation &&
           app->pending_pick.window == window;
}

/**
 * @brief Remember who asked; never answer a reused session.
 *
 * One question at a time.  The window whose question is up may put a
 * newer one, which replaces it: an application keeps one note of what it
 * asked last, and an answer to the older question would be read as the
 * newer one's.  Any other ask is refused while the panel is taken.
 */
void
desktop_remote_pick(desktop_t *app, int session, const tiku_msg_t *msg)
{
    int32_t window = 0, mode = TIKU_APP_PICK_OPEN;
    const char *start = tiku_msg_find_string(msg, "start", 0);
    const char *name = tiku_msg_find_string(msg, "name", 0);
    char mapped[TIKU_PATH_MAX];

    if (session < 0 || session >= TIKU_REMOTE_SESSIONS ||
        !app->remote.session[session].used)
        return;
    (void)tiku_msg_find_int32(msg, "window", 0, &window);
    (void)tiku_msg_find_int32(msg, "mode", 0, &mode);
    if (app->pending_pick.kind && !pick_is_from(app, session, (uint32_t)window))
        return;
    if (mode < TIKU_APP_PICK_OPEN || mode > TIKU_APP_PICK_LOCAL_FOLDER)
        return;
    if (mode == TIKU_APP_PICK_LOCAL_OPEN ||
        mode == TIKU_APP_PICK_LOCAL_FOLDER) {
        if (!local_picker_start(app, start, mapped, sizeof mapped)) {
            snprintf(app->status, sizeof app->status,
                     "No local filesystem is available for this application");
            desktop_damage_all(app);
            return;
        }
        start = mapped;
    }
    app->pending_pick.kind = 2;
    app->pending_pick.session = session;
    app->pending_pick.generation = app->remote.session[session].generation;
    app->pending_pick.slot = NULL;
    app->pending_pick.window = (uint32_t)window;
    app->pending_pick.local = mode == TIKU_APP_PICK_LOCAL_OPEN ||
                              mode == TIKU_APP_PICK_LOCAL_FOLDER;
    if (mode == TIKU_APP_PICK_SAVE)
        tiku_trk_panel_window_open_save(&app->file_panel,
            &app->pending_pick, TIKU_TRK_CMD_APP_PICK, start, name);
    else if (mode == TIKU_APP_PICK_LOCAL_FOLDER)  /* the panel's own way */
        tiku_trk_panel_window_open(&app->file_panel, &app->pending_pick,
                                   TIKU_TRK_CMD_APP_PICK, start);
    else
        tiku_trk_panel_window_open_file(&app->file_panel,
            &app->pending_pick, TIKU_TRK_CMD_APP_PICK, start, name);
    desktop_damage_all(app);
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
    uint64_t generation = app->pending_pick.generation;
    int local_only = app->pending_pick.local;
    char local[TIKU_PATH_MAX];

    app->pending_pick.kind = 0;
    app->pending_pick.slot = NULL;
    if (path == NULL) {
        return;
    }
    if (local_only) {
        tiku_backend_t *b = tiku_backend_serving(app->backend, path,
                                                local, sizeof local);
        if (!b || !b->ops || strcmp(b->ops->name, "posix")) {
            snprintf(app->status, sizeof app->status,
                     "This app opens local files; copy the device file first");
            desktop_damage_all(app);
            return;
        }
        path = local;
    }
    if (kind == 1 && slot != NULL && slot->running &&
        slot->descriptor != NULL && slot->descriptor->picked != NULL) {
        slot->descriptor->picked(slot->state, window, path);
        desktop_damage_all(app);
    } else if (kind == 2 && session >= 0 && session < TIKU_REMOTE_SESSIONS &&
               app->remote.session[session].used &&
               app->remote.session[session].generation == generation) {
        tiku_msg_t *m = tiku_msg_new(TIKU_MSG_PICKED);

        if (m != NULL) {
            (void)tiku_msg_add_int32(m, "window", (int32_t)window);
            (void)tiku_msg_add_string(m, "path", path);
            (void)tiku_remote_tell(&app->remote, session, m);
            tiku_msg_free(m);
        }
    }
}

/**
 * @brief The file panel's answer: an application's pick delivered, a
 *        scope added to the search, or the selection moved or copied to
 *        @p destination on behalf of @p target's folder.
 */
void
fpanel_commit(void *context, void *target, int command,
              const char *destination)
{
    desktop_t *app = context;
    folder_t *folder = target;
    char paths[16][TIKU_PATH_MAX];
    const char *selection[16];
    int count;

    if (command == TIKU_TRK_CMD_READER_OPEN) {
        if (destination != NULL) (void)desktop_reader_file(app, destination);
        return;
    }
    if (command == TIKU_TRK_CMD_IMAGE_OPEN) {
        if (destination != NULL) (void)desktop_image_file(app, destination);
        return;
    }
    if (command == TIKU_TRK_CMD_APP_PICK) {
        /*
         * An application's ask, answered.  Which application is not the
         * panel's business -- it carried an opaque target and this is
         * where the shell reads its own note about who was asking.
         */
        deliver_pick(app, destination);
        return;
    }
    if (command == TIKU_TRK_CMD_CHOOSE_SOUND && destination != NULL) {
        tiku_trk_sounds_window_chosen(&app->sounds_window, app->sound_pick,
                                      destination);
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

/** @brief The Sounds window's "…": any file at all for @p event. */
void
sound_choose(void *context, tiku_sound_event_t event)
{
    desktop_t *app = context;
    const char *home = getenv("HOME");

    app->sound_pick = event;
    tiku_trk_panel_window_open_file(&app->file_panel, &app->sounds_window,
                                    TIKU_TRK_CMD_CHOOSE_SOUND,
                                    (home != NULL && home[0] != '\0')
                                        ? home : "/",
                                    NULL);
}

/** @brief Resolve local documents before handing them to the reader process. */
int desktop_reader_file(desktop_t *app, const char *path)
{
    char local[TIKU_PATH_MAX];
    tiku_backend_t *b = tiku_backend_serving(app->backend, path, local,
                                            sizeof local);
    if (!b || !b->ops || strcmp(b->ops->name, "posix")) return 0;
    return desktop_launch_app(app, "reader", local);
}

/** @brief Resolve namespace paths before handing files to local processes. */
int desktop_document_open(void *ctx, const tiku_trk_app_t *application,
                           const char *path)
{
    desktop_t *app = ctx;
    char local[TIKU_PATH_MAX];
    struct stat sb;
    tiku_backend_t *b = tiku_backend_serving(app->backend, path,
                                            local, sizeof local);
    if (!b || !b->ops || strcmp(b->ops->name, "posix") ||
        local[0] != '/' || access(local, R_OK) ||
        access(application->cmd, X_OK) || access(application->module, R_OK) ||
        stat(local, &sb) || !S_ISREG(sb.st_mode))
        return 0;
    const char *paths[] = {local};
    return tiku_trk_launch_run(application, paths, 1) == TIKU_TRK_LAUNCH_OK;
}

/** @brief A host filename must not be interpreted against a device root. */
int desktop_document_local(desktop_t *app, const char *path)
{
    if (!path || path[0] != '/' || strlen(path) >= TIKU_PATH_MAX) return 0;
    char mapped[TIKU_PATH_MAX];
    if (!tiku_mount_is(app->backend)) {
        if (!app->backend || strcmp(app->backend->ops->name, "posix"))
            return 0;
        return desktop_app_open_path(app, path);
    }
    for (int i = 0; i < tiku_mount_slots(app->backend); i++) {
        tiku_backend_t *b = tiku_mount_at(app->backend, i);
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        if (!b || !b->ops || strcmp(b->ops->name, "posix") || !prefix)
            continue;
        if (!strcmp(prefix, "/")) return desktop_app_open_path(app, path);
        if (snprintf(mapped, sizeof mapped, "%s%s", prefix, path) >=
            (int)sizeof mapped) return 0;
        return desktop_app_open_path(app, mapped);
    }
    return 0;
}

/** @brief Legacy image routing delegates to the installed document app. */
int desktop_image_file(desktop_t *app, const char *path)
{
    char local[TIKU_PATH_MAX];
    tiku_backend_t *b = tiku_backend_serving(app->backend, path, local,
                                            sizeof local);
    if (!b || !b->ops || strcmp(b->ops->name, "posix")) return 0;
    return desktop_launch_app(app, "image", local);
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
            desktop_damage_all(app);
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
