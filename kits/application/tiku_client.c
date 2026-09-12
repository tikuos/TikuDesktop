/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_client.c - the out-of-process runner for one descriptor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>

#include "tiku_client.h"
#include "tiku_dl.h"
#include "tiku_remote.h"

/*
 * The stamp that says "a TikuOS application built this".
 *
 * A listing cannot tell tiku-term from /bin/ls: both are executables, and
 * the type of a file here is decided by its NAME, which a program does
 * not have an extension for.  Reading the whole binary looking for a
 * descriptor id would cost megabytes per row.
 *
 * So the build says it instead, in a section of its own: a reader opens
 * the file, walks the section table -- a few hundred bytes near the head
 * -- and knows.  Anything that links this kit carries it, which is
 * exactly the set of things that ARE TikuOS applications.
 */
__attribute__((used, section(".tikuos")))
static const char tiku_app_stamp_client[] = "TikuOS-application-1";

typedef struct {
    tiku_remote_client_t remote;
    /* Windows this application is holding.  The session ends when the
     * last one goes -- which for an application with one window is the
     * moment its window is closed, exactly as before. */
    int                       windows;
    /* The desk's clipboard, mirrored: the shell TELLs every change, so
     * a paste reads from here and never waits on the wire. */
    char                      clip[4096];
    /* The boards the shell has mounted, kept warm the way the clipboard
     * is: a menu asks while it is being drawn. */
    char                      boards[512];
    /* The desktop's current device, mirrored the same way: an app that
     * defaults its work to it asks while a menu is being built. */
    char                      current[64];
} client_ctx_t;

static uint32_t
svc_open(void *ctx, const char *title, int w, int h)
{
    client_ctx_t *c = ctx;
    uint32_t id = tiku_remote_open(&c->remote, title, w, h);

    if (id != 0u) {
        c->windows++;
    }
    return id;
}

static int
svc_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    client_ctx_t *c = ctx;

    return tiku_remote_frame(&c->remote, id, px, w, h);
}

/**
 * @brief Hand the window over the cheap way when the cheap way is whole.
 *
 * Three things have to be true to send commands: the far end has to be
 * able to play them, the list has to describe the WHOLE window -- an
 * icon in it and it does not -- and it has to actually be smaller, which
 * for a window of four fills it is not.  Any of them false and the
 * pixels go, and nothing above here notices either way.
 */
static int
svc_present(void *ctx, uint32_t id, const struct tiku_dl *dl,
            const uint32_t *px, int w, int h)
{
    client_ctx_t *c = ctx;
    size_t frame = 4u * (size_t)w * (size_t)h;

    /*
     * FOUR things, and the first cannot be assumed: the far end has to
     * have SAID it can play a stream.  Assuming it is survivable only
     * while both ends are built together -- an end that cannot play a
     * command steps over it (tiku_dl.h) and the window arrives missing
     * whatever it drew, with the list still saying it is whole.
     *
     * Zero features means the desktop has not answered yet, or is too
     * old to answer at all.  Both get the pixels, which every version
     * has always understood.  And a list carrying ICONS needs the far
     * end to have claimed those separately: an end that plays streams
     * but not icon commands would step over them and show a window
     * with holes where its art was.
     */
    if (dl != NULL &&
        (tiku_remote_peer_features(&c->remote) &
             TIKU_FEAT_COMMAND_STREAM) != 0u &&
        (tiku_dl_icons(dl) == 0 ||
         (tiku_remote_peer_features(&c->remote) &
              TIKU_FEAT_ICON_STREAM) != 0u) &&
        /* A list carrying PICTURES needs the far end to have claimed
         * those too: one that stepped over them would show a window
         * with holes where its thumbnails were. */
        (tiku_dl_pictures(dl) == 0 ||
         (tiku_remote_peer_features(&c->remote) &
              TIKU_FEAT_PICTURE_STREAM) != 0u) &&
        tiku_dl_misses(dl) == 0 &&
        tiku_dl_flat_size(dl) * 2u < frame &&
        tiku_remote_draw(&c->remote, id, dl, w, h)) {
        return 1;
    }
    return tiku_remote_frame(&c->remote, id, px, w, h);
}

static int
svc_menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    client_ctx_t *c = ctx;

    return tiku_remote_menus(&c->remote, id, set);
}

static int
svc_raise(void *ctx, uint32_t id)
{
    client_ctx_t *c = ctx;

    tiku_remote_raise(&c->remote, id);
    return 0;
}

