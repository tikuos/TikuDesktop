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

#include <stdio.h>
#include <stdio.h>
#include <string.h>

#include "tiku_board_basic.h"
#include "tiku_mount.h"
#include "tiku_channel.h"
#include "tiku_tx.h"

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

/** @brief How many boards are mounted. */
static int
board_count(desktop_t *app)
{
    int i, slots = tiku_mount_slots(app->backend), n = 0;

    for (i = 0; i < slots; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);

        if (prefix != NULL && tiku_mount_at(app->backend, i) != NULL &&
            strncmp(prefix, "/devices/", 9) == 0) {
            n++;
        }
    }
    return n;
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

/** @brief The channel of the board mounted at @p prefix, or NULL. */
static tiku_channel_t *
channel_of_board(desktop_t *app, const char *prefix)
{
    int k;

    for (k = 0; k < TIKU_MOUNT_MAX; k++) {
        if (app->channels[k] != NULL &&
            strcmp(app->channel_board[k], prefix) == 0) {
            return app->channels[k];
        }
    }
    return NULL;
}

/** @brief To the board's interpreter, on its own stream. */
static int
stream_send(void *ctx, const void *bytes, size_t len)
{
    return tiku_channel_basic_send(ctx, bytes, len);
}

/** @brief What a program on it has printed. */
static int
stream_recv(void *ctx, void *buf, size_t max)
{
    return tiku_channel_basic_recv(ctx, buf, max);
}

/** @brief Move the channel the stream rides on. */
static void
stream_pump(void *ctx)
{
    (void)tiku_channel_pump(ctx);
}

/**
 * @brief The run, over the board's own interpreter stream when it has
 *        one and through its shell when it has not.
 *
 * A board carrying the overlay answers on a stream beside its console,
 * so its namespace stays live while the program runs; one without is
 * held as before, which is the only way to reach an interpreter that
 * has no other door.
 */
static tiku_board_basic_t *
run_begin(desktop_t *app, const char *board, tiku_backend_t *b,
          const char *program)
{
    char prefix[TIKU_PATH_MAX];
    tiku_channel_t *ch;

    (void)snprintf(prefix, sizeof prefix, "/devices/%s",
                   (board != NULL && board[0] != '\0') ? board : "");
    ch = channel_of_board(app, prefix);
    if (ch != NULL) {
        tiku_board_basic_stream_t stream;

        stream.send = stream_send;
        stream.recv = stream_recv;
        stream.pump = stream_pump;
        stream.ctx = ch;
        return tiku_board_basic_start_stream(&stream, program);
    }
    return tiku_board_basic_start(b, program);
}

/**
  * @brief The boards, one a line, each "name<TAB>port<TAB>part".
 *
 * Told rather than asked for: an application drawing a menu needs both,
 * and neither may cost it a wait on the wire.
 */
int
desktop_board_list(desktop_t *app, char *out, size_t max)
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
                char port[128];
        char row[320];
        const tiku_identity_t *id;
        size_t need;

        if (prefix == NULL || b == NULL ||
            strncmp(prefix, "/devices/", 9) != 0) {
            continue;
        }
                port[0] = '\0';
        (void)tiku_backend_tiku_port(b, port, sizeof port);
        id = tiku_backend_tiku_identity(b);
        (void)snprintf(row, sizeof row, "%s\t%s\t%s\n", leaf_of(prefix),
                       port, (id != NULL) ? id->mcu : "");
        need = strlen(row);
        if (n + need >= max) {
            break;
        }
        memcpy(out + n, row, need);
        n += need;
        out[n] = '\0';
    }
    return (int)n;
}

/** @brief The channel slot of the board mounted at @p prefix, or -1. */
static int
channel_slot_of(desktop_t *app, const char *prefix)
{
    int k;

    for (k = 0; k < TIKU_MOUNT_MAX; k++) {
        if (app->channels[k] != NULL &&
            strcmp(app->channel_board[k], prefix) == 0) {
            return k;
        }
    }
    return -1;
}

