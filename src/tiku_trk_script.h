/*
 * The new Tracker for TikuOS.
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

#include <X11/Xlib.h>

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
    KeySym      ks;         /* KEY: the keysym, already resolved        */
    unsigned    state;      /* X modifier mask                          */
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
    const char *find_text;  /* what the Find panel is asking for         */
    const char *find_type;  /* the type it is narrowed to                */
    /* Bit 0: a row whose type is NOT the one the panel asked for.  Bit 1:
     * at least one row was looked at.  Together they say whether a search
     * really was narrowed. */
    int         kinds;
} tiku_trk_script_state_t;

typedef struct tiku_trk_script tiku_trk_script_t;

/**
 * @brief Read a script.  @return NULL if it cannot be read.
 *
 * Grammar, one instruction per line, '#' to end of line is a comment:
 *
 *     key <keysym> [ctrl] [shift] [alt]   -- e.g. `key n ctrl`
 *     click <x> <y> [button]              -- press and release together
 *     press <x> <y> [button]
 *     release <x> <y> [button]
 *     wait <turns>                        -- let the loop run
 *     shot <path>
 *     expect-status <substring>
 *     expect-file <path>
 *     expect-no-file <path>
 *     expect-rows-at-least <n>
 *     remove <path>                       -- change the world behind it
 *     expect-perm <path> <rwx>            -- the owner bits, e.g. "rw-"
 *     expect-windows <n>
 *     expect-selected <n>
 *     expect-selected-name <name>
 *     expect-find-text <text>             -- the Find panel's own field
 *     expect-find-type <label>
 *     expect-type-narrowed                -- every row is of the asked type
 *     expect-title <substring>
 *     note <text>                         -- what the next check is for
 *     quit
 */
tiku_trk_script_t *tiku_trk_script_open(const char *path);

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
