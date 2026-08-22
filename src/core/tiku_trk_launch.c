/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_launch.c - the application registry, the matching, the launch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_launch.h"
#include "tiku_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/** @brief Where a preferred handler is filed: one attribute per type. */
#define PREF_NODE "\x01apps"

void
tiku_trk_launch_clear(tiku_trk_apps_t *r)
{
    if (r != NULL) {
        memset(r, 0, sizeof *r);
    }
}

int
tiku_trk_launch_add(tiku_trk_apps_t *r, const char *name, const char *cmd,
                    const char *types)
{
    tiku_trk_app_t *a;
    const char *p;

    if (r == NULL || name == NULL || name[0] == '\0' ||
        r->count >= TIKU_TRK_APP_MAX) {
        return -1;
    }
    a = &r->app[r->count];
    memset(a, 0, sizeof *a);
    snprintf(a->name, sizeof a->name, "%s", name);
    snprintf(a->cmd, sizeof a->cmd, "%s", (cmd != NULL) ? cmd : "");
    for (p = (types != NULL) ? types : ""; *p != '\0'; ) {
        const char *comma = strchr(p, ',');
        size_t len = (comma != NULL) ? (size_t)(comma - p) : strlen(p);

        while (len > 0u && (*p == ' ' || *p == '\t')) {
            p++;
            len--;
        }
        while (len > 0u && (p[len - 1u] == ' ' || p[len - 1u] == '\t')) {
            len--;
        }
        if (len == 1u && p[0] == '*') {
            a->takes_anything = 1;
        } else if (len > 0u && a->ntype < TIKU_TRK_APP_TYPES_MAX) {
            snprintf(a->type[a->ntype], TIKU_TYPE_MAX, "%.*s", (int)len,
                     p);
            a->ntype++;
        }
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    r->count++;
    return r->count - 1;
}

int
tiku_trk_launch_load(tiku_trk_apps_t *r, const char *path)
{
    FILE *f;
    char line[512];
    int n = 0;

    if (r == NULL || path == NULL) {
        return -1;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char *name = line, *cmd, *types, *nl;

        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        cmd = strchr(name, '\t');
        if (cmd == NULL) {
            continue;           /* a line that names no command is not one */
        }
        *cmd++ = '\0';
        types = strchr(cmd, '\t');
        if (types != NULL) {
            *types++ = '\0';
        }
        {
            /* A registry entry whose program has been thrown away is not
             * an application any more: listed, it would rank in Open With
             * and then fail to run (AW-041).  Only an ABSOLUTE command
             * can be checked -- a bare name is resolved by PATH at run
             * time and is not ours to second-guess. */
            struct stat st;

            if (cmd[0] == '/' && stat(cmd, &st) != 0) {
                continue;
            }
        }
        if (tiku_trk_launch_add(r, name, cmd, types) >= 0) {
            n++;
        }
    }
    (void)fclose(f);
    return n;
}

tiku_trk_relation_t
tiku_trk_launch_relation(const tiku_trk_app_t *app, const char *type)
{
    tiku_trk_relation_t best = TIKU_TRK_REL_NONE;
    int i;

    if (app == NULL) {
        return TIKU_TRK_REL_NONE;
    }
    if (type != NULL && type[0] != '\0') {
        for (i = 0; i < app->ntype; i++) {
            const char *claim = app->type[i];
            size_t len = strlen(claim);

            if (strcmp(claim, type) == 0) {
                return TIKU_TRK_REL_EXACT;   /* nothing beats it */
            }
            /* A claim ending in '/' is the SUPERTYPE claim: "text/" reads
             * every text there is, which is a weaker match than naming the
             * type itself (MA-017). */
            if (len > 0u && claim[len - 1u] == '/' &&
                strncmp(claim, type, len) == 0) {
                best = TIKU_TRK_REL_SUPERTYPE;
            }
        }
    }
    if (best == TIKU_TRK_REL_NONE && app->takes_anything) {
        best = TIKU_TRK_REL_SUPERHANDLER;
    }
    return best;
}

const char *
tiku_trk_launch_preferred(struct tiku_store *store, const char *type,
                          char *buf, size_t max)
{
    int n;

    if (buf == NULL || max == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (store == NULL || type == NULL || type[0] == '\0') {
        return buf;
    }
    n = tiku_state_read(store, PREF_NODE, type, buf, max - 1u);
    if (n < 0) {
        n = 0;
    }
    buf[n] = '\0';
    return buf;
}

int
tiku_trk_launch_set_preferred(struct tiku_store *store, const char *type,
                              const char *app_name)
{
    if (store == NULL || type == NULL || type[0] == '\0') {
        return -1;
    }
    return tiku_state_write(store, PREF_NODE, type,
                                (app_name != NULL) ? app_name : "",
                                (app_name != NULL) ? strlen(app_name) : 0u);
}

/** @brief Order: better relation first, the user's choice ahead of both. */
static int
cand_before(const tiku_trk_cand_t *a, const tiku_trk_cand_t *b)
{
    if (a->preferred != b->preferred) {
        return a->preferred > b->preferred;
    }
    if (a->rel != b->rel) {
        return (int)a->rel > (int)b->rel;
    }
    return strcmp(a->app->name, b->app->name) < 0;
}

int
tiku_trk_launch_candidates(const tiku_trk_apps_t *r,
                           const char *const *types, int ntypes,
                           struct tiku_store *store,
                           tiku_trk_cand_t *out, int max)
{
    int i, t, n = 0;

    if (r == NULL || out == NULL || max <= 0) {
        return 0;
    }
    for (i = 0; i < r->count && n < max; i++) {
        tiku_trk_relation_t best = TIKU_TRK_REL_NONE;
        int preferred = 0;

        /* The UNION: an application that can open any one of the types is
         * a candidate for the selection, and the relation it is shown with
         * is the best one it has for anything in it (AW-040, MA-080). */
        for (t = 0; t < ntypes; t++) {
            tiku_trk_relation_t rel =
                tiku_trk_launch_relation(&r->app[i], types[t]);
            char pref[TIKU_TRK_APP_NAME_MAX];

            if ((int)rel > (int)best) {
                best = rel;
            }
            (void)tiku_trk_launch_preferred(store, types[t], pref,
                                            sizeof pref);
            if (pref[0] != '\0' && strcmp(pref, r->app[i].name) == 0) {
                preferred = 1;
            }
        }
        if (best == TIKU_TRK_REL_NONE) {
            continue;
        }
        out[n].app = &r->app[i];
        out[n].rel = best;
        out[n].preferred = preferred;
        n++;
    }
    /* Insertion sort: the list is a menu's worth, and the comparison is
     * the explanation the Relation column shows. */
    for (i = 1; i < n; i++) {
        tiku_trk_cand_t hold = out[i];
        int k = i;

        while (k > 0 && cand_before(&hold, &out[k - 1])) {
            out[k] = out[k - 1];
            k--;
        }
        out[k] = hold;
    }
    return n;
}

const tiku_trk_app_t *
tiku_trk_launch_agreed(const tiku_trk_apps_t *r, const char *const *types,
                       int ntypes, struct tiku_store *store)
{
    char first[TIKU_TRK_APP_NAME_MAX];
    int t, i;

    if (r == NULL || types == NULL || ntypes <= 0 || store == NULL) {
        return NULL;
    }
    (void)tiku_trk_launch_preferred(store, types[0], first, sizeof first);
    if (first[0] == '\0') {
        return NULL;
    }
    for (t = 1; t < ntypes; t++) {
        char other[TIKU_TRK_APP_NAME_MAX];

        (void)tiku_trk_launch_preferred(store, types[t], other,
                                        sizeof other);
        if (strcmp(first, other) != 0) {
            /* Two documents wanting different applications: there is no
             * answer to preselect (AW-043). */
            return NULL;
        }
    }
    for (i = 0; i < r->count; i++) {
        if (strcmp(r->app[i].name, first) == 0) {
            return &r->app[i];
        }
    }
    return NULL;
}

const char *
tiku_trk_launch_relation_label(tiku_trk_relation_t rel)
{
    switch (rel) {
    case TIKU_TRK_REL_EXACT:        return "opens this type";
    case TIKU_TRK_REL_SUPERTYPE:    return "opens this kind of file";
    case TIKU_TRK_REL_SUPERHANDLER: return "opens anything";
    default:                        return "";
    }
}

const char *
tiku_trk_launch_reason(tiku_trk_launch_err_t err)
{
    switch (err) {
    case TIKU_TRK_LAUNCH_NO_APP:
        return "There is no application that opens this.";
    case TIKU_TRK_LAUNCH_NOTHING:
        return "There is nothing to open.";
    case TIKU_TRK_LAUNCH_IS_TRACKER:
        return "That is this Tracker, which is already running.";
    case TIKU_TRK_LAUNCH_FAILED:
        return "That application could not be started.";
    default:
        return "";
    }
}

tiku_trk_launch_err_t
tiku_trk_launch_run(const tiku_trk_app_t *app, const char *const *paths,
                    int n)
{
    const char *argv[TIKU_TRK_APP_MAX + 3];
    int argc = 0, i;
    pid_t pid;

    if (app == NULL || app->cmd[0] == '\0') {
        return TIKU_TRK_LAUNCH_NO_APP;
    }
    if (app->is_tracker) {
        /* Choosing this Tracker is not a launch: the running one takes it,
         * which is what stops a second copy of the desktop appearing
         * (AW-035). */
        return TIKU_TRK_LAUNCH_IS_TRACKER;
    }
    argv[argc++] = app->cmd;
    for (i = 0; i < n && argc < (int)(sizeof argv / sizeof argv[0]) - 1;
         i++) {
        if (paths[i] == NULL || paths[i][0] == '\0') {
            continue;
        }
        /* An application is never handed itself (AW-038). */
        if (app->path[0] != '\0' && strcmp(paths[i], app->path) == 0) {
            continue;
        }
        argv[argc++] = paths[i];
    }
    argv[argc] = NULL;
    pid = fork();
    if (pid < 0) {
        return TIKU_TRK_LAUNCH_FAILED;
    }
    if (pid == 0) {
        const char *home = getenv("HOME");

        /* Everything launched starts in the user's home directory, which
         * is what the original gives it (AW-002). */
        if (home != NULL && home[0] != '\0') {
            if (chdir(home) != 0) {
                /* Not fatal: a home that cannot be entered is not a
                 * reason to refuse to open the document. */
                (void)0;
            }
        }
        execvp(argv[0], (char *const *)(void *)argv);
        _exit(127);
    }
    /* Reaped without waiting: a launch that blocked the window it was
     * started from would be a launch the user watches (AW-036). */
    (void)waitpid(pid, NULL, WNOHANG);
    return TIKU_TRK_LAUNCH_OK;
}