static int
svc_place(void *ctx, uint32_t id, int role)
{
    client_ctx_t *c = ctx;

    tiku_remote_place(&c->remote, id, role);
    return 0;
}

static int
svc_sizeable(void *ctx, uint32_t id)
{
    client_ctx_t *c = ctx;

    tiku_remote_sizeable(&c->remote, id);
    return 0;
}

static void
svc_close(void *ctx, uint32_t id)
{
    client_ctx_t *c = ctx;

    /* The window is given back by NAME; leaving is what happens when
     * the last one has gone, which the pump decides. */
    tiku_remote_close_window(&c->remote, id);
    if (c->windows > 0) {
        c->windows--;
    }
}

static int64_t
now_us(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/**
 * @brief Ask the shell to let the person pick a file.
 *
 * Over SAY, the road that already carries a self-describing message
 * upward: the answer comes back over TELL, and neither direction needs
 * an op of its own for one question.
 */
static int
svc_pick(void *ctx, uint32_t id, int mode, const char *start,
         const char *name)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_PICK);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_int32(m, "window", (int32_t)id);
    (void)tiku_msg_add_int32(m, "mode", (int32_t)mode);
    if (start != NULL) {
        (void)tiku_msg_add_string(m, "start", start);
    }
    if (name != NULL) {
        (void)tiku_msg_add_string(m, "name", name);
    }
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

static int
svc_clip_set(void *ctx, const char *text)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m;
    int sent;

    /* The local copy first: a copy-then-paste inside this application
     * must work even before the shell's echo comes back around. */
    snprintf(c->clip, sizeof c->clip, "%s", (text != NULL) ? text : "");
    m = tiku_msg_new(TIKU_MSG_CLIP);
    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_string(m, "text", (text != NULL) ? text : "");
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

static int
svc_clip_get(void *ctx, char *out, size_t max)
{
    client_ctx_t *c = ctx;

    if (out == NULL || max == 0u) {
        return 0;
    }
    snprintf(out, max, "%s", c->clip);
    return (int)strlen(out);
}

/** @brief The @p which'th field of @p row, up to a tab or its end. */
static size_t
field_of(const char *row, int which, char *out, size_t max)
{
    const char *p = row;
    int i;

    out[0] = '\0';
    for (i = 0; i < which; i++) {
        const char *tab = strchr(p, '\t');

        if (tab == NULL || *p == '\n') {
            return 0u;
        }
        p = tab + 1;
    }
    {
        size_t n = strcspn(p, "\t\n");

        if (n >= max) {
            n = max - 1u;
        }
        memcpy(out, p, n);
        out[n] = '\0';
        return n;
    }
}

static int
svc_board_port(void *ctx, const char *board, char *out, size_t max)
{
    client_ctx_t *c = ctx;
    const char *row = c->boards;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    while (*row != '\0') {
        char name[64];

        (void)field_of(row, 0, name, sizeof name);
        if (board != NULL && strcmp(name, board) == 0) {
            return (int)field_of(row, 1, out, max);
        }
        row = strchr(row, '\n');
        if (row == NULL) {
            break;
        }
        row++;
    }
    return 0;
}

static int
svc_board_mcu(void *ctx, const char *board, char *out, size_t max)
{
    client_ctx_t *c = ctx;
    const char *row = c->boards;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    while (*row != '\0') {
        char name[64];

        (void)field_of(row, 0, name, sizeof name);
        if (board != NULL && strcmp(name, board) == 0) {
            return (int)field_of(row, 2, out, max);
        }
        row = strchr(row, '\n');
        if (row == NULL) {
            break;
        }
        row++;
    }
    return 0;
}

/** @brief Ask the shell for something about one board, by name. */
static int
say_board(client_ctx_t *c, uint32_t what, const char *board)
{
    tiku_msg_t *m = tiku_msg_new(what);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_string(m, "board", (board != NULL) ? board : "");
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

static int
svc_board_release(void *ctx, const char *board)
{
    return say_board(ctx, TIKU_MSG_BOARDFREE, board);
}

static int
svc_board_reclaim(void *ctx, const char *board)
{
    return say_board(ctx, TIKU_MSG_BOARDBACK, board);
}

static int
svc_board_find(void *ctx)
{
    return say_board(ctx, TIKU_MSG_BOARDLOOK, "");
}

/** @brief Open a desktop namespace path without interpreting it locally. */
static int svc_open_path(void *ctx, const char *path)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_OPENPATH);
    if (!m || !path) { tiku_msg_free(m); return 0; }
    tiku_msg_add_string(m, "path", path);
    int sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m); return sent;
}

