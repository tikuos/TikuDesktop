/*
 * TikuTracker -- the file manager for TikuOS.
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
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#include "tiku_dl.h"
#include "tiku_iconpaint.h"
#include "tiku_remote.h"
#include "tiku_ui.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/*
 * A desktop is a process of its own, reading the line as the bytes
 * arrive.  This test is one thread, and a pty holds as little as 1024
 * bytes (macOS; Linux holds several kilobytes), so a client writing a
 * frame larger than that would be waiting for a reader that is itself.
 *
 * A shovel thread stands where that process would: it moves bytes
 * between the pseudo-terminal and a socket pair the listener is adopted
 * on, so the line is always being drained.  The session still crosses a
 * real pty, which is the claim under test; what changes is only that
 * something is reading the far end, as something always is.
 *
 * One buffer per direction, and a direction holding bytes is not read
 * from again until they have gone, so neither end can be filled by the
 * other and the shovel cannot wedge.
 */
struct shovel_dir {
    int           from, to;
    size_t        at, len;
    unsigned char buf[8192];
};

static struct shovel_dir shovel_way[2];
static pthread_t         shovel_thread;
static volatile sig_atomic_t shovel_stop;

/** @brief Move bytes both ways until the test says stop. */
static void *
shovel(void *unused)
{
    struct pollfd p[2];
    int i;

    (void)unused;
    while (!shovel_stop) {
        for (i = 0; i < 2; i++) {
            int holding = shovel_way[i].at < shovel_way[i].len;

            p[i].fd = holding ? shovel_way[i].to : shovel_way[i].from;
            p[i].events = holding ? POLLOUT : POLLIN;
            p[i].revents = 0;
        }
        if (poll(p, 2u, 20) <= 0) {
            continue;
        }
        for (i = 0; i < 2; i++) {
            struct shovel_dir *d = &shovel_way[i];
            ssize_t n;

            if ((p[i].revents & (POLLERR | POLLNVAL)) != 0) {
                shovel_stop = 1;    /* the line went; stop, do not spin */
                break;
            }
            if (d->at < d->len) {
                if ((p[i].revents & POLLOUT) == 0) {
                    continue;
                }
                n = write(d->to, d->buf + d->at, d->len - d->at);
                if (n > 0) {
                    d->at += (size_t)n;
                } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    shovel_stop = 1;
                    break;
                }
            } else {
                if ((p[i].revents & (POLLIN | POLLHUP)) == 0) {
                    continue;
                }
                n = read(d->from, d->buf, sizeof d->buf);
                if (n > 0) {
                    d->at = 0u;
                    d->len = (size_t)n;
                } else if (n == 0 ||
                           (errno != EAGAIN && errno != EINTR)) {
                    shovel_stop = 1;    /* end of the line, not a stall */
                    break;
                }
            }
        }
    }
    return NULL;
}

/** @brief Start the shovel between the line and the desktop's end. */
static int
shovel_start(int line, int desk)
{
    /* Room for the largest message in one piece.  The pty cannot be given
     * any, which is the whole reason this thread exists; the pair can, and
     * a desktop reading a 10 KB menu set should not have to be polled
     * eight times to see the front of it. */
    int room = 256 * 1024;

    (void)setsockopt(desk, SOL_SOCKET, SO_SNDBUF, &room, sizeof room);
    (void)setsockopt(desk, SOL_SOCKET, SO_RCVBUF, &room, sizeof room);
    (void)fcntl(line, F_SETFL, O_NONBLOCK);
    (void)fcntl(desk, F_SETFL, O_NONBLOCK);
    shovel_way[0].from = line; shovel_way[0].to = desk;
    shovel_way[1].from = desk; shovel_way[1].to = line;
    shovel_stop = 0;
    return pthread_create(&shovel_thread, NULL, shovel, NULL) == 0;
}

/** @brief Stop it, and wait: the descriptors are closed after this. */
static void
shovel_end(void)
{
    shovel_stop = 1;
    (void)pthread_join(shovel_thread, NULL);
}

