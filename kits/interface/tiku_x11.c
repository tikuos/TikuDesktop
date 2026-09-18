/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_x11.c - the native window backend.
 *
 * Plain Xlib: the interface layer owns every pixel, so a toolkit here would
 * only add a dependency and a second event model.  The wasm build replaces
 * this one file with a canvas.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <locale.h>
#include <poll.h>
#include "tiku_present.h"

#include "tiku_host.h"
#include "tiku_gfx.h"
#include "tiku_x11_clipboard.h"
#include "tiku_x11_command.h"
#include "tiku_text_input.h"

struct tiku_host {
    Display *display;
    Window window;
    GC gc;
    XImage *image;
    Pixmap backing;            /* last complete frame, including during boot */
    int mapped;                /* first presentation reveals the window */
    Atom delete_window;
    Cursor resize_cursor;
    Cursor cursors[6];         /* by TIKU_CURSOR_*, made when first asked */
    int cursor_kind;
    int scale;
    int logical_width;
    int logical_height;
    int native_width;
    int native_height;
    tiku_x11_clipboard_t clipboard;
    tiku_event_t deferred;
    int has_deferred, clipboard_ready;
    XIM input_method;
    XIC input_context;
    XIMCallback input_destroy;
    tiku_text_input_t commit;
    unsigned command_mask;     /* which modifiers mean "this is a command" */
    int fullscreen;            /* the window is the screen, or wants to be */
    int force_present;
    uint64_t presented_frames, presented_bytes, presented_rects;
};

/** @brief Xlib owns destruction when an input-method service disconnects. */
static void input_destroyed(XIM im, XPointer ctx, XPointer data)
{
    tiku_host_t *host = (tiku_host_t *)ctx;
    (void)im; (void)data;
    host->input_method = NULL; host->input_context = NULL;
    tiku_text_input_reset(&host->commit);
}

/** @brief Negotiate IM-managed composition UI, falling back to local Compose. */
static void input_open(tiku_host_t *host)
{
    XIMStyles *styles = NULL;
    XIMStyle chosen = 0;
    if (host->input_method) return;
    (void)XSetLocaleModifiers("");
    host->input_method = XOpenIM(host->display, NULL, NULL, NULL);
    if (!host->input_method) {
        (void)XSetLocaleModifiers("@im=none");
        host->input_method = XOpenIM(host->display, NULL, NULL, NULL);
        (void)XSetLocaleModifiers("");
    }
    if (!host->input_method) return;
    if (!XGetIMValues(host->input_method, XNQueryInputStyle, &styles, NULL) && styles) {
        XIMStyle choices[] = {XIMPreeditNothing | XIMStatusNothing,
                               XIMPreeditNone | XIMStatusNone};
        for (int j = 0; !chosen && j < 2; j++)
            for (unsigned i = 0; i < styles->count_styles; i++)
                if (styles->supported_styles[i] == choices[j]) chosen = choices[j];
    }
    if (styles) XFree(styles);
    if (chosen) host->input_context = XCreateIC(host->input_method,
        XNInputStyle, chosen, XNClientWindow, host->window,
        XNFocusWindow, host->window, NULL);
    if (!host->input_context) {
        XCloseIM(host->input_method); host->input_method = NULL; return;
    }
    host->input_destroy = (XIMCallback){(XPointer)host, input_destroyed};
    (void)XSetIMValues(host->input_method, XNDestroyCallback, &host->input_destroy, NULL);
    long mask = 0;
    XWindowAttributes attr;
    if (!XGetICValues(host->input_context, XNFilterEvents, &mask, NULL) &&
        XGetWindowAttributes(host->display, host->window, &attr))
        XSelectInput(host->display, host->window, attr.your_event_mask | mask);
}

void tiku_host_cancel_composition(tiku_host_t *host)
{
    if (!host) return;
    if (host->input_context) {
        char *discard = Xutf8ResetIC(host->input_context);
        if (discard) XFree(discard);
    }
    tiku_text_input_reset(&host->commit);
}

