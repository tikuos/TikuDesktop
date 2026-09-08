/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_boardrun.c - a program running on a mounted board.
 *
 * The shell owns the mounts and the lease, so it is the shell that runs
 * a program on a board; an application asks for one and is TOLD what it
 * prints.  One at a time, because a board's shell is held while its
 * program runs and there is only one line to hold.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

#include <string.h>

#include "tiku_board_basic.h"
#include "tiku_mount.h"

/** @brief The name a board is mounted under, without the /devices/. */
static const char *
leaf_of(const char *prefix)
{
    const char *slash = strrchr(prefix, '/');

    return (slash != NULL && slash[1] != '\0') ? slash + 1 : prefix;
}

/** @brief The boards mounted under /devices, one name a line. */
int
desktop_board_names(desktop_t *app, char *out, size_t max)
{
    size_t n = 0u;
    int i, slots;

    if (app == NULL || out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    slots = tiku_mount_slots(app->backend);
    for (i = 0; i < slots; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        tiku_backend_t *b = tiku_mount_at(app->backend, i);
        const char *leaf;
        size_t need;

        if (prefix == NULL || b == NULL ||
            strncmp(prefix, "/devices/", 9) != 0) {
            continue;           /* the local files are not a board */
        }
        leaf = leaf_of(prefix);
        need = strlen(leaf) + 1u;
        if (n + need >= max) {
            break;
        }
        memcpy(out + n, leaf, strlen(leaf));
        n += strlen(leaf);
        out[n++] = '\n';
        out[n] = '\0';
    }
    return (int)n;
}

/** @brief The backend mounted as @p name, or NULL. */
static tiku_backend_t *
board_named(desktop_t *app, const char *name)
{
    int i, slots = tiku_mount_slots(app->backend);

    for (i = 0; i < slots; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        tiku_backend_t *b = tiku_mount_at(app->backend, i);

        if (prefix == NULL || b == NULL ||
            strncmp(prefix, "/devices/", 9) != 0) {
            continue;
        }
        if (name == NULL || name[0] == '\0' ||
            strcmp(leaf_of(prefix), name) == 0) {
            return b;           /* no name asked: the first board */
        }
    }
    return NULL;
}

/**
 * @brief Start @p program on the board called @p board.
 *
 * @param slot the embedded application to tell, or NULL for a session.
 * @return nonzero when the run began; one at a time, since the board's
 *         shell is held while it runs.
 */
int
desktop_board_run(desktop_t *app, struct desktop_embed *slot,
                  const char *board, const char *program)
{
    tiku_backend_t *b;

    if (app == NULL || program == NULL || app->board_run != NULL) {
        return 0;               /* one line, one program */
    }
    b = board_named(app, board);
    if (b == NULL) {
        return 0;
    }
        app->board_run = tiku_board_basic_start(b, program);
    if (app->board_run == NULL) {
        return 0;
    }
    app->board_run_slot = slot;
    app->board_run_saving = 0;
    app->board_run_asked = 0;
    (void)snprintf(app->board_run_name, sizeof app->board_run_name, "%s",
                   (board != NULL) ? board : "");
    return 1;
}

/**
 * @brief Keep @p program on the board called @p board under @p name.
 *
 * The same one-at-a-time line a run takes; told as SAVED or FAILED.
 */
int
desktop_board_save(desktop_t *app, struct desktop_embed *slot,
                   const char *board, const char *name, const char *program)
{
    tiku_backend_t *b;

    if (app == NULL || program == NULL || app->board_run != NULL) {
        return 0;
    }
    b = board_named(app, board);
    if (b == NULL) {
        return 0;
    }
    app->board_run = tiku_board_basic_save(b, name, program);
    if (app->board_run == NULL) {
        return 0;
    }
    app->board_run_slot = slot;
    app->board_run_saving = 1;
    app->board_run_asked = 0;
    (void)snprintf(app->board_run_name, sizeof app->board_run_name, "%s",
                   (name != NULL) ? name : "");
    return 1;
}

/** @brief Tell whoever asked for the run one thing about it. */
static void
tell_asker(desktop_t *app, int what, const char *text)
{
    struct desktop_embed *slot = app->board_run_slot;

    if (slot != NULL && slot->running && slot->descriptor != NULL &&
        slot->descriptor->board_said != NULL) {
        slot->descriptor->board_said(slot->state, what, text);
        return;
    }
    if (app->board_run_session >= 0) {
        desktop_board_tell(app, app->board_run_session, what, text);
    }
}

/** @brief Answer the running program's INPUT with @p text. */
int
desktop_board_input(desktop_t *app, const char *text)
{
    if (app == NULL || app->board_run == NULL) {
        return 0;
    }
    return (tiku_board_basic_input(app->board_run, text) == 0);
}

/** @brief Break the running program, as Ctrl-C at its console does. */
int
desktop_board_stop(desktop_t *app)
{
    if (app == NULL || app->board_run == NULL) {
        return 0;
    }
    tiku_board_basic_stop(app->board_run);
    return 1;
}

/** @brief Tell whoever asked for the run one line of it. */
static void
say_line(const char *line, void *ctx)
{
    tell_asker(ctx, TIKU_APP_BOARD_LINE, line);
}

/** @brief Move the run along and pass on what it printed; called on the
 *         shell's own beat. */
void
desktop_board_pump(desktop_t *app)
{
    tiku_board_basic_state_t st;
    struct desktop_embed *slot;
    int what;

    if (app == NULL || app->board_run == NULL) {
        return;
    }
    /* Short: this rides the shell's own beat, and a board's program is
     * far slower than the beat is. */
        st = tiku_board_basic_pump(app->board_run, 5, say_line, app);
    if (st == TIKU_BOARD_BASIC_ASKING) {
        /* Told once per question: the prompt the program printed, so
         * whoever answers sees what they are answering. */
        if (!app->board_run_asked) {
            app->board_run_asked = 1;
            tell_asker(app, TIKU_APP_BOARD_ASKS,
                       tiku_board_basic_prompt(app->board_run));
        }
        return;
    }
    if (st == TIKU_BOARD_BASIC_SENDING || st == TIKU_BOARD_BASIC_RUNNING) {
        app->board_run_asked = 0;
        return;
    }
    what = (st == TIKU_BOARD_BASIC_ENDED)   ? (app->board_run_saving
                                                ? TIKU_APP_BOARD_SAVED
                                                : TIKU_APP_BOARD_ENDED)
         : (st == TIKU_BOARD_BASIC_STOPPED) ? TIKU_APP_BOARD_STOPPED
                                            : TIKU_APP_BOARD_FAILED;
    slot = app->board_run_slot;
    /* Freed BEFORE the telling: an application told a run has ended may
     * ask for the next one in that very call, and the board's shell has
     * to be its own again by then. */
    tiku_board_basic_free(app->board_run);
    app->board_run = NULL;
    app->board_run_slot = NULL;
        if (slot != NULL && slot->running && slot->descriptor != NULL &&
        slot->descriptor->board_said != NULL) {
        slot->descriptor->board_said(slot->state, what,
                                     app->board_run_saving
                                         ? app->board_run_name : "");
    } else if (app->board_run_session >= 0) {
        int session = app->board_run_session;

        app->board_run_session = -1;
        desktop_board_tell(app, session, what,
                           app->board_run_saving ? app->board_run_name : "");
    }
    app->board_run_saving = 0;
}
