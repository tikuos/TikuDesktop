/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_codeview_host.c - a code view in a small application, hosted in
 * the test's own process and in a child over the desktop transport, as
 * display lists and as pixels.  The same input reaches both, and every
 * frame the desktop shows must be the frame drawn in process: typing,
 * moves, a press and drag, a scrollbar, a face change, a tiny window, a
 * very large one, and closing the window, which frees the view.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE 1
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tiku_client.h"
#include "tiku_codeview.h"
#include "tiku_dl.h"
#include "tiku_editdoc_private.h"
#include "tiku_remote.h"

static int fails;

/** @brief Count a failure and say which contract row it belongs to. */
static void
check(int cond, const char *row, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-9s %s\n", row, what);
        fails++;
    }
}

/*---------------------------------------------------------------------------*/
/* The application: one window that is one code view.                        */
/*---------------------------------------------------------------------------*/

static const char sample[] =
    "10 REM a program to look at\n"
    "20 FOR I = 1 TO 10\n"
    "30 \tPRINT \"line \"; I\n"
    "40 NEXT I\n"
    "50 PRINT \"caf\xc3\xa9 \xce\xb1\xce\xb2\"\n"
    "60 END\n";

/* Frames go as pixels only, never as a list: set before the child forks. */
static int pixels_only;

typedef struct {
    const tiku_app_services_t *svc;
    uint32_t id;
    int w, h;
    tiku_surface_t *surface;
    tiku_dl_t *dl;
    tiku_editdoc_t *doc;
    tiku_codeview_t *view;
} host_t;

/** @brief Paint the whole window and hand it over. */
static void
host_paint(host_t *h)
{
    tiku_rect_t all = { 0, 0, h->w, h->h };

    tiku_dl_clear(h->dl);
    h->surface->record = h->dl;
    tiku_codeview_set_bounds(h->view, all);
    tiku_codeview_paint(h->view, h->surface);
    h->surface->record = NULL;
    if (pixels_only || h->svc->present == NULL)
        (void)h->svc->frame(h->svc->ctx, h->id, h->surface->px, h->w, h->h);
    else
        (void)h->svc->present(h->svc->ctx, h->id, h->dl, h->surface->px,
                              h->w, h->h);
}

static int
host_start(void **state, const tiku_app_services_t *services)
{
    host_t *h = calloc(1, sizeof *h);

    if (h == NULL)
        return 1;
    h->svc = services;
    h->w = 360;
    h->h = 240;
    if (tiku_editdoc_create(NULL, &h->doc) != TIKU_EDIT_OK ||
        tiku_editdoc_load(h->doc, sample, strlen(sample)) != TIKU_EDIT_OK ||
        tiku_codeview_create(h->doc, &h->view) != TIKU_EDIT_OK)
        return 1;
    tiku_codeview_set_options(h->view, TIKU_CODEVIEW_LINE_NUMBERS |
                                       TIKU_CODEVIEW_VSCROLL |
                                       TIKU_CODEVIEW_HSCROLL |
                                       TIKU_CODEVIEW_INVISIBLES);
    tiku_codeview_set_language(h->view, TIKU_SYNTAX_BASIC);
    h->surface = tiku_surface_new(h->w, h->h, TIKU_C_PANEL);
    h->dl = tiku_dl_new();
    h->id = services->open(services->ctx, "Code view", h->w, h->h);
    if (h->surface == NULL || h->dl == NULL || h->id == 0)
        return 1;
    if (services->sizeable != NULL)
        (void)services->sizeable(services->ctx, h->id);
    *state = h;
    host_paint(h);
    return 0;
}

static void
host_stop(void *state)
{
    host_t *h = state;

    if (h == NULL)
        return;
    tiku_codeview_destroy(h->view);
    tiku_editdoc_release(h->doc);
    tiku_surface_free(h->surface);
    tiku_dl_free(h->dl);
    free(h);
}

