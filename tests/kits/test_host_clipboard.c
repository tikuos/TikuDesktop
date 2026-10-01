/* Real X11 selection peers on an explicitly isolated test server only.
 * SPDX-License-Identifier: Apache-2.0 */
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tiku_x11_clipboard.h"
#include "tiku_host.h"
#include "tiku_x11_command.h"
#include "tiku_text_input.h"

enum { NORMAL, LATIN, CHUNKED, SILENT, REFUSE };
static Display *peer;
static Window peer_window;
static Atom clip, utf8, incr, prop;
static const char *payload;
static size_t length, sent;
static int mode, active, got_reply;
static size_t piece = 3;       /* bytes of each INCR piece the peer sends */
static Window receiver;
static Atom receive_prop, reply_prop;
static int64_t now = 10000000;

static void peer_event(XEvent *e)
{
    if (e->type == SelectionNotify) {
        got_reply = 1; reply_prop = e->xselection.property;
    } else if (e->type == SelectionRequest) {
        XSelectionRequestEvent *q = &e->xselectionrequest;
        if (mode == SILENT) return;
        XEvent reply = {0};
        reply.xselection = (XSelectionEvent){.type=SelectionNotify,
            .requestor=q->requestor, .selection=q->selection, .target=q->target,
            .time=q->time, .property=None};
        if (mode != REFUSE && (q->target == utf8 || q->target == XA_STRING) &&
            (mode != LATIN || q->target == XA_STRING)) {
            reply.xselection.property = q->property;
            if (mode == CHUNKED) {
                unsigned long n = length;
                receiver = q->requestor; receive_prop = q->property; sent = 0; active = 1;
                if (length > TIKU_CLIPBOARD_LIMIT) active = 0;
                XSelectInput(peer, receiver, PropertyChangeMask);
                XChangeProperty(peer, receiver, receive_prop, incr, 32,
                                 PropModeReplace, (unsigned char *)&n, 1);
            } else {
                /* A megabyte a request, appended: a property may pass the
                 * clipboard's limit though no one request could. */
                const unsigned char *at = (const unsigned char *)payload;
                size_t left = length;
                int first = 1;
                do {
                    size_t n = left > (1u << 20) ? (1u << 20) : left;
                    XChangeProperty(peer, q->requestor, q->property,
                        mode == LATIN ? XA_STRING : utf8, 8,
                        first ? PropModeReplace : PropModeAppend, at, (int)n);
                    at += n; left -= n; first = 0;
                } while (left);
            }
        }
        XSendEvent(peer, q->requestor, False, 0, &reply);
    } else if (e->type == PropertyNotify && active &&
               e->xproperty.window == receiver && e->xproperty.atom == receive_prop &&
               e->xproperty.state == PropertyDelete) {
        size_t n = length - sent;
        if (n > piece) n = piece; /* 3 splits multi-byte UTF-8 characters. */
        XChangeProperty(peer, receiver, receive_prop, utf8, 8,
                         PropModeReplace, (const unsigned char *)payload + sent, (int)n);
        sent += n;
        if (!n) active = 0;
    }
    XFlush(peer);
}

static void pump(tiku_x11_clipboard_t *c)
{
    XEvent e;
    XSync(c->display, False); XSync(peer, False);
    while (XPending(c->display)) {
        XNextEvent(c->display, &e);
        tiku_x11_clipboard_event(c, &e, now);
    }
    while (XPending(peer)) { XNextEvent(peer, &e); peer_event(&e); }
    now += 1000;
    tiku_x11_clipboard_tick(c, now);
}
static void owner(int kind, const char *text, size_t n)
{
    mode = kind; payload = text; length = n; active = 0;
    XSetSelectionOwner(peer, clip, peer_window, CurrentTime);
    XSync(peer, False);
    assert(XGetSelectionOwner(peer, clip) == peer_window);
}
static int fetch(tiku_x11_clipboard_t *c)
{
    c->result = 0;
    assert(tiku_x11_clipboard_request(c, now, 1));
    for (int i = 0; !c->result && i < 1200; i++) pump(c);
    assert(c->result && !c->requestor);
    return c->result;
}
static void publish(tiku_x11_clipboard_t *c, const char *text)
{
    tiku_clipboard_set(text); tiku_x11_clipboard_tick(c, now);
    for (int i = 0; c->publishing && i < 20; i++) pump(c);
    assert(!c->publishing && c->acquired);
    XSync(c->display, False);
    assert(XGetSelectionOwner(peer, clip) == c->window);
}
static unsigned char *get(tiku_x11_clipboard_t *c, Atom target,
                           Atom *type, int *format, unsigned long *count)
{
    got_reply = 0;
    XConvertSelection(peer, clip, target, prop, peer_window, CurrentTime);
    XFlush(peer);
    for (int i = 0; !got_reply && i < 100; i++) pump(c);
    assert(got_reply);
    if (!reply_prop) return NULL;
    unsigned long after;
    unsigned char *data = NULL;
    assert(XGetWindowProperty(peer, peer_window, prop, 0, 2048, True,
        AnyPropertyType, type, format, count, &after, &data) == Success && !after);
    return data;
}