int
main(void)
{
    tiku_remote_listener_t listener;
    tiku_remote_client_t client;
    int master, slave, desk[2];
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
    check(socketpair(AF_UNIX, SOCK_STREAM, 0, desk) == 0,
          "a pair stands in for the desktop's own end of the line");
    {
        int room = 256 * 1024;

        (void)setsockopt(desk[0], SOL_SOCKET, SO_SNDBUF, &room, sizeof room);
        (void)setsockopt(desk[0], SOL_SOCKET, SO_RCVBUF, &room, sizeof room);
    }
    check(shovel_start(master, desk[1]),
          "and something is reading the line, as something always is");
    check(tiku_remote_adopt(&listener, desk[0]) == 0,
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
            if (listener.session[0].used && listener.session[0].win[0].opened &&
                listener.session[0].win[0].frame != NULL &&
                listener.session[0].win[0].has_menus) {
                break;
            }
            usleep(10000);
        }
        check(listener.session[0].used && listener.session[0].win[0].opened,
              "OPEN crossed: the session wants its window");
        check(strcmp(listener.session[0].win[0].title, "Over the wire") == 0,
              "with its title intact");
        check(listener.session[0].win[0].frame != NULL &&
              listener.session[0].win[0].fw == 40 &&
              listener.session[0].win[0].frame[0] == 0x00ffcb00u,
              "FRAME crossed: the pixels arrived as sent");
        check(listener.session[0].win[0].has_menus &&
              listener.session[0].win[0].menus.nmenu == 1 &&
              strcmp(listener.session[0].win[0].menus.menu[0].item[0].label,
                     "Ping") == 0,
              "MENUS crossed: the same struct is the wire format");
        {
            /*
             * The handshake goes BOTH ways now.
             *
             * A client said what it could do and the desktop never
             * answered, so a client had no way to know whether the far
             * end could play a command stream -- and sent one anyway.
             * That is survivable only while both ends are built
             * together: an op the far end has not got is stepped over
             * (tiku_dl.h), so the window arrives missing whatever it
             * drew, and the list still says it is whole.
             */
            uint32_t wid = 0;
            tiku_event_t ev;
            int cmd = 0, type = 0;

            for (spins = 0; spins < 200; spins++) {
                type = tiku_remote_read(&client, &wid, &ev, &cmd);
                if (type == (int)TIKU_RMSG_WELCOME) {
                    break;
                }
                (void)tiku_remote_poll(&listener);
                usleep(10000);
            }
            check(type == (int)TIKU_RMSG_WELCOME,
                  "the desktop answers a HELLO rather than only hearing "
                  "one");
            check((tiku_remote_peer_features(&client) &
                   TIKU_FEAT_COMMAND_STREAM) != 0u,
                  "and says it can play a stream, which is the thing a "
                  "client has to know before it sends one");
            check((tiku_remote_peer_features(&client) &
                   TIKU_FEAT_ICON_STREAM) != 0u,
                  "and that its streams may carry icons, claimed apart "
                  "from the stream itself: an end could play one without "
                  "the other");
        }
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
            listener.session[0].win[0].window = &fake;
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
         * And the third way to hand a window over: not the pixels but
         * what was DRAWN.  The claim is that nothing downstream can tell
         * -- so the pixels the session ends up holding must be the ones
         * drawing it here would have produced, and the bytes that
         * crossed must be a fraction of them.
         */
        {
            tiku_dl_t *dl = tiku_dl_new();
            tiku_surface_t here;
            uint32_t *mine = calloc(64u * 32u, sizeof *mine);
            size_t stream;
            const uint32_t *theirs;
            int differing = 0, k;

            memset(&here, 0, sizeof here);
            here.px = mine;
            here.w = 64; here.h = 32; here.scale = 1;
            here.clip.w = 64; here.clip.h = 32;

            (void)tiku_dl_panel(dl, (tiku_rect_t){ 0, 0, 64, 32 });
            (void)tiku_dl_button(dl, (tiku_rect_t){ 4, 4, 56, 20 },
                                 "Go", 0u);
            tiku_ui_panel(&here, (tiku_rect_t){ 0, 0, 64, 32 });
            tiku_ui_button(&here, (tiku_rect_t){ 4, 4, 56, 20 },
                                "Go", 0u);
            stream = tiku_dl_flat_size(dl);

            check(tiku_remote_draw(&client, 1u, dl, 64, 32) == 1,
                  "a window goes over as what was drawn, not as pixels");
            for (spins = 0; spins < 200; spins++) {
                (void)tiku_remote_poll(&listener);
                if (listener.session[0].win[0].fw == 64 &&
                    listener.session[0].win[0].fh == 32) {
                    break;
                }
                usleep(10000);
            }
            theirs = tiku_remote_pixels(&listener.session[0].win[0]);
            for (k = 0; theirs != NULL && k < 64 * 32; k++) {
                if (theirs[k] != mine[k]) {
                    differing++;
                }
            }
            check(theirs != NULL && differing == 0,
                  "and the far end holds the pixels drawing it here "
                  "would have made -- every one of 2048");
            check(stream * 20u < 64u * 32u * 4u,
                  "for a twentieth of the bytes, which is the whole "
                  "reason a device on a serial line can be drawn to");

            /*
             * And a window WITH AN ICON, which until the icon op was
             * the one thing that forced every such window back to the
             * pixel path.  Recorded by drawing -- the same road a real
             * window takes -- sent as commands, and the far end's
             * pixels are the ones drawing it here made.
             */
            {
                tiku_dl_t *idl = tiku_dl_new();
                tiku_surface_t mine2;
                uint32_t *px2 = calloc(64u * 32u, sizeof *px2);
                const uint32_t *theirs2;
                int diff2 = 0, k2;

                tiku_dl_set_icon_painter(tiku_icon_stream_play);
                memset(&mine2, 0, sizeof mine2);
                mine2.px = px2;
                mine2.w = 64; mine2.h = 32; mine2.scale = 1;
                mine2.clip.w = 64; mine2.clip.h = 32;
                mine2.record = idl;
                tiku_fill(&mine2, (tiku_rect_t){ 0, 0, 64, 32 },
                          TIKU_RGB(216, 216, 216));
                (void)tiku_icon_paint(&mine2, "folder", 4, 4, 24);
                mine2.record = NULL;
                check(tiku_dl_misses(idl) == 0 &&
                      tiku_dl_icons(idl) == 1,
                      "an icon window records whole now, where it used "
                      "to be the picture that spoiled the list");
                check(tiku_remote_draw(&client, 1u, idl, 64, 32) == 1,
                      "and goes over as commands, art and all");
                for (spins = 0; spins < 200; spins++) {
                    (void)tiku_remote_poll(&listener);
                    theirs2 = tiku_remote_pixels(&listener.session[0].win[0]);
                    if (theirs2 != NULL &&
                        theirs2[4 * 64 + 4] != mine[4 * 64 + 4]) {
                        /* the icon frame replaced the old window */
                    }
                    diff2 = 0;
                    for (k2 = 0; k2 < 64 * 32; k2++) {
                        if (theirs2 != NULL && theirs2[k2] != px2[k2]) {
                            diff2++;
                        }
                    }
                    if (theirs2 != NULL && diff2 == 0) {
                        break;
                    }
                    usleep(10000);
                }
                check(diff2 == 0,
                      "and the far end holds every pixel drawing it here "
                      "made: the art crossed once as its definition and "
                      "was rasterised again over there");
                tiku_dl_free(idl);
                free(px2);
            }
            /*
             * A DRAW is a WHOLE window, as a frame is.  So a second one
             * that covers less than the first must not leave the first
             * showing through the gap -- which is what happens if the
             * surface it plays into is not cleared before it plays.
             */
            {
                tiku_dl_t *small = tiku_dl_new();
                int lingering = 0;

                (void)tiku_dl_fill(small, (tiku_rect_t){ 0, 0, 8, 8 },
                                   TIKU_C_DOC);
                (void)tiku_remote_draw(&client, 1u, small, 64, 32);
                for (spins = 0; spins < 200; spins++) {
                    (void)tiku_remote_poll(&listener);
                    theirs = tiku_remote_pixels(&listener.session[0].win[0]);
                    if (theirs != NULL && theirs[63] != mine[63]) {
                        break;
                    }
                    usleep(10000);
                }
                /*
                 * Everything the small one does NOT cover has to be
                 * clear.  Asked that way rather than "differs from the
                 * old", because a pixel may honestly share a colour with
                 * what was there before -- the button's highlight is
                 * white and so is the new fill.
                 */
                theirs = tiku_remote_pixels(&listener.session[0].win[0]);
                for (k = 0; theirs != NULL && k < 64 * 32; k++) {
                    int row = k / 64, col = k % 64;

                    if ((row >= 8 || col >= 8) && theirs[k] != 0u) {
                        lingering++;
                    }
                }
                check(theirs != NULL && lingering == 0,
                      "and a smaller one after it leaves nothing of the "
                      "first showing through: a stream is a whole window");
                tiku_dl_free(small);
            }

            /*
             * And a DRAW built by hand to lie about its size.  The client
             * end refuses these before sending, so it is written straight
             * onto the wire -- which is the only way the far end will
             * ever meet one, and the reason it checks at all.
             */
            {
                unsigned char msg[32];
                uint32_t type = TIKU_RMSG_DRAW, len = 12u;
                /*
                 * Big enough to be absurd, small enough that the machine
                 * would happily give it: 4096 x 4096 is sixty-four
                 * megabytes taken on the word of a twenty-byte message.
                 * A size chosen so large that malloc refuses would test
                 * malloc, not the check.
                 */
                int32_t huge = 4096;
                uint32_t one = 1u;

                memcpy(msg, &type, 4);
                memcpy(msg + 4, &len, 4);
                memcpy(msg + 8, &one, 4);
                memcpy(msg + 12, &huge, 4);
                memcpy(msg + 16, &huge, 4);
                (void)!write(client.fd, msg, 20u);
                for (spins = 0; spins < 40; spins++) {
                    (void)tiku_remote_poll(&listener);
                    usleep(5000);
                }
                check(listener.session[0].win[0].fw == 64 &&
                      listener.session[0].win[0].fh == 32,
                      "a stream claiming a sixty-four megabyte window is "
                      "refused, and the window it had is untouched");
            }
            tiku_dl_free(dl);
            free(mine);
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
    shovel_end();
    (void)close(desk[1]);
    (void)close(master);
    (void)close(slave);
    printf("\n%s\n", fails ? "FAILURES" : "the wire does not care what "
           "carries it");
    return fails ? 1 : 0;
}
