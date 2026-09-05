/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script.h - a scripted, displayless driver for the two shells.
 *
 * Both front-ends are the product, so every shell behaviour has to be proven
 * twice.  Proving it by hand twice does not scale, and the engine's own test
 * depth has already hidden one shell that queued file work and never ran it.
 * A script is a contract row expressed as keystrokes and assertions, run with
 * no X server: the window is drawn into memory exactly as it is on screen.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SCRIPT_H_
#define TIKU_TRK_SCRIPT_H_

#include <stddef.h>

#include "tiku_event.h"

/** @brief What the driver should do for one step. */
typedef enum {
    TIKU_TRK_SC_IDLE = 0,   /* nothing to inject; run one loop turn      */
    TIKU_TRK_SC_KEY,
    TIKU_TRK_SC_PRESS,
    TIKU_TRK_SC_MOVE,
    TIKU_TRK_SC_RELEASE,
    TIKU_TRK_SC_MENU_PICK,
    TIKU_TRK_SC_SHOT,
    TIKU_TRK_SC_QUIT
} tiku_trk_script_do_t;

/** @brief One instruction, filled by the pump. */
typedef struct {
    tiku_trk_script_do_t what;
    unsigned    key;        /* KEY: logical key or printable character */
    unsigned    modifiers;  /* platform-neutral modifier mask          */
    int         x, y;       /* PRESS / RELEASE                          */
    unsigned    button;
    const char *path;       /* SHOT: where to write; MENU_PICK: label   */
} tiku_trk_script_step_t;

/** @brief What the driver can tell the script about itself. */
typedef struct {
    const char *status;     /* the shell's status line, or NULL         */
    const char *title;      /* the focused window's title, or NULL      */
    int         scroll;     /* the focused view's first visible row     */
    int         rows;       /* how many rows it holds                   */
    const char *alert;      /* the open alert's text, or NULL           */
    int         windows;    /* how many windows are open                */
    int         selected;   /* how many poses the focused view has       */
    const char *sel_name;   /* the first selected pose's name, or NULL   */
    int         renaming;   /* an inline name editor is active           */
    const char *find_text;  /* what the Find panel is asking for         */
    const char *find_type;  /* the type it is narrowed to                */
    const char *location;   /* the browser navigator's current path      */
    int         nav_back;   /* whether the browser can step backward     */
    int         nav_forward;
    int         nav_up;
    int         nav_visible;
    int         viewport_rows; /* rows that fit, rather than total poses */
    int         win_x, win_y;   /* the focused window's frame            */
    int         columns;        /* how many its list shows               */
    int         bar_x, bar_y;   /* where the Deskbar has put itself      */
    /* Bit 0: a row whose type is NOT the one the panel asked for.  Bit 1:
     * at least one row was looked at.  Together they say whether a search
     * really was narrowed. */
    int         kinds;
    /* The labels of the menu that is showing, joined by '|': a menu built
     * from what is selected cannot be checked any other way. */
    const char *menu;
    /* What the count view says, which is the one line that reports the
     * selection rather than the folder. */
    const char *count;
    /* The Desktop is a surface of its own: it has no window to be focused,
     * so what it holds cannot be read through the focused-window fields. */
    int         desk_selected;
    const char *desk_sel_name;
    /* Where a named pose sits in the focused view, in view pixels.  A
     * position is the only way to check an ARRANGEMENT -- how many rows a
     * window holds says nothing about where a drop put them. */
    int       (*pose_at)(void *ctx, const char *name, int *x, int *y);
    /* The DESKTOP's poses only: what the *-desk ops aim at, so a same-named
     * row in the focused window cannot shadow the icon.  NULL falls back
     * to pose_at. */
    int       (*pose_desk_at)(void *ctx, const char *name, int *x, int *y);
    int       (*pose_screen)(void *ctx, const char *name, int *x, int *y);
    /* Where a visible list row can be clicked in shell coordinates.  The
     * shell owns chrome geometry, so scripts must not reconstruct it. */
    int       (*row_point)(void *ctx, int row, int *x, int *y);
    int       (*cell_point)(void *ctx, int row, const char *title, int *x,
                            int *y);
    int       (*rows_drawn)(void *ctx, int reset);
    /* Where the TAB of the window titled @p title is.  A test that wants a
     * window somewhere else has to grab its tab, and a tab's position is
     * chrome -- the thing a test must never encode, because it moves
     * whenever the chrome does. */
    int       (*tab_point)(void *ctx, const char *title, int *x, int *y);
    /* Any named fact the shell can say, by the same noun table the
     * conductor's QUERY answers -- one surface, principle 3: a script
     * line and a driver on a socket read the same answer.  0 for a noun
     * this shell cannot say. */
    int       (*say)(void *ctx, const char *noun, char *out, size_t max);
    void       *say_ctx;
    /* The focused window narrated from its display list, the same lines
     * the conductor's "narrate" noun answers with -- computed only when
     * an op asks, because a narration is a full content draw. */
    size_t    (*narrate)(void *ctx, char *out, size_t max);
    /* One pixel of what is actually on screen, 0xRRGGBB.  Drawing is the
     * only evidence for a rule about how something LOOKS -- a band style
     * changes no state a checker could otherwise read. */
    unsigned  (*pixel_at)(void *ctx, int x, int y);
    /* How many pixels a full redraw would put differently from what the
     * frames actually painted.  A window that paints what changed can only
     * be checked against the picture it is meant to be equal to: this
     * draws that picture and counts the difference. */
    long      (*redraw_diff)(void *ctx);
    void       *redraw_ctx;
    void       *pose_ctx;
} tiku_trk_script_state_t;

typedef struct tiku_trk_script tiku_trk_script_t;

/**
 * @brief Read a script.  @return NULL if it cannot be read.
 *
 * The instructions a script may hold are listed in doc/scripting.md,
 * beside the harness that runs them.
 */
tiku_trk_script_t *tiku_trk_script_open(const char *path);

/** @brief Release a script; NULL is fine. */
void tiku_trk_script_free(tiku_trk_script_t *s);

/**
 * @brief Advance one instruction.
 *
 * Assertions and waits are settled inside the pump and reported as IDLE, so
 * the driver only ever handles the four things it alone can do: inject an
 * event, take a picture, or stop.
 *
 * @return 0 when the script has run out, 1 otherwise.
 */
int tiku_trk_script_pump(tiku_trk_script_t *s,
                         const tiku_trk_script_state_t *st,
                         tiku_trk_script_step_t *out);

/** @brief Failed checks so far; the process exit code should follow it. */
int tiku_trk_script_failures(const tiku_trk_script_t *s);

/** @brief Print the tally.  @return the exit code the driver should use. */
int tiku_trk_script_report(const tiku_trk_script_t *s, const char *who);

#endif /* TIKU_TRK_SCRIPT_H_ */
