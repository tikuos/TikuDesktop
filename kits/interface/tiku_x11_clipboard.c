/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_x11_clipboard.c - the X11 CLIPBOARD selection, in and out, up to
 * TIKU_CLIPBOARD_LIMIT: a property read a piece at a time whatever its
 * size, INCR taken in as it comes, and text too large for one request
 * sent INCR too (ICCCM 2.7.2), a piece each time the requestor deletes
 * the last.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_x11_clipboard.h"
#include <X11/Xatom.h>
#include <stdlib.h>
#include <string.h>

#define READ_UNITS (64L * 1024L)    /* 32-bit units one property read takes */
#define SINGLE_MAX 256000u          /* bytes one request carries anywhere */
#define PIECE      (64u * 1024u)    /* bytes of each INCR piece sent */
#define WAIT_US    1000000          /* an import with no progress this long */
#define SEND_US    5000000          /* a requestor quiet this long is let go */

/* A requestor may disappear between SelectionRequest and ChangeProperty.
 * Scope the error trap to this display, restoring the previous handler. */
static Display *trapped;
static int error_code;
static XErrorHandler previous_handler;
static int on_error(Display *d, XErrorEvent *e)
{
    if (d == trapped) { error_code = e->error_code; return 0; }
    return previous_handler ? previous_handler(d, e) : 0;
}
static void trap(Display *d)
{
    XSync(d, False);
    trapped = d; error_code = 0;
    previous_handler = XSetErrorHandler(on_error);
}
static int untrap(Display *d)
{
    XSync(d, False);
    XSetErrorHandler(previous_handler);
    trapped = NULL;
    return error_code;
}

/* An import given up: its window, its bytes and their room. */
static void cancel(tiku_x11_clipboard_t *c)
{
    if (c->requestor) XDestroyWindow(c->display, c->requestor);
    c->requestor = None; c->incremental = 0; c->used = 0;
    free(c->incoming); c->incoming = NULL; c->room = 0;
}

/* The transfer out in pieces ended, its requestor no longer watched. */
static void send_stop(tiku_x11_clipboard_t *c)
{
    if (c->send.requestor) {
        trap(c->display);
        XSelectInput(c->display, c->send.requestor, NoEventMask);
        (void)untrap(c->display);
    }
    free(c->send.data);
    memset(&c->send, 0, sizeof c->send);
}

/* A failed external replacement is unavailable, never a stale local paste.
 * Empty pastes are deliberately no-ops in the field kit and app services.
 * A refresh nobody asked for keeps its failure to itself: the text stands
 * and no error reaches a status line the hand never aimed at. */
static void finish(tiku_x11_clipboard_t *c, int result, int64_t now)
{
    if (result != TIKU_CLIPBOARD_OK && !c->asked) {
        c->cached_owner = c->source;
        c->refreshed = now;
        c->asked = 0;
        cancel(c);
        XFlush(c->display);
        return;
    }
    if (result != TIKU_CLIPBOARD_OK) tiku_clipboard_set("");
    c->stamp = tiku_clipboard_stamp(); /* Imports must never claim ownership. */
    c->cached_owner = c->source;
    c->refreshed = now;
    c->result = result;
    c->asked = 0;
    cancel(c);
    XFlush(c->display);
}

void tiku_x11_clipboard_init(tiku_x11_clipboard_t *c, Display *d, Window w)
{
    memset(c, 0, sizeof *c);
    c->display = d; c->window = w;
    c->clipboard = XInternAtom(d, "CLIPBOARD", False);
    c->utf8 = XInternAtom(d, "UTF8_STRING", False);
    c->targets = XInternAtom(d, "TARGETS", False);
    c->text = XInternAtom(d, "TEXT", False);
    c->timestamp = XInternAtom(d, "TIMESTAMP", False);
    c->incr = XInternAtom(d, "INCR", False);
    c->property = XInternAtom(d, "_TIKU_CLIPBOARD", False);
    c->clock = XInternAtom(d, "_TIKU_CLIPBOARD_TIME", False);
    c->stamp = tiku_clipboard_stamp();
}
void tiku_x11_clipboard_close(tiku_x11_clipboard_t *c)
{
    cancel(c);
    send_stop(c);
    free(c->outgoing); c->outgoing = NULL; c->outlen = 0;
}

