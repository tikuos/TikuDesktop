/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_shm.c - a frame that is referenced rather than copied.
 *
 * Two peers on one machine share the surface; two joined by a line
 * cannot, and must fall back to sending the pixels.  Both are checked
 * here, because a fallback nobody exercises is a fallback that rots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1

#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

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

static void
settle(tiku_remote_listener_t *listener, int turns)
{
    struct timespec nap;

    nap.tv_sec = 0;
    nap.tv_nsec = 2 * 1000 * 1000;
    while (turns-- > 0) {
        (void)tiku_remote_poll(listener);
        (void)nanosleep(&nap, NULL);
    }
}

static void
listener_init(tiku_remote_listener_t *listener)
{
    int i;

    memset(listener, 0, sizeof *listener);
    listener->fd = -1;
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        listener->session[i].fd = -1;
    }
}

int
main(void)
{
    tiku_remote_listener_t listener;
    tiku_remote_client_t client;
    tiku_remote_session_t *s;
    uint32_t pixels[64];
    uint32_t id;
    int pair[2];
    const uint32_t *shown;

    printf("a frame is referenced, not copied (DEV-007)\n");
    (void)signal(SIGPIPE, SIG_IGN);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        printf("  [FAIL] no socket pair\n");
        return 1;
    }
    listener_init(&listener);
    (void)tiku_remote_adopt(&listener, pair[0]);
    (void)tiku_remote_connect_fd(&client, "shm", pair[1]);
    s = &listener.session[0];

    id = tiku_remote_open(&client, "Shared", 8, 8);
    settle(&listener, 40);
    check(s->opened, "the window is asked for over the socket");
    check((s->features & TIKU_FEAT_SHARED_SURFACE) != 0u,
          "and the peer says it can share a surface, being on this "
          "machine");

    memset(pixels, 0, sizeof pixels);
    pixels[0] = 0x00112233u;
    (void)tiku_remote_frame(&client, id, pixels, 8, 8);
    settle(&listener, 40);
    check(s->shared != NULL, "a surface arrives that both ends can see");
    check(s->frame == NULL,
          "and no copy of the pixels was ever sent -- that is the point");
    shown = tiku_remote_pixels(s);
    check(shown != NULL && shown[0] == 0x00112233u,
          "what the window draws is the application's own memory");

    pixels[0] = 0x00445566u;
    (void)tiku_remote_frame(&client, id, pixels, 8, 8);
    settle(&listener, 40);
    shown = tiku_remote_pixels(s);
    check(s->shown == 1, "the next frame lands in the OTHER buffer");
    check(shown != NULL && shown[0] == 0x00445566u,
          "and that is the one shown, so nothing is half drawn");

    check(tiku_remote_mapped(&listener) == 1,
          "the surface is held while the session is alive");
    tiku_remote_disconnect(&client);
    settle(&listener, 20);
    tiku_remote_shutdown(&listener);
    check(tiku_remote_mapped(&listener) == 0,
          "and given back when it ends -- a freed session hides a leak, "
          "so the mapping is counted rather than looked for");

    /* The other half: a line cannot share memory, and must not pretend. */
    {
        char slave_name[128];
        struct termios tio;
        int master, slave;

        master = posix_openpt(O_RDWR | O_NOCTTY);
        if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0 ||
            ptsname_r(master, slave_name, sizeof slave_name) != 0) {
            printf("  [FAIL] cannot make a pty pair\n");
            return 1;
        }
        slave = open(slave_name, O_RDWR | O_NOCTTY);
        if (tcgetattr(master, &tio) == 0) {
            cfmakeraw(&tio);
            (void)tcsetattr(master, TCSANOW, &tio);
        }
        if (slave >= 0 && tcgetattr(slave, &tio) == 0) {
            cfmakeraw(&tio);
            (void)tcsetattr(slave, TCSANOW, &tio);
        }
        listener_init(&listener);
        (void)tiku_remote_adopt(&listener, master);
        (void)tiku_remote_connect_fd(&client, "line", slave);
        s = &listener.session[0];

        id = tiku_remote_open(&client, "Over a line", 8, 8);
        pixels[0] = 0x00778899u;
        (void)tiku_remote_frame(&client, id, pixels, 8, 8);
        settle(&listener, 60);
        check((s->features & TIKU_FEAT_SHARED_SURFACE) == 0u,
              "a peer on a line does not claim to share memory");
        check(s->shared == NULL && s->frame != NULL,
              "so its pixels are sent, as they always were");
        shown = tiku_remote_pixels(s);
        check(shown != NULL && shown[0] == 0x00778899u,
              "and the window draws them the same way");
        tiku_remote_disconnect(&client);
        tiku_remote_shutdown(&listener);
        (void)close(slave);
    }

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe pixels cross once, or not at all\n");
    return 0;
}