/** @brief The port @p name is on ("/dev/ttyACM0"), into @p out. */
int
desktop_board_port(desktop_t *app, const char *name, char *out, size_t max)
{
    tiku_backend_t *b;

    if (app == NULL || out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    b = board_named(app, name);
    if (b == NULL) {
        return 0;
    }
    return tiku_backend_tiku_port(b, out, max);
}

/** @brief What @p name IS, as its die says ("nRF54LM20B"), into @p out. */
int
desktop_board_mcu(desktop_t *app, const char *name, char *out, size_t max)
{
    tiku_backend_t *b;
    const tiku_identity_t *id;

    if (app == NULL || out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    b = board_named(app, name);
    if (b == NULL) {
        return 0;
    }
    id = tiku_backend_tiku_identity(b);
    if (id == NULL) {
        return 0;
    }
    return (int)strlen(strncpy(out, id->mcu, max - 1u));
}

/**
 * @brief Start looking for boards: the ports to ask, asked one per beat.
 *
 * Every port on a machine, each waited on in turn, is twenty seconds
 * the desk would not answer for; asked one at a time between beats it
 * stays a desk, and the boards appear as they are found.  A port this
 * shell already holds is left alone.
 *
 * @param session who asked over the wire, or -1 for an application in
 *                this process; told LOOKED when the last port has been.
 */
void
desktop_board_look_begin(desktop_t *app, int session)
{
    if (app == NULL || app->look.going) {
        return;                 /* one look at a time; the asker hears */
    }
    app->look.count = tiku_tx_scan_serial(app->look.port, TIKU_MOUNT_MAX);
    app->look.at = 0;
    app->look.baud = (app->board_baud > 0) ? app->board_baud : 115200;
    app->look.session = session;
    app->look.going = 1;
}

/** @brief One port of a look in progress; the last one says so. */
void
desktop_board_look_step(desktop_t *app)
{
    if (app == NULL || !app->look.going) {
        return;
    }
    if (app->look.at < app->look.count) {
        (void)mount_one(app, app->look.port[app->look.at], app->look.baud,
                        NULL, 1);
        app->look.at++;
        if (app->look.at < app->look.count) {
            return;
        }
    }
    app->look.going = 0;
    desktop_board_names_tell(app);
    if (app->look.session >= 0) {
        char how[32];

        (void)snprintf(how, sizeof how, "%d", board_count(app));
        desktop_board_tell(app, app->look.session, TIKU_APP_BOARD_LOOKED,
                           how);
    }
    app->dirty = 1;
}

/**
 * @brief Look for boards, waiting: every port, here and now.
 * @return how many boards are mounted afterwards.
 */
int
desktop_board_look(desktop_t *app)
{
    desktop_board_look_begin(app, -1);
    while (app != NULL && app->look.going) {
        desktop_board_look_step(app);
    }
    return (app != NULL) ? board_count(app) : 0;
}

/**
 * @brief Let the board called @p name go, port and all.
 *
 * Something else on this machine needs it -- a bench run that reflashes
 * it, above all -- and two owners on one port corrupt each other.  What
 * is remembered is the board's DIE, because the thing coming back may
 * be on a different node than the thing that went.
 */
int
desktop_board_release(desktop_t *app, const char *name)
{
    char prefix[TIKU_PATH_MAX];
    tiku_backend_t *b;
    const tiku_identity_t *id;
    int slot, i;

    if (app == NULL || name == NULL || name[0] == '\0') {
        return 0;
    }
    if (app->board_run != NULL) {
        return 0;               /* a program is on it: not while it runs */
    }
    b = board_named(app, name);
    if (b == NULL) {
        return 0;
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (!app->released[i].used) {
            break;
        }
    }
    if (i == TIKU_MOUNT_MAX) {
        return 0;
    }
    id = tiku_backend_tiku_identity(b);
    if (id == NULL || id->uid[0] == '\0') {
        return 0;               /* nothing to find it by: keep it mounted */
    }
    (void)snprintf(app->released[i].name, sizeof app->released[i].name,
                   "%s", name);
    (void)snprintf(app->released[i].uid, sizeof app->released[i].uid,
                   "%s", id->uid);
    app->released[i].used = 1;
    (void)snprintf(prefix, sizeof prefix, "/devices/%s", name);
    /* The windows go with the port: a channel on a cable nobody holds
     * is a channel talking to a board that may be being reflashed. */
    slot = channel_slot_of(app, prefix);
    if (slot >= 0) {
        tiku_channel_close(app->channels[slot]);
        app->channels[slot] = NULL;
        app->channel_board[slot][0] = '\0';
        app->channel_mount[slot][0] = '\0';
    }
    /* The router took the obligation when it was mounted, so removing
     * it closes it: closing again here would be closing it twice. */
    (void)tiku_mount_remove(app->backend, prefix);
    app->dirty = 1;
    return 1;
}

/**
 * @brief Take the board called @p name back, wherever it is now.
 *
 * @return nonzero when it answered again and is mounted where it was.
 */
int
desktop_board_reclaim(desktop_t *app, const char *name)
{
    char prefix[TIKU_PATH_MAX];
    tiku_backend_t *dev;
    tiku_model_t gui;
    int i, baud;

    if (app == NULL || name == NULL) {
        return 0;
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (app->released[i].used &&
            strcmp(app->released[i].name, name) == 0) {
            break;
        }
    }
    if (i == TIKU_MOUNT_MAX) {
        return 0;               /* not one this shell let go */
    }
    baud = (app->board_baud > 0) ? app->board_baud : 115200;
    dev = tiku_backend_tiku_open_uid(app->released[i].uid, baud);
    if (dev == NULL) {
        return 0;               /* not back yet: asking again is free */
    }
    (void)snprintf(prefix, sizeof prefix, "/devices/%s",
                   app->released[i].name);
    tiku_backend_tiku_set_write_timeout(dev, 4000);
    if (tiku_mount_add(app->backend, prefix, dev, 1) != 0) {
        tiku_backend_close(dev);
        return 0;
    }
    if (dev->ops->stat(dev, "/gui", &gui) == 0) {
        int slot = channel_keep(app,
                                tiku_channel_open(
                                    tiku_backend_tiku_session(dev)));

        if (slot >= 0) {
            (void)snprintf(app->channel_board[slot], TIKU_PATH_MAX, "%s",
                           prefix);
        }
    }
    app->released[i].used = 0;
    app->dirty = 1;
    return 1;
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
            app->board_run = run_begin(app, board, b, program);
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
    if (app == NULL) {
        return;
    }
    desktop_board_look_step(app);   /* a look in progress, a port a beat */
    if (app->board_run == NULL) {
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
