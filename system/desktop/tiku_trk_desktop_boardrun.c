/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_boardrun.c - boards for applications: running and saving
 * BASIC programs on a mounted board, looking for boards, the current device,
 * and letting a board go and taking it back.
 *
 * An application asks the desktop to run a program on a board and is sent
 * what the program prints.  One run at a time, held in app->board_run.  A
 * board with a window channel runs the program over the channel's
 * interpreter stream; a board without one has its shell held for the run.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"
#include "tiku_trail.h"

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

/**
 * @brief The boards mounted under /devices, one name a line, in name order.
 * @return the bytes written to @p out.
 */
int
desktop_board_names(desktop_t *app, char *out, size_t max)
{
    size_t n = 0u;
    int i, slots;

    if (app == NULL || out == NULL || max == 0u) {
        return 0;
    }
        const char *leaves[TIKU_MOUNT_MAX];
    int nleaves = 0, j;

    out[0] = '\0';
    slots = tiku_mount_slots(app->backend);
    for (i = 0; i < slots; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        tiku_backend_t *b = tiku_mount_at(app->backend, i);

        if (prefix == NULL || b == NULL ||
            strncmp(prefix, "/devices/", 9) != 0) {
            continue;           /* the local files are not a board */
        }
        /* In name order: ports are probed in parallel, so the mount
         * order is the order the boards answered in. */
        for (j = nleaves; j > 0 &&
             strcmp(leaves[j - 1], leaf_of(prefix)) > 0; j--) {
            leaves[j] = leaves[j - 1];
        }
        leaves[j] = leaf_of(prefix);
        nleaves++;
    }
    for (i = 0; i < nleaves; i++) {
        const char *leaf = leaves[i];
        size_t need = strlen(leaf) + 1u;
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
    const char *want = name;
    tiku_backend_t *first = NULL;
    int i, slots = tiku_mount_slots(app->backend);

    /* No name asked: the current device when one is set, otherwise the
     * first board. */
    if ((want == NULL || want[0] == '\0') &&
        app->current_device[0] != '\0') {
        want = app->current_device;
    }
    for (i = 0; i < slots; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        tiku_backend_t *b = tiku_mount_at(app->backend, i);

        if (prefix == NULL || b == NULL ||
            strncmp(prefix, "/devices/", 9) != 0) {
            continue;
        }
        if (first == NULL) {
            first = b;
        }
        if (want == NULL || want[0] == '\0' ||
            strcmp(leaf_of(prefix), want) == 0) {
            return b;
        }
    }
    /* An empty name falls back to the first board; a name that is not
     * mounted gets NULL. */
    return (name == NULL || name[0] == '\0') ? first : NULL;
}

/** @brief The mount prefix of board @p b into @p out; empty when @p b is
 *         not mounted. */
static void
prefix_of(desktop_t *app, const tiku_backend_t *b, char *out, size_t max)
{
    int i, slots = tiku_mount_slots(app->backend);

    out[0] = '\0';
    for (i = 0; i < slots; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);

        if (prefix != NULL && tiku_mount_at(app->backend, i) == b) {
            (void)snprintf(out, max, "%s", prefix);
            return;
        }
    }
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

/** @brief Send bytes to the board's interpreter over its channel. */
static int
stream_send(void *ctx, const void *bytes, size_t len)
{
    return tiku_channel_basic_send(ctx, bytes, len);
}

/** @brief Read what the program printed, from the board's channel. */
static int
stream_recv(void *ctx, void *buf, size_t max)
{
    return tiku_channel_basic_recv(ctx, buf, max);
}

/** @brief Pump the board's channel, which carries the interpreter stream. */
static void
stream_pump(void *ctx)
{
    (void)tiku_channel_pump(ctx);
}

/**
 * @brief Start @p program on board @p b: over the interpreter stream of the
 *        board's window channel when it has one, through its held shell
 *        when it has not.
 *
 * A streamed run leaves the board's shell free, so its namespace answers
 * while the program runs.  A board without a channel can only be reached
 * through its shell.
 *
 * @return the run, or NULL when it would not start
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
 * @brief Fill @p order with the mount slots of the boards under /devices,
 *        in name order.
 * @return how many.
 */
static int
board_slots_sorted(desktop_t *app, int *order)
{
    int i, j, n = 0, slots = tiku_mount_slots(app->backend);

    for (i = 0; i < slots; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);

        if (prefix == NULL || tiku_mount_at(app->backend, i) == NULL ||
            strncmp(prefix, "/devices/", 9) != 0) {
            continue;
        }
        for (j = n; j > 0 &&
             strcmp(leaf_of(tiku_mount_prefix_at(app->backend,
                                                 order[j - 1])),
                    leaf_of(prefix)) > 0; j--) {
            order[j] = order[j - 1];
        }
        order[j] = i;
        n++;
    }
    return n;
}

