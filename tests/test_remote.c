/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tests/test_remote.c - the window session over a SERIAL-SHAPED link.
 *
 * The claim under test is the one the architecture rests on: the session
 * does not care what carries it.  Here it runs over a pseudo-terminal --
 * the same byte-stream discipline a device's serial port has -- with the
 * listener adopting one end and the client connecting on the other, and
 * every message class crossing: OPEN, FRAME, MENUS up; PICK down.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE 1
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "tiku_desk_remote.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

int
main(void)
{
    tiku_desk_remote_listener_t listener;
    tiku_desk_remote_client_t client;
    int master, slave;
    char *slave_path;

    printf("the session over a serial-shaped link (a pty pair)\n");
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
        printf("  [SKIP] no pty on this host\n");
        return 0;
    }
    slave_path = ptsname(master);
    slave = open(slave_path, O_RDWR | O_NOCTTY);
    check(slave >= 0, "the other end of the line opens");
    {
        /* Raw, as a device link would be: no tty line discipline
         * chewing the frames. */
        struct termios tio;

        (void)tcgetattr(slave, &tio);
        cfmakeraw(&tio);
        (void)tcsetattr(slave, TCSANOW, &tio);
        (void)tcgetattr(master, &tio);
        cfmakeraw(&tio);
        (void)tcsetattr(master, TCSANOW, &tio);
    }
    memset(&listener, 0, sizeof listener);
    listener.fd = -1;
    check(tiku_desk_remote_adopt(&listener, master) == 0,
          "the listener adopts the line as a session");
    check(tiku_desk_remote_connect_fd(&client, "serial-app", slave) == 0,
          "the client speaks HELLO down it");
    {
        uint32_t id = tiku_desk_remote_open(&client, "Over the wire",
                                            40, 20);
        uint32_t px[40 * 20];
        tiku_desk_menuset_t menus;
        int i, spins;

        for (i = 0; i < 40 * 20; i++) {
            px[i] = 0x00ffcb00u;
        }
        (void)tiku_desk_remote_frame(&client, id, px, 40, 20);
        memset(&menus, 0, sizeof menus);
        menus.nmenu = 1;
        snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Wire");
        menus.menu[0].nitem = 1;
        snprintf(menus.menu[0].item[0].label,
                 sizeof menus.menu[0].item[0].label, "Ping");
        menus.menu[0].item[0].command = 7;
        menus.menu[0].item[0].enabled = 1;
        (void)tiku_desk_remote_menus(&client, id, &menus);
        for (spins = 0; spins < 200; spins++) {
            (void)tiku_desk_remote_poll(&listener);
            if (listener.session[0].used && listener.session[0].opened &&
                listener.session[0].frame != NULL &&
                listener.session[0].has_menus) {
                break;
            }
            usleep(10000);
        }
        check(listener.session[0].used && listener.session[0].opened,
              "OPEN crossed: the session wants its window");
        check(strcmp(listener.session[0].title, "Over the wire") == 0,
              "with its title intact");
        check(listener.session[0].frame != NULL &&
              listener.session[0].fw == 40 &&
              listener.session[0].frame[0] == 0x00ffcb00u,
              "FRAME crossed: the pixels arrived as sent");
        check(listener.session[0].has_menus &&
              listener.session[0].menus.nmenu == 1 &&
              strcmp(listener.session[0].menus.menu[0].item[0].label,
                     "Ping") == 0,
              "MENUS crossed: the same struct is the wire format");
        {
            /* And back down: a pick.  The listener addresses sessions by
             * window; on an adopted line there is none yet, so speak to
             * the session's fd the way the desktop would after binding
             * a window -- bind a fake and use the public path. */
            tiku_desk_event_t event;
            struct tiku_desk_window fake;
            uint32_t evid = 0;
            int command = 0;
            int type = 0;

            memset(&fake, 0, sizeof fake);
            listener.session[0].window = &fake;
            tiku_desk_remote_pick(&listener, &fake, 7);
            for (spins = 0; spins < 200; spins++) {
                type = tiku_desk_remote_read(&client, &evid, &event,
                                             &command);
                if (type != 0) {
                    break;
                }
                usleep(10000);
            }
            check(type == TIKU_DESK_RMSG_PICK && command == 7,
                  "PICK crossed back: the answer rode the same line");
        }
    }
    tiku_desk_remote_disconnect(&client);
    tiku_desk_remote_shutdown(&listener);
    printf("\n%s\n", fails ? "FAILURES" : "the wire does not care what "
           "carries it");
    return fails ? 1 : 0;
}
