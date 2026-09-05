/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw.c - the session client and the list writer, over a link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_draw.h"

#include "tiku_metrics.h"

#include <kernel/link/tiku_link_console.h>

#include <string.h>

/* The session's messages, as the desktop numbers them. */
#define RMSG_HELLO   1u
#define RMSG_OPEN    2u
#define RMSG_CLOSE   5u
#define RMSG_DRAW    9u
#define RMSG_RAISE   10u
#define RMSG_SIZEABLE 11u
#define RMSG_PLACE   12u
#define RMSG_MENUS_TEXT 13u
#define RMSG_SAY     8u
#define RMSG_EVENT   16u
#define RMSG_PICK    17u
#define RMSG_TELL    19u
#define MSG_MAGIC    0x534D4B54u    /* 'TKMS' */
#define MSG_T_INT32  4u
#define MSG_T_STRING 8u
#define WHAT_PICK    0x7069636bu    /* 'pick' */
#define WHAT_PICKED  0x70696b64u    /* 'pikd' */
#define WHAT_CLIP    0x636c6970u    /* 'clip' */
#define RMSG_CLOSED  18u
#define RMSG_WELCOME 20u
#define REMOTE_VERSION 1u
#define FEAT_COMMAND_STREAM 0x2u

#define FRAME_MAX 256           /* what the desktop sends: an event */

/* The window session rides a console link; its messages arrive as whole
 * frames in this buffer, and go out as one frame each. */
static tiku_link_console_t session_link;
static tiku_link_t *link;
static unsigned char frame_buf[FRAME_MAX];

static tiku_draw_window_t *windows[TIKU_DRAW_WINDOWS];
static uint32_t next_id = 1;
static int said_hello;
/*
 * A desktop has greeted, so it is listening.  Until then a window's open
 * and its list are held: a board boots and mounts at once, and its
 * window frames sent into the desktop's busy mount-time namespace reads
 * are the frames the shared console drops.  The greeting arrives after
 * the mount, when the line is quiet, and everything is said then.
 */
static int greeted;

/** @brief Announce @p win to a listening desktop: its open, then its
 *         list, then its menus and whether it may be resized. */
static void announce(tiku_draw_window_t *win);

/*---------------------------------------------------------------------------*/
/* Messages out                                                               */
/*---------------------------------------------------------------------------*/

/** @brief The 8-byte message head: type, then payload length. */
static void
wire_head(unsigned char *h, uint32_t type, uint32_t len)
{
    h[0] = (unsigned char)type;
    h[1] = (unsigned char)(type >> 8);
    h[2] = (unsigned char)(type >> 16);
    h[3] = (unsigned char)(type >> 24);
    h[4] = (unsigned char)len;
    h[5] = (unsigned char)(len >> 8);
    h[6] = (unsigned char)(len >> 16);
    h[7] = (unsigned char)(len >> 24);
}

/** @brief One session message as one link message: its head, its payload. */
static void
send_msg(uint32_t type, const unsigned char *payload, uint32_t len)
{
    unsigned char head[8];

    wire_head(head, type, len);
    (void)tiku_link_send2(link, head, sizeof head, payload, len);
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
    (void)payload;
    /* The open itself waits for the greeting, so it does not race the
     * desktop's mount; a window opened after the greeting is announced
     * at once. */
    if (greeted) {
        announce(win);
    }
    return 0;
}