/**
 * @brief The mounted boards in name order, one a line:
 *        "name<TAB>port<TAB>processor".
 *
 * The port is the one the board was opened on and the processor is what
 * the board reported when it mounted: building the list reads nothing from
 * the wire.
 *
 * @return the bytes written to @p out.
 */
int
desktop_board_list(desktop_t *app, char *out, size_t max)
{
        size_t n = 0u;
    int order[TIKU_MOUNT_MAX];
    int i, k, nsorted;

    if (app == NULL || out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
        nsorted = board_slots_sorted(app, order);
    for (k = 0; k < nsorted; k++) {
        const char *prefix;

        i = order[k];
        prefix = tiku_mount_prefix_at(app->backend, i);
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

/**
 * @brief The port board @p name is on ("/dev/ttyACM0"), into @p out; an
 *        empty name means the current device, or the first board.
 */
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

/**
 * @brief The processor board @p name reports ("nRF54LM20B"), into @p out.
 * @return its length, or 0 when the board or its identity is unknown.
 */
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
 * @brief Unmount board @p name and close its port, so another program on
 *        this machine (a bench run that reflashes it) can open the port.
 *
 * Two owners on one port corrupt each other.  The board's uid is kept in
 * app->released, since the board may come back on a different port.
 *
 * @return 1 when released; 0 while a run is in progress, for an unknown
 *         board or one with no uid, when the released table is full, or
 *         when the unmount fails.
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
        return 0;               /* no release while any run is going */
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
    /* The board's window channel closes with the port: the board may be
     * reflashed while it is let go. */
    slot = channel_slot_of(app, prefix);
    if (slot >= 0) {
        tiku_channel_close(app->channels[slot]);
        app->channels[slot] = NULL;
        app->channel_board[slot][0] = '\0';
        app->channel_mount[slot][0] = '\0';
    }
        tiku_trail("release %s", name);
    /* The unmount closes the backend on a worker, after any job still
     * asking the board, so it is not closed here. */
    if (desktop_board_unmount_released(app, prefix) != 0) {
        app->released[i].used = 0;
        return 0;
    }
    desktop_damage_all(app);
    return 1;
}


/**
 * @brief Start @p program on the board called @p board.
 *
 * @param slot the embedded application to tell, or NULL when the caller
 *             sets app->board_run_session to the asking session.
 * @return 1 when the run began; 0 while another run or save is in
 *         progress, when no board matches, or when the run does not start.
 */
int
desktop_board_run(desktop_t *app, struct desktop_embed *slot,
                  const char *board, const char *program)
{
    tiku_backend_t *b;

    if (app == NULL || program == NULL || app->board_run != NULL) {
        return 0;               /* one run at a time */
    }
    b = board_named(app, board);
    if (b == NULL) {
        return 0;
    }
                tiku_trail("run: start on %s", (board != NULL) ? board : "?");
    app->board_run = run_begin(app, board, b, program);
    if (app->board_run == NULL) {
        tiku_trail("run: would not start");
        return 0;
    }
    app->board_run_state = -1;
    app->board_run_slot = slot;
    app->board_run_saving = 0;
    app->board_run_asked = 0;
    (void)snprintf(app->board_run_name, sizeof app->board_run_name, "%s",
                   (board != NULL) ? board : "");
    prefix_of(app, b, app->board_run_prefix, sizeof app->board_run_prefix);
    return 1;
}

/**
 * @brief Save @p program on the board called @p board under @p name.
 *
 * A save uses the run slot: it is refused while a run is going, and no
 * run starts until it ends.  When it ends, the asker is told
 * TIKU_APP_BOARD_SAVED, TIKU_APP_BOARD_STOPPED or TIKU_APP_BOARD_FAILED.
 *
 * @return 1 when the save began.
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
    prefix_of(app, b, app->board_run_prefix, sizeof app->board_run_prefix);
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

/**
 * @brief Pump the run and pass on what it printed, its start and its INPUT
 *        prompts; when it ends, free it and tell the asker how it ended.
 * @note Called once per turn of the loop.
 */
void
desktop_board_pump(desktop_t *app)
{
    tiku_board_basic_state_t st;
    struct desktop_embed *slot;
    int what;
    if (app == NULL) {
        return;
    }
    
    if (app->board_run == NULL) {
        return;
    }
    /* Five milliseconds at most: this runs on every turn of the loop. */
            st = tiku_board_basic_pump(app->board_run, 5, say_line, app);
    if ((int)st != app->board_run_state) {
        static const char *const NAMED[] = { "sending", "running", "asking",
                                             "ended", "stopped", "failed" };

        app->board_run_state = (int)st;
        tiku_trail("run: %s", ((int)st >= 0 && (int)st < 6)
                                  ? NAMED[(int)st] : "?");
        if (st == TIKU_BOARD_BASIC_RUNNING) {
            /* The program is handed over and RUN, or SAVE, sent. */
            tell_asker(app, TIKU_APP_BOARD_RUNNING, "");
        }
    }
    if (st == TIKU_BOARD_BASIC_ASKING) {
        /* Each question is told once, with the prompt the program
         * printed. */
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
    {
        tiku_board_basic_t *run = app->board_run;
        int session = app->board_run_session;
        int saving = app->board_run_saving;

        app->board_run = NULL;
        app->board_run_slot = NULL;
        app->board_run_session = -1;
        app->board_run_saving = 0;
        if (tiku_board_basic_streamed(run)) {
            /* A streamed run is freed here, on the loop: leaving the
             * interpreter pumps the board's channel, which the loop also
             * pumps.  It is freed before the asker is told, since an
             * application told the run ended may start the next one from
             * that call. */
            tiku_board_basic_free(run);
            desktop_board_say(app, session, slot, what,
                              saving ? app->board_run_name : "");
        } else {
            /* A held run's shell is given back on a worker, since
             * leaving drains and syncs the wire.  The asker is told once
             * the shell is back, and a run it starts then finds the shell
             * free. */
            desktop_wire_run_over(app, run, what, session, slot, saving,
                                  app->board_run_name,
                                  app->board_run_prefix);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* LOOKING FOR BOARDS, CONNECTING AND RECLAIMING                             */
/*---------------------------------------------------------------------------*/

/** @brief Add an asker to @p list unless it is there already; nobody
 *         (-1, NULL) is not added. */
static void
look_asker_add(desktop_asker_t *list, int *n, int session,
               struct desktop_embed *slot)
{
    int i;

    if (session < 0 && slot == NULL) {
        return;
    }
    for (i = 0; i < *n; i++) {
        if (list[i].session == session && list[i].slot == slot) {
            return;
        }
    }
    if (*n < DESKTOP_ASKERS) {
        list[*n].session = session;
        list[*n].slot = slot;
        (*n)++;
    }
}

/** @brief Start a look for the askers already in look.asked[]. */
static void
look_start(desktop_t *app)
{
    char scan[TIKU_MOUNT_MAX][256];
    int found, i;

    found = tiku_tx_scan_serial(scan, TIKU_MOUNT_MAX);
    app->look.going = 1;
    app->look.gen++;
    app->look.baud = (app->board_baud > 0) ? app->board_baud : 115200;
    app->look.pending = 0;
    app->look.count = 0;
    app->look.landed = 0;
    tiku_trail("look: %d ports to ask", found);
    for (i = 0; i < found; i++) {
        if (desktop_wire_probe(app, DESKTOP_ASK_LOOK, scan[i],
                               app->look.baud, 1, NULL, 1) == 0) {
            app->look.pending++;
            app->look.count++;
        }
    }
    if (app->look.pending == 0) {
        desktop_board_look_done(app);
    }
}

/**
 * @brief Probe every serial port at once for boards.
 *
 * A board that answers is recorded as a connect candidate under its
 * processor name and its port is let go; nothing is mounted.  The asker
 * (a session, or an application hosted here) is told when the last port
 * has answered.
 */
void
desktop_board_look_begin(desktop_t *app, int session,
                         struct desktop_embed *slot)
{
        if (app == NULL) {
        return;
    }
    if (app->look.going) {
        /* One look at a time: a look asked for during this one is
         * started by desktop_board_look_done() when this one ends, and
         * every asker of it is told when that second look ends. */
        app->look.again = 1;
        look_asker_add(app->look.again_by, &app->look.nagain, session, slot);
        return;
    }
    app->look.nasked = 0;
    look_asker_add(app->look.asked, &app->look.nasked, session, slot);
    look_start(app);
}

/**
 * @brief End a look: send every remote session the board list, tell each
 *        asker how many boards are mounted, and start a look asked for
 *        during this one.
 */
void
desktop_board_look_done(desktop_t *app)
{
    char how[32];
    int i;

    app->look.going = 0;
    tiku_trail("look: done, %d boards", board_count(app));
    desktop_board_names_tell(app);
    (void)snprintf(how, sizeof how, "%d", board_count(app));
    for (i = 0; i < app->look.nasked; i++) {
        desktop_board_say(app, app->look.asked[i].session,
                          app->look.asked[i].slot, TIKU_APP_BOARD_LOOKED,
                          how);
    }
    app->look.nasked = 0;
    desktop_damage_all(app);
    if (app->look.again) {
        memcpy(app->look.asked, app->look.again_by,
               sizeof app->look.asked);
        app->look.nasked = app->look.nagain;
        app->look.again = 0;
        app->look.nagain = 0;
        look_start(app);
    }
}

/**
 * @brief The first board mounted, in the order the menus list them.
 * @return nonzero when there is one, with its leaf in @p out.
 */
static int
board_first(desktop_t *app, char *out, size_t max)
{
    char names[512];
    const char *nl;
    size_t len;

    out[0] = '\0';
    if (desktop_board_names(app, names, sizeof names) <= 0) {
        return 0;
    }
    nl = strchr(names, '\n');
    len = (nl != NULL) ? (size_t)(nl - names) : strlen(names);
    if (len == 0u) {
        return 0;
    }
    if (len >= max) {
        len = max - 1u;
    }
    memcpy(out, names, len);
    out[len] = '\0';
    return 1;
}

/**
 * @brief Tell every open application, and every remote session, that the
 *        current device changed.
 */
void
device_current_broadcast(desktop_t *app)
{
    int e;

    if (app == NULL) {
        return;
    }
    for (e = 0; e < TIKU_TRK_EMBED_MAX; e++) {
        if (app->embeds[e].running && app->embeds[e].descriptor != NULL &&
            app->embeds[e].descriptor->board_said != NULL) {
            app->embeds[e].descriptor->board_said(app->embeds[e].state,
                TIKU_APP_BOARD_CURRENT, app->current_device);
        }
    }
    desktop_board_names_tell(app);
    desktop_damage_all(app);
}

/**
 * @brief Make @p name the current device (empty for this machine), save it
 *        and its port in the preferences, and tell every open application
 *        and remote session through device_current_broadcast().
 */
void
device_set_current(desktop_t *app, const char *name)
{
    tiku_trk_prefs_t *prefs;

    if (app == NULL) {
        return;
    }
    snprintf(app->current_device, sizeof app->current_device, "%s",
             (name != NULL) ? name : "");
    app->current_device_port[0] = '\0';
    if (app->current_device[0] != '\0') {
        (void)desktop_board_port(app, app->current_device,
                                 app->current_device_port,
                                 sizeof app->current_device_port);
    }
    prefs = prefs_model(app);
    tiku_trk_prefs_set_text(prefs, "CurrentDevice", app->current_device);
    tiku_trk_prefs_set_text(prefs, "CurrentDevicePort",
                            app->current_device_port);
    (void)tiku_trk_prefs_save(prefs, app->prefs_window.path);
    device_current_broadcast(app);
}

/**
 * @brief Probe @p port on a worker and mount the board that answers under
 *        /devices; it becomes the current device when none is.
 * @return 0 when the probe was posted, -1 otherwise.
 */
int
device_connect(desktop_t *app, const char *port)
{
    int baud;

    if (app == NULL || port == NULL || port[0] == '\0') {
        return -1;
    }
    baud = (app->board_baud > 0) ? app->board_baud : 115200;
    return desktop_wire_probe(app, DESKTOP_ASK_CONNECT, port, baud, 0, NULL,
                              0);
}

/**
 * @brief Unmount board @p name and offer its port to connect again; when it
 *        was the current device, the first remaining board becomes
 *        current, or this machine when none is left.
 * @return 0 when the board was unmounted, -1 otherwise.
 */
int
device_disconnect(desktop_t *app, const char *name)
{
    char prefix[TIKU_PATH_MAX], port[256];
    int was_current, rc;

    if (app == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    was_current = (app->current_device[0] != '\0' &&
                   strcmp(app->current_device, name) == 0);
    /* The port is read before the unmount, to put the board back on the
     * connect list under its name. */
    port[0] = '\0';
    (void)desktop_board_port(app, name, port, sizeof port);
    snprintf(prefix, sizeof prefix, "/devices/%s", name);
    rc = desktop_board_unmount(app, prefix);
    if (rc == 0 && port[0] != '\0') {
        device_candidate_record(app, port, name);
    }
    if (was_current) {
        /* The current board has gone: another board still mounted
         * becomes current, or this machine when none is.  board_first()
         * runs after the unmount, so the board leaving is not the one
         * chosen. */
        char next[64];

        device_set_current(app, board_first(app, next, sizeof next)
                                    ? next : "");
    }
    return rc;
}

/**
 * @brief Ask for a released board back, wherever it is now: every port is
 *        probed at once and the one with its uid is mounted.  The asker
 *        is told RECLAIMED or FAILED once every port has answered.
 * @return 1 when the asking was taken; 0 for a board this desktop never
 *         let go, or while a reclaim is under way.
 */
int
desktop_board_reclaim(desktop_t *app, const char *name, int session,
                      struct desktop_embed *slot)
{
    char scan[16][256];
    int i, found;

    if (app == NULL || name == NULL || app->reclaim.going) {
        return 0;
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (app->released[i].used &&
            strcmp(app->released[i].name, name) == 0) {
            break;
        }
    }
    if (i == TIKU_MOUNT_MAX) {
        return 0;               /* not one this desktop let go */
    }
    app->reclaim.going = 1;
    app->reclaim.session = session;
    app->reclaim.slot = slot;
    app->reclaim.found = 0;
    app->reclaim.pending = 0;
    app->reclaim.baud = (app->board_baud > 0) ? app->board_baud : 115200;
    snprintf(app->reclaim.name, sizeof app->reclaim.name, "%s", name);
    snprintf(app->reclaim.uid, sizeof app->reclaim.uid, "%s",
             app->released[i].uid);
    found = tiku_tx_scan_serial(scan, 16);
    for (i = 0; i < found; i++) {
        if (desktop_wire_probe(app, DESKTOP_ASK_RECLAIM, scan[i],
                               app->reclaim.baud, 1, NULL, 0) == 0) {
            app->reclaim.pending++;
        }
    }
    tiku_trail("reclaim %s: %d ports to ask", name, app->reclaim.pending);
    if (app->reclaim.pending == 0) {
        desktop_board_reclaim_done(app);
    }
    return 1;
}

/**
 * @brief A probe for a reclaim answered with @p dev, or NULL.  The board
 *        with the uid is mounted where it was, with a channel kept and
 *        adopted into the remote listener for a board with a /gui; the
 *        rest are closed.
 * @note Called on the loop.
 */
void
desktop_board_reclaim_landed(desktop_t *app, tiku_backend_t *dev)
{
    const tiku_identity_t *id = (dev != NULL)
                                ? tiku_backend_tiku_identity(dev) : NULL;

    app->reclaim.pending--;
    if (dev != NULL && !app->reclaim.found && id != NULL &&
        strcmp(id->uid, app->reclaim.uid) == 0) {
        char prefix[TIKU_PATH_MAX];
        tiku_model_t gui;
        int i;

        (void)snprintf(prefix, sizeof prefix, "/devices/%s",
                       app->reclaim.name);
        tiku_backend_tiku_set_write_timeout(dev, 4000);
        if (tiku_mount_add(app->backend, prefix, dev, 1) != 0) {
            tiku_backend_close(dev);
        } else {
            if (dev->ops->stat(dev, "/gui", &gui) == 0) {
                int slot = channel_keep(app,
                                        tiku_channel_open(
                                            tiku_backend_tiku_session(dev)));

                if (slot >= 0) {
                    (void)snprintf(app->channel_board[slot], TIKU_PATH_MAX,
                                   "%s", prefix);
                    (void)channel_attach(app, slot);
                }
            }
            for (i = 0; i < TIKU_MOUNT_MAX; i++) {
                if (app->released[i].used &&
                    strcmp(app->released[i].name, app->reclaim.name) == 0) {
                    app->released[i].used = 0;
                }
            }
            app->reclaim.found = 1;
            desktop_damage_all(app);
            tiku_trail("reclaim %s", app->reclaim.name);
        }
    } else if (dev != NULL) {
        tiku_backend_close(dev);
    }
    if (app->reclaim.pending <= 0) {
        desktop_board_reclaim_done(app);
    }
}

/**
 * @brief Every port has answered the reclaim: tell the asker RECLAIMED or
 *        FAILED, turn the board's tray cell red when it was not found,
 *        and send every remote session the board list.
 */
void
desktop_board_reclaim_done(desktop_t *app)
{
    app->reclaim.going = 0;
    if (!app->reclaim.found) {
        desktop_devtray_failed(app, NULL, app->reclaim.name,
                               "not found on any port");
    }
    desktop_board_say(app, app->reclaim.session, app->reclaim.slot,
                      app->reclaim.found ? TIKU_APP_BOARD_RECLAIMED
                                         : TIKU_APP_BOARD_FAILED,
                      app->reclaim.name);
    desktop_board_names_tell(app);
}

/**
 * @brief Tell whoever asked: a session over the wire, or an application
 *        hosted in this process, or nobody.
 */
void
desktop_board_say(desktop_t *app, int session, struct desktop_embed *slot,
                  int what, const char *text)
{
    if (session >= 0) {
        desktop_board_tell(app, session, what, text);
    } else if (slot != NULL && slot->running && slot->descriptor != NULL &&
               slot->descriptor->board_said != NULL) {
        slot->descriptor->board_said(slot->state, what, text);
    }
}