/** @brief Name this application's page of the desktop's help folder. */
static int svc_help_page(void *ctx, const char *page)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_HELPPAGE);
    if (!m || !page) { tiku_msg_free(m); return 0; }
    tiku_msg_add_string(m, "page", page);
    int sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m); return sent;
}

static int
svc_current_device(void *ctx, char *out, size_t max)
{
    client_ctx_t *c = ctx;

    if (out == NULL || max == 0u) {
        return 0;
    }
    snprintf(out, max, "%s", c->current);
    return (int)strlen(out);
}

static int
svc_board_names(void *ctx, char *out, size_t max)
{
    client_ctx_t *c = ctx;

    if (out == NULL || max == 0u) {
        return 0;
    }
        {
        /* The shell tells name AND port; a caller asking for names is
         * asking for names. */
        const char *row = c->boards;
        size_t n = 0u;

        out[0] = '\0';
        while (*row != '\0' && n + 1u < max) {
            char name[64];
            size_t k = field_of(row, 0, name, sizeof name);

            if (k > 0u && n + k + 1u < max) {
                memcpy(out + n, name, k);
                n += k;
                out[n++] = '\n';
                out[n] = '\0';
            }
            row = strchr(row, '\n');
            if (row == NULL) {
                break;
            }
            row++;
        }
        return (int)n;
    }
    return (int)strlen(out);
}

static int
svc_board_run(void *ctx, const char *board, const char *program)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_BOARDRUN);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_string(m, "board", (board != NULL) ? board : "");
    (void)tiku_msg_add_string(m, "program",
                              (program != NULL) ? program : "");
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

static int
svc_board_input(void *ctx, const char *text)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_BOARDIN);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_string(m, "text", (text != NULL) ? text : "");
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

static int
svc_board_save(void *ctx, const char *board, const char *name,
               const char *program)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_BOARDSAVE);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_string(m, "board", (board != NULL) ? board : "");
    (void)tiku_msg_add_string(m, "name", (name != NULL) ? name : "");
    (void)tiku_msg_add_string(m, "program",
                              (program != NULL) ? program : "");
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

static int
svc_board_stop(void *ctx)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_BOARDSTOP);
    int sent;

    if (m == NULL) {
        return 0;
    }
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

