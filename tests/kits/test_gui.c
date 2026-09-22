/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_gui.c - the board library, run on the host against the desktop's
 * own readers.
 *
 * The kernel is five stub headers and the functions below; the library's
 * sources are included whole.  A form renders the panel text the desktop
 * parses and takes writes as the shell would deliver them; a window's
 * frames, unescaped, unflatten with the desktop's own display-list
 * reader and narrate as the desktop would say them; the desktop's
 * messages, delivered whole to the channel the library registers on the
 * console, reach the owner, queued.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku.h"
#include <kernel/process/tiku_process.h>
#include <kernel/vfs/tiku_vfs.h>
#include <kernel/link/tiku_link_console.h>

#include "tiku_dl.h"
#include "tiku_form.h"
#include "tiku_msg.h"

/* The two message kinds the desk and a board agree on (tiku_remote.h,
 * which carries the desktop's event record and so stays out of here). */
#define TIKU_MSG_PICK   0x7069636bu
#define TIKU_MSG_PICKED 0x70696b64u

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* The kernel, as far as the library needs one                               */
/*---------------------------------------------------------------------------*/

static struct tiku_process owner = { "test" };
static tiku_event_t posted_ev[16];
static void *posted_data[16];
static int posted;
static int notified;
static tiku_vfs_node_t gui_root;
static unsigned char wire[65536];
static size_t wire_len;

uint8_t
tiku_process_post(struct tiku_process *p, tiku_event_t ev,
                  tiku_event_data_t data)
{
    (void)p;
    if (posted < 16) {
        posted_ev[posted] = ev;
        posted_data[posted] = data;
    }
    posted++;
    return 1;
}

void
tiku_vfs_notify(const tiku_vfs_node_t *node)
{
    (void)node;
    notified++;
}

const tiku_vfs_node_t *
tiku_vfs_resolve(const char *path)
{
    return (strcmp(path, "/gui") == 0) ? &gui_root : NULL;
}

static void
stub_putc(char c)
{
    if (wire_len < sizeof wire) {
        wire[wire_len++] = (unsigned char)c;
    }
}

/** @brief One byte of a frame's payload, SLIP-escaped onto the wire. */
static void
stub_escape(unsigned char b)
{
    if (b == 0xC0) {
        stub_putc((char)0xDB);
        stub_putc((char)0xDC);
    } else if (b == 0xDB) {
        stub_putc((char)0xDB);
        stub_putc((char)0xDD);
    } else if (b == 0) {
        stub_putc((char)0xDB);
        stub_putc((char)0xDE);
    } else {
        stub_putc((char)b);
    }
}

#include "tiku_gui.c"
#include "tiku_draw.c"
#include "tiku_draw_shell.c"
#include "tiku_draw_basic.c"

/*---------------------------------------------------------------------------*/
/* The test                                                                  */
/*---------------------------------------------------------------------------*/

static int fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

static char listed[512];

static void
list_cb(const char *name, void *ctx)
{
    (void)ctx;
    strcat(listed, name);
    strcat(listed, " ");
}

/**
 * @brief The next frame in the wire from @p *at, unescaped into @p out.
 * @return its length, 0 when there is none.
 */
static size_t
next_frame(size_t *at, unsigned char *out, size_t max)
{
    size_t n = 0;
    int esc = 0, in = 0;

    while (*at < wire_len) {
        unsigned char b = wire[(*at)++];

        if (!in) {
            if (b == 0xC0 && *at < wire_len && wire[*at] == 0xF1) {
                in = 1;
                (*at)++;
            }
            continue;
        }
        if (b == 0xC0) {
            return n;
        }
        if (esc) {
            esc = 0;
            b = (b == 0xDC) ? 0xC0 : (b == 0xDD) ? 0xDB : (b == 0xDE) ? 0 : b;
        } else if (b == 0xDB) {
            esc = 1;
            continue;
        }
        if (n < max) {
            out[n++] = b;
        }
    }
    return 0;
}

/* The link the board opens, as the test plays it: it keeps the message
 * receiver and the marker, frames what the board sends into the wire the
 * desktop's readers parse, and delivers a message the desktop wrote. */
static struct tiku_link { int used; } the_link;
static tiku_link_recv_fn link_recv;
static void *link_ctx;
static uint8_t link_marker;
static uint8_t link_buf[512];

