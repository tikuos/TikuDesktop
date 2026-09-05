/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw.c - the session client, the list writer and the frame codec.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_draw.h"

#include "tiku_metrics.h"

#include <kernel/shell/tiku_shell_io.h>

#include <string.h>

/* The session's messages, as the desktop numbers them. */
#define RMSG_HELLO   1u
#define RMSG_OPEN    2u
#define RMSG_CLOSE   5u
#define RMSG_DRAW    9u
#define RMSG_EVENT   16u
#define RMSG_CLOSED  18u
#define RMSG_WELCOME 20u
#define REMOTE_VERSION 1u
#define FEAT_COMMAND_STREAM 0x2u

/* The list's ops, as the desktop numbers them. */
#define OP_FILL 1u
#define OP_FRAME 2u
#define OP_HLINE 4u
#define OP_VLINE 5u
#define OP_TEXT 6u
#define OP_TEXT_CENTERED 7u
#define OP_PANEL 16u
#define OP_RAISED 17u
#define OP_SUNKEN 18u
#define OP_BUTTON 19u
#define OP_CHECKBOX 20u
#define OP_RADIO 21u
#define OP_LIST_ROW 22u
#define OP_GAUGE 25u
#define OP_TEXTFIELD 27u
#define OP_SLIDER 29u
#define OP_MENUFIELD 32u

/* The frame on the console. */
#define FR_END 0xC0u
#define FR_ESC 0xDBu
#define FR_ESC_END 0xDCu
#define FR_ESC_ESC 0xDDu
#define FR_ESC_NUL 0xDEu
#define FR_MARK 0xF1u
#define FRAME_MAX 256           /* what the desktop sends: an event */

static tiku_draw_window_t *windows[TIKU_DRAW_WINDOWS];
static uint32_t next_id = 1;
static int said_hello;

/*---------------------------------------------------------------------------*/
/* Bytes out                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief One raw byte to the console, past the CRLF rule. */
static void
raw(unsigned char b)
{
    const tiku_shell_io_t *io = tiku_shell_io_get_backend();

    if (io != NULL && io->putc != NULL) {
        io->putc((char)b);
    }
}

/** @brief One escaped byte of a frame. */
static void
escaped(unsigned char b)
{
    if (b == FR_END) {
        raw(FR_ESC);
        raw(FR_ESC_END);
    } else if (b == FR_ESC) {
        raw(FR_ESC);
        raw(FR_ESC_ESC);
    } else if (b == 0u) {
        raw(FR_ESC);
        raw(FR_ESC_NUL);
    } else {
        raw(b);
    }
}

/** @brief One session message as one frame: header, then payload. */
static void
send_msg(uint32_t type, const unsigned char *payload, uint32_t len)
{
    unsigned char head[8];
    uint32_t i;

    head[0] = (unsigned char)type;
    head[1] = (unsigned char)(type >> 8);
    head[2] = (unsigned char)(type >> 16);
    head[3] = (unsigned char)(type >> 24);
    head[4] = (unsigned char)len;
    head[5] = (unsigned char)(len >> 8);
    head[6] = (unsigned char)(len >> 16);
    head[7] = (unsigned char)(len >> 24);
    raw(FR_END);
    raw(FR_MARK);
    for (i = 0; i < 8; i++) {
        escaped(head[i]);
    }
    for (i = 0; i < len; i++) {
        escaped(payload[i]);
    }
    raw(FR_END);
}

static void
put16(unsigned char *p, int v)
{
    p[0] = (unsigned char)((uint16_t)v & 0xFFu);
    p[1] = (unsigned char)(((uint16_t)v >> 8) & 0xFFu);
}

static void
put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static uint32_t
get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/*---------------------------------------------------------------------------*/
/* The session                                                               */
/*---------------------------------------------------------------------------*/

