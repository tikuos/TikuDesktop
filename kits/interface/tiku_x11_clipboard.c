/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_x11_clipboard.c - the X11 CLIPBOARD selection, in and out.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_x11_clipboard.h"
#include <X11/Xatom.h>
#include <string.h>

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

static void cancel(tiku_x11_clipboard_t *c)
{
    if (c->requestor) XDestroyWindow(c->display, c->requestor);
    c->requestor = None; c->incremental = 0; c->used = 0;
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
{ cancel(c); }

void tiku_x11_clipboard_tick(tiku_x11_clipboard_t *c, int64_t now)
{
    if (c->stamp != tiku_clipboard_stamp()) {
        cancel(c);
        c->result = 0;
        c->stamp = tiku_clipboard_stamp();
        memcpy(c->outgoing, tiku_clipboard_text(), strlen(tiku_clipboard_text()) + 1);
        /* Obtain a real server timestamp, including for copy from remote apps. */
        if (!c->publishing) {
            c->publishing = 1;
            XChangeProperty(c->display, c->window, c->clock, XA_INTEGER,
                             8, PropModeReplace, (unsigned char *)"", 0);
            XFlush(c->display);
        }
    }
    if (c->requestor && now >= c->deadline) finish(c, TIKU_CLIPBOARD_TIMEOUT, now);
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
    c->deadline = now + 1000000;
    XConvertSelection(c->display, c->clipboard, c->requested, c->property,
                       c->requestor, CurrentTime);
    XFlush(c->display);
    return 1;
}

static void import(tiku_x11_clipboard_t *c, int64_t now)
{
    char converted[TIKU_CLIPBOARD_MAX];
    const char *text = c->incoming;
    size_t length = c->used;
    if (XGetSelectionOwner(c->display, c->clipboard) != c->source) {
        finish(c, TIKU_CLIPBOARD_UNAVAILABLE, now); return;
    }
    if (c->received == XA_STRING) {
        length = 0;
        for (size_t i = 0; i < c->used; i++) {
            unsigned char ch = (unsigned char)c->incoming[i];
            size_t n = ch < 128 ? 1 : 2;
            if (length + n >= sizeof converted) {
                finish(c, TIKU_CLIPBOARD_TOO_LARGE, now); return;
            }
            if (n == 2) converted[length++] = (char)(0xc0 | (ch >> 6));
            converted[length++] = (char)(n == 1 ? ch : 0x80 | (ch & 63));
        }
        text = converted;
    }
    finish(c, tiku_clipboard_import(text, length) ? TIKU_CLIPBOARD_OK :
                                                   TIKU_CLIPBOARD_INVALID, now);
}

static void read_property(tiku_x11_clipboard_t *c, int initial, int64_t now)
{
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char *data = NULL;
    int rc = XGetWindowProperty(c->display, c->requestor, c->property, 0,
        TIKU_CLIPBOARD_MAX / 4, True, AnyPropertyType, &type, &format,
        &count, &after, &data);
    if (rc != Success || type == None) { if (data) XFree(data); return; }
    if (initial && type == c->incr) {
        int valid = format == 32 && count == 1 && !after;
        unsigned long lower = valid ? *(unsigned long *)data : 0;
        XFree(data);
        if (!valid) finish(c, TIKU_CLIPBOARD_INVALID, now);
        else if (lower >= TIKU_CLIPBOARD_MAX) finish(c, TIKU_CLIPBOARD_TOO_LARGE, now);
        else { c->incremental = 1; XFlush(c->display); }
        return;
    }
    int result = 0;
    if (after || count >= TIKU_CLIPBOARD_MAX - c->used) result = TIKU_CLIPBOARD_TOO_LARGE;
    else if (format != 8 || (type != c->utf8 && type != XA_STRING) ||
             (c->received != None && type != c->received)) result = TIKU_CLIPBOARD_INVALID;
    else {
        c->received = type;
        if (count) memcpy(c->incoming + c->used, data, count);
        c->used += count;
    }
    if (data) XFree(data);
    if (result) finish(c, result, now);
    else if (!c->incremental || !count) import(c, now);
    XFlush(c->display);
}

/* XA_STRING is Latin-1, not UTF-8. Refuse unrepresentable Unicode honestly. */
static int latin1(const char *s, unsigned char *out)
{
    int n = 0;
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch < 128) out[n++] = ch;
        else if ((ch == 0xc2 || ch == 0xc3) &&
                 ((unsigned char)*s & 0xc0) == 0x80)
            out[n++] = (unsigned char)(((ch & 3) << 6) | (*s++ & 63));
        else return -1;
    }
    return n;
}

static void serve(tiku_x11_clipboard_t *c, XSelectionRequestEvent *q)
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
            XChangeProperty(c->display, q->requestor, prop, c->utf8, 8,
                PropModeReplace, (unsigned char *)c->outgoing, (int)strlen(c->outgoing));
            reply.xselection.property = prop;
        } else if (q->target == XA_STRING) {
            unsigned char converted[TIKU_CLIPBOARD_MAX];
            int n = latin1(c->outgoing, converted);
            if (n >= 0) {
                XChangeProperty(c->display, q->requestor, prop, XA_STRING,
                                 8, PropModeReplace, converted, n);
                reply.xselection.property = prop;
            }
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
          (e->xproperty.window == c->window && e->xproperty.atom == c->clock)));
}
int tiku_x11_clipboard_event(tiku_x11_clipboard_t *c, XEvent *e, int64_t now)
{
    if (!tiku_x11_clipboard_match(c->display, e, (XPointer)c)) return 0;
    if (e->type == SelectionRequest) serve(c, &e->xselectionrequest);
    else if (e->type == SelectionClear && e->xselectionclear.selection == c->clipboard)
        c->refreshed = 0;
    else if (e->type == PropertyNotify && e->xproperty.window == c->window &&
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
