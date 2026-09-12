/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_apps.c - the installed applications: where they are found
 * beside this shell, what they are called and drawn as, and the pickers
 * built from them
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"
#include "tiku_trk_desktop_appgroups.inl"

/** @brief One destination row of a picker (CW-035). */
static void
picker_add(desktop_t *app, int k, const char *label, const char *path,
           int move_dev_ok)
{
    tiku_trk_cwmenu_t *cw = &app->menus;
    int n = cw->picker_paths[k];
    int i;

    if (path == NULL || path[0] == '\0' || n >= TIKU_TRK_CMD_DEST_MAX ||
        (k == 0 && !move_dev_ok)) {
        return;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(cw->picker_path[k][i], path) == 0) {
            return;             /* one row per destination */
        }
    }
    if (tiku_menu_add(&cw->picker[k], label,
            TIKU_TRK_CMD_DEST_BASE + k * TIKU_TRK_CMD_DEST_MAX + n,
            0, 0u, 1) >= 0) {
        snprintf(cw->picker_path[k][n], TIKU_PATH_MAX, "%s", path);
        cw->picker_paths[k] = n + 1;
    }
}

/**
 * @brief The Move to / Copy to / Create link pickers (CW-035, CW-058).
 *
 * Current folder, Recent folders, Desktop, Home, then volumes -- and Move
 * offers only destinations on the item's own device, the addLocalOnly
 * rule: a cross-device move is a copy wearing the wrong name.
 */
void
build_pickers(desktop_t *app, folder_t *fl)
{
    static const char *title[3] = { "Move to", "Copy to", "Create link" };
    const char *home = getenv("HOME");
    char recent[8][TIKU_PATH_MAX];
    char desk[TIKU_PATH_MAX];
    struct stat here;
    tiku_volumes_t vs;
    int have_here;
    int nrecent;
    int nvol;
    int k, i;

    if (fl == NULL) {
        return;
    }
    have_here = stat(fl->path, &here) == 0;
    nrecent = tiku_trk_recent_list(app->store, TIKU_TRK_RECENT_FOLDERS, 6,
                                   recent, 8);
    nvol = (tiku_volumes_scan(&vs) > 0) ? vs.n : 0;
    (void)tiku_desktop_dir(desk, sizeof desk);
    for (k = 0; k < 3; k++) {
        tiku_menu_clear(&app->menus.picker[k]);
        snprintf(app->menus.picker[k].title,
                 sizeof app->menus.picker[k].title, "%s", title[k]);
        app->menus.picker_paths[k] = 0;
        picker_add(app, k, "Current folder", fl->path, 1);
        for (i = 0; i < nrecent; i++) {
            struct stat rst;
            const char *leaf = strrchr(recent[i], '/');
            const char *rows[8];
            char label[TIKU_PATH_MAX];
            int r;

            if (stat(recent[i], &rst) != 0) {
                continue;       /* a destination that went away */
            }
            /* Two recents can share a leaf name; the one that does gains
             * the minimum distinguishing path (NeXT UIG ch8). */
            for (r = 0; r < nrecent; r++) {
                rows[r] = recent[r];
            }
            (void)tiku_attr_disambiguate(recent[i], rows, nrecent,
                                             label, sizeof label);
            if (strcmp(recent[i], desk) == 0 ||
                (home != NULL && strcmp(recent[i], home) == 0)) {
                /* Desktop and Home keep their own NAMED rows below; a
                 * recent that is one of them would shadow the name. */
                continue;
            }
            (void)leaf;
            picker_add(app, k, label, recent[i],
                       have_here && rst.st_dev == here.st_dev);
        }
        {
            struct stat dst;

            if (stat(desk, &dst) == 0) {
                picker_add(app, k, "Desktop", desk,
                           have_here && dst.st_dev == here.st_dev);
            }
            if (home != NULL && stat(home, &dst) == 0) {
                picker_add(app, k, "Home", home,
                           have_here && dst.st_dev == here.st_dev);
            }
        }
        for (i = 0; i < nvol; i++) {
            struct stat vst;

            if (!tiku_volume_shows_icon(&vs, &vs.v[i]) ||
                stat(vs.v[i].mount, &vst) != 0) {
                continue;
            }
            picker_add(app, k, vs.v[i].name, vs.v[i].mount,
                       have_here && vst.st_dev == here.st_dev);
        }
        if (k < 2 && app->menus.picker[k].count > 0) {
            /* Somewhere deeper than a row can name: the panel is the
             * navigator (AW-050), reached from inside the picker. */
            (void)tiku_menu_add_separator(&app->menus.picker[k]);
            (void)tiku_menu_add(&app->menus.picker[k],
                "Choose folder\xe2\x80\xa6",
                (k == 0) ? TIKU_TRK_CMD_MOVE_TO : TIKU_TRK_CMD_COPY_TO,
                0, 0u, 1);
        }
    }
}