void
tiku_draw_hello(const char *name)
{
    unsigned char payload[40];

    memset(payload, 0, sizeof payload);
    put32(payload, REMOTE_VERSION);
    strncpy((char *)payload + 4, name ? name : "board", 31);
    put32(payload + 36, FEAT_COMMAND_STREAM);
    send_msg(RMSG_HELLO, payload, sizeof payload);
    said_hello = 1;
}

int
tiku_draw_open(tiku_draw_window_t *win, const char *title, int w, int h,
               struct tiku_process *owner)
{
    unsigned char payload[4 + 4 + 4 + 64];
    int i;

    if (win == NULL || w < 1 || h < 1) {
        return -1;
    }
    for (i = 0; i < TIKU_DRAW_WINDOWS && windows[i] != win; i++) {
        ;
    }
    if (i == TIKU_DRAW_WINDOWS) {
        for (i = 0; i < TIKU_DRAW_WINDOWS && windows[i] != NULL; i++) {
            ;
        }
        if (i == TIKU_DRAW_WINDOWS) {
            return -1;
        }
        windows[i] = win;
    }
    if (!said_hello) {
        tiku_draw_hello("board");
    }
    win->id = next_id++;
    win->w = w;
    win->h = h;
    win->title = title;
    win->owner = owner;
    win->len = 0;
    win->open = 1;
    memset(payload, 0, sizeof payload);
    put32(payload, win->id);
    put32(payload + 4, (uint32_t)w);
    put32(payload + 8, (uint32_t)h);
    strncpy((char *)payload + 12, title ? title : "", 63);
    send_msg(RMSG_OPEN, payload, sizeof payload);
    return 0;
}

void
tiku_draw_close(tiku_draw_window_t *win)
{
    unsigned char payload[4];
    int i;

    if (win == NULL || !win->open) {
        return;
    }
    put32(payload, win->id);
    send_msg(RMSG_CLOSE, payload, sizeof payload);
    win->open = 0;
    for (i = 0; i < TIKU_DRAW_WINDOWS; i++) {
        if (windows[i] == win) {
            windows[i] = NULL;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* The list                                                                  */
/*---------------------------------------------------------------------------*/

void
tiku_draw_begin(tiku_draw_window_t *win)
{
    if (win != NULL) {
        win->len = 0;
    }
}

/** @brief Room for a command of @p len payload bytes, its header written. */
static unsigned char *
begin(tiku_draw_window_t *win, unsigned op, size_t len)
{
    unsigned char *p;

    if (win == NULL || win->len + 4u + len > sizeof win->list) {
        return NULL;
    }
    p = win->list + win->len;
    put16(p, (int)op);
    put16(p + 2, (int)len);
    win->len += 4u + len;
    return p + 4;
}

static void
put_rect(unsigned char *p, int x, int y, int w, int h)
{
    put16(p, x);
    put16(p + 2, y);
    put16(p + 4, w);
    put16(p + 6, h);
}

/** @brief A rectangle and a colour: fill, frame, sunken. */
static int
rect_colour(tiku_draw_window_t *win, unsigned op, int x, int y, int w,
            int h, uint32_t c)
{
    unsigned char *p = begin(win, op, 12);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, x, y, w, h);
    put32(p + 8, c);
    return 1;
}

int
tiku_draw_fill(tiku_draw_window_t *win, int x, int y, int w, int h,
               uint32_t colour)
{
    return rect_colour(win, OP_FILL, x, y, w, h, colour);
}

int
tiku_draw_frame(tiku_draw_window_t *win, int x, int y, int w, int h,
                uint32_t colour)
{
    return rect_colour(win, OP_FRAME, x, y, w, h, colour);
}

int
tiku_draw_sunken(tiku_draw_window_t *win, int x, int y, int w, int h,
                 uint32_t face)
{
    return rect_colour(win, OP_SUNKEN, x, y, w, h, face);
}

/** @brief A line from (x, y) running @p run pixels. */
static int
line(tiku_draw_window_t *win, unsigned op, int x, int y, int run,
     uint32_t c)
{
    unsigned char *p = begin(win, op, 10);

    if (p == NULL) {
        return 0;
    }
    put16(p, x);
    put16(p + 2, y);
    put16(p + 4, run);
    put32(p + 6, c);
    return 1;
}

int
tiku_draw_hline(tiku_draw_window_t *win, int x, int y, int w, uint32_t colour)
{
    return line(win, OP_HLINE, x, y, w, colour);
}

int
tiku_draw_vline(tiku_draw_window_t *win, int x, int y, int h, uint32_t colour)
{
    return line(win, OP_VLINE, x, y, h, colour);
}

int
tiku_draw_text(tiku_draw_window_t *win, tiku_draw_face_t face, int x, int y,
               const char *text, uint32_t colour)
{
    size_t n = (text != NULL) ? strlen(text) + 1u : 1u;
    unsigned char *p = begin(win, OP_TEXT, 9u + n);

    if (p == NULL) {
        return 0;
    }
    put16(p, x);
    put16(p + 2, y);
    put32(p + 4, colour);
    p[8] = (unsigned char)face;
    memcpy(p + 9, text ? text : "", n);
    return 1;
}

int
tiku_draw_text_centered(tiku_draw_window_t *win, tiku_draw_face_t face,
                        int x, int y, int w, int h, const char *text,
                        uint32_t colour)
{
    size_t n = (text != NULL) ? strlen(text) + 1u : 1u;
    unsigned char *p = begin(win, OP_TEXT_CENTERED, 13u + n);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, x, y, w, h);
    put32(p + 8, colour);
    p[12] = (unsigned char)face;
    memcpy(p + 13, text ? text : "", n);
    return 1;
}

/** @brief A rectangle alone: panel, raised. */
static int
rect_only(tiku_draw_window_t *win, unsigned op, int x, int y, int w, int h)
{
    unsigned char *p = begin(win, op, 8);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, x, y, w, h);
    return 1;
}

