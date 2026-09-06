/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw.h - a window of the board's own, drawn on the desktop.
 *
 * The desktop's window session, spoken from a process: a window is
 * opened by title and size, drawn as a list of the desktop's own
 * controls (a button, a gauge, a line of text) in a few hundred bytes,
 * and what is pressed or typed in it comes back as TIKU_EVENT_DRAW.  The
 * frames ride the console beside the shell, cut out of the stream at
 * both ends, so nothing new is wired.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DRAW_H_
#define TIKU_DRAW_H_

#include "tiku.h"
#include <kernel/process/tiku_process.h>

struct tiku_link;

#define TIKU_DRAW_LIST_MAX 1536     /**< a fair list buffer, in bytes */
#define TIKU_DRAW_WINDOWS  8
#define TIKU_DRAW_QUEUE    8        /**< events waiting for the owner */
/** @brief Bytes of icon art and pictures one window may carry: those
 *         cross the cable whole, and a window that costs a minute is
 *         refused rather than sent. */
#define TIKU_DRAW_ART_BUDGET 2048

/** @brief The event a window's press or key posts; data is the window. */
#define TIKU_EVENT_DRAW (TIKU_EVENT_USER + 0x41)

/** @brief What the desktop said happened, one event at a time. */
typedef enum {
    TIKU_DRAW_NONE = 0,
    TIKU_DRAW_KEY_DOWN = 4,
    TIKU_DRAW_KEY_UP = 5,
    TIKU_DRAW_POINTER_DOWN = 6,
    TIKU_DRAW_POINTER_UP = 7,
    TIKU_DRAW_POINTER_MOVE = 8,
    TIKU_DRAW_RESIZE = 3,       /**< the person resized: w and h are new */
    TIKU_DRAW_WHEEL = 9,        /**< dy tells how far */
    TIKU_DRAW_CLOSED = 100,     /**< the person closed the window */
    TIKU_DRAW_SESSION = 101,    /**< a desktop arrived: the window was
                                     announced again; draw it afresh */
    TIKU_DRAW_PICK = 102,       /**< a menu item chosen: command says which */
    TIKU_DRAW_PICKED = 103,     /**< the file panel answered: path */
    TIKU_DRAW_CLIP = 104        /**< the desk's clipboard changed: path holds
                                     the text */
} tiku_draw_kind_t;

typedef struct {
    tiku_draw_kind_t kind;
    unsigned         key;       /**< a key's code, else 0 */
    unsigned         modifiers;
    unsigned         button;
    int              x, y;      /**< within the window's content */
    int              dy;        /**< a wheel's turn */
    int              w, h;      /**< a resize's new content size */
    int              command;   /**< a pick's command */
    char             text[8];   /**< the key's characters */
    char             path[96];  /**< a picked path, or clipboard text */
} tiku_draw_event_t;

/** @brief The faces the desktop resolves by number. */
typedef enum {
    TIKU_DRAW_PLAIN = 0, TIKU_DRAW_BOLD = 1, TIKU_DRAW_MONO = 2
} tiku_draw_face_t;

