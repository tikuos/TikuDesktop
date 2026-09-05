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

#define TIKU_DRAW_LIST_MAX 1536     /**< one window's list, in bytes */
#define TIKU_DRAW_WINDOWS  2
#define TIKU_DRAW_QUEUE    8        /**< events waiting for the owner */

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
    TIKU_DRAW_CLOSED = 100,     /**< the person closed the window */
    TIKU_DRAW_SESSION = 101     /**< a desktop arrived: the window was
                                     announced again; draw it afresh */
} tiku_draw_kind_t;

typedef struct {
    tiku_draw_kind_t kind;
    unsigned         key;       /**< a key's code, else 0 */
    unsigned         modifiers;
    unsigned         button;
    int              x, y;      /**< within the window's content */
    char             text[8];   /**< the key's characters */
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
    unsigned char        list[TIKU_DRAW_LIST_MAX];
    size_t               len;       /**< bytes of list written */
    int                  open;
} tiku_draw_window_t;

/**
 * @brief Say hello to the desktop as @p name.  Called once; sent again
 *        by itself when the desktop answers a later open with silence.
 */
void tiku_draw_hello(const char *name);

/** @brief Open @p win as a window of @p w by @p h, owned by @p owner. */
int tiku_draw_open(tiku_draw_window_t *win, const char *title, int w,
                   int h, struct tiku_process *owner);

/** @brief Close it on the desktop. */
void tiku_draw_close(tiku_draw_window_t *win);

/** @brief Start a fresh list for @p win: the window is drawn from nothing. */
void tiku_draw_begin(tiku_draw_window_t *win);

/* The commands.  Each returns 1 when written, 0 when the list is full. */
int tiku_draw_fill(tiku_draw_window_t *win, int x, int y, int w, int h,
                   uint32_t colour);
int tiku_draw_frame(tiku_draw_window_t *win, int x, int y, int w, int h,
                    uint32_t colour);
int tiku_draw_hline(tiku_draw_window_t *win, int x, int y, int w,
                    uint32_t colour);
int tiku_draw_vline(tiku_draw_window_t *win, int x, int y, int h,
                    uint32_t colour);
int tiku_draw_text(tiku_draw_window_t *win, tiku_draw_face_t face, int x,
                   int y, const char *text, uint32_t colour);
int tiku_draw_text_centered(tiku_draw_window_t *win, tiku_draw_face_t face,
                            int x, int y, int w, int h, const char *text,
                            uint32_t colour);
int tiku_draw_panel(tiku_draw_window_t *win, int x, int y, int w, int h);
int tiku_draw_raised(tiku_draw_window_t *win, int x, int y, int w, int h);
int tiku_draw_sunken(tiku_draw_window_t *win, int x, int y, int w, int h,
                     uint32_t face);
int tiku_draw_button(tiku_draw_window_t *win, int x, int y, int w, int h,
                     const char *label, unsigned state);
int tiku_draw_checkbox(tiku_draw_window_t *win, int x, int y, int w, int h,
                       const char *label, unsigned state);
int tiku_draw_radio(tiku_draw_window_t *win, int x, int y, int w, int h,
                    const char *label, unsigned state);
int tiku_draw_gauge(tiku_draw_window_t *win, int x, int y, int w, int h,
                    int per_mille);
int tiku_draw_textfield(tiku_draw_window_t *win, int x, int y, int w, int h,
                        const char *text, unsigned state);
int tiku_draw_slider(tiku_draw_window_t *win, int x, int y, int w, int h,
                     int lo, int hi, int value);
int tiku_draw_menufield(tiku_draw_window_t *win, int x, int y, int w, int h,
                        const char *label, unsigned state);
int tiku_draw_list_row(tiku_draw_window_t *win, int x, int y, int w, int h,
                       const char *text, int selected);

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
 * @brief The shell's read loop hands every console byte here first.
 * @return 1 when the byte belonged to a frame, 0 when it is the shell's.
 */
int tiku_draw_frame_byte(int ch);

#endif /* TIKU_DRAW_H_ */