/* A conversion the peer takes INCR: the announcement, then each piece
 * as the owning side sends it when the last is deleted, to the empty one.
 * @return the bytes taken into @p out. */
static size_t get_incr(tiku_x11_clipboard_t *c, Atom target, char *out)
{
    Atom type; int format; unsigned long count, after;
    unsigned char *data = NULL;
    size_t used = 0;
    got_reply = 0;
    XConvertSelection(peer, clip, target, prop, peer_window, CurrentTime);
    XFlush(peer);
    for (int i = 0; !got_reply && i < 100; i++) pump(c);
    assert(got_reply && reply_prop);
    assert(XGetWindowProperty(peer, peer_window, prop, 0, 1, True,
        AnyPropertyType, &type, &format, &count, &after, &data) == Success);
    assert(type == incr && format == 32 && count == 1); XFree(data);
    for (int i = 0; i < 10000; i++) {
        pump(c);
        data = NULL;
        assert(XGetWindowProperty(peer, peer_window, prop, 0, 1L << 20, False,
            AnyPropertyType, &type, &format, &count, &after, &data) == Success);
        if (type == None) continue;
        assert(type == (target == XA_STRING ? XA_STRING : utf8) && !after);
        if (count) memcpy(out + used, data, count);
        used += count;
        XFree(data);
        XDeleteProperty(peer, peer_window, prop); XFlush(peer);
        if (!count) break;
    }
    return used;
}

/* Text too large for one request, served INCR in both encodings; a second
 * requestor turned away while one takes pieces; a stalled one let go. */
static void export_test(tiku_x11_clipboard_t *c)
{
    size_t n = 3u << 20;
    char *text = malloc(n + 1), *got = malloc(n + 1);
    assert(text && got);
    for (size_t i = 0; i < n; i++) text[i] = (char)('A' + i % 23);
    text[n] = 0;
    publish(c, text);
    assert(get_incr(c, utf8, got) == n && !memcmp(got, text, n));
    assert(get_incr(c, XA_STRING, got) == n && !memcmp(got, text, n));
    assert(!c->send.requestor);          /* each ended with its empty piece */
    Window other = XCreateSimpleWindow(peer, DefaultRootWindow(peer), 0,0,1,1,0,0,0);
    got_reply = 0;
    XConvertSelection(peer, clip, utf8, prop, peer_window, CurrentTime);
    XFlush(peer);
    for (int i = 0; !got_reply && i < 100; i++) pump(c);
    assert(got_reply && reply_prop && c->send.requestor == peer_window);
    got_reply = 0;
    XConvertSelection(peer, clip, utf8, prop, other, CurrentTime);
    XFlush(peer);
    for (int i = 0; !got_reply && i < 100; i++) pump(c);
    assert(got_reply && !reply_prop);    /* one transfer out at a time */
    now += 5000001;                      /* the first never reads on */
    pump(c);
    assert(!c->send.requestor);
    XDeleteProperty(peer, peer_window, prop);
    XDestroyWindow(peer, other); XSync(peer, False);
    free(text); free(got);
}