tiku_link_t *
tiku_link_console_open(tiku_link_console_t *lc, uint8_t marker,
                       uint8_t *buf, size_t cap)
{
    (void)lc;
    (void)buf;
    (void)cap;
    link_marker = marker;
    return &the_link;
}

void
tiku_link_on_recv(tiku_link_t *l, tiku_link_recv_fn fn, void *ctx)
{
    (void)l;
    link_recv = fn;
    link_ctx = ctx;
}

/* The stub stands in for the console's link, which confers everything. */
uint8_t
tiku_link_cap(const tiku_link_t *l)
{
    (void)l;
    return 0xFFu;
}

int
tiku_link_send2(tiku_link_t *l, const void *head, size_t hlen,
                const void *body, size_t blen)
{
    const unsigned char *h = head, *b = body;
    size_t i;

    (void)l;
    stub_putc((char)0xC0);
    stub_putc((char)link_marker);
    for (i = 0; i < hlen; i++) {
        stub_escape(h[i]);
    }
    for (i = 0; i < blen; i++) {
        stub_escape(b[i]);
    }
    stub_putc((char)0xC0);
    return 0;
}

/** @brief Deliver a whole message to the board, as its link would. */
static void
feed(const unsigned char *msg, size_t len)
{
    if (link_recv == NULL || len > sizeof link_buf) {
        return;
    }
    memcpy(link_buf, msg, len);
    link_recv(link_ctx, link_buf, len);
}

/** @brief An EVENT message for window @p id, the desktop's 64-byte record. */
static size_t
event_msg(unsigned char *out, uint32_t id, uint32_t kind, int x, int y)
{
    memset(out, 0, 8 + 4 + 64);
    out[0] = 16;
    out[4] = 68;
    memcpy(out + 8, &id, 4);
    memcpy(out + 12, &kind, 4);
    memcpy(out + 12 + 40, &x, 4);
    memcpy(out + 12 + 44, &y, 4);
    return 8 + 68;
}