int
tiku_draw_panel(tiku_draw_window_t *win, int x, int y, int w, int h)
{
    return rect_only(win, OP_PANEL, x, y, w, h);
}

int
tiku_draw_raised(tiku_draw_window_t *win, int x, int y, int w, int h)
{
    return rect_only(win, OP_RAISED, x, y, w, h);
}

/** @brief A control with a label and a state. */
static int
labelled(tiku_draw_window_t *win, unsigned op, int x, int y, int w, int h,
         const char *label, unsigned state)
{
    size_t n = (label != NULL) ? strlen(label) + 1u : 1u;
    unsigned char *p = begin(win, op, 12u + n);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, x, y, w, h);
    put32(p + 8, state);
    memcpy(p + 12, label ? label : "", n);
    return 1;
}

int
tiku_draw_button(tiku_draw_window_t *win, int x, int y, int w, int h,
                 const char *label, unsigned state)
{
    return labelled(win, OP_BUTTON, x, y, w, h, label, state);
}

int
tiku_draw_checkbox(tiku_draw_window_t *win, int x, int y, int w, int h,
                   const char *label, unsigned state)
{
    return labelled(win, OP_CHECKBOX, x, y, w, h, label, state);
}

int
tiku_draw_radio(tiku_draw_window_t *win, int x, int y, int w, int h,
                const char *label, unsigned state)
{
    return labelled(win, OP_RADIO, x, y, w, h, label, state);
}

int
tiku_draw_textfield(tiku_draw_window_t *win, int x, int y, int w, int h,
                    const char *text, unsigned state)
{
    return labelled(win, OP_TEXTFIELD, x, y, w, h, text, state);
}

int
tiku_draw_menufield(tiku_draw_window_t *win, int x, int y, int w, int h,
                    const char *label, unsigned state)
{
    return labelled(win, OP_MENUFIELD, x, y, w, h, label, state);
}

int
tiku_draw_list_row(tiku_draw_window_t *win, int x, int y, int w, int h,
                   const char *text, int selected)
{
    return labelled(win, OP_LIST_ROW, x, y, w, h, text, selected ? 1u : 0u);
}

