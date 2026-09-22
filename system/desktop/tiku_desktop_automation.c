/* tiku_desktop_automation.c -- automation.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_trk_desktop_private.h"

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
            desktop_damage_all(app);
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
                desktop_damage_all(app);
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
    desktop_damage_all(app);
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
 * @brief Run @p addon on the focused folder's selection: an external one
 *        in a process of its own, a built-in one through its command.
 *        @return 1 when it ran.
 */
int
desktop_invoke_addon(const tiku_trk_addon_t *addon, void *context)
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