int
main(void)
{
    tiku_gui_form_t form;
    static int temp, lamp, bright;
    static char name[16] = "workshop", mode[8] = "slow";
    char text[1024];
    tiku_form_t parsed;
    int unknown;

    tiku_draw_init();
    printf("the forms road\n");
    tiku_gui_form_init(&form, "demo", "Board one", &owner);
    (void)tiku_gui_text(&form, "note", "Sensors");
    (void)tiku_gui_gauge(&form, "temp", "Temperature", &temp, 0, 100);
    (void)tiku_gui_toggle(&form, "lamp", "Lamp", &lamp, NULL);
    (void)tiku_gui_field(&form, "name", "Name", name, sizeof name, NULL);
    (void)tiku_gui_slider(&form, "bright", "Brightness", &bright, 0, 100,
                          NULL);
    (void)tiku_gui_choice(&form, "mode", "Mode", "slow|fast", mode,
                          sizeof mode, NULL);
    (void)tiku_gui_button(&form, "count", "Count", NULL);
    check(tiku_gui_add(&form) == 0 && notified == 1,
          "a form is published, and the directory rings once");
    listed[0] = '\0';
    gui_dynops.list_dir("", list_cb, NULL);
    check(strcmp(listed, "demo/ ") == 0, "the root lists the form as a folder");
    listed[0] = '\0';
    gui_dynops.list_dir("demo/", list_cb, NULL);
    check(strcmp(listed, "panel note temp lamp name bright mode count ") == 0,
          "and the form lists its panel and rows in order");
    check(gui_dynops.read("demo/panel", text, sizeof text) > 0 &&
              tiku_form_parse(&parsed, text, &unknown) == 7 && unknown == 0 &&
              parsed.row[0].kind == TIKU_FORM_TEXT &&
              strcmp(parsed.row[0].bind, "/gui/demo/note") == 0 &&
              parsed.row[3].kind == TIKU_FORM_ENTRY &&
              parsed.row[4].kind == TIKU_FORM_SLIDER && parsed.row[4].hi == 100 &&
              parsed.row[5].kind == TIKU_FORM_CHOICE &&
              strcmp(parsed.row[5].options, "slow|fast") == 0,
          "the panel text parses with the desktop's own form reader");
    check(strcmp(parsed.title, "Board one") == 0, "titled as the form was");
    posted = 0;
    check(gui_dynops.write("demo/lamp", "1", 1) == 0 && lamp == 1 &&
              posted == 1 && posted_ev[0] == TIKU_EVENT_GUI &&
              posted_data[0] == &form.row[2],
          "a write to a toggle stores it and posts the row to the owner");
    check(gui_dynops.read("demo/lamp", text, sizeof text) > 0 &&
              strcmp(text, "1\n") == 0, "and the row reads back");
    check(gui_dynops.write("demo/temp", "500", 3) != 0 && temp == 0,
          "a gauge write past its range is refused");
    check(gui_dynops.write("demo/mode", "fast", 4) == 0 &&
              strcmp(mode, "fast") == 0 &&
              gui_dynops.write("demo/mode", "warp", 4) != 0 &&
              strcmp(mode, "fast") == 0,
          "a choice takes a word on offer and refuses one that is not");
    check(gui_dynops.write("demo/name", "a much too long name here", 25) != 0
              && strcmp(name, "workshop") == 0,
          "a field longer than its buffer is refused whole");
    posted = 0;
    check(gui_dynops.write("demo/count", "1", 1) == 0 && posted == 1 &&
              gui_dynops.read("demo/count", text, sizeof text) > 0 &&
              strcmp(text, "1\n") == 0,
          "a button counts its press");
    check(gui_dynops.read("demo/nope", text, sizeof text) < 0 &&
              gui_dynops.read("other/panel", text, sizeof text) < 0,
          "a row or a form that is not there reads as absent");

    printf("the drawing road\n");
    {
        tiku_draw_window_t win;
        static unsigned char list[4096];
        unsigned char msg[8192];
        size_t at = 0, n;
        tiku_dl_t *dl;
        char said[2048];

        /* A desktop is listening: the board holds a window's traffic
         * until greeted, so the test greets first and the open, then the
         * list, flow as they would to a desktop that was there. */
        {
            unsigned char inv[16] = { 20, 0, 0, 0, 8, 0, 0, 0,
                                      1, 0, 0, 0, 2, 0, 0, 0x80 };

            feed(inv, sizeof inv);
        }
        wire_len = 0;
        check(tiku_draw_open(&win, "Board window", 240, 160, &owner, list,
                             sizeof list) == 0,
              "a window opens on the program's own buffer");
        n = next_frame(&at, msg, sizeof msg);
        check(n == 8 + 40 && msg[0] == 1 && msg[8] == 1 &&
                  strcmp((char *)msg + 12, "board") == 0 && msg[8 + 36] == 2,
              "the first frame is a hello: version 1, a name, command streams");
        n = next_frame(&at, msg, sizeof msg);
        check(n == 8 + 76 && msg[0] == 2 && msg[8] == 1 && msg[12] == 240 &&
                  strcmp((char *)msg + 20, "Board window") == 0,
              "the second an open: id, size, title");
        tiku_draw_begin(&win);
        (void)tiku_draw_panel(&win, 0, 0, 240, 160);
        (void)tiku_draw_text(&win, 12, 22, TIKU_DRAW_TEXT, TIKU_DRAW_BOLD,
                             "Drawn by the board");
        (void)tiku_draw_button(&win, 12, 60, 80, 24, 0u, "Count");
        (void)tiku_draw_slider(&win, 12, 96, 216, 24, 0, 100, 40);
        (void)tiku_draw_gauge(&win, 12, 128, 216, 20, 400);
        check(tiku_draw_present(&win) == 0, "a list presents");
        n = next_frame(&at, msg, sizeof msg);
        check(n > 20 && msg[0] == 9 && msg[8] == 1 && msg[12] == 240 &&
                  msg[16] == 160,
              "the third a draw for that window at its size");
        dl = (n > 20) ? tiku_dl_unflatten(msg + 20, n - 20, NULL) : NULL;
        check(dl != NULL && tiku_dl_count(dl) == 5,
              "whose list the desktop's own reader takes whole, five commands");
        said[0] = '\0';
        if (dl != NULL) {
            (void)tiku_dl_say(dl, said, sizeof said);
        }
        check(strstr(said, "text \"Drawn by the board\"") != NULL &&
                  strstr(said, "button \"Count\"") != NULL &&
                  strstr(said, "slider at 40 of 0..100") != NULL &&
                  strstr(said, "gauge at 40%") != NULL,
              "and says as the desktop would: the text, the button, the slider");
        tiku_dl_free(dl);
        check(tiku_draw_text_width(TIKU_DRAW_PLAIN, "Count") > 20 &&
                  tiku_draw_text_height(TIKU_DRAW_PLAIN) == 15,
              "text is measured from the baked metrics");

        printf("every op\n");
        {
            /* One of each op the table names, written by the generated
             * writers and read back by the desktop's own reader. */
            static const unsigned char art[4] = { 1, 2, 3, 4 };
            static const uint32_t px[4] = { 0xff0000u, 0x00ff00u, 0x0000ffu, 0 };
            int ok = 1;

            wire_len = 0;
            tiku_draw_begin(&win);
            ok &= tiku_draw_fill(&win, 0, 0, 10, 10, 1);
            ok &= tiku_draw_frame(&win, 0, 0, 10, 10, 2);
            ok &= tiku_draw_bevel(&win, 0, 0, 10, 10, 3, 4);
            ok &= tiku_draw_hline(&win, 0, 0, 10, 5);
            ok &= tiku_draw_vline(&win, 0, 0, 10, 6);
            ok &= tiku_draw_text(&win, 1, 2, 7, TIKU_DRAW_PLAIN, "t");
            ok &= tiku_draw_text_centered(&win, 0, 0, 10, 10, 8, TIKU_DRAW_BOLD, "c");
            ok &= tiku_draw_clip_set(&win, 0, 0, 10, 10);
            ok &= tiku_draw_clip_reset(&win);
            ok &= tiku_draw_panel(&win, 0, 0, 10, 10);
            ok &= tiku_draw_raised(&win, 0, 0, 10, 10);
            ok &= tiku_draw_sunken(&win, 0, 0, 10, 10, 9);
            ok &= tiku_draw_button(&win, 0, 0, 10, 10, 0u, "b");
            ok &= tiku_draw_checkbox(&win, 0, 0, 10, 10, 0u, "k");
            ok &= tiku_draw_radio(&win, 0, 0, 10, 10, 0u, "r");
            ok &= tiku_draw_list_row(&win, 0, 0, 10, 10, 1u, "l");
            ok &= tiku_draw_icon_art(&win, 77u, art, sizeof art);
            ok &= tiku_draw_icon(&win, 77u, 1, 2, 16, 255u, 0u, 0u);
            ok &= tiku_draw_gauge(&win, 0, 0, 10, 10, 500);
            ok &= tiku_draw_tip(&win, 0, 0, 10, 10, 0u, "p");
            ok &= tiku_draw_textfield(&win, 0, 0, 10, 10, 0u, "f");
            ok &= tiku_draw_scrollbar(&win, 0, 0, 10, 10, 250, 500, 0);
            ok &= tiku_draw_slider(&win, 0, 0, 10, 10, 0, 100, 40);
            ok &= tiku_draw_alert_icon(&win, 5, 5, 1);
            ok &= tiku_draw_tabs(&win, 0, 0, 10, 10, 2, 1, "A\0B\0", 4);
            ok &= tiku_draw_menufield(&win, 0, 0, 10, 10, 0u, "m");
            ok &= tiku_draw_picture(&win, 0, 0, 2, 2, px, sizeof px);
            check(ok, "all twenty-seven ops write into the list");
            (void)tiku_draw_present(&win);
            at = 0;
            n = next_frame(&at, msg, sizeof msg);
            {
                size_t took = 0;

                dl = (n > 20) ? tiku_dl_unflatten(msg + 20, n - 20, &took)
                              : NULL;
                if (dl == NULL || tiku_dl_count(dl) != 27) {
                    printf("         (frame %zu bytes, reader took %zu, count %d)\n",
                           n, took, dl ? tiku_dl_count(dl) : -1);
                }
            }
            check(dl != NULL && tiku_dl_count(dl) == 27,
                  "and the desktop's reader takes all twenty-seven back");
            said[0] = '\0';
            if (dl != NULL) {
                (void)tiku_dl_say(dl, said, sizeof said);
            }
            check(strstr(said, "scrollbar") != NULL && strstr(said, "tabs") != NULL
                      && strstr(said, "picture") != NULL &&
                      strstr(said, "icon") != NULL,
                  "and says the scrollbar, the tabs, the picture and the icon");
            tiku_dl_free(dl);
            tiku_draw_begin(&win);
            {
                static const uint32_t big[40 * 40];

                check(tiku_draw_picture(&win, 0, 0, 40, 40, big, sizeof big) == 0
                          && win.len == 0,
                      "a picture past the window's art budget is refused");
            }
        }

        printf("events back\n");
        posted = 0;
        n = event_msg(msg, win.id, 6, 20, 70);
        feed(msg, n);
        n = event_msg(msg, win.id, 7, 20, 70);
        feed(msg, n);
        check(posted == 2 && posted_data[0] == &win,
              "a press and its release each post to the owner");
        check(tiku_draw_event(TIKU_EVENT_DRAW, &win) == &win &&
                  win.event.kind == TIKU_DRAW_POINTER_DOWN && win.event.x == 20 &&
                  win.event.y == 70,
              "the owner takes the press first, with its point");
        check(tiku_draw_event(TIKU_EVENT_DRAW, &win) == &win &&
                  win.event.kind == TIKU_DRAW_POINTER_UP &&
                  tiku_draw_event(TIKU_EVENT_DRAW, &win) == NULL,
              "then the release, then nothing");
        n = event_msg(msg, 99, 6, 1, 1);
        posted = 0;
        feed(msg, n);
        check(posted == 0, "an event for a window not open goes nowhere");
        {
            /* Paint something, so the re-announce below carries a list. */
            tiku_draw_begin(&win);
            (void)tiku_draw_text(&win, 12, 22, TIKU_DRAW_TEXT, TIKU_DRAW_BOLD,
                                 "again");
            (void)tiku_draw_present(&win);
            while (tiku_draw_event(TIKU_EVENT_DRAW, &win) != NULL) {
                ;
            }

            /* A plain welcome, the listener's reply to a hello: nothing.
             * An invitation: the windows announced again -- open and
             * draw, but no fresh hello, so the desktop's own reply to a
             * hello cannot start the two trading hellos -- and the owner
             * told a session began. */
            unsigned char welcome[16] = { 20, 0, 0, 0, 8, 0, 0, 0,
                                          1, 0, 0, 0, 2, 0, 0, 0 };

            wire_len = 0;
            posted = 0;
            feed(welcome, sizeof welcome);
            check(wire_len == 0 && posted == 0,
                  "a welcome without the invitation is let pass");
            welcome[15] = 0x80;
            feed(welcome, sizeof welcome);
            at = 0;
            n = next_frame(&at, msg, sizeof msg);
            check(n == 84 && msg[0] == 2,
                  "an invitation is answered with the window, not a hello");
            n = next_frame(&at, msg, sizeof msg);
            check(n >= 20 && msg[0] == 9, "and the window drawn as it was");
            check(posted == 1 && tiku_draw_event(TIKU_EVENT_DRAW, &win) == &win
                      && win.event.kind == TIKU_DRAW_SESSION,
                  "and the owner hears the session begin");
        }
        {
            /* The channel the library registered at boot: the marker the
             * desktop's frames carry, stripped before the message is read. */
            posted = 0;
            check(link_marker == 0xF1 && link_recv != NULL,
                  "the session opened its link and listens for messages");
            n = event_msg(msg, win.id, 6, 3, 4);
            feed(msg, n);
            check(posted == 1, "and a message on it reaches the owner");
        }
        printf("the rest of the session\n");
        {
            unsigned char pick[8 + 8], told[512];
            tiku_msg_t *m;
            size_t k;

            /* Whatever the decoder tests above left queued is taken first,
             * so each event below is the one just fed. */
            while (tiku_draw_event(TIKU_EVENT_DRAW, &win) != NULL) {
                ;
            }
            wire_len = 0;
            tiku_draw_menus(&win, "menu File\nitem 1 o Open\n");
            at = 0;
            n = next_frame(&at, msg, sizeof msg);
            check(n == 8 + 4 + 24 && msg[0] == 13 &&
                      memcmp(msg + 12, "menu File\nitem 1 o Open\n", 24) == 0,
                  "menus go as a text message with the window's id");
            wire_len = 0;
            tiku_draw_raise(&win);
            tiku_draw_sizeable(&win);
            tiku_draw_place(&win, 2);
            at = 0;
            n = next_frame(&at, msg, sizeof msg);
            check(n == 12 && msg[0] == 10, "raise");
            n = next_frame(&at, msg, sizeof msg);
            check(n == 12 && msg[0] == 11, "sizeable");
            n = next_frame(&at, msg, sizeof msg);
            check(n == 16 && msg[0] == 12 && msg[12] == 2, "place, with its role");
            /* A pick from the desktop's menu: type 17, the id and command. */
            memset(pick, 0, sizeof pick);
            pick[0] = 17;
            pick[4] = 8;
            memcpy(pick + 8, &win.id, 4);
            pick[12] = 4;
            posted = 0;
            feed(pick, sizeof pick);
            check(posted == 1 && tiku_draw_event(TIKU_EVENT_DRAW, &win) == &win &&
                      win.event.kind == TIKU_DRAW_PICK && win.event.command == 4,
                  "a menu pick reaches the owner with its command");
            /* Asking for a file is a message the desktop's own reader
             * takes apart. */
            wire_len = 0;
            tiku_draw_pick(&win, 1, "/home", "notes.txt");
            at = 0;
            n = next_frame(&at, msg, sizeof msg);
            m = (n > 8 && msg[0] == 8) ? tiku_msg_unflatten(msg + 8, n - 8, NULL)
                                        : NULL;
            {
                int32_t mode = -1, id = -1;
                const char *start = m ? tiku_msg_find_string(m, "start", 0) : NULL;
                const char *nm = m ? tiku_msg_find_string(m, "name", 0) : NULL;

                if (m != NULL) {
                    (void)tiku_msg_find_int32(m, "mode", 0, &mode);
                    (void)tiku_msg_find_int32(m, "window", 0, &id);
                }
                check(m != NULL && tiku_msg_what(m) == TIKU_MSG_PICK && mode == 1 &&
                          (uint32_t)id == win.id && start && nm &&
                          strcmp(start, "/home") == 0 && strcmp(nm, "notes.txt") == 0,
                      "the ask is a pick message the desktop reads whole");
                tiku_msg_free(m);
            }
            /* And the answer: a TELL the desktop would send, framed. */
            m = tiku_msg_new(TIKU_MSG_PICKED);
            (void)tiku_msg_add_int32(m, "window", (int32_t)win.id);
            (void)tiku_msg_add_string(m, "path", "/home/notes.txt");
            told[0] = 19;
            memset(told + 1, 0, 7);
            (void)tiku_msg_flatten(m, told + 8, sizeof told - 8, &k);
            told[4] = (unsigned char)k;
            told[5] = (unsigned char)(k >> 8);
            tiku_msg_free(m);
            posted = 0;
            feed(told, 8 + k);
            check(posted == 1 && tiku_draw_event(TIKU_EVENT_DRAW, &win) == &win &&
                      win.event.kind == TIKU_DRAW_PICKED &&
                      strcmp(win.event.path, "/home/notes.txt") == 0,
                  "the picked path comes back to the window that asked");
            /* A resize event carries the new size into the window. */
            {
                unsigned char rs[8 + 68];

                n = event_msg(rs, win.id, 3, 0, 0);
                rs[12 + 56] = 200;
                rs[12 + 60] = 100;
                posted = 0;
                feed(rs, n);
                check(posted == 1 && tiku_draw_event(TIKU_EVENT_DRAW, &win) == &win &&
                          win.event.kind == TIKU_DRAW_RESIZE && win.w == 200 &&
                          win.h == 100,
                      "a resize is heard and the window knows its new size");
            }
        }

        wire_len = 0;
        tiku_draw_close(&win);
        at = 0;
        n = next_frame(&at, msg, sizeof msg);
        check(n == 12 && msg[0] == 5 && !win.open, "a close is a close frame");
    }
    printf("%s\n", fails ? "FAILURES" : "the board library speaks the desktop's language");
    return fails ? 1 : 0;
}