/** @brief Send @p win's open message.  Its list follows via present. */
static void
send_open(tiku_draw_window_t *win)
{
    unsigned char payload[4 + 4 + 4 + 64];

    memset(payload, 0, sizeof payload);
    tiku_draw_put32(payload, win->id);
    tiku_draw_put32(payload + 4, (uint32_t)win->w);
    tiku_draw_put32(payload + 8, (uint32_t)win->h);
    strncpy((char *)payload + 12, win->title ? win->title : "", 63);
    send_msg(RMSG_OPEN, payload, sizeof payload);
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

void
tiku_draw_menus(tiku_draw_window_t *win, const char *text)
{
    unsigned char head[12];
    size_t n = text ? strlen(text) : 0u;

    if (win == NULL || !win->open) {
        return;
    }
    win->menus = text;
    if (!greeted) {
        return;
    }
    wire_head(head, RMSG_MENUS_TEXT, (uint32_t)(4u + n));
    tiku_draw_put32(head + 8, win->id);
    (void)tiku_link_send2(link, head, sizeof head, text, n);
}

/** @brief A message about @p win alone: its id, then @p extra words. */
static void
about(uint32_t type, const tiku_draw_window_t *win, int32_t extra,
      int with_extra)
{
    unsigned char payload[8];

    if (win == NULL || !win->open) {
        return;
    }
    tiku_draw_put32(payload, win->id);
    tiku_draw_put32(payload + 4, (uint32_t)extra);
    send_msg(type, payload, with_extra ? 8u : 4u);
}

void
tiku_draw_raise(tiku_draw_window_t *win)
{
    about(RMSG_RAISE, win, 0, 0);
}

void
tiku_draw_sizeable(tiku_draw_window_t *win)
{
    if (win != NULL) {
        win->sizeable = 1;
    }
    if (greeted) {
        about(RMSG_SIZEABLE, win, 0, 0);
    }
}

void
tiku_draw_place(tiku_draw_window_t *win, int role)
{
    about(RMSG_PLACE, win, role, 1);
}

/*---------------------------------------------------------------------------*/
/* Messages: the desk's own self-describing shape, the little of it a       */
/* board says and hears                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Write one field's head: name, type, one value of @p len. */
static size_t
field_head(unsigned char *p, const char *name, uint32_t type, uint32_t len)
{
    size_t n = strlen(name);

    tiku_draw_put16(p, (int)n);
    memcpy(p + 2, name, n);
    tiku_draw_put32(p + 2 + n, type);
    tiku_draw_put32(p + 6 + n, 1u);
    tiku_draw_put32(p + 10 + n, len);
    return 14u + n;
}

/** @brief A string field into @p p.  @return bytes written. */
static size_t
field_string(unsigned char *p, const char *name, const char *value)
{
    size_t len = strlen(value) + 1u;
    size_t n = field_head(p, name, MSG_T_STRING, (uint32_t)len);

    memcpy(p + n, value, len);
    return n + len;
}

/** @brief An int32 field into @p p.  @return bytes written. */
static size_t
field_int32(unsigned char *p, const char *name, int32_t value)
{
    size_t n = field_head(p, name, MSG_T_INT32, 4u);

    tiku_draw_put32(p + n, (uint32_t)value);
    return n + 4u;
}

/** @brief The message head: magic, what, how many fields follow. */
static size_t
msg_head(unsigned char *p, uint32_t what, uint32_t fields)
{
    tiku_draw_put32(p, MSG_MAGIC);
    tiku_draw_put32(p + 4, what);
    tiku_draw_put32(p + 8, fields);
    return 12u;
}

void
tiku_draw_pick(tiku_draw_window_t *win, int mode, const char *start,
               const char *name)
{
    unsigned char m[320];
    size_t n;

    if (win == NULL || !win->open) {
        return;
    }
    start = start ? start : "/";
    name = name ? name : "";
    if (strlen(start) > 120u || strlen(name) > 60u) {
        return;
    }
    n = msg_head(m, WHAT_PICK, 4u);
    n += field_int32(m + n, "window", (int32_t)win->id);
    n += field_int32(m + n, "mode", mode);
    n += field_string(m + n, "start", start);
    n += field_string(m + n, "name", name);
    send_msg(RMSG_SAY, m, (uint32_t)n);
}

void
tiku_draw_clipboard(const char *text)
{
    unsigned char m[200];
    size_t n;

    text = text ? text : "";
    if (strlen(text) > 160u) {
        return;
    }
    n = msg_head(m, WHAT_CLIP, 1u);
    n += field_string(m + n, "text", text);
    send_msg(RMSG_SAY, m, (uint32_t)n);
}

/**
 * @brief Find a field by name in a flattened message and copy its first
 *        value out.  @return the value's length, or -1.
 */
static int
msg_find(const unsigned char *m, size_t len, const char *name, uint32_t type,
         unsigned char *out, size_t max)
{
    size_t at = 12u, want = strlen(name);
    uint32_t fields, f;

    if (len < 12u || get32(m) != MSG_MAGIC) {
        return -1;
    }
    fields = get32(m + 8);
    for (f = 0; f < fields && at + 2u <= len; f++) {
        size_t nlen = (size_t)m[at] | ((size_t)m[at + 1] << 8);
        const unsigned char *fname = m + at + 2;
        uint32_t ftype, count, v;

        at += 2u + nlen;
        if (at + 8u > len) {
            return -1;
        }
        ftype = get32(m + at);
        count = get32(m + at + 4);
        at += 8u;
        for (v = 0; v < count; v++) {
            uint32_t vlen;

            if (at + 4u > len) {
                return -1;
            }
            vlen = get32(m + at);
            at += 4u;
            if (at + vlen > len) {
                return -1;
            }
            if (v == 0u && nlen == want && memcmp(fname, name, want) == 0 &&
                ftype == type) {
                size_t n = (vlen < max) ? vlen : max;

                memcpy(out, m + at, n);
                return (int)n;
            }
            at += vlen;
        }
    }
    return -1;
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

/**
 * @brief Everything about @p win a fresh desktop must be told: its open,
 *        its list, its menus and its resize.  The menus and the resize
 *        are remembered on the window so they can be said again here.
 */
static void
announce(tiku_draw_window_t *win)
{
    send_open(win);
    if (win->len > 0u) {
        (void)tiku_draw_present(win);   /* a window already painted */
    }
    if (win->menus != NULL) {
        tiku_draw_menus(win, win->menus);
    }
    if (win->sizeable) {
        tiku_draw_sizeable(win);
    }
}

int
tiku_draw_present(tiku_draw_window_t *win)
{
    unsigned char head[20];

    if (win == NULL || !win->open) {
        return -1;
    }
    if (!greeted) {
        return 0;                       /* held until a desktop greets */
    }
    /* The list is the message's body, sent straight from the window's own
     * buffer after the head, so a list as large as it needs no second copy. */
    wire_head(head, RMSG_DRAW, (uint32_t)(12u + win->len));
    tiku_draw_put32(head + 8, win->id);
    tiku_draw_put32(head + 12, (uint32_t)win->w);
    tiku_draw_put32(head + 16, (uint32_t)win->h);
    (void)tiku_link_send2(link, head, 20u, win->list, win->len);
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
        e.dy = (int)(int32_t)get32(r + 52);
        e.w = (int)(int32_t)get32(r + 56);
        e.h = (int)(int32_t)get32(r + 60);
        if (e.kind == TIKU_DRAW_RESIZE && e.w > 0 && e.h > 0) {
            win->w = e.w;
            win->h = e.h;
        }
        push(win, &e);
    } else if (type == RMSG_PICK && plen >= 8u) {
        win = window_of(get32(p));
        if (win == NULL) {
            return;
        }
        memset(&e, 0, sizeof e);
        e.kind = TIKU_DRAW_PICK;
        e.command = (int)(int32_t)get32(p + 4);
        push(win, &e);
    } else if (type == RMSG_TELL && plen >= 12u) {
        /* The desk answering: a picked path for the window that asked,
         * or the clipboard's new text, told to every window. */
        uint32_t what = get32(p + 4);
        unsigned char path[96];
        unsigned char wid[4];
        int n, i;

        memset(&e, 0, sizeof e);
        if (what == WHAT_PICKED) {
            n = msg_find(p, plen, "path", MSG_T_STRING, path, sizeof path - 1);
            if (n < 0 || msg_find(p, plen, "window", MSG_T_INT32, wid, 4) < 0) {
                return;
            }
            win = window_of(get32(wid));
            if (win == NULL) {
                return;
            }
            path[n] = '\0';
            e.kind = TIKU_DRAW_PICKED;
            memcpy(e.path, path, (size_t)n + 1u);
            push(win, &e);
        } else if (what == WHAT_CLIP) {
            n = msg_find(p, plen, "text", MSG_T_STRING, path, sizeof path - 1);
            if (n < 0) {
                return;
            }
            path[n] = '\0';
            e.kind = TIKU_DRAW_CLIP;
            memcpy(e.path, path, (size_t)n + 1u);
            for (i = 0; i < TIKU_DRAW_WINDOWS; i++) {
                if (windows[i] != NULL && windows[i]->open) {
                    push(windows[i], &e);
                }
            }
        }
    } else if (type == RMSG_WELCOME && plen >= 8u &&
               (get32(p + 4) & 0x80000000u) != 0u) {
        /* A desktop arrived, or came back, and invites: every open
         * window is announced now, when the line is quiet enough for its
         * frames to land -- an open, a list, the menus and the resize. */
        int i;

        greeted = 1;
        for (i = 0; i < TIKU_DRAW_WINDOWS; i++) {
            tiku_draw_window_t *w = windows[i];

            if (w != NULL && w->open) {
                announce(w);
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

/** @brief One whole message from the desk, as the console delivers it. */
static void
on_frame(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    take_message(buf, len);
}

void
tiku_draw_init(void)
{
    link = tiku_link_console_open(&session_link, TIKU_LINK_CONSOLE_SESSION,
                                  frame_buf, sizeof frame_buf);
    tiku_link_on_recv(link, on_frame, NULL);
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