static int
pump(const tiku_app_descriptor_t *app, client_ctx_t *ctx)
{
    tiku_app_services_t services;
    void *state = NULL;

    memset(&services, 0, sizeof services);
    services.ctx = ctx;
    services.open = svc_open;
    services.frame = svc_frame;
    services.present = svc_present;
    services.menus = svc_menus;
    services.close = svc_close;
    services.pick = svc_pick;
    services.place = svc_place;
    services.raise_window = svc_raise;
    services.sizeable = svc_sizeable;
    services.clip_set = svc_clip_set;
    services.clip_get = svc_clip_get;
    services.board_names = svc_board_names;
    services.board_run = svc_board_run;
    services.board_input = svc_board_input;
    services.board_stop = svc_board_stop;
        services.board_save = svc_board_save;
        services.board_port = svc_board_port;
    services.board_mcu = svc_board_mcu;
    services.board_release = svc_board_release;
    services.open_path = svc_open_path;
    services.help_page = svc_help_page;
        services.board_reclaim = svc_board_reclaim;
    services.board_find = svc_board_find;
    services.current_device = svc_current_device;
    if (app->start != NULL && app->start(&state, &services) != 0) {
        tiku_remote_disconnect(&ctx->remote);
        return 1;
    }
    for (;;) {
        int done = 0;
        int dead = 0;

        /* DRAIN what is queued before sleeping: a drag is many messages,
         * and one-per-nap starves it. */
        for (;;) {
            tiku_event_t event;
            uint32_t id = 0;
            int command = 0;
            int type = tiku_remote_read(&ctx->remote, &id, &event,
                                             &command);

            if (type == 0) {
                break;
            }
            if (type < 0) {
                dead = 1;
                break;
            }
            if (type == TIKU_RMSG_CLOSED) {
                /* The runtime took a window.  An application holding
                 * others keeps going; the last one going is the end. */
                if (app->closed != NULL) {
                    app->closed(state, id);
                }
                if (ctx->windows > 0) {
                    ctx->windows--;
                }
                if (ctx->windows <= 0) {
                    dead = 1;
                    break;
                }
                continue;
            }
            if (type == TIKU_RMSG_EVENT &&
                tiku_app_deliver_event(app, state, id, &event)) {
                done = 1;
            }
            if (type == TIKU_RMSG_PICK && app->pick != NULL &&
                app->pick(state, id, command)) {
                done = 1;
            }
            if (type == TIKU_RMSG_TELL) {
                /* The shell said something.  One thing is worth hearing
                 * today: the path a pick() ended in. */
                tiku_msg_t *heard = tiku_remote_heard(&ctx->remote);

                if (heard != NULL) {
                    if (tiku_msg_what(heard) == TIKU_MSG_BOARDS) {
                        const char *n = tiku_msg_find_string(heard,
                                                             "names", 0);
                        const char *cur = tiku_msg_find_string(heard,
                                                               "current", 0);
                        char was[64];

                        snprintf(ctx->boards, sizeof ctx->boards, "%s",
                                 (n != NULL) ? n : "");
                        snprintf(was, sizeof was, "%s", ctx->current);
                        snprintf(ctx->current, sizeof ctx->current, "%s",
                                 (cur != NULL) ? cur : "");
                        /* An application out here defaults its work to the
                         * current device as one embedded in the shell does,
                         * so the change has to reach it the same way. */
                        if (strcmp(was, ctx->current) != 0 &&
                            app->board_said != NULL) {
                            app->board_said(state, TIKU_APP_BOARD_CURRENT,
                                            ctx->current);
                        }
                    }
                    if (tiku_msg_what(heard) == TIKU_MSG_BOARDOUT &&
                        app->board_said != NULL) {
                        int32_t what = 0;
                        const char *text = tiku_msg_find_string(heard,
                                                                "text", 0);

                        (void)tiku_msg_find_int32(heard, "what", 0, &what);
                        app->board_said(state, (int)what,
                                        (text != NULL) ? text : "");
                    }
                    if (tiku_msg_what(heard) == TIKU_MSG_CLIP) {
                        const char *text =
                            tiku_msg_find_string(heard, "text", 0);

                        snprintf(ctx->clip, sizeof ctx->clip, "%s",
                                 (text != NULL) ? text : "");
                    }
                    if (tiku_msg_what(heard) == TIKU_MSG_PICKED &&
                        app->picked != NULL) {
                        const char *path =
                            tiku_msg_find_string(heard, "path", 0);
                        int32_t win = 0;

                        (void)tiku_msg_find_int32(heard, "window", 0,
                                                  &win);
                        if (path != NULL) {
                            app->picked(state, (uint32_t)win, path);
                        }
                    }
                    tiku_msg_free(heard);
                }
            }
        }
        if (app->tick != NULL) {
            app->tick(state, now_us());
        }
        if (done || dead) {
            break;
        }
        {
            /* Woken by the desktop rather than by a clock: a keystroke
             * reaches the application as it arrives instead of at the
             * end of whatever nap was already running.
             *
             * The timeout stays SHORT and unconditional because tick is
             * how an application drives everything the session does not
             * carry -- a terminal's pty, a clock's minute -- and this
             * runtime cannot know what else it is waiting on. */
            struct pollfd fds;

            fds.fd = ctx->remote.fd;
            fds.events = POLLIN;
            fds.revents = 0;
            (void)poll(&fds, 1, TIKU_CLIENT_TICK_MS);
        }
    }
    if (app->stop != NULL) {
        app->stop(state);
    }
    tiku_remote_disconnect(&ctx->remote);
    return 0;
}

int
tiku_client_run(const tiku_app_descriptor_t *app)
{
    client_ctx_t ctx;

    if (app == NULL) {
        return 1;
    }
    memset(&ctx, 0, sizeof ctx);
    if (tiku_remote_connect(&ctx.remote,
                                 (app->name != NULL) ? app->name : "app",
                                 8000) != 0) {
        return 1;
    }
    return pump(app, &ctx);
}

int
tiku_client_run_fd(const tiku_app_descriptor_t *app, int fd)
{
    client_ctx_t ctx;

    if (app == NULL) {
        return 1;
    }
    memset(&ctx, 0, sizeof ctx);
    if (tiku_remote_connect_fd(&ctx.remote,
            (app->name != NULL) ? app->name : "app", fd) != 0) {
        return 1;
    }
    return pump(app, &ctx);
}
