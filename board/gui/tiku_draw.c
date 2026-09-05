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

/** @brief Open a frame and write a message's header for @p len bytes. */
static void
send_begin(uint32_t type, uint32_t len)
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
}

/** @brief One session message as one frame: header, then payload. */
static void
send_msg(uint32_t type, const unsigned char *payload, uint32_t len)
{
    uint32_t i;

    send_begin(type, len);
    for (i = 0; i < len; i++) {
        escaped(payload[i]);
    }
    raw(FR_END);
}

void
tiku_draw_put16(unsigned char *p, int v)
{
    p[0] = (unsigned char)((uint16_t)v & 0xFFu);
    p[1] = (unsigned char)(((uint16_t)v >> 8) & 0xFFu);
}

void
tiku_draw_put32(unsigned char *p, uint32_t v)
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
    tiku_draw_put32(payload, REMOTE_VERSION);
    strncpy((char *)payload + 4, name ? name : "board", 31);
    tiku_draw_put32(payload + 36, FEAT_COMMAND_STREAM);
    send_msg(RMSG_HELLO, payload, sizeof payload);
    said_hello = 1;
}

int
tiku_draw_open(tiku_draw_window_t *win, const char *title, int w, int h,
               struct tiku_process *owner, unsigned char *list, size_t cap)
{
    unsigned char payload[4 + 4 + 4 + 64];
    int i;

    if (win == NULL || w < 1 || h < 1 || list == NULL || cap < 16u) {
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
    win->list = list;
    win->cap = cap;
    win->len = 0;
    win->art = 0;
    win->open = 1;
    memset(payload, 0, sizeof payload);
    tiku_draw_put32(payload, win->id);
    tiku_draw_put32(payload + 4, (uint32_t)w);
    tiku_draw_put32(payload + 8, (uint32_t)h);
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
    tiku_draw_put32(payload, win->id);
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
        win->art = 0;
    }
}

unsigned char *
tiku_draw_op_begin(tiku_draw_window_t *win, unsigned op, size_t len)
{
    unsigned char *p;

    if (win == NULL || win->list == NULL || win->len + 4u + len > win->cap) {
        return NULL;
    }
    /* Art and pictures are the bytes that make a window slow: budgeted
     * per window, and refused past it rather than sent. */
    if (op == 23u || op == 33u) {
        if (win->art + len > TIKU_DRAW_ART_BUDGET) {
            return NULL;
        }
        win->art += len;
    }
    p = win->list + win->len;
    tiku_draw_put16(p, (int)op);
    tiku_draw_put16(p + 2, (int)len);
    win->len += 4u + len;
    return p + 4;
}

/* The writers, one per op, generated from the desktop's table. */
#define TIKU_DRAW_OPS_BODIES
#include "tiku_draw_ops.h"
#undef TIKU_DRAW_OPS_BODIES

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
    unsigned char head[12];
    size_t i;

    if (win == NULL || !win->open) {
        return -1;
    }
    /* The frame is written straight out, the list after its head, so a
     * list as large as the program's buffer needs no second copy. */
    tiku_draw_put32(head, win->id);
    tiku_draw_put32(head + 4, (uint32_t)win->w);
    tiku_draw_put32(head + 8, (uint32_t)win->h);
    send_begin(RMSG_DRAW, (uint32_t)(12u + win->len));
    for (i = 0; i < 12u; i++) {
        escaped(head[i]);
    }
    for (i = 0; i < win->len; i++) {
        escaped(win->list[i]);
    }
    raw(FR_END);
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
                tiku_draw_put32(payload, w->id);
                tiku_draw_put32(payload + 4, (uint32_t)w->w);
                tiku_draw_put32(payload + 8, (uint32_t)w->h);
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