/** @brief Command and plus or minus change the face's size; Command W
 *         ends; a resize repaints at the new size; the rest is the
 *         view's. */
static int
host_event(void *state, const tiku_event_t *e)
{
    host_t *h = state;

    if (e->type == TIKU_EVENT_RESIZE && e->width > 0 && e->height > 0) {
        h->w = e->width;
        h->h = e->height;
        if (tiku_surface_resize(h->surface, h->w, h->h, TIKU_C_PANEL) != 0)
            return 1;
    } else if (e->type == TIKU_EVENT_KEY_DOWN &&
               (e->modifiers & TIKU_MOD_CMD) &&
               (e->key == '+' || e->key == '-' || e->key == 'w')) {
        if (e->key == 'w')
            return 1;
        (void)tiku_font_set_size(e->key == '+' ? 16 : 12);
    } else if (!tiku_codeview_event(h->view, e)) {
        return 0;
    }
    host_paint(h);
    return 0;
}

static const tiku_app_descriptor_t host_app = {
    .id = "test.codeview", .name = "Code view",
    .start = host_start, .stop = host_stop, .event = host_event
};

/*---------------------------------------------------------------------------*/
/* Hosted in process.                                                         */
/*---------------------------------------------------------------------------*/

static uint32_t *local_px;
static int local_w, local_h, local_replays_ok = 1, local_frames;

static uint32_t
l_open(void *ctx, const char *title, int w, int h)
{
    (void)ctx;
    (void)title;
    (void)w;
    (void)h;
    return 1;
}

/** @brief Keep the frame, and check its list plays back to it. */
static int
l_present(void *ctx, uint32_t id, const struct tiku_dl *dl,
          const uint32_t *px, int w, int h)
{
    size_t n = (size_t)w * (size_t)h;
    tiku_surface_t *played = tiku_surface_new(w, h, TIKU_C_PANEL);

    (void)ctx;
    (void)id;
    if (dl != NULL && played != NULL) {
        (void)tiku_dl_play(dl, played);
        if (tiku_dl_misses(dl) != 0 ||
            memcmp(played->px, px, n * sizeof *px) != 0)
            local_replays_ok = 0;
    }
    tiku_surface_free(played);
    free(local_px);
    local_px = malloc(n * sizeof *px);
    if (local_px != NULL)
        memcpy(local_px, px, n * sizeof *px);
    local_w = w;
    local_h = h;
    local_frames++;
    return 1;
}

static int
l_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    return l_present(ctx, id, NULL, px, w, h);
}

static int
l_sizeable(void *ctx, uint32_t id)
{
    (void)ctx;
    (void)id;
    return 1;
}

/*---------------------------------------------------------------------------*/
/* Hosted over the desktop transport.                                         */
/*---------------------------------------------------------------------------*/

/** @brief Read until the desktop shows the frame the local host drew,
 *         without hiding a lost session. */
static int
await_frame(tiku_remote_listener_t *desk)
{
    int i;

    for (i = 0; i < 2500; i++) {
        tiku_remote_session_t *s;
        const uint32_t *px;

        tiku_remote_poll(desk);
        s = &desk->session[0];
        if (!s->used)
            return 0;
        px = tiku_remote_pixels(&s->win[0]);
        if (px != NULL && local_px != NULL && s->win[0].fw == local_w &&
            s->win[0].fh == local_h &&
            memcmp(px, local_px, (size_t)local_w * (size_t)local_h *
                   sizeof *px) == 0)
            return 1;
        (void)tiku_remote_wait(desk, 2000);
    }
    return 0;
}

/** @brief One input, to both hosts; whether the desktop then shows the
 *         frame drawn in process. */
static int
both(void *local, tiku_remote_listener_t *desk, tiku_window_t *win,
     const tiku_event_t *e)
{
    tiku_remote_event(desk, win, e);
    (void)host_event(local, e);
    return await_frame(desk);
}