int
tiku_draw_gauge(tiku_draw_window_t *win, int x, int y, int w, int h,
                int per_mille)
{
    unsigned char *p = begin(win, OP_GAUGE, 10);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, x, y, w, h);
    put16(p + 8, per_mille);
    return 1;
}

int
tiku_draw_slider(tiku_draw_window_t *win, int x, int y, int w, int h,
                 int lo, int hi, int value)
{
    unsigned char *p = begin(win, OP_SLIDER, 14);

    if (p == NULL) {
        return 0;
    }
    put_rect(p, x, y, w, h);
    put16(p + 8, lo);
    put16(p + 10, hi);
    put16(p + 12, value);
    return 1;
}

/** @brief The metrics table of @p face. */
static const unsigned char *
metrics(tiku_draw_face_t face)
{
    switch (face) {
    case TIKU_DRAW_BOLD: return TIKU_METRICS_BOLD;
    case TIKU_DRAW_MONO: return TIKU_METRICS_MONO;
    default:             return TIKU_METRICS_PLAIN;
    }
}

int
tiku_draw_text_width(tiku_draw_face_t face, const char *text)
{
    const unsigned char *m = metrics(face);
    int w = 0;

    for (; text != NULL && *text != '\0'; text++) {
        unsigned char c = (unsigned char)*text;

        w += m[2 + ((c >= 32u && c < 127u) ? c - 32u : 0u)];
    }
    return w;
}

int
tiku_draw_text_height(tiku_draw_face_t face)
{
    return metrics(face)[0];
}

int
tiku_draw_text_ascent(tiku_draw_face_t face)
{
    return metrics(face)[1];
}