void tiku_x11_clipboard_tick(tiku_x11_clipboard_t *c, int64_t now)
{
    if (c->stamp != tiku_clipboard_stamp()) {
        size_t n = tiku_clipboard_length();
        cancel(c);
        c->result = 0;
        c->stamp = tiku_clipboard_stamp();
        free(c->outgoing);
        c->outgoing = n ? malloc(n + 1) : NULL;
        c->outlen = c->outgoing ? n : 0;
        if (c->outgoing) memcpy(c->outgoing, tiku_clipboard_text(), n + 1);
        /* Obtain a real server timestamp, including for copy from remote apps. */
        if (!c->publishing) {
            c->publishing = 1;
            XChangeProperty(c->display, c->window, c->clock, XA_INTEGER,
                             8, PropModeReplace, (unsigned char *)"", 0);
            XFlush(c->display);
        }
    }
    if (c->requestor && now >= c->deadline) finish(c, TIKU_CLIPBOARD_TIMEOUT, now);
    if (c->send.requestor && now >= c->send.deadline) send_stop(c);
}

int tiku_x11_clipboard_request(tiku_x11_clipboard_t *c, int64_t now, int force)
{
    if (c->publishing || c->requestor) return c->requestor != None;
    Window owner = XGetSelectionOwner(c->display, c->clipboard);
    if (owner == c->window) return 0;
    if (!force && owner == c->cached_owner && c->refreshed &&
        now - c->refreshed < 250000) return 0;
    c->source = owner;
    if (!owner) {
        /* No owner means no host text, including after its owner exits. */
        tiku_clipboard_set("");
        finish(c, TIKU_CLIPBOARD_OK, now);
        return 0;
    }
    c->requestor = XCreateSimpleWindow(c->display,
        DefaultRootWindow(c->display), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(c->display, c->requestor, PropertyChangeMask);
    c->requested = c->utf8; c->received = None; c->used = 0;
    c->asked = force != 0;
    c->deadline = now + WAIT_US;
    XConvertSelection(c->display, c->clipboard, c->requested, c->property,
                       c->requestor, CurrentTime);
    XFlush(c->display);
    return 1;
}

/* Room for @p more bytes past what came in, and a NUL. */
static int grow(tiku_x11_clipboard_t *c, size_t more)
{
    size_t need = c->used + more + 1, room = c->room ? c->room : 4096;
    char *bigger;
    if (need <= c->room) return 1;
    while (room < need) room *= 2;
    if (!(bigger = realloc(c->incoming, room))) return 0;
    c->incoming = bigger; c->room = room;
    return 1;
}

static void import(tiku_x11_clipboard_t *c, int64_t now)
{
    char *converted = NULL;
    const char *text = c->incoming ? c->incoming : "";
    size_t length = c->used;
    int ok;
    if (XGetSelectionOwner(c->display, c->clipboard) != c->source) {
        finish(c, TIKU_CLIPBOARD_UNAVAILABLE, now); return;
    }
    if (c->received == XA_STRING) {       /* Latin-1: two bytes at most each */
        if (!(converted = malloc(2 * c->used + 1))) {
            finish(c, TIKU_CLIPBOARD_TOO_LARGE, now); return;
        }
        length = 0;
        for (size_t i = 0; i < c->used; i++) {
            unsigned char ch = (unsigned char)c->incoming[i];
            if (ch >= 128) converted[length++] = (char)(0xc0 | (ch >> 6));
            converted[length++] = (char)(ch < 128 ? ch : 0x80 | (ch & 63));
        }
        if (length > TIKU_CLIPBOARD_LIMIT) {
            free(converted); finish(c, TIKU_CLIPBOARD_TOO_LARGE, now); return;
        }
        text = converted;
    }
    ok = tiku_clipboard_import(text, length);
    free(converted);
    finish(c, ok ? TIKU_CLIPBOARD_OK : TIKU_CLIPBOARD_INVALID, now);
}

/* The requestor's property read whole, a piece at a time however large,
 * then deleted -- which in an INCR transfer asks for the next piece. */
static void read_property(tiku_x11_clipboard_t *c, int initial, int64_t now)
{
    Atom type = None;
    int format = 0, result = 0;
    unsigned long count = 0, after = 0, got = 0;
    long offset = 0;
    do {
        unsigned char *data = NULL;
        if (XGetWindowProperty(c->display, c->requestor, c->property, offset,
                READ_UNITS, False, AnyPropertyType, &type, &format, &count,
                &after, &data) != Success || type == None) {
            if (data) XFree(data);
            if (offset == 0) return;              /* nothing there yet */
            break;
        }
        if (initial && offset == 0 && type == c->incr) {
            int valid = format == 32 && count == 1 && !after;
            unsigned long lower = valid ? *(unsigned long *)data : 0;
            XFree(data);
            XDeleteProperty(c->display, c->requestor, c->property);
            if (!valid) finish(c, TIKU_CLIPBOARD_INVALID, now);
            else if (lower > TIKU_CLIPBOARD_LIMIT)
                finish(c, TIKU_CLIPBOARD_TOO_LARGE, now);
            else { c->incremental = 1; c->deadline = now + WAIT_US; }
            XFlush(c->display);
            return;
        }
        if (format != 8 || (type != c->utf8 && type != XA_STRING) ||
            (c->received != None && type != c->received))
            result = TIKU_CLIPBOARD_INVALID;
        else if (count > TIKU_CLIPBOARD_LIMIT - c->used || !grow(c, count))
            result = TIKU_CLIPBOARD_TOO_LARGE;
        else {
            c->received = type;
            if (count) memcpy(c->incoming + c->used, data, count);
            c->used += count; got += count;
            offset += (long)(count / 4);
        }
        if (data) XFree(data);
    } while (!result && after);
    XDeleteProperty(c->display, c->requestor, c->property);
    c->deadline = now + WAIT_US;          /* progress: the clock starts again */
    if (result) finish(c, result, now);
    else if (!c->incremental || !got) import(c, now);
    XFlush(c->display);
}

/* XA_STRING is Latin-1, not UTF-8. Refuse unrepresentable Unicode honestly.
 * @return the bytes made into *out (the caller frees), or -1. */
static long latin1(const char *s, size_t n, unsigned char **out)
{
    unsigned char *o = malloc(n + 1);
    long k = 0;
    if (!o) return -1;
    for (size_t i = 0; i < n;) {
        unsigned char ch = (unsigned char)s[i++];
        if (ch < 128) o[k++] = ch;
        else if ((ch == 0xc2 || ch == 0xc3) && i < n &&
                 ((unsigned char)s[i] & 0xc0) == 0x80)
            o[k++] = (unsigned char)(((ch & 3) << 6) | (s[i++] & 63));
        else { free(o); return -1; }
    }
    *out = o;
    return k;
}

/* @p n bytes of @p data, of @p type, onto a requestor's property: in one
 * request when they fit any server's, else as the one INCR transfer out
 * (on a copy), announced by the size.  @return whether it will get them. */
static int put_text(tiku_x11_clipboard_t *c, XSelectionRequestEvent *q,
                    Atom prop, Atom type, const char *data, size_t n,
                    int64_t now)
{
    long size = (long)n;
    char *copy;
    if (n <= SINGLE_MAX) {
        XChangeProperty(c->display, q->requestor, prop, type, 8,
            PropModeReplace, (const unsigned char *)(data ? data : ""), (int)n);
        return 1;
    }
    if (c->send.requestor || !(copy = malloc(n))) return 0;  /* one at a time */
    memcpy(copy, data, n);
    XSelectInput(c->display, q->requestor, PropertyChangeMask);
    XChangeProperty(c->display, q->requestor, prop, c->incr, 32,
                     PropModeReplace, (unsigned char *)&size, 1);
    c->send = (tiku_x11_send_t){.requestor = q->requestor, .property = prop,
        .type = type, .data = copy, .len = n, .deadline = now + SEND_US};
    return 1;
}

/* The next piece to a requestor that deleted the last; the empty piece
 * after the text ends the transfer (ICCCM 2.7.2). */
static void send_next(tiku_x11_clipboard_t *c, int64_t now)
{
    tiku_x11_send_t *s = &c->send;
    size_t n = s->len - s->sent < PIECE ? s->len - s->sent : PIECE;
    trap(c->display);
    XChangeProperty(c->display, s->requestor, s->property, s->type, 8,
        PropModeReplace, (unsigned char *)s->data + s->sent, (int)n);
    if (untrap(c->display) || n == 0) { send_stop(c); return; }
    s->sent += n;
    s->deadline = now + SEND_US;
}

static void serve(tiku_x11_clipboard_t *c, XSelectionRequestEvent *q,
                  int64_t now)
{
    XEvent reply = {0};
    Atom prop = q->property ? q->property : q->target;
    reply.xselection = (XSelectionEvent){.type=SelectionNotify,
        .display=q->display, .requestor=q->requestor, .selection=q->selection,
        .target=q->target, .time=q->time, .property=None};
    int owned = q->selection == c->clipboard &&
        XGetSelectionOwner(c->display, c->clipboard) == c->window &&
        (q->time == CurrentTime || (int32_t)(q->time - c->acquired) >= 0);
    trap(c->display);
    if (owned) {
        if (q->target == c->targets) {
            Atom targets[] = {c->targets, c->timestamp, c->utf8, c->text, XA_STRING};
            XChangeProperty(c->display, q->requestor, prop, XA_ATOM, 32,
                             PropModeReplace, (unsigned char *)targets, 5);
            reply.xselection.property = prop;
        } else if (q->target == c->timestamp) {
            XChangeProperty(c->display, q->requestor, prop, XA_INTEGER, 32,
                             PropModeReplace, (unsigned char *)&c->acquired, 1);
            reply.xselection.property = prop;
        } else if (q->target == c->utf8 || q->target == c->text) {
            if (put_text(c, q, prop, c->utf8, c->outgoing, c->outlen, now))
                reply.xselection.property = prop;
        } else if (q->target == XA_STRING) {
            unsigned char *converted = NULL;
            long n = latin1(c->outgoing ? c->outgoing : "", c->outlen,
                            &converted);
            if (n >= 0 && put_text(c, q, prop, XA_STRING, (char *)converted,
                                   (size_t)n, now))
                reply.xselection.property = prop;
            free(converted);
        }
    }
    if (untrap(c->display)) reply.xselection.property = None;
    trap(c->display);
    XSendEvent(c->display, q->requestor, False, 0, &reply);
    (void)untrap(c->display);
}

Bool tiku_x11_clipboard_match(Display *d, XEvent *e, XPointer ctx)
{
    tiku_x11_clipboard_t *c = (tiku_x11_clipboard_t *)ctx;
    (void)d;
    return (e->type == SelectionRequest && e->xselectionrequest.selection == c->clipboard) ||
        (e->type == SelectionClear && e->xselectionclear.selection == c->clipboard) ||
        (e->type == SelectionNotify && e->xselection.selection == c->clipboard) ||
        (e->type == PropertyNotify &&
         (e->xproperty.window == c->requestor ||
          (c->send.requestor && e->xproperty.window == c->send.requestor) ||
          (e->xproperty.window == c->window && e->xproperty.atom == c->clock)));
}
int tiku_x11_clipboard_event(tiku_x11_clipboard_t *c, XEvent *e, int64_t now)
{
    if (!tiku_x11_clipboard_match(c->display, e, (XPointer)c)) return 0;
    if (e->type == SelectionRequest) serve(c, &e->xselectionrequest, now);
    else if (e->type == SelectionClear && e->xselectionclear.selection == c->clipboard)
        c->refreshed = 0;
    else if (e->type == PropertyNotify && c->send.requestor &&
             e->xproperty.window == c->send.requestor) {
        if (e->xproperty.atom == c->send.property &&
            e->xproperty.state == PropertyDelete) send_next(c, now);
    } else if (e->type == PropertyNotify && e->xproperty.window == c->window &&
             e->xproperty.atom == c->clock && e->xproperty.state == PropertyNewValue &&
             c->publishing) {
        c->publishing = 0; c->acquired = e->xproperty.time;
        c->cached_owner = c->window; c->refreshed = 0;
        XSetSelectionOwner(c->display, c->clipboard, c->window, c->acquired);
        XFlush(c->display);
    } else if (e->type == SelectionNotify && c->requestor &&
               e->xselection.requestor == c->requestor &&
               e->xselection.selection == c->clipboard &&
               e->xselection.target == c->requested) {
        if (e->xselection.property == c->property) read_property(c, 1, now);
        else if (e->xselection.property == None && c->requested == c->utf8) {
            c->requested = XA_STRING;
            XConvertSelection(c->display, c->clipboard, XA_STRING, c->property,
                               c->requestor, CurrentTime);
            XFlush(c->display);
        } else finish(c, TIKU_CLIPBOARD_UNAVAILABLE, now);
    } else if (e->type == PropertyNotify && c->requestor && c->incremental &&
               e->xproperty.window == c->requestor && e->xproperty.atom == c->property &&
               e->xproperty.state == PropertyNewValue) read_property(c, 0, now);
    return 1;
}