/** @brief A key event. */
static tiku_event_t
key(unsigned k, unsigned mods, const char *text)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_KEY_DOWN;
    e.key = k;
    e.modifiers = mods;
    if (text != NULL)
        memcpy(e.text, text, strnlen(text, sizeof e.text - 1));
    return e;
}

/** @brief A pointer event of @p type at @p x, @p y. */
static tiku_event_t
pointer(tiku_event_type_t type, int x, int y, int64_t at)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.button = type == TIKU_EVENT_POINTER_MOVE ? 0 : 1;
    e.time_us = at;
    return e;
}

/** @brief A resize to @p w by @p h. */
static tiku_event_t
resize(int w, int h)
{
    tiku_event_t e;

    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_RESIZE;
    e.width = w;
    e.height = h;
    return e;
}

/* An allocator that counts the editor kit's live blocks. */
static long live;

static void *
t_alloc(size_t n)
{
    size_t *p = malloc(n + 16);

    if (p == NULL)
        return NULL;
    live++;
    return (char *)p + 16;
}

static void *
t_realloc(void *old, size_t n)
{
    char *p;

    if (old == NULL)
        return t_alloc(n);
    p = realloc((char *)old - 16, n + 16);
    return p != NULL ? p + 16 : NULL;
}

static void
t_free(void *p)
{
    if (p != NULL) {
        live--;
        free((char *)p - 16);
    }
}

/** @brief Drive one session over the transport, the local host beside
 *         it, and compare every frame. */
