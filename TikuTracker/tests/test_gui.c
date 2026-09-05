/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_gui.c - the board library, run on the host against the desktop's
 * own readers.
 *
 * The kernel is four stub headers and the functions below; the library's
 * sources are included whole.  A form renders the panel text the desktop
 * parses and takes writes as the shell would deliver them; a window's
 * frames, unescaped, unflatten with the desktop's own display-list
 * reader and narrate as the desktop would say them; events framed as
 * the desktop sends them reach the owner, queued.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku.h"
#include <kernel/process/tiku_process.h>
#include <kernel/vfs/tiku_vfs.h>
#include <kernel/shell/tiku_shell_io.h>

#include "tiku_dl.h"
#include "tiku_form.h"

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

static const tiku_shell_io_t stub_io = { stub_putc, NULL, NULL, 0u, 0u };

const tiku_shell_io_t *
tiku_shell_io_get_backend(void)
{
    return &stub_io;
}

#include "tiku_gui.c"
#include "tiku_draw.c"

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

/** @brief Feed a whole message to the board as the desktop would frame it. */
static void
feed(const unsigned char *msg, size_t len)
{
    size_t i;

    (void)tiku_draw_frame_byte(0xC0);
    (void)tiku_draw_frame_byte(0xF1);
    for (i = 0; i < len; i++) {
        unsigned char b = msg[i];

        if (b == 0xC0) {
            (void)tiku_draw_frame_byte(0xDB);
            (void)tiku_draw_frame_byte(0xDC);
        } else if (b == 0xDB) {
            (void)tiku_draw_frame_byte(0xDB);
            (void)tiku_draw_frame_byte(0xDD);
        } else if (b == 0) {
            (void)tiku_draw_frame_byte(0xDB);
            (void)tiku_draw_frame_byte(0xDE);
        } else {
            (void)tiku_draw_frame_byte(b);
        }
    }
    (void)tiku_draw_frame_byte(0xC0);
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
        unsigned char msg[4096];
        size_t at = 0, n;
        tiku_dl_t *dl;
        char said[1024];

        wire_len = 0;
        check(tiku_draw_open(&win, "Board window", 240, 160, &owner) == 0,
              "a window opens");
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
            /* A plain welcome, the listener's reply to a hello: nothing.
             * An invitation: hello, open and draw again, and the owner
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
            check(n == 48 && msg[0] == 1, "an invitation is answered: hello");
            n = next_frame(&at, msg, sizeof msg);
            check(n == 84 && msg[0] == 2, "open");
            n = next_frame(&at, msg, sizeof msg);
            check(n > 20 && msg[0] == 9, "and the window drawn as it was");
            check(posted == 1 && tiku_draw_event(TIKU_EVENT_DRAW, &win) == &win
                      && win.event.kind == TIKU_DRAW_SESSION,
                  "and the owner hears the session begin");
        }
        {
            /* A stray END before a frame, and text around it, do not
             * unseat the decoder. */
            posted = 0;
            check(tiku_draw_frame_byte('l') == 0 && tiku_draw_frame_byte(0xC0) == 1
                      && tiku_draw_frame_byte('s') == 0,
                  "text passes, and a lone END followed by text costs only the END");
            n = event_msg(msg, win.id, 6, 3, 4);
            (void)tiku_draw_frame_byte(0xC0);
            feed(msg, n);
            check(posted == 1, "and a frame after a doubled END still lands");
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
