/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_client_wake.c - how soon an application answers what it is sent.
 *
 * An out-of-process application is only as quick as the runtime that
 * hands it events.  This measures the round trip an event actually
 * takes, over a socket pair, with a real descriptor on the far end.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_gfx.h"
#include "tiku_remote.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

static int64_t
now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The application under test: it answers a keystroke with a frame. */
typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    uint32_t                        id;
} echo_state_t;

static int
echo_start(void **state, const tiku_app_services_t *services)
{
    echo_state_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->surface = tiku_surface_new(8, 8, TIKU_C_PANEL);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    st->id = services->open(services->ctx, "Echo", 8, 8);
    *state = st;
    return 0;
}

static void
echo_stop(void *state)
{
    echo_state_t *st = state;

    if (st != NULL) {
        tiku_surface_free(st->surface);
        free(st);
    }
}

static int
echo_event(void *state, const tiku_event_t *event)
{
    echo_state_t *st = state;

    if (event->type == TIKU_EVENT_KEY_DOWN) {
        (void)st->services->frame(st->services->ctx, st->id,
                                  st->surface->px, 8, 8);
        if (event->key == 'q') {
            return 1;
        }
    }
    return 0;
}

static const tiku_app_descriptor_t echo_app = {
    .id = "org.tikuos.test.echo",
    .name = "Echo",
    .start = echo_start,
    .stop = echo_stop,
    .event = echo_event
};

/**
 * @brief Wait for the desktop to be told something, or @p budget_ms.
 *
 * The poll's own answer is what is watched: a frame may arrive as
 * pixels or as a note that a shared surface changed, and the difference
 * is not this test's business.
 */
static int64_t
await_frame(tiku_remote_listener_t *listener, int budget_ms)
{
    int64_t start = now_ms();

    while (now_ms() - start < budget_ms) {
        struct timespec nap;

        if (tiku_remote_poll(listener)) {
            return now_ms() - start;
        }
        nap.tv_sec = 0;
        nap.tv_nsec = 1000 * 1000;
        (void)nanosleep(&nap, NULL);
    }
    return -1;
}

int
main(void)
{
    tiku_remote_listener_t listener;
    struct tiku_window fake;
    tiku_event_t key;
    int pair[2];
    pid_t kid;
    int i;
    int64_t worst = 0;

    printf("an application answers when it is spoken to (DEV-005)\n");
    (void)signal(SIGPIPE, SIG_IGN);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        printf("  [FAIL] no socket pair\n");
        return 1;
    }
    (void)fflush(stdout);
    kid = fork();
    if (kid < 0) {
        printf("  [FAIL] cannot fork the application\n");
        return 1;
    }
    if (kid == 0) {
        (void)close(pair[0]);
        (void)tiku_client_run_fd(&echo_app, pair[1]);
        (void)close(pair[1]);
        _exit(0);
    }
    (void)close(pair[1]);

    memset(&listener, 0, sizeof listener);
    listener.fd = -1;
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        listener.session[i].fd = -1;
    }
    check(tiku_remote_adopt(&listener, pair[0]) == 0,
          "the desktop takes the application's end of the pair");

    /* Wait for the window before timing anything: the open is the
     * application starting up, not the latency being measured. */
    for (i = 0; i < 400 && !listener.session[0].opened; i++) {
        struct timespec nap;

        (void)tiku_remote_poll(&listener);
        nap.tv_sec = 0;
        nap.tv_nsec = 5 * 1000 * 1000;
        (void)nanosleep(&nap, NULL);
    }
    check(listener.session[0].opened, "and it asks for a window");

    memset(&key, 0, sizeof key);
    key.type = TIKU_EVENT_KEY_DOWN;
    key.key = 'x';
    /* Events are addressed to a WINDOW; a bare listener has none, so a
     * fake one is bound to the session and the public path used. */
    memset(&fake, 0, sizeof fake);
    listener.session[0].window = &fake;
    for (i = 0; i < 5; i++) {
        int64_t took;

        tiku_remote_event(&listener, &fake, &key);
        took = await_frame(&listener, 500);
        if (took < 0) {
            worst = 500;
            break;
        }
        if (took > worst) {
            worst = took;
        }
    }
    printf("         slowest of five round trips: %d ms\n", (int)worst);
    /* A napping runtime answered in 30-60 ms; anything under 20 leaves
     * room for a loaded machine without letting the nap back in. */
    check(worst >= 0 && worst < 20,
          "an event is answered without waiting out a nap");

    key.key = 'q';
    tiku_remote_event(&listener, &fake, &key);
    for (i = 0; i < 100; i++) {
        struct timespec nap;

        (void)tiku_remote_poll(&listener);
        if (waitpid(kid, NULL, WNOHANG) == kid) {
            kid = -1;
            break;
        }
        nap.tv_sec = 0;
        nap.tv_nsec = 5 * 1000 * 1000;
        (void)nanosleep(&nap, NULL);
    }
    if (kid > 0) {
        (void)kill(kid, SIGKILL);
        (void)waitpid(kid, NULL, 0);
    }
    tiku_remote_shutdown(&listener);

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe runtime does not make an application wait\n");
    return 0;
}