static void
session(const char *how)
{
    tiku_ed_allocator_t counted = { t_alloc, t_realloc, t_free };
    tiku_app_services_t services;
    tiku_remote_listener_t desk;
    tiku_window_t fake;
    void *local = NULL;
    int pair[2], status = 0, i, ok;
    pid_t child;
    char what[160];
    static const char typed[] = "70 PRINT \"\xc3\xa9t\xc3\xa9\"";

    memset(&services, 0, sizeof services);
    services.open = l_open;
    services.present = l_present;
    services.frame = l_frame;
    services.sizeable = l_sizeable;
    (void)tiku_font_set_size(12);
    tiku_ed_test_allocator(&counted);
    live = 0;
    if (host_start(&local, &services) != 0)
        exit(1);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
        exit(1);
    child = fork();
    if (child == 0) {
        close(pair[0]);
        _exit(tiku_client_run_fd(&host_app, pair[1]));
    }
    close(pair[1]);
    memset(&desk, 0, sizeof desk);
    memset(&fake, 0, sizeof fake);
    desk.fd = -1;
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++)
        desk.session[i].fd = -1;
    if (tiku_remote_adopt(&desk, pair[0]) != 0)
        exit(1);
    ok = await_frame(&desk);
    desk.session[0].win[0].window = &fake;
    snprintf(what, sizeof what, "%s: the first frame is the one drawn in "
             "process", how);
    check(ok, "HOST-001", what);

    {
        tiku_event_t e;

        e = key(TIKU_KEY_END, TIKU_MOD_CMD, NULL);
        ok = both(local, &desk, &fake, &e);
        e = key(TIKU_KEY_RETURN, 0, NULL);
        ok = ok && both(local, &desk, &fake, &e);
        for (i = 0; typed[i] && ok; ) {
            char one[8] = { 0 };
            int n = ((unsigned char)typed[i] & 0xE0) == 0xC0 ? 2 : 1;

            memcpy(one, typed + i, (size_t)n);
            e = key((unsigned char)typed[i], 0, one);
            ok = both(local, &desk, &fake, &e);
            i += n;
        }
        snprintf(what, sizeof what, "%s: typing, Return and UTF-8 show the "
                 "same frames", how);
        check(ok, "HOST-002", what);
        e = key(TIKU_KEY_UP, TIKU_MOD_SHIFT, NULL);
        ok = both(local, &desk, &fake, &e);
        e = key(TIKU_KEY_LEFT, TIKU_MOD_OPTION, NULL);
        ok = ok && both(local, &desk, &fake, &e);
        e = key(TIKU_KEY_HOME, TIKU_MOD_CMD, NULL);
        ok = ok && both(local, &desk, &fake, &e);
        snprintf(what, sizeof what, "%s: selections and moves too", how);
        check(ok, "HOST-002", what);
        e = pointer(TIKU_EVENT_POINTER_DOWN, 60, 40, 1000000);
        ok = both(local, &desk, &fake, &e);
        e = pointer(TIKU_EVENT_POINTER_MOVE, 120, 70, 0);
        ok = ok && both(local, &desk, &fake, &e);
        e = pointer(TIKU_EVENT_POINTER_UP, 120, 70, 0);
        ok = ok && both(local, &desk, &fake, &e);
        e = pointer(TIKU_EVENT_POINTER_DOWN, 352, 200, 3000000);
        ok = ok && both(local, &desk, &fake, &e);
        e = pointer(TIKU_EVENT_POINTER_UP, 352, 200, 0);
        ok = ok && both(local, &desk, &fake, &e);
        snprintf(what, sizeof what, "%s: a press, a drag and a scrollbar "
                 "arrow", how);
        check(ok, "HOST-002", what);
        e = key('+', TIKU_MOD_CMD, NULL);
        ok = both(local, &desk, &fake, &e);
        e = key(TIKU_KEY_DOWN, 0, NULL);
        ok = ok && both(local, &desk, &fake, &e);
        e = key('-', TIKU_MOD_CMD, NULL);
        ok = ok && both(local, &desk, &fake, &e);
        snprintf(what, sizeof what, "%s: a change of face size", how);
        check(ok, "HOST-003", what);
        e = resize(30, 20);
        ok = both(local, &desk, &fake, &e);
        e = key(TIKU_KEY_DOWN, 0, NULL);
        ok = ok && both(local, &desk, &fake, &e);
        snprintf(what, sizeof what, "%s: a window too small for its "
                 "margins", how);
        check(ok, "HOST-003", what);
        e = resize(3840, 2160);
        ok = both(local, &desk, &fake, &e);
        e = key(TIKU_KEY_END, 0, NULL);
        ok = ok && both(local, &desk, &fake, &e);
        e = resize(480, 300);
        ok = ok && both(local, &desk, &fake, &e);
        snprintf(what, sizeof what, "%s: a 3840 by 2160 window and back", how);
        check(ok, "HOST-003", what);
        snprintf(what, sizeof what, "%s: the last frame went as %s", how,
                 pixels_only ? "pixels" : "a list");
        check(desk.session[0].win[0].list_fresh == !pixels_only, "HOST-001",
              what);

        e = key('w', TIKU_MOD_CMD, NULL);
        tiku_remote_event(&desk, &fake, &e);
    }
    for (i = 0; i < 2500 && child != 0; i++) {
        tiku_remote_poll(&desk);
        if (waitpid(child, &status, WNOHANG) == child)
            child = 0;
        else
            usleep(2000);
    }
    if (child != 0) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
    snprintf(what, sizeof what, "%s: closing the window ends the child "
             "cleanly", how);
    check(child == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "HOST-004", what);
    tiku_remote_shutdown(&desk);
    host_stop(local);
    snprintf(what, sizeof what, "%s: and the view and document free all "
             "they held", how);
    check(live == 0, "HOST-004", what);
    tiku_ed_test_allocator(NULL);
}

int
main(void)
{
    (void)tiku_font_set_family(0);
    (void)tiku_font_set_size(12);
    pixels_only = 0;
    session("display lists");
    pixels_only = 1;
    session("pixels");
    check(local_frames > 20 && local_replays_ok, "HOST-001",
          "in process, every frame's list plays back to its pixels");
    free(local_px);
    printf("%s\n", fails ? "FAIL: code view hosting" :
           "PASS: code view hosting -- in process and over the transport, "
           "as lists and as pixels, through typing, moves, faces, sizes "
           "and closing");
    return fails != 0;
}