static void protocol_test(Display *d)
{
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0,0,1,1,0,0,0);
    XSelectInput(d, w, PropertyChangeMask);
    tiku_x11_clipboard_t c;
    tiku_x11_clipboard_init(&c, d, w);
    owner(NORMAL, "caf\xc3\xa9", 5);
    assert(fetch(&c) == TIKU_CLIPBOARD_OK);
    assert(!strcmp(tiku_clipboard_text(), "caf\xc3\xa9"));
    tiku_x11_clipboard_tick(&c, now);
    assert(XGetSelectionOwner(d, clip) == peer_window); /* No ownership echo. */
    owner(LATIN, "caf\xe9", 4);
    assert(fetch(&c) == TIKU_CLIPBOARD_OK && !strcmp(tiku_clipboard_text(), "caf\xc3\xa9"));
    owner(CHUNKED, "a\xf0\x9f\x8c\x8d\nxyz", 9);
    assert(fetch(&c) == TIKU_CLIPBOARD_OK && !strcmp(tiku_clipboard_text(), payload));
    owner(NORMAL, "bad\xff", 4);
    assert(fetch(&c) == TIKU_CLIPBOARD_INVALID && !*tiku_clipboard_text());
    owner(NORMAL, "a\0b", 3);
    assert(fetch(&c) == TIKU_CLIPBOARD_INVALID);
    size_t most = TIKU_CLIPBOARD_LIMIT;
    char *large = malloc(most + 2); assert(large);
    for (size_t i = 0; i < most + 2; i++) large[i] = (char)('a' + i % 26);
    owner(NORMAL, large, most + 1);
    assert(fetch(&c) == TIKU_CLIPBOARD_TOO_LARGE);
    owner(CHUNKED, large, most + 1);
    assert(fetch(&c) == TIKU_CLIPBOARD_TOO_LARGE);
    owner(NORMAL, large, 5u << 20);    /* read in pieces, one property */
    assert(fetch(&c) == TIKU_CLIPBOARD_OK);
    assert(tiku_clipboard_length() == 5u << 20 &&
           !memcmp(tiku_clipboard_text(), large, 5u << 20));
    piece = 64u * 1024u;
    owner(CHUNKED, large + 1, 3u << 20); /* INCR, in 64 KB pieces */
    assert(fetch(&c) == TIKU_CLIPBOARD_OK);
    assert(tiku_clipboard_length() == 3u << 20 &&
           !memcmp(tiku_clipboard_text(), large + 1, 3u << 20));
    owner(CHUNKED, large, most);       /* the limit exactly */
    assert(fetch(&c) == TIKU_CLIPBOARD_OK);
    assert(tiku_clipboard_length() == most &&
           !memcmp(tiku_clipboard_text(), large, most));
    piece = 3;
    /* A refresh nobody asked for keeps its failure to itself: the text
     * that was there stands, and no result is raised at the user. */
    owner(REFUSE, "", 0);
    c.result = 0;
    assert(tiku_x11_clipboard_request(&c, now + 2000000, 0));
    for (int i = 0; c.requestor && i < 1200; i++) pump(&c);
    assert(!c.requestor && c.result == 0);
    assert(tiku_clipboard_length() == most);
    memset(large, 0xe9, most / 2 + 1); /* two bytes each once UTF-8 */
    owner(LATIN, large, most / 2 + 1);
    assert(fetch(&c) == TIKU_CLIPBOARD_TOO_LARGE);
    free(large);
    owner(NORMAL, "", 0);
    assert(fetch(&c) == TIKU_CLIPBOARD_OK && !*tiku_clipboard_text());
    owner(REFUSE, "", 0);
    assert(fetch(&c) == TIKU_CLIPBOARD_UNAVAILABLE);
    owner(SILENT, "", 0);
    c.result = 0; assert(tiku_x11_clipboard_request(&c, now, 1));
    Window obsolete = c.requestor;
    tiku_x11_clipboard_tick(&c, now + 1000001);
    assert(c.result == TIKU_CLIPBOARD_TIMEOUT && !c.requestor);
    XEvent late = {0};
    late.xselection = (XSelectionEvent){.type=SelectionNotify, .requestor=obsolete,
        .selection=clip, .target=utf8, .property=prop};
    tiku_x11_clipboard_event(&c, &late, now);
    assert(!*tiku_clipboard_text());
    /* A new local copy wins over an outstanding external conversion. */
    assert(tiku_x11_clipboard_request(&c, now, 1));
    XSetSelectionOwner(peer, XA_PRIMARY, peer_window, CurrentTime);
    XSync(peer, False);
    publish(&c, "local caf\xc3\xa9");
    assert(!c.requestor);
    assert(XGetSelectionOwner(d, XA_PRIMARY) == peer_window);
    Atom type; int format; unsigned long count;
    unsigned char *data = get(&c, utf8, &type, &format, &count);
    assert(data && type == utf8 && format == 8 && count == 11);
    assert(!memcmp(data, "local caf\xc3\xa9", count)); XFree(data);
    data = get(&c, XA_STRING, &type, &format, &count);
    assert(data && type == XA_STRING && count == 10 && data[9] == 0xe9); XFree(data);
    data = get(&c, c.targets, &type, &format, &count);
    assert(data && type == XA_ATOM && format == 32 && count == 5); XFree(data);
    data = get(&c, c.timestamp, &type, &format, &count);
    assert(data && type == XA_INTEGER && format == 32 && count == 1);
    /* Xlib sign-extends a 32-bit item into its long; the time is 32 bits. */
    assert((uint32_t)*(unsigned long *)data == (uint32_t)c.acquired); XFree(data);
    assert(!get(&c, XInternAtom(d, "image/png", False), &type, &format, &count));
    publish(&c, "\xf0\x9f\x8c\x8d");
    assert(!get(&c, XA_STRING, &type, &format, &count));
    export_test(&c);
    /* A vanished requestor must not terminate the desktop through Xlib. */
    Window dead = XCreateSimpleWindow(peer, DefaultRootWindow(peer), 0,0,1,1,0,0,0);
    XDestroyWindow(peer, dead); XSync(peer, False);
    XEvent request = {0};
    request.xselectionrequest = (XSelectionRequestEvent){.type=SelectionRequest,
        .display=d, .owner=w, .requestor=dead, .selection=clip, .target=utf8,
        .property=prop, .time=CurrentTime};
    tiku_x11_clipboard_event(&c, &request, now);
    owner(NORMAL, "new external", 12);
    assert(fetch(&c) == TIKU_CLIPBOARD_OK && !strcmp(tiku_clipboard_text(), payload));
    XSetSelectionOwner(peer, clip, None, CurrentTime); XSync(peer, False);
    tiku_x11_clipboard_request(&c, now, 1);
    assert(!*tiku_clipboard_text());
    tiku_x11_clipboard_close(&c); XDestroyWindow(d, w); XSync(d, False);
    puts("PASS: X11 UTF-8/Latin-1, INCR, limits, timeout, stale replies, ownership and dead peers");
}

