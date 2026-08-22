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
 * every message class crossing: OPEN, FRAME, MENUS up; PICK down; and a
 * self-describing message both ways, which is the one that does not need
 * the two ends to have been built together.
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

int
main(void)
{
    tiku_remote_listener_t listener;
    tiku_remote_client_t client;
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
    /*
     * Filled with rubbish on purpose.  Connecting starts a client from
     * nothing, and it has to: the struct HOLDS something now -- the last
     * message it heard -- and a pointer nobody set is one disconnect away
     * from being freed.  A zeroed stack would hide that; this does not.
     */
    memset(&client, 0xA5, sizeof client);
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
    check(tiku_remote_adopt(&listener, master) == 0,
          "the listener adopts the line as a session");
    check(tiku_remote_connect_fd(&client, "serial-app", slave) == 0,
          "the client speaks HELLO down it");
    {
        uint32_t id = tiku_remote_open(&client, "Over the wire",
                                            40, 20);
        uint32_t px[40 * 20];
        tiku_menuset_t menus;
        int i, spins;

        for (i = 0; i < 40 * 20; i++) {
            px[i] = 0x00ffcb00u;
        }
        (void)tiku_remote_frame(&client, id, px, 40, 20);
        memset(&menus, 0, sizeof menus);
        menus.nmenu = 1;
        snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Wire");
        menus.menu[0].nitem = 1;
        snprintf(menus.menu[0].item[0].label,
                 sizeof menus.menu[0].item[0].label, "Ping");
        menus.menu[0].item[0].command = 7;
        menus.menu[0].item[0].enabled = 1;
        (void)tiku_remote_menus(&client, id, &menus);
        for (spins = 0; spins < 200; spins++) {
            (void)tiku_remote_poll(&listener);
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
            tiku_event_t event;
            struct tiku_window fake;
            uint32_t evid = 0;
            int command = 0;
            int type = 0;

            memset(&fake, 0, sizeof fake);
            listener.session[0].window = &fake;
            tiku_remote_pick(&listener, &fake, 7);
            for (spins = 0; spins < 200; spins++) {
                type = tiku_remote_read(&client, &evid, &event,
                                             &command);
                if (type != 0) {
                    break;
                }
                usleep(10000);
            }
            check(type == TIKU_RMSG_PICK && command == 7,
                  "PICK crossed back: the answer rode the same line");
        }
        /*
         * And the one message class that is not a struct.  Everything
         * above is a layout both ends were compiled against; this is a
         * message that carries its own shape, which is what a device on
         * the end of a cable running another build has to send.
         */
        {
            tiku_msg_t *m = tiku_msg_new(0x44524147u);   /* DRAG */
            tiku_msg_t *item = tiku_msg_new(0x4954454Du);/* ITEM */
            tiku_msg_t *up = NULL;
            int i;

            (void)tiku_msg_add_string(m, "action", "drop");
            (void)tiku_msg_add_ref(m, "refs", "/Desktop/a.txt");
            (void)tiku_msg_add_ref(m, "refs", "/Desktop/caf\xc3\xa9.txt");
            (void)tiku_msg_add_ref(m, "refs", "/Desktop/c.txt");
            (void)tiku_msg_add_point(m, "where", 120.5f, 64.25f);
            (void)tiku_msg_add_int32(item, "size", 4242);
            (void)tiku_msg_add_message(m, "item", item);
            tiku_msg_free(item);

            check(tiku_remote_say(&client, m) == 1,
                  "a message that describes itself goes up the same line");
            tiku_msg_free(m);

            for (spins = 0; spins < 200; spins++) {
                (void)tiku_remote_poll(&listener);
                up = tiku_remote_said(&listener, 0);
                if (up != NULL) {
                    break;
                }
                usleep(10000);
            }
            check(up != NULL && tiku_msg_what(up) == 0x44524147u,
                  "and arrives knowing what it is");
            check(up != NULL && tiku_msg_count(up, "refs") == 3 &&
                  strcmp(tiku_msg_find_ref(up, "refs", 1),
                         "/Desktop/caf\xc3\xa9.txt") == 0,
                  "with its three refs in order, accents and all");
            {
                float x = 0.0f, y = 0.0f;
                tiku_msg_t *inner = (up != NULL)
                    ? tiku_msg_find_message(up, "item", 0) : NULL;
                int32_t size = 0;

                check(up != NULL &&
                      tiku_msg_find_point(up, "where", 0, &x, &y) &&
                      x == 120.5f && y == 64.25f,
                      "and the point it was dropped at");
                check(inner != NULL &&
                      tiku_msg_find_int32(inner, "size", 0, &size) &&
                      size == 4242,
                      "and a whole message nested inside it");
                tiku_msg_free(inner);
            }
            tiku_msg_free(up);

            /*
             * A burst nobody drains.  The queue holds eight and says how
             * many it let go, because a queue that quietly forgets is the
             * kind of thing found much later, in something else.
             */
            for (i = 0; i < 12; i++) {
                tiku_msg_t *b = tiku_msg_new(0x42555253u);

                (void)tiku_msg_add_int32(b, "n", i);
                (void)tiku_remote_say(&client, b);
                tiku_msg_free(b);
            }
            for (spins = 0; spins < 200; spins++) {
                (void)tiku_remote_poll(&listener);
                if (tiku_remote_lost(&listener, 0) >= 4) {
                    break;
                }
                usleep(10000);
            }
            check(tiku_remote_lost(&listener, 0) == 4,
                  "twelve said and eight held: the four that did not fit"
                  " are counted, not forgotten");
            {
                int held = 0, in_order = 1;

                for (;;) {
                    tiku_msg_t *b = tiku_remote_said(&listener, 0);
                    int32_t got_n = -1;

                    if (b == NULL) {
                        break;
                    }
                    if (!tiku_msg_find_int32(b, "n", 0, &got_n) ||
                        got_n != held) {
                        in_order = 0;
                    }
                    held++;
                    tiku_msg_free(b);
                }
                check(held == 8 && in_order,
                      "and the eight that were held come back in the order"
                      " they were said");
            }

            /* And back down, the same way. */
            {
                tiku_msg_t *down = tiku_msg_new(0x4F50454Eu);
                tiku_msg_t *heard = NULL;
                tiku_event_t event;
                uint32_t evid = 0;
                int command = 0, type = 0;

                (void)tiku_msg_add_string(down, "open",
                                               "/Desktop/notes.txt");
                (void)tiku_msg_add_bool(down, "read-only", 1);
                check(tiku_remote_tell(&listener, 0, down) == 1,
                      "the desktop can say one back down");
                tiku_msg_free(down);

                for (spins = 0; spins < 200; spins++) {
                    type = tiku_remote_read(&client, &evid, &event,
                                                 &command);
                    if (type != 0) {
                        break;
                    }
                    usleep(10000);
                }
                heard = tiku_remote_heard(&client);
                check(type == TIKU_RMSG_TELL && heard != NULL &&
                      tiku_msg_what(heard) == 0x4F50454Eu,
                      "and the application hears it");
                {
                    int ro = 0;

                    check(heard != NULL &&
                          strcmp(tiku_msg_find_string(heard, "open", 0),
                                 "/Desktop/notes.txt") == 0 &&
                          tiku_msg_find_bool(heard, "read-only", 0,
                                                  &ro) && ro == 1,
                          "with every field it was given, by name");
                }
                tiku_msg_free(heard);
                check(tiku_remote_heard(&client) == NULL,
                      "and taking it leaves nothing behind to take twice");
            }

            /*
             * One too big for the wire: refused before a byte of it is
             * written, rather than written and then found nonsense at the
             * far end, where the only answer left is to drop the link.
             *
             * Down a sink, not down the line under test.  A refusal that
             * did not happen would put four megabytes into a pty nobody
             * is draining, and the test would hang rather than fail --
             * and a test that hangs when it is wrong does not say so.
             */
            {
                size_t ceiling = 12u + 4u * (size_t)TIKU_REMOTE_MAX_DIM
                                         * (size_t)TIKU_REMOTE_MAX_DIM;
                unsigned char *big = (unsigned char *)calloc(ceiling, 1u);
                tiku_msg_t *fat = tiku_msg_new(0x42494721u);
                tiku_msg_t *thin = tiku_msg_new(0x4F4B4159u);
                tiku_remote_client_t sink;
                int null_fd = open("/dev/null", O_WRONLY);

                memset(&sink, 0xA5, sizeof sink);
                (void)tiku_remote_connect_fd(&sink, "sink", null_fd);
                (void)tiku_msg_add_int32(thin, "after", 1);
                check(big != NULL && null_fd >= 0 &&
                      tiku_msg_add_blob(fat, "payload", big, ceiling) &&
                      tiku_remote_say(&sink, fat) == 0,
                      "a message too big for the wire is refused, not sent");
                check(tiku_remote_say(&sink, thin) == 1,
                      "while one that fits still goes");
                tiku_remote_disconnect(&sink);
                tiku_msg_free(fat);
                tiku_msg_free(thin);
                free(big);
            }

            /*
             * And then leave one waiting each way and walk off.  Nothing
             * is asserted here: the assertion is what a sanitizer says
             * about the two frees below, and it says it only in a build
             * that has one.  A queue that is emptied by a tidy test is a
             * queue whose emptying is never tested.
             */
            {
                tiku_msg_t *stray = tiku_msg_new(0x4C454654u);
                tiku_event_t event;
                uint32_t evid = 0;
                int command = 0, arrived = 0;

                (void)tiku_msg_add_string(stray, "note", "nobody asks");
                (void)tiku_remote_say(&client, stray);
                (void)tiku_remote_tell(&listener, 0, stray);
                tiku_msg_free(stray);

                for (spins = 0; spins < 200; spins++) {
                    if (tiku_remote_poll(&listener)) {
                        arrived = 1;
                        break;
                    }
                    usleep(10000);
                }
                check(arrived, "one more is said, and left where it lands");
                for (spins = 0; spins < 200; spins++) {
                    if (tiku_remote_read(&client, &evid, &event,
                                              &command)
                        == TIKU_RMSG_TELL) {
                        break;
                    }
                    usleep(10000);
                }
                /* Both are still held.  Neither is taken.  Walk off. */
            }
        }
    }
    tiku_remote_disconnect(&client);
    tiku_remote_shutdown(&listener);
    printf("\n%s\n", fails ? "FAILURES" : "the wire does not care what "
           "carries it");
    return fails ? 1 : 0;
}