/** @brief Encode a keysym's Unicode value when no XIM context is available. */
static int input_utf8(unsigned cp, char *out)
{
    if (cp < 128) { out[0] = (char)cp; return 1; }
    if (cp < 2048) { out[0] = (char)(0xc0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp >= 0xd800 && cp <= 0xdfff) return 0;
    if (cp < 65536) { out[0] = (char)(0xe0 | cp >> 12);
        out[1] = (char)(0x80 | ((cp >> 6) & 63));
        out[2] = (char)(0x80 | (cp & 63)); return 3; }
    if (cp > 0x10ffff) return 0;
    out[0] = (char)(0xf0 | cp >> 18);
    out[1] = (char)(0x80 | ((cp >> 12) & 63));
    out[2] = (char)(0x80 | ((cp >> 6) & 63));
    out[3] = (char)(0x80 | (cp & 63)); return 4;
}

/**
 * @brief How many device pixels make one drawn pixel, at @p dpi.
 *
 * Whole numbers only, between one and four.  A desktop drawn at a
 * fraction of a pixel is a desktop with seams in every border, and past
 * four the arithmetic costs more than the screen can show.
 */
static int
scale_for_dpi(double dpi)
{
    int scale = (int)(dpi / 96.0 + 0.5);

    if (scale < 1) { scale = 1; }
    if (scale > 4) { scale = 4; }
    return scale;
}

/**
 * @brief What a screen's own measurements say its dpi is.
 *
 * Asked only when nobody has said.  Both numbers come from the same
 * server describing the same screen, so their ratio holds whichever
 * convention it counts in: device pixels give a high dpi and a large
 * scale, drawn pixels a low dpi and a small one, and the picture ends
 * up the same size either way.  A server with nothing to say fakes
 * about ninety-six, which lands on scale 1.
 *
 * @return the dpi, or 0.0 when the numbers cannot mean anything.
 */
static double
dpi_from_size(int pixels, int millimetres)
{
    if (pixels <= 0 || millimetres <= 0) {
        return 0.0;
    }
    return (double)pixels * 25.4 / (double)millimetres;
}

/** @brief Read the desktop's integer UI scale from its Xft DPI setting. */
static int
display_scale(Display *display)
{
    XrmDatabase database;
    XrmValue value;
    char *manager, *type = NULL;
    double dpi = 0.0;
    int screen;

    const char *forced = getenv("TIKU_SCALE");

    if (forced != NULL && forced[0] != '\0') {
        return scale_for_dpi(atoi(forced) * 96.0);
    }
    manager = XResourceManagerString(display);
    if (manager != NULL) {
        XrmInitialize();
        database = XrmGetStringDatabase(manager);
        if (database != NULL &&
            XrmGetResource(database, "Xft.dpi", "Xft.Dpi", &type,
                           &value) && value.addr != NULL) {
            dpi = strtod(value.addr, NULL);
        }
        if (database != NULL) { XrmDestroyDatabase(database); }
    }
    /*
     * Xft.dpi is a thing a DESKTOP SESSION sets, and on a Mac there is
     * no session to set it: a server started under Aqua carries no
     * resource database at all.  So a Retina screen said ninety-six and
     * the whole desktop came up at half the size it should be, every
     * window and every glyph, with nothing on screen to say why.  The
     * screen's own measurements are what is left to ask, and they are
     * the more honest answer anyway -- a person's eyes are a distance
     * from the glass, not a number in a database.
     */
    if (dpi <= 0.0) {
        screen = DefaultScreen(display);
        dpi = dpi_from_size(DisplayWidth(display, screen),
                            DisplayWidthMM(display, screen));
    }
    if (dpi <= 0.0) {
        dpi = 96.0;
    }
    return scale_for_dpi(dpi);
}

/** @brief Map a physical pointer position into the logical framebuffer. */
static int
logical_coord(int value, int native_extent, int logical_extent)
{
    if (value < 0) { return -1; }
    if (value >= native_extent) { return logical_extent; }
    return (int)((int64_t)value * logical_extent / native_extent);
}

/** @brief Replace the XImage after the window manager changes its size. */
static int
resize_image(tiku_host_t *host, int width, int height)
{
    XImage *image;
    char *pixels;
    int screen;

    if (host == NULL || width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / 4u / (size_t)height) {
        return -1;
    }
    pixels = malloc((size_t)width * (size_t)height * 4u);
    if (pixels == NULL) { return -1; }
    screen = DefaultScreen(host->display);
    image = XCreateImage(host->display, DefaultVisual(host->display, screen),
        (unsigned)DefaultDepth(host->display, screen), ZPixmap, 0, pixels,
        (unsigned)width, (unsigned)height, 32, 0);
    if (image == NULL) {
        free(pixels);
        return -1;
    }
    Pixmap backing = XCreatePixmap(host->display, host->window,
        (unsigned)width, (unsigned)height,
        (unsigned)DefaultDepth(host->display, screen));
    if (backing == None) { XDestroyImage(image); return -1; }
    /* The old background remains server-owned until the replacement is
     * fully painted and installed by present(), even across a resize. */
    if (host->backing != None) XFreePixmap(host->display, host->backing);
    host->backing = backing;
    if (host->image != NULL) { XDestroyImage(host->image); }
    host->image = image;
    host->force_present = 1;
    host->native_width = width;
    host->native_height = height;
    host->logical_width = (width + host->scale - 1) / host->scale;
    host->logical_height = (height + host->scale - 1) / host->scale;
    return 0;
}

/**
 * @brief Which modifier bits a keymap puts the Command key on.
 *
 * Not a constant anywhere: X lets the keymap say what each of its eight
 * modifiers carries, so the Command bit is wherever Meta and Super sit,
 * which differs between servers.  The four the desktop already spends
 * are removed at the end, since Meta and Alt share a key on most
 * keymaps and Mod1 is Option here: keeping it would make Option-click
 * copy a file and act on it at once.
 *
 * @param map   The server's modifier map.
 * @param syms  Its keycode-to-keysym table.
 * @param first The keycode @p syms starts at, @p count its length in
 *              keycodes and @p per its stride.
 * @return the mask, or 0 when this keymap has no key free to mean it.
 */
static unsigned
command_mask_of(const XModifierKeymap *map, const KeySym *syms,
                int first, int count, int per)
{
    unsigned mask = 0u;
    int index, slot, which;

    if (map == NULL || syms == NULL || per <= 0 || count <= 0) {
        return 0u;
    }
    for (index = 0; index < 8; index++) {
        for (slot = 0; slot < map->max_keypermod; slot++) {
            KeyCode code = map->modifiermap[index * map->max_keypermod +
                                            slot];

            /* The map and the table are two round trips, and a keymap
             * can change between them: a code outside the range that
             * was fetched names no row of it. */
            if (code == 0 || (int)code < first || (int)code >= first + count) {
                continue;
            }
            for (which = 0; which < per; which++) {
                KeySym sym = syms[(code - first) * per + which];

                if (sym == XK_Meta_L || sym == XK_Meta_R ||
                    sym == XK_Super_L || sym == XK_Super_R) {
                    mask |= 1u << index;
                }
            }
        }
    }
    return mask & ~(unsigned)(ShiftMask | LockMask | ControlMask | Mod1Mask);
}

/**
 * @brief The modifiers that mean "this is an instruction, not a letter".
 *
 * Control always, because every shortcut this desktop has is a Control
 * chord and a machine that had them yesterday must have them today.  On
 * a machine whose command key is Command, that key as well -- added,
 * not swapped, so nothing a person already knows stops working and the
 * chord their hand reaches for starts.
 */
static unsigned
command_modifiers(Display *display)
{
    XModifierKeymap *map;
    KeySym *syms;
    int first = 0, count = 0, per = 0;
    unsigned mask = ControlMask;

    if (!tiku_x11_command_is_meta()) {
        return mask;
    }
    XDisplayKeycodes(display, &first, &count);
    count = count - first + 1;
    if (count <= 0) {
        return mask;
    }
    syms = XGetKeyboardMapping(display, (KeyCode)first, count, &per);
    map = XGetModifierMapping(display);
    mask |= command_mask_of(map, syms, first, count, per);
    if (map != NULL) { XFreeModifiermap(map); }
    if (syms != NULL) { XFree(syms); }
    return mask;
}

/** @brief Translate native modifiers once, at the host boundary. */
static unsigned
event_modifiers(const tiku_host_t *host, unsigned state)
{
    unsigned modifiers = 0u;

    if ((state & ShiftMask) != 0u) { modifiers |= TIKU_MOD_SHIFT; }
    if ((state & host->command_mask) != 0u) { modifiers |= TIKU_MOD_CMD; }
    if ((state & Mod1Mask) != 0u) { modifiers |= TIKU_MOD_OPTION; }
    if ((state & LockMask) != 0u) { modifiers |= TIKU_MOD_CAPS; }
    return modifiers;
}

/** @brief Translate navigation keys; printable values remain characters. */
static unsigned
event_key(KeySym keysym)
{
    switch (keysym) {
    case XK_Up:        return TIKU_KEY_UP;
    case XK_Down:      return TIKU_KEY_DOWN;
    case XK_Left:      return TIKU_KEY_LEFT;
    case XK_Right:     return TIKU_KEY_RIGHT;
    case XK_Home:      return TIKU_KEY_HOME;
    case XK_End:       return TIKU_KEY_END;
    case XK_Prior:     return TIKU_KEY_PAGE_UP;
    case XK_Next:      return TIKU_KEY_PAGE_DOWN;
    case XK_BackSpace: return TIKU_KEY_BACKSPACE;
    case XK_Delete:    return TIKU_KEY_DELETE;
    case XK_Escape:    return TIKU_KEY_ESCAPE;
    case XK_Return:
    case XK_KP_Enter:  return TIKU_KEY_RETURN;
    case XK_Tab:       return TIKU_KEY_TAB;
    case XK_Menu:      return TIKU_KEY_MENU;
    case XK_F2:        return TIKU_KEY_F2;
    case XK_F1:        return TIKU_KEY_F1;
    case XK_Shift_L:
    case XK_Shift_R:   return TIKU_KEY_SHIFT;
    case XK_Control_L:
    case XK_Control_R: return TIKU_KEY_CMD;
    case XK_Alt_L:
    case XK_Alt_R:
    case XK_Meta_L:
    case XK_Meta_R:    return TIKU_KEY_OPTION;
    default:
        if (keysym >= XK_A && keysym <= XK_Z) {
            return (unsigned)(keysym - XK_A + 'a');
        }
        return (keysym >= 0x20 && keysym < 0x7fu)
                   ? (unsigned)keysym : 0u;
    }
}

tiku_host_t *
tiku_host_open(int width, int height, const char *title)
{
    tiku_host_t *host;
    int screen;

    if (width <= 0 || height <= 0) {
        return NULL;
    }
    /* Only character classification changes; numeric VFS formatting stays C. */
    (void)setlocale(LC_CTYPE, "");
    host = calloc(1u, sizeof *host);
    if (host == NULL) {
        return NULL;
    }
    host->display = XOpenDisplay(NULL);
    if (host->display == NULL) {
        free(host);
        return NULL;
    }
    screen = DefaultScreen(host->display);
    host->scale = display_scale(host->display);
    host->command_mask = command_modifiers(host->display);
    if (width > INT_MAX / host->scale ||
        height > INT_MAX / host->scale) {
        tiku_host_close(host);
        return NULL;
    }
    host->window = XCreateSimpleWindow(host->display,
        RootWindow(host->display, screen), 0, 0,
        (unsigned)(width * host->scale),
        (unsigned)(height * host->scale), 0,
        BlackPixel(host->display, screen),
        BlackPixel(host->display, screen));
    XSelectInput(host->display, host->window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask | FocusChangeMask | PropertyChangeMask);
    tiku_x11_clipboard_init(&host->clipboard, host->display, host->window);
    host->clipboard_ready = 1;
    input_open(host);
    host->delete_window = XInternAtom(host->display, "WM_DELETE_WINDOW",
                                      False);
    XSetWMProtocols(host->display, host->window, &host->delete_window, 1);
    XStoreName(host->display, host->window,
               (title != NULL) ? title : "TikuOS");
    host->gc = XCreateGC(host->display, host->window, 0, NULL);
    XSetGraphicsExposures(host->display, host->gc, False);
    if (resize_image(host, width * host->scale,
                     height * host->scale) != 0) {
        tiku_host_close(host);
        return NULL;
    }
    host->resize_cursor = XCreateFontCursor(host->display,
                                            XC_sb_h_double_arrow);
    return host;
}

void
tiku_host_close(tiku_host_t *host)
{
    if (host == NULL) {
        return;
    }
    if (host->image != NULL) {
        XDestroyImage(host->image);
    }
    if (host->input_context) XDestroyIC(host->input_context);
    host->input_context = NULL;
    if (host->input_method) XCloseIM(host->input_method);
    host->input_method = NULL;
    if (host->display != NULL && host->resize_cursor != None) {
        XFreeCursor(host->display, host->resize_cursor);
    }
    if (host->display != NULL) {
        int k;

        for (k = 0; k < 6; k++) {
            if (host->cursors[k] != None) {
                XFreeCursor(host->display, host->cursors[k]);
            }
        }
    }
    if (host->display != NULL && host->gc != NULL) {
        XFreeGC(host->display, host->gc);
    }
    if (host->display != NULL && host->backing != None) {
        XFreePixmap(host->display, host->backing);
    }
    if (host->display != NULL && host->window != None) {
        if (host->clipboard_ready) tiku_x11_clipboard_close(&host->clipboard);
        XDestroyWindow(host->display, host->window);
    }
    if (host->display != NULL) {
        XCloseDisplay(host->display);
    }
    free(host);
}

int
tiku_host_present(tiku_host_t *host,
                       const tiku_surface_t *surface)
{
    if (host == NULL || surface == NULL || host->image == NULL ||
        surface->w != host->logical_width ||
        surface->h != host->logical_height) {
        return -1;
    }
    tiku_rect_t rects[TIKU_PRESENT_RECTS];
    int n = tiku_present_update((tiku_rgb_t *)host->image->data,
        host->native_width, host->native_height,
        host->image->bytes_per_line / (int)sizeof(tiku_rgb_t),
        surface, host->force_present, rects, TIKU_PRESENT_RECTS);
    if (n < 0) return -1;
    host->force_present = 0;
    host->presented_frames++;
    for (int i = 0; i < n; i++) {
        tiku_rect_t r = rects[i];
        XPutImage(host->display, host->backing, host->gc, host->image,
            r.x, r.y, r.x, r.y, (unsigned)r.w, (unsigned)r.h);
        host->presented_bytes += (uint64_t)r.w*r.h*sizeof(tiku_rgb_t);
        host->presented_rects++;
    }
    if (n) {
        /* Expose handling may wait until boot finishes. Let the server
         * restore real pixels, never a white clear behind sparse uploads. */
        XSetWindowBackgroundPixmap(host->display, host->window, host->backing);
        if (!host->mapped) {
            XMapWindow(host->display, host->window);
            host->mapped = 1;
        } else {
            for (int i = 0; i < n; i++) {
                tiku_rect_t r = rects[i];
                XCopyArea(host->display, host->backing, host->window, host->gc,
                    r.x, r.y, (unsigned)r.w, (unsigned)r.h, r.x, r.y);
            }
        }
        XFlush(host->display);
    }
    return 0;
}

void tiku_host_present_stats(tiku_host_t *host, uint64_t *frames,
                             uint64_t *bytes, uint64_t *rectangles)
{
    if (frames) *frames = host ? host->presented_frames : 0;
    if (bytes) *bytes = host ? host->presented_bytes : 0;
    if (rectangles) *rectangles = host ? host->presented_rects : 0;
}

void tiku_host_wait(tiku_host_t *host, int64_t timeout_us)
{
    if (timeout_us <= 0) return;
    if (timeout_us > 60000000) timeout_us = 60000000;
    struct pollfd fd = {.fd = -1, .events = POLLIN};
    if (host) {
        if (host->commit.offset < host->commit.length ||
            host->clipboard.result ||
            (host->has_deferred && !host->clipboard.requestor)) return;
        /* Queued user input behind a clipboard import cannot run yet.
         * Wait for protocol traffic instead of spinning on that queue. */
        if (!(host->has_deferred && host->clipboard.requestor) &&
            XPending(host->display)) return;
        fd.fd = ConnectionNumber(host->display);
        XFlush(host->display);
    }
    (void)poll(&fd, 1, (int)((timeout_us + 999) / 1000));
}

int
tiku_host_scale(const tiku_host_t *host)
{
    return (host != NULL && host->scale > 1) ? host->scale : 1;
}

void
tiku_host_set_fullscreen(tiku_host_t *host, int on)
{
    Atom state, full;
    int screen;

    if (host == NULL || host->display == NULL) {
        return;
    }
    host->fullscreen = (on != 0);
    state = XInternAtom(host->display, "_NET_WM_STATE", False);
    full = XInternAtom(host->display, "_NET_WM_STATE_FULLSCREEN", False);
    if (!host->mapped) {
        /* Before the map: a manager reads the state off the window as it
         * takes it, so the first frame is already the whole screen. */
        if (on) {
            XChangeProperty(host->display, host->window, state, XA_ATOM, 32,
                            PropModeReplace, (unsigned char *)&full, 1);
        } else {
            XDeleteProperty(host->display, host->window, state);
        }
    } else {
        /* After it: the manager owns the window's state, and is asked
         * through the root, as the standard says (1 adds, 0 removes). */
        XEvent e;

        memset(&e, 0, sizeof e);
        e.xclient.type = ClientMessage;
        e.xclient.window = host->window;
        e.xclient.message_type = state;
        e.xclient.format = 32;
        e.xclient.data.l[0] = on ? 1 : 0;
        e.xclient.data.l[1] = (long)full;
        e.xclient.data.l[2] = 0;
        e.xclient.data.l[3] = 1;
        XSendEvent(host->display, DefaultRootWindow(host->display), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &e);
    }
    /* And the window itself, for a server with no manager to answer --
     * a test server, a bare Xvfb.  Under a manager this is overruled by
     * the manager's own placement, which is the answer wanted anyway. */
    if (on) {
        screen = DefaultScreen(host->display);
        XMoveResizeWindow(host->display, host->window, 0, 0,
                          (unsigned)DisplayWidth(host->display, screen),
                          (unsigned)DisplayHeight(host->display, screen));
    }
    XFlush(host->display);
}

void
tiku_host_set_title(tiku_host_t *host, const char *title)
{
    if (host != NULL && title != NULL) {
        XStoreName(host->display, host->window, title);
    }
}

/** @brief Clipboard deadlines use elapsed time, not X server timestamps. */
static int64_t monotonic_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

/** @brief Publish an imported clipboard before releasing its deferred input. */
static int clipboard_result(tiku_host_t *host, tiku_event_t *event)
{
    if (!host->clipboard.result) return 0;
    memset(event, 0, sizeof *event);
    event->type = TIKU_EVENT_CLIPBOARD;
    event->key = (unsigned)host->clipboard.result;
    host->clipboard.result = 0;
    return 1;
}

int
tiku_host_poll(tiku_host_t *host, tiku_event_t *event)
{
    XEvent native;

    if (host == NULL || event == NULL) {
        return 0;
    }
    int64_t now = monotonic_us();
    if (tiku_text_input_next(&host->commit, event)) return 1;
    tiku_x11_clipboard_tick(&host->clipboard, now);
    /* Do not consume later clicks/keys while importing for an earlier event.
     * Only protocol events bypass the input barrier. Each poll stays bounded. */
    for (int i = 0; i < 32 && XCheckIfEvent(host->display, &native,
         tiku_x11_clipboard_match, (XPointer)&host->clipboard); i++)
        (void)tiku_x11_clipboard_event(&host->clipboard, &native, now);
    if (clipboard_result(host, event)) return 1;
    /* Input waits only for an import someone is waiting on.  A refresh
     * armed by an ordinary click must never hold the desk still while a
     * stopped application sits on the selection. */
    if (host->clipboard.requestor && host->has_deferred) return 0;
    if (host->has_deferred) {
        *event = host->deferred;
        host->has_deferred = 0;
        return 1;
    }
    if (!XPending(host->display)) return 0;
    XNextEvent(host->display, &native);
    memset(event, 0, sizeof *event);
    if (XFilterEvent(&native, None)) return 1;
    switch (native.type) {
    case Expose:
        host->force_present = 1;
        event->type = TIKU_EVENT_EXPOSE;
        break;
    case KeyPress:
    case KeyRelease: {
        KeySym keysym = NoSymbol;
        char text[TIKU_TEXT_INPUT_MAX] = {0};
        Status status = XLookupNone;
        int n;
        if (native.type == KeyPress && host->input_context) {
            n = Xutf8LookupString(host->input_context, &native.xkey, text,
                                  sizeof text - 1, &keysym, &status);
            if (status == XBufferOverflow) {
                tiku_host_cancel_composition(host);
                event->type = TIKU_EVENT_TEXT_INPUT;
                event->button = TIKU_TEXT_REJECT;
                return 1;
            }
            if (status != XLookupChars && status != XLookupBoth) n = 0;
        } else {
            n = XLookupString(&native.xkey, text, sizeof text - 1, &keysym, NULL);
            if (keysym >= 0x01000000 && keysym <= 0x0110ffff)
                n = input_utf8((unsigned)(keysym & 0xffffff), text);
            else if (keysym >= 0xa0 && keysym <= 0xff)
                n = input_utf8((unsigned)keysym, text);
        }

        event->type = (native.type == KeyPress)
                          ? TIKU_EVENT_KEY_DOWN
                          : TIKU_EVENT_KEY_UP;
        event->modifiers = event_modifiers(host, native.xkey.state);
        event->key = event_key(keysym);
        event->time_us = (int64_t)native.xkey.time * 1000;
        if (n < 0 || n >= (int)sizeof text) n = 0;
        text[n] = 0;
        if (native.type == KeyPress && n &&
            !(event->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION)) &&
            (n > 1 || (unsigned char)text[0] >= 128 || status == XLookupChars)) {
            if (tiku_text_input_set(&host->commit, text, (size_t)n)) {
                (void)tiku_text_input_next(&host->commit, event);
            } else { memset(event, 0, sizeof *event);
                event->type = TIKU_EVENT_TEXT_INPUT; event->button = TIKU_TEXT_REJECT; }
        } else if (native.type == KeyPress && n < (int)sizeof event->text)
            memcpy(event->text, text, (size_t)n + 1);
        if (native.type == KeyPress &&
            (event->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION)))
            tiku_host_cancel_composition(host);
        break;
    }
    case ButtonPress:
    case ButtonRelease:
        event->type = (native.type == ButtonPress)
                          ? TIKU_EVENT_POINTER_DOWN
                          : TIKU_EVENT_POINTER_UP;
        event->x = logical_coord(native.xbutton.x, host->native_width,
                                 host->logical_width);
        event->y = logical_coord(native.xbutton.y, host->native_height,
                                 host->logical_height);
        event->button = native.xbutton.button;
        event->modifiers = event_modifiers(host, native.xbutton.state);
        event->time_us = (int64_t)native.xbutton.time * 1000;
        break;
    case MotionNotify:
        for (int moves = 0; moves < 64 && XPending(host->display); moves++) {
            XEvent next;
            XPeekEvent(host->display, &next);
            if (next.type != MotionNotify ||
                next.xmotion.window != native.xmotion.window ||
                next.xmotion.state != native.xmotion.state) break;
            /* Collapse only consecutive moves: never cross a release. */
            XNextEvent(host->display, &native);
        }
        event->type = TIKU_EVENT_POINTER_MOVE;
        event->x = logical_coord(native.xmotion.x, host->native_width,
                                 host->logical_width);
        event->y = logical_coord(native.xmotion.y, host->native_height,
                                 host->logical_height);
        event->modifiers = event_modifiers(host, native.xmotion.state);
        event->time_us = (int64_t)native.xmotion.time * 1000;
        break;
    case FocusIn:
        input_open(host);
        if (host->input_context) XSetICFocus(host->input_context);
        event->type = TIKU_EVENT_ACTIVATED;
        break;
    case FocusOut:
        tiku_host_cancel_composition(host);
        if (host->input_context) XUnsetICFocus(host->input_context);
        event->type = TIKU_EVENT_DEACTIVATED;
        break;
    case ConfigureNotify:
        event->type = TIKU_EVENT_RESIZE;
        if (native.xconfigure.width != host->native_width ||
            native.xconfigure.height != host->native_height) {
            (void)resize_image(host, native.xconfigure.width,
                               native.xconfigure.height);
        }
        event->width = host->logical_width;
        event->height = host->logical_height;
        break;
    case ClientMessage:
        if ((Atom)native.xclient.data.l[0] == host->delete_window) {
            event->type = TIKU_EVENT_CLOSE;
        }
        break;
    default:
        event->type = TIKU_EVENT_NONE;
        break;
    }
    int paste = event->type == TIKU_EVENT_KEY_DOWN && event->key == 'v' &&
                (event->modifiers & TIKU_MOD_CMD);
    if (event->type == TIKU_EVENT_POINTER_DOWN && event->button == 1)
        tiku_host_cancel_composition(host);
    if (event->type == TIKU_EVENT_ACTIVATED || paste ||
        (event->type == TIKU_EVENT_POINTER_DOWN && event->button == 1)) {
        int forced = paste || event->type == TIKU_EVENT_ACTIVATED;
        int waiting = tiku_x11_clipboard_request(&host->clipboard, now,
                                                 forced);
        if ((waiting && forced) || host->clipboard.result) {
            host->deferred = *event;
            host->has_deferred = 1;
            return clipboard_result(host, event);
        }
    }
    return 1;
}