static Window find_host(void)
{
    Window root, parent, *children = NULL, found = None;
    unsigned count = 0;
    XQueryTree(peer, DefaultRootWindow(peer), &root, &parent, &children, &count);
    for (unsigned i = 0; i < count; i++) {
        char *name = NULL;
        if (XFetchName(peer, children[i], &name) && name) {
            if (!strcmp(name, "Clipboard test host")) found = children[i];
            XFree(name);
        }
    }
    if (children) XFree(children);
    assert(found); return found;
}
static void host_test(const char *display)
{
    setenv("DISPLAY", display, 1);
    setenv("LC_ALL", "C.UTF-8", 1);
    setenv("XMODIFIERS", "@im=none", 1);
    /* Only the private test server's keymap is changed. Open the host after
     * mapping so its Xlib keyboard cache starts with these deterministic keys. */
    KeySym compose_keys[] = {XK_Multi_key, XK_apostrophe, XK_e, 0x010003bb};
    XChangeKeyboardMapping(peer, 240, 1, compose_keys, 4);
    /* Meta on Mod2, which is the shape a Mac's keymap has: the Command
     * key is not a modifier X knows by name, it is whichever numbered
     * one the keymap hung it on.  Installed BEFORE the host opens,
     * because the host reads the map once and remembers. */
    KeySym meta = XK_Meta_L;
    XChangeKeyboardMapping(peer, 250, 1, &meta, 1);
    XSync(peer, False);
    XModifierKeymap *modmap = XGetModifierMapping(peer);
    assert(modmap);
    for (int slot = 0; slot < modmap->max_keypermod; slot++)
        if (!modmap->modifiermap[4 * modmap->max_keypermod + slot]) {
            modmap->modifiermap[4 * modmap->max_keypermod + slot] = 250;
            break;
        }
    assert(XSetModifierMapping(peer, modmap) == MappingSuccess);
    XFreeModifiermap(modmap);
    XSync(peer, False);
    tiku_host_t *host = tiku_host_open(320,240,"Clipboard test host");
    assert(host);
    tiku_event_t ev;
    for (int i = 0; i < 20; i++) while (tiku_host_poll(host, &ev)) {}
    Window w = find_host();
    owner(NORMAL, "fresh external", 14);
    XEvent paste = {0};
    paste.xkey = (XKeyEvent){.type=KeyPress, .display=peer, .window=w,
        .root=DefaultRootWindow(peer), .keycode=XKeysymToKeycode(peer, XK_v),
        .state=ControlMask, .same_screen=True};
    XSendEvent(peer, w, False, KeyPressMask, &paste);
    XEvent after = paste; after.xkey.keycode = XKeysymToKeycode(peer, XK_a);
    XSendEvent(peer, w, False, KeyPressMask, &after); XFlush(peer);
    int changed = 0, pasted = 0, following = 0;
    for (int i = 0; i < 2000 && !following; i++) {
        while (tiku_host_poll(host, &ev)) {
            if (ev.type == TIKU_EVENT_CLIPBOARD) {
                assert(ev.key == TIKU_CLIPBOARD_OK);
                assert(!strcmp(tiku_clipboard_text(), "fresh external")); changed = 1;
            }
            if (ev.type == TIKU_EVENT_KEY_DOWN && ev.key == 'v') {
                assert(changed); pasted = 1;
            }
            if (ev.type == TIKU_EVENT_KEY_DOWN && ev.key == 'a') {
                assert(pasted); following = 1;
            }
        }
        XSync(peer, False);
        while (XPending(peer)) { XEvent e; XNextEvent(peer, &e); peer_event(&e); }
        struct timespec pause = {0,1000000}; nanosleep(&pause, NULL);
    }
    assert(changed && pasted && following);
    /*
     * Which key means "this is an instruction".  Control always, on
     * every host, because every shortcut this desktop has is a Control
     * chord.  The Command key as well where the machine has one -- and
     * whether it has one is the seam's answer, not this file's guess.
     *
     * Option is the one that must NOT come back as a command: Meta and
     * Alt share a key on most keymaps, so a rule that took every
     * modifier carrying Meta would take Option too, and holding Option
     * would start acting on files rather than copying them.
     */
    {
        static const struct { unsigned state; const char *what; } PRESS[] = {
            { ControlMask, "Control" },
            { Mod2Mask,    "the Command key, on whichever modifier" },
            { Mod1Mask,    "Option" }
        };
        int mac = tiku_x11_command_is_meta();

        for (unsigned t = 0; t < sizeof PRESS / sizeof PRESS[0]; t++) {
            XEvent key = {0};
            int seen = 0;

            key.xkey = (XKeyEvent){.type=KeyPress, .display=peer, .window=w,
                .root=DefaultRootWindow(peer), .same_screen=True,
                .state=PRESS[t].state,
                .keycode=XKeysymToKeycode(peer, XK_b)};
            XSendEvent(peer, w, False, KeyPressMask, &key);
            XSync(peer, False);
            for (int i = 0; i < 200 && !seen; i++) {
                while (tiku_host_poll(host, &ev)) {
                    if (ev.type != TIKU_EVENT_KEY_DOWN) continue;
                    seen = 1;
                    if (PRESS[t].state == ControlMask)
                        assert(ev.modifiers & TIKU_MOD_CMD);
                    else if (PRESS[t].state == Mod1Mask) {
                        assert(ev.modifiers & TIKU_MOD_OPTION);
                        assert(!(ev.modifiers & TIKU_MOD_CMD));
                    } else
                        assert(!!(ev.modifiers & TIKU_MOD_CMD) == mac);
                }
                struct timespec pause = {0,1000000}; nanosleep(&pause, NULL);
            }
            assert(seen);
            (void)PRESS[t].what;
        }
        printf("PASS: a command is Control%s, and never Option\n",
               mac ? " or Command" : " alone, as this host has no Command key");
    }

    /*
     * Compose needs an input method, and an input method needs Xlib to
     * support a locale.  Homebrew's libX11 on macOS supports NONE --
     * not even "C" -- so XOpenIM cannot succeed there and the host
     * falls back to XLookupString, which reads plain keys and knows
     * nothing of a Compose prefix.  That is a real limit of that build
     * and not of this tree, so it is said out loud rather than failed
     * or quietly passed: the assertions below describe an input method
     * and there is none to describe.
     */
    if (!XSupportsLocale()) {
        puts("SKIP: XIM Compose -- this host's Xlib supports no locale, so "
             "there is no input method to compose with");
        tiku_host_cancel_composition(host);
        tiku_host_close(host);
        puts("PASS: host emits clipboard update before Paste and preserves "
             "later input order");
        return;
    }
    tiku_text_input_t commit = {0};
    int completed = 0, key_down = 0;
    for (int code = 240; code <= 243; code++) {
        XEvent key = {0};
        key.xkey = (XKeyEvent){.type=KeyPress, .display=peer, .window=w,
            .root=DefaultRootWindow(peer), .keycode=(unsigned)code, .same_screen=True};
        XSendEvent(peer, w, False, KeyPressMask, &key);
        key.type = KeyRelease;
        XSendEvent(peer, w, False, KeyReleaseMask, &key);
    }
    XSync(peer, False);
    for (int i = 0; i < 300; i++) {
        while (tiku_host_poll(host, &ev)) {
            if (ev.type == TIKU_EVENT_KEY_DOWN) key_down++;
            int result = tiku_text_input_event(&commit, &ev);
            assert(result != TIKU_TEXT_REJECTED);
            if (result == TIKU_TEXT_COMPLETE) {
                assert(!strcmp(commit.text, completed ? "λ" : "é"));
                completed++;
            }
        }
        struct timespec pause = {0,1000000}; nanosleep(&pause, NULL);
    }
    assert(completed == 2 && !key_down);
    /* Discard a partial Compose sequence before another field gets focus. */
    for (int phase = 0; phase < 2; phase++) {
        for (int code = phase ? 242 : 240; code <= (phase ? 242 : 241); code++) {
            XEvent key = {0};
            key.xkey = (XKeyEvent){.type=KeyPress, .display=peer, .window=w,
                .root=DefaultRootWindow(peer), .keycode=(unsigned)code, .same_screen=True};
            XSendEvent(peer, w, False, KeyPressMask, &key);
            key.type = KeyRelease;
            XSendEvent(peer, w, False, KeyReleaseMask, &key);
        }
        XSync(peer, False);
        for (int i = 0; i < 50; i++) {
            while (tiku_host_poll(host, &ev)) {
                assert(ev.type != TIKU_EVENT_TEXT_INPUT);
                if (ev.type == TIKU_EVENT_KEY_DOWN) {
                    assert(phase && ev.key == 'e' && !strcmp(ev.text, "e"));
                    key_down++;
                }
            }
            struct timespec pause = {0,1000000}; nanosleep(&pause, NULL);
        }
        tiku_host_cancel_composition(host);
    }
    assert(key_down == 1);
    tiku_host_cancel_composition(host);
    tiku_host_close(host);
    puts("PASS: host emits clipboard update before Paste and preserves later input order");
    puts("PASS: XIM Compose, direct Unicode input and cancellation preserve shortcut semantics");
}
int main(void)
{
    const char *display = getenv("TIKU_CLIPBOARD_TEST_DISPLAY");
    if (!display || !*display) {
        puts("SKIP: host clipboard requires isolated server; run make test-host-clipboard");
        return 0;
    }
    Display *d = XOpenDisplay(display);
    peer = XOpenDisplay(display);
    assert(d && peer);
    peer_window = XCreateSimpleWindow(peer, DefaultRootWindow(peer), 0,0,1,1,0,0,0);
    clip = XInternAtom(peer, "CLIPBOARD", False);
    utf8 = XInternAtom(peer, "UTF8_STRING", False);
    incr = XInternAtom(peer, "INCR", False);
    prop = XInternAtom(peer, "_TIKU_TEST_CLIP", False);
    protocol_test(d); host_test(display);
    XCloseDisplay(peer); XCloseDisplay(d);
    return 0;
}
