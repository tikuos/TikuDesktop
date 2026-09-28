/* Private X11 clipboard transport. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_X11_CLIPBOARD_H_
#define TIKU_X11_CLIPBOARD_H_
#include <X11/Xlib.h>
#include <stdint.h>
#include "tiku_clipboard.h"

/* One peer taking our text a piece at a time (ICCCM 2.7.2, INCR). */
typedef struct {
    Window requestor;           /* None when no transfer is running */
    Atom property, type;        /* where it reads each piece; as what */
    char *data;                 /* our own copy of what it takes */
    size_t len, sent;           /* its bytes; sent so far */
    int64_t deadline;           /* a peer silent past this is let go */
} tiku_x11_send_t;

typedef struct {
    Display *display;
    Window window, requestor, source, cached_owner;
    Atom clipboard, utf8, targets, text, timestamp, incr, property, clock;
    Atom requested, received;
    Time acquired;
    unsigned stamp;
    int publishing, incremental, result;
    int asked;                  /* the import someone is waiting on */
    int64_t deadline, refreshed;
    char *incoming;             /* text being taken in, held at its size */
    size_t used, room;          /* its bytes; what is allocated */
    char *outgoing;             /* the text published; NULL when empty */
    size_t outlen;              /* its bytes */
    tiku_x11_send_t send;       /* the one transfer out in pieces */
} tiku_x11_clipboard_t;

void tiku_x11_clipboard_init(tiku_x11_clipboard_t *c, Display *d, Window w);
void tiku_x11_clipboard_close(tiku_x11_clipboard_t *c);
/** @brief Export local changes and enforce the external-transfer deadlines. */
void tiku_x11_clipboard_tick(tiku_x11_clipboard_t *c, int64_t now);
/** @brief Refresh external text, using a short cache unless force is set.
 * Returns nonzero while a transfer is pending; never waits for its owner.
 */
int tiku_x11_clipboard_request(tiku_x11_clipboard_t *c, int64_t now, int force);
/** @brief Consume selection/property events, leaving ordinary input alone. */
int tiku_x11_clipboard_event(tiku_x11_clipboard_t *c, XEvent *e, int64_t now);
/** @brief Predicate for protocol-only polling while normal input is deferred. */
Bool tiku_x11_clipboard_match(Display *d, XEvent *e, XPointer ctx);
#endif