void
tiku_host_set_cursor(tiku_host_t *host, int kind)
{
    static const unsigned shapes[6] = {
        XC_left_ptr, XC_xterm, XC_sb_h_double_arrow, XC_sb_v_double_arrow,
        XC_bottom_right_corner, XC_hand2
    };

    if (host == NULL || kind < 0 || kind >= 6 || kind == host->cursor_kind) {
        return;
    }
    host->cursor_kind = kind;
    if (kind == TIKU_CURSOR_ARROW) {
        XUndefineCursor(host->display, host->window);
        XFlush(host->display);
        return;
    }
    if (host->cursors[kind] == None) {
        host->cursors[kind] = XCreateFontCursor(host->display, shapes[kind]);
    }
    XDefineCursor(host->display, host->window, host->cursors[kind]);
    XFlush(host->display);
}

void
tiku_host_set_resize_cursor(tiku_host_t *host, int enabled)
{
    tiku_host_set_cursor(host, enabled ? TIKU_CURSOR_RESIZE_H
                                       : TIKU_CURSOR_ARROW);
}

int
tiku_host_pointer(tiku_host_t *host, int *x, int *y,
                       unsigned *modifiers)
{
    Window root, child;
    int root_x, root_y, local_x, local_y;
    unsigned state;

    if (host == NULL || !XQueryPointer(host->display, host->window,
                                       &root, &child, &root_x, &root_y,
                                       &local_x, &local_y, &state)) {
        return 0;
    }
    if (x != NULL) {
        *x = logical_coord(local_x, host->native_width,
                           host->logical_width);
    }
    if (y != NULL) {
        *y = logical_coord(local_y, host->native_height,
                           host->logical_height);
    }
    if (modifiers != NULL) { *modifiers = event_modifiers(host, state); }
    return 1;
}