int
tiku_draw_present(tiku_draw_window_t *win)
{
    unsigned char payload[12 + TIKU_DRAW_LIST_MAX];

    if (win == NULL || !win->open) {
        return -1;
    }
    put32(payload, win->id);
    put32(payload + 4, (uint32_t)win->w);
    put32(payload + 8, (uint32_t)win->h);
    memcpy(payload + 12, win->list, win->len);
    send_msg(RMSG_DRAW, payload, (uint32_t)(12u + win->len));
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Bytes in                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief The window the session calls @p id, or NULL. */
static tiku_draw_window_t *
window_of(uint32_t id)
{
    int i;

    for (i = 0; i < TIKU_DRAW_WINDOWS; i++) {
        if (windows[i] != NULL && windows[i]->open && windows[i]->id == id) {
            return windows[i];
        }
    }
    return NULL;
}

/** @brief Queue @p e for @p win's owner and wake it; a full queue drops
 *         the oldest, since the newest is what the pointer is doing now. */
static void
push(tiku_draw_window_t *win, const tiku_draw_event_t *e)
{
    unsigned char at;

    if (win->count == TIKU_DRAW_QUEUE) {
        win->head = (unsigned char)((win->head + 1u) % TIKU_DRAW_QUEUE);
        win->count--;
    }
    at = (unsigned char)((win->head + win->count) % TIKU_DRAW_QUEUE);
    win->queue[at] = *e;
    win->count++;
    if (win->owner != NULL) {
        (void)tiku_process_post(win->owner, TIKU_EVENT_DRAW, win);
    }
}

/**
 * @brief One whole message from the desktop: an event is queued for
 *        the window's owner; a close is noted the same way.
 */
static void
take_message(const unsigned char *msg, size_t len)
{
    uint32_t type, plen;
    const unsigned char *p = msg + 8;
    tiku_draw_window_t *win;
    tiku_draw_event_t e;

    if (len < 8u) {
        return;
    }
    type = get32(msg);
    plen = get32(msg + 4);
    if (8u + plen > len) {
        return;
    }
    if (type == RMSG_EVENT && plen >= 4u + 64u) {
        /* The desktop's event record: 64 bytes, laid out as the host
         * packs it -- the type at 0, the key and its fellows from 16,
         * the text at 32, the point at 40. */
        const unsigned char *r = p + 4;

        win = window_of(get32(p));
        if (win == NULL) {
            return;
        }
        memset(&e, 0, sizeof e);
        e.kind = (tiku_draw_kind_t)get32(r);
        e.modifiers = get32(r + 16);
        e.key = get32(r + 20);
        e.button = get32(r + 24);
        memcpy(e.text, r + 32, 7);
        e.x = (int)(int32_t)get32(r + 40);
        e.y = (int)(int32_t)get32(r + 44);
        push(win, &e);
    } else if (type == RMSG_WELCOME && plen >= 8u &&
               (get32(p + 4) & 0x80000000u) != 0u) {
        /* A desktop arrived, or came back, and invites: everything drawn
         * before it was listening is said again.  Its reply to the hello
         * is a WELCOME too, without the invitation, and passes. */
        int i;

        tiku_draw_hello("board");
        for (i = 0; i < TIKU_DRAW_WINDOWS; i++) {
            tiku_draw_window_t *w = windows[i];

            if (w != NULL && w->open) {
                unsigned char payload[4 + 4 + 4 + 64];

                memset(payload, 0, sizeof payload);
                put32(payload, w->id);
                put32(payload + 4, (uint32_t)w->w);
                put32(payload + 8, (uint32_t)w->h);
                strncpy((char *)payload + 12, w->title ? w->title : "", 63);
                send_msg(RMSG_OPEN, payload, sizeof payload);
                (void)tiku_draw_present(w);
                /* And the owner is told, so it may draw the window as
                 * it is now rather than as it was last presented. */
                memset(&e, 0, sizeof e);
                e.kind = TIKU_DRAW_SESSION;
                push(w, &e);
            }
        }
        return;
    } else if (type == RMSG_CLOSED && plen >= 4u) {
        win = window_of(get32(p));
        if (win == NULL) {
            return;
        }
        memset(&e, 0, sizeof e);
        e.kind = TIKU_DRAW_CLOSED;
        win->open = 0;
        push(win, &e);
    }
}

int
tiku_draw_frame_byte(int ch)
{
    static unsigned char fbuf[FRAME_MAX];
    static size_t flen;
    static int armed, in_frame, esc;
    unsigned char b = (unsigned char)ch;

    if (in_frame) {
        if (b == FR_END) {
            if (flen > 0u) {
                take_message(fbuf, flen);
            }
            in_frame = 0;
            armed = 1;
            flen = 0;
            esc = 0;
            return 1;
        }
        if (esc) {
            esc = 0;
            b = (b == FR_ESC_END) ? FR_END : (b == FR_ESC_ESC) ? FR_ESC
              : (b == FR_ESC_NUL) ? 0u : b;
        } else if (b == FR_ESC) {
            esc = 1;
            return 1;
        }
        if (flen < sizeof fbuf) {
            fbuf[flen++] = b;
        }
        return 1;
    }
    if (armed) {
        if (b == FR_END) {
            return 1;           /* one END after another: still armed */
        }
        armed = 0;
        if (b == FR_MARK) {
            in_frame = 1;
            flen = 0;
            esc = 0;
            return 1;
        }
        return 0;               /* a stray END: this byte is the shell's */
    }
    if (b == FR_END) {
        armed = 1;
        return 1;
    }
    return 0;
}

tiku_draw_window_t *
tiku_draw_event(tiku_event_t ev, tiku_event_data_t data)
{
    tiku_draw_window_t *win = (ev == TIKU_EVENT_DRAW)
                                  ? (tiku_draw_window_t *)data : NULL;

    if (win == NULL || win->count == 0u) {
        return NULL;
    }
    win->event = win->queue[win->head];
    win->head = (unsigned char)((win->head + 1u) % TIKU_DRAW_QUEUE);
    win->count--;
    return win;
}