/** @brief A colour as the desktop packs it: 0xRRGGBB. */
#define TIKU_DRAW_RGB(r, g, b) \
    (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define TIKU_DRAW_PANEL  TIKU_DRAW_RGB(216, 216, 216)
#define TIKU_DRAW_TEXT   TIKU_DRAW_RGB(0, 0, 0)
#define TIKU_DRAW_DOC    TIKU_DRAW_RGB(255, 255, 255)

typedef struct tiku_draw_window {
    uint32_t             id;        /**< the session's number for it */
    int                  w, h;      /**< the content's size */
    const char          *title;
    struct tiku_process *owner;     /**< receives TIKU_EVENT_DRAW */
    tiku_draw_event_t    event;     /**< the one taken, read by the owner */
    tiku_draw_event_t    queue[TIKU_DRAW_QUEUE]; /**< arrived, not yet taken */
    unsigned char        head, count;
    unsigned char       *list;      /**< the program's buffer for the list */
    size_t               cap;       /**< its size */
    size_t               len;       /**< bytes of list written */
    size_t               art;       /**< icon art and picture bytes so far */
    const char          *menus;     /**< the menu text, remembered */
    int                  sizeable;  /**< the person may resize it */
    int                  open;
} tiku_draw_window_t;

/**
 * @brief Say hello to the desktop as @p name.  Called once; sent again
 *        by itself when the desktop answers a later open with silence.
 */
void tiku_draw_hello(const char *name);

/**
 * @brief Open @p win as a window of @p w by @p h, owned by @p owner,
 *        its list written into @p list of @p cap bytes: a small window
 *        pays for a small buffer.
 */
int tiku_draw_open(tiku_draw_window_t *win, const char *title, int w,
                   int h, struct tiku_process *owner, unsigned char *list,
                   size_t cap);

/** @brief Close it on the desktop. */
void tiku_draw_close(tiku_draw_window_t *win);

/**
 * @brief Give @p win a menu bar, written as text: `menu <title>`, then
 *        `item <command> <shortcut|-> <label>` and `sep` lines.  A pick
 *        comes back as TIKU_DRAW_PICK with the command.
 */
void tiku_draw_menus(tiku_draw_window_t *win, const char *text);

/** @brief Bring it forward. */
void tiku_draw_raise(tiku_draw_window_t *win);

/** @brief Let the person resize it; a TIKU_DRAW_RESIZE follows each. */
void tiku_draw_sizeable(tiku_draw_window_t *win);

/** @brief What kind of window it is, as the desktop places kinds. */
void tiku_draw_place(tiku_draw_window_t *win, int role);

/**
 * @brief Ask the desktop for a file: @p mode 0 to open, 1 to save,
 *        listing @p start, the name @p name.  The answer arrives as
 *        TIKU_DRAW_PICKED with the path.
 */
void tiku_draw_pick(tiku_draw_window_t *win, int mode, const char *start,
                    const char *name);

/** @brief Put @p text on the desk's clipboard. */
void tiku_draw_clipboard(const char *text);

/** @brief Start a fresh list for @p win: the window is drawn from nothing. */
void tiku_draw_begin(tiku_draw_window_t *win);

/*
 * The commands, one per op of the display list, their fields in wire
 * order (a rectangle as x, y, w, h; a colour as 0xRRGGBB; a control's
 * state before its label).  Generated from kits/interface/dl_ops.txt,
 * so an op the desktop gains is here the same day.  Each returns 1
 * when written, 0 when the list is full.
 */
#define TIKU_DRAW_OPS_PROTOTYPES
#include "tiku_draw_ops.h"
#undef TIKU_DRAW_OPS_PROTOTYPES

/* The byte helpers the generated writers use; not for a program. */
unsigned char *tiku_draw_op_begin(tiku_draw_window_t *win, unsigned op,
                                  size_t len);
void tiku_draw_put16(unsigned char *p, int v);
void tiku_draw_put32(unsigned char *p, uint32_t v);

/** @brief The states a control may be drawn in. */
#define TIKU_DRAW_S_DISABLED 0x01u
#define TIKU_DRAW_S_ON       0x10u
#define TIKU_DRAW_S_PRESSED  0x20u

/**
 * @brief How wide @p text draws in @p face on the desktop, from the
 *        metrics baked in tiku_metrics.h; a glyph past ASCII counts as
 *        a space.
 */
int tiku_draw_text_width(tiku_draw_face_t face, const char *text);

/** @brief The face's line height and ascent on the desktop. */
int tiku_draw_text_height(tiku_draw_face_t face);
int tiku_draw_text_ascent(tiku_draw_face_t face);

/** @brief Send the list: the window on the desktop becomes it. */
int tiku_draw_present(tiku_draw_window_t *win);

/**
 * @brief From the owning process's thread: for TIKU_EVENT_DRAW, the
 *        window it is about with the next waiting event in its event
 *        field; NULL otherwise.  Events queue while the process is busy
 *        (a press and its release arrive together), so the process
 *        calls this until it answers NULL.
 */
tiku_draw_window_t *tiku_draw_event(tiku_event_t ev, tiku_event_data_t data);

/**
 * @brief Register the session's channel on the console: from here on a
 *        desktop's frames reach their windows.  Called once at boot, from
 *        the kernel's one hook for this library, before any process runs.
 */
void tiku_draw_init(void);

/**
 * @brief Carry the session on @p link from here on, or on the console
 *        again for NULL.
 *
 * The windows are held until the desktop on the new link greets, exactly
 * as at boot, so it sees them opened rather than half-drawn.
 */
void tiku_draw_set_link(struct tiku_link *link);

/** @brief Send one session message of @p type with @p payload.
 *  @return 0, or -1 when the link refused it (no room now, or down). */
int tiku_draw_send_msg(uint32_t type, const void *payload, uint32_t len);

/** @brief Whether the session is on the console link right now. */
uint8_t tiku_draw_link_is_console(void);

/**
 * @brief What the link carrying the session confers: a TIKU_VFS_CAP_*
 *        mask, as the medium's backend declares it.
 *
 * The cable confers everything; an address confers nothing until the
 * desk at the other end is weighed.  A process acting on what a window
 * was told holds its writes to this, so a window shown over the air
 * cannot do what one on the cable can:
 *
 * @code
 * uint8_t prev = tiku_vfs_caller_cap_set(tiku_draw_cap());
 * rc = tiku_vfs_write("/dev/led0", "1", 1u);
 * (void)tiku_vfs_caller_cap_set(prev);
 * @endcode
 */
uint8_t tiku_draw_cap(void);

#endif /* TIKU_DRAW_H_ */