int
tiku_host_show(const tiku_surface_t *s, const char *title)
{
    Display *d;
    Window w;
    XImage *img;
    GC gc;
    Atom del;
    char *pixels;
    int screen, running = 1;

    d = XOpenDisplay(NULL);
    if (d == NULL) {
        return -1;
    }
    screen = DefaultScreen(d);
    w = XCreateSimpleWindow(d, RootWindow(d, screen), 0, 0,
                            (unsigned)s->w, (unsigned)s->h, 0,
                            BlackPixel(d, screen), WhitePixel(d, screen));
    XStoreName(d, w, title);
    XSelectInput(d, w, ExposureMask | KeyPressMask | ButtonPressMask |
                       StructureNotifyMask);
    del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, w, &del, 1);
    XMapWindow(d, w);
    gc = XCreateGC(d, w, 0, NULL);

    /* XImage frees this on XDestroyImage, so it must be its own copy. */
    pixels = malloc((size_t)s->w * (size_t)s->h * 4u);
    if (pixels == NULL) {
        XCloseDisplay(d);
        return -1;
    }
    memcpy(pixels, s->px, (size_t)s->w * (size_t)s->h * 4u);
    img = XCreateImage(d, DefaultVisual(d, screen),
                       (unsigned)DefaultDepth(d, screen), ZPixmap, 0, pixels,
                       (unsigned)s->w, (unsigned)s->h, 32, 0);
    if (img == NULL) {
        free(pixels);
        XCloseDisplay(d);
        return -1;
    }
    while (running) {
        XEvent e;
        XNextEvent(d, &e);
        switch (e.type) {
        case Expose:
            XPutImage(d, w, gc, img, 0, 0, 0, 0, (unsigned)s->w,
                      (unsigned)s->h);
            break;
        case KeyPress:
        case ButtonPress:
            running = 0;
            break;
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == del) {
                running = 0;
            }
            break;
        default:
            break;
        }
    }
    XDestroyImage(img);
    XFreeGC(d, gc);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return 0;
}