/**
 * @brief Locate a companion program shipped beside this shell.
 *
 * Looked for where the build puts it, relative to THIS binary, so a tree
 * that was moved or built elsewhere still finds its own programs.
 *
 * @return nonzero when @p out names a program that exists.
 */
static int
find_companion(const char *argv0, const char *name, char *out, size_t max)
{
    static const char *const where[] = {
        "%s/%s",                        /* installed beside this shell   */
        "%s/build/%s",                  /* the tree's own build          */
        "%s/libexec/tiku/%s"
    };
    char dir[TIKU_PATH_MAX];
    char *slash;
    size_t i;
    int up;

    /* TIKU_APPS says where APPLICATIONS are installed, which is not
     * where this desktop's own companion programs live; a runner looked
     * for there would be missing on every install that separates the
     * two. */
    snprintf(dir, sizeof dir, "%s", (argv0 != NULL) ? argv0 : "");
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    /* Up the tree rather than a fixed number of dots: a development
     * build sits one directory deeper than an installed one, and a count
     * that is right for either is wrong for the other.  Climbing by
     * APPENDING "/.." rather than trimming keeps a relative argv[0]
     * working, which one trimmed down to "." cannot. */
    for (up = 0; up < 5; up++) {
        char step[TIKU_PATH_MAX];
        int k;

        snprintf(step, sizeof step, "%s", dir);
        for (k = 0; k < up; k++) {
            size_t used = strlen(step);

            if (used + 4u >= sizeof step) {
                break;
            }
            snprintf(step + used, sizeof step - used, "/..");
        }
        for (i = 0; i < sizeof where / sizeof where[0]; i++) {
            char found[TIKU_PATH_MAX];
            char real[PATH_MAX];

            snprintf(found, sizeof found, where[i], step, name);
            if (access(found, X_OK) != 0) {
                continue;
            }
            /* ABSOLUTE, because everything launched starts in the user's
             * home: a path relative to this shell's directory names
             * nothing once the child has moved there.
             *
             * Resolved through a buffer of realpath()'s own size, which
             * is PATH_MAX and not this tree's shorter path limit: handed
             * a smaller one, a fortified libc aborts the process instead
             * of truncating. */
            if (realpath(found, real) != NULL) {
                snprintf(out, max, "%s", real);
            } else {
                snprintf(out, max, "%s", found);
            }
            return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

/**
 * @brief Find the directory of installed applications, if there is one.
 *
 * Climbed from this binary the same way a companion program is, so a
 * development tree and an installed one both answer.
 */
static int
find_apps_dir(const char *argv0, char *out, size_t max)
{
    char dir[TIKU_PATH_MAX];
    char *slash;
    int up;

    if (getenv("TIKU_APPS") != NULL) {
        snprintf(out, max, "%s", getenv("TIKU_APPS"));
        return access(out, X_OK) == 0;
    }
    snprintf(dir, sizeof dir, "%s", (argv0 != NULL) ? argv0 : "");
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    for (up = 0; up < 5; up++) {
        char step[TIKU_PATH_MAX];
        char found[TIKU_PATH_MAX];
        char real[PATH_MAX];
        int k;

        snprintf(step, sizeof step, "%s", dir);
        for (k = 0; k < up; k++) {
            size_t used = strlen(step);

            if (used + 4u >= sizeof step) {
                break;
            }
            snprintf(step + used, sizeof step - used, "/..");
        }
        /* Through real[], for the reason find_companion() gives. */
        snprintf(found, sizeof found, "%s/build/apps", step);
        if (access(found, X_OK) == 0 && realpath(found, real) != NULL) {
            snprintf(out, max, "%s", real);
            return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

/** @brief The label an installed file is offered under. */
static void
app_label(const char *file, char *out, size_t max)
{
    size_t i;

    /* From the file's own name: reading it would mean loading it, and a
     * file the desktop will not load has no name to read. */
    if (strncmp(file, "tiku_", 5) == 0) {
        file += 5;
    }
    snprintf(out, max, "%s", file);
    for (i = 0; out[i] != '\0'; i++) {
        if (out[i] == '_') {
            out[i] = ' ';
        }
    }
    i = strlen(out);
    if (i > 3u && strcmp(out + i - 3, ".so") == 0) {
        out[i - 3] = '\0';
    }
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 'a' + 'A');
    }
}

/**
 * @brief The art a program's row wears, guessed from its file name.
 *
 * A hint, not a fact: nothing but the file's name is known without
 * loading it, and a program the desktop will not load has nothing else
 * to say for itself.
 */
static const char *
app_icon(const char *file)
{
    if (strstr(file, "edit") != NULL) {
        return "text_file";
    }
    return "application";
}

/**
 * @brief Offer every application installed beside this desktop.
 *
 * A file in `apps` is started in a process of its own through the
 * runner; one in `apps/trusted` is loaded into this process.  Moving the
 * file between the two is the whole of the decision.
 */
void
offer_installed(desktop_t *app, const char *argv0)
{
    char root[TIKU_PATH_MAX];
    char runner[TIKU_PATH_MAX];
    char trusted[TIKU_PATH_MAX];
    struct dirent *entry;
    DIR *dir;
    int have_runner;

    if (!find_apps_dir(argv0, root, sizeof root)) {
        return;
    }
    have_runner = find_companion(argv0, "tiku-run", runner, sizeof runner);
    if (have_runner) {
        snprintf(app->runner, sizeof app->runner, "%s", runner);
    }

    /* Discover first; startup preferences decide what runs after boot. */
    snprintf(trusted, sizeof trusted, "%s/trusted", root);
    dir = opendir(trusted);
    while (dir != NULL && (entry = readdir(dir)) != NULL) {
        char path[TIKU_PATH_MAX];
        size_t n = strlen(entry->d_name);

        if (n < 4u || strcmp(entry->d_name + n - 3, ".so") != 0) {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", trusted, entry->d_name);
        desktop_startup_offer(app, path, entry->d_name, "trusted");
    }
    if (dir != NULL) {
        (void)closedir(dir);
    }

    {
        /* Sorted, because a directory hands its entries back in whatever
         * order it likes and a menu whose rows move between runs is a
         * menu nobody can learn. */
        char name[TIKU_TRK_DB_APPS_MAX][64];
        int count = 0;
        int i, j;

        dir = opendir(root);
        while (dir != NULL && (entry = readdir(dir)) != NULL &&
               count < TIKU_TRK_DB_APPS_MAX) {
            size_t n = strlen(entry->d_name);

            if (n < 4u || n >= sizeof name[0] ||
                strcmp(entry->d_name + n - 3, ".so") != 0 || !have_runner) {
                continue;
            }
            snprintf(name[count++], sizeof name[0], "%s", entry->d_name);
        }
        if (dir != NULL) {
            (void)closedir(dir);
        }
        for (i = 1; i < count; i++) {
            char hold[64];

            snprintf(hold, sizeof hold, "%s", name[i]);
            for (j = i; j > 0 && strcmp(name[j - 1], hold) > 0; j--) {
                snprintf(name[j], sizeof name[0], "%s", name[j - 1]);
            }
            snprintf(name[j], sizeof name[0], "%s", hold);
        }
        for (i = 0; i < count; i++) {
            char path[TIKU_PATH_MAX];
            char label[TIKU_TRK_DB_LABEL_MAX];

            snprintf(path, sizeof path, "%s/%s", root, name[i]);
            app_label(name[i], label, sizeof label);
            /* Renamed before the catalogue is offered, so the Deskbar
             * row and Startup Preferences name the program the same. */
            if (!strcmp(name[i], "DeveloperCalculator.so"))
                snprintf(label, sizeof label, "Developer Calculator");
            else if (!strcmp(name[i], "Bench.so"))
                snprintf(label, sizeof label, "TikuBench");
            desktop_startup_offer(app, path, label, "apps");
            /* The row carries the FILE; how it is started is decided
             * where the pick is answered, which is the one place that
             * knows the runner's own path. */
            (void)tiku_trk_deskbar_add_app_in(app->deskbar,
                installed_group(name[i]), label, path, app_icon(name[i]));
        }
    }

    {
        /* Where a file is installed decides where it is OFFERED, the
         * rule trusted/ already keeps: apps/programming/ is the
         * Programming row's list -- the tools a person makes programs
         * with, not one more thing that happens to be installed. */
        char progdir[TIKU_PATH_MAX];
        char name[TIKU_TRK_DB_APPS_MAX][64];
        int count = 0;
        int i, j;

        snprintf(progdir, sizeof progdir, "%s/programming", root);
        dir = opendir(progdir);
        while (dir != NULL && (entry = readdir(dir)) != NULL &&
               count < TIKU_TRK_DB_APPS_MAX) {
            size_t n = strlen(entry->d_name);

            if (n < 4u || n >= sizeof name[0] ||
                strcmp(entry->d_name + n - 3, ".so") != 0 ||
                !have_runner) {
                continue;
            }
            snprintf(name[count++], sizeof name[0], "%s", entry->d_name);
        }
        if (dir != NULL) {
            (void)closedir(dir);
        }
        for (i = 1; i < count; i++) {
            char hold[64];

            snprintf(hold, sizeof hold, "%s", name[i]);
            for (j = i; j > 0 && strcmp(name[j - 1], hold) > 0; j--) {
                snprintf(name[j], sizeof name[0], "%s", name[j - 1]);
            }
            snprintf(name[j], sizeof name[0], "%s", hold);
        }
        for (i = 0; i < count; i++) {
            char path[TIKU_PATH_MAX];
            char label[TIKU_TRK_DB_LABEL_MAX];

            snprintf(path, sizeof path, "%s/%s", progdir, name[i]);
            app_label(name[i], label, sizeof label);
            desktop_startup_offer(app, path, label, "programming");
            (void)tiku_trk_deskbar_add_app_in(app->deskbar,
                TIKU_TRK_DB_GROUP_PROGRAMMING, label, path,
                app_icon(name[i]));
        }
    }
}
