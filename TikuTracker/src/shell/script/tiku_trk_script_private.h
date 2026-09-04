/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script_private.h - what the tiku_trk_script files share: the
 * includes, the types, the small accessors that read them, and the functions
 * one file defines for another.  Nothing outside this directory includes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SCRIPT_PRIVATE_H_
#define TIKU_TRK_SCRIPT_PRIVATE_H_

/* split: public TikuTracker/src/shell/tiku_trk_script.h */

/* split: tag sc */

#include "tiku_trk_script.h"

#include "tiku_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <sys/stat.h>

#define SC_MAX_STEPS 512
#define SC_TEXT_MAX  256

/** @brief What one line was asking for. */
typedef enum {
    OP_KEY = 0, OP_PRESS, OP_MOVE, OP_RELEASE, OP_CLICK, OP_CLICK_ROW, OP_CLICK_DESK, OP_PRESS_DESK, OP_MOVE_DESK, OP_RELEASE_DESK, OP_DRAG_WINDOW,
    OP_PRESS_ROW, OP_MOVE_ROW, OP_RELEASE_ROW, OP_CLICK_WINDOW, OP_CLICK_CELL,
    OP_WAIT, OP_SHOT, OP_EXPECT_LOCATION, OP_EXPECT_NAV_BACK,
    OP_EXPECT_NAV_FORWARD, OP_EXPECT_NAV_UP, OP_EXPECT_NAV_VISIBLE,
    OP_EXPECT_VIEWPORT_ROWS,
    OP_EXPECT_STATUS, OP_EXPECT_TITLE, OP_EXPECT_ALERT, OP_EXPECT_SCROLL, OP_EXPECT_ROWS, OP_REMOVE, OP_EXPECT_PERM, OP_EXPECT_WINDOWS, OP_EXPECT_SEL, OP_EXPECT_SELNAME, OP_EXPECT_RENAMING, OP_EXPECT_FINDTEXT, OP_EXPECT_FINDTYPE, OP_EXPECT_FOLDERS, OP_EXPECT_WINAT, OP_EXPECT_COLS, OP_EXPECT_BARAT, OP_EXPECT_FILE, OP_EXPECT_NO_FILE, OP_EXPECT_ICONAT, OP_EXPECT_ICONSCREEN, OP_EXPECT_PIXEL, OP_EXPECT_INK, OP_EXPECT_FONT,
    OP_EXPECT_NO_TITLE, OP_DRAW_RESET, OP_EXPECT_DRAWN, OP_EXPECT_MENU, OP_EXPECT_NO_MENU, OP_EXPECT_NARRATE, OP_EXPECT_TOLD, OP_EXPECT_MOUNTS, OP_EXPECT_CONTROL, OP_EXPECT_READ, OP_EXPECT_OPERATION, OP_EXPECT_COUNT, OP_EXPECT_LINK, OP_EXPECT_FILEHAS, OP_EXPECT_FILELACKS, OP_EXPECT_DESKSEL, OP_EXPECT_DESKICON, OP_EXPECT_NO_DESKICON, OP_EXPECT_DESKSELNAME,
    OP_NOTE, OP_MENU_PICK, OP_WRITE, OP_SAME_AS_REDRAW, OP_QUIT
} sc_op_t;

typedef struct {
    sc_op_t  op;
    unsigned key;
    unsigned modifiers;
    int      button_x, button_y;        /* a resolved tab point          */
    int      x, y;
    unsigned button;
    int      count;                     /* WAIT turns                    */
    char     text[SC_TEXT_MAX];         /* path, substring or note       */
    /* A second string, where one is needed.  Long enough for a PATH: it
     * was a word, and a needle too long to hold was silently truncated to
     * nothing -- which made every check that used it pass. */
    char     text2[512];
    int      line;
} sc_step_t;

struct tiku_trk_script {
    sc_step_t step[SC_MAX_STEPS];
    int       count;
    int       cursor;
    int       waiting;                  /* turns still owed to a WAIT    */
    int       pending_release;
    int       dragging_window;          /* a tab drag, mid-play          */
    int       drag_to_x, drag_to_y;          /* a CLICK's second half         */
    char      note[SC_TEXT_MAX];        /* what the next check is for    */
    int       passes, fails;
    int       finished;                 /* quit, or the last step, ran  */
};


/* Shared across the family: defined in one file, used in another. */

/* tiku_trk_script.c */
int sc_there(const char *path);
unsigned sc_key_of(const char *name);
unsigned sc_mods_of(char **w, int n, int from);
void sc_rest(char **w, int n, int from, char *out, size_t max);
void sc_verdict(tiku_trk_script_t *s, int ok, const char *what,
                const char *arg);

/* tiku_trk_script_expect.c */
int sc_parse_desktop(sc_step_t *st, char **w, int n);
int sc_parse_file_contents(sc_step_t *st, char **w, int n);
int sc_parse_file_state(sc_step_t *st, char **w, int n);
int sc_parse_said(sc_step_t *st, char **w, int n);
int sc_parse_window(sc_step_t *st, char **w, int n);
int sc_pump_desktop(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                    tiku_trk_script_step_t *out, sc_step_t *cur);
int sc_pump_file_contents(tiku_trk_script_t *s,
                          const tiku_trk_script_state_t *st,
                          tiku_trk_script_step_t *out, sc_step_t *cur);
int sc_pump_file_state(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                       tiku_trk_script_step_t *out, sc_step_t *cur);
int sc_pump_pixels(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                   tiku_trk_script_step_t *out, sc_step_t *cur);
int sc_pump_said(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                 tiku_trk_script_step_t *out, sc_step_t *cur);
int sc_pump_window(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                   tiku_trk_script_step_t *out, sc_step_t *cur);

/* tiku_trk_script_gesture.c */
int sc_parse_gesture(sc_step_t *st, char **w, int n, int lineno);
int sc_pump_gesture(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                    tiku_trk_script_step_t *out, sc_step_t *cur);

/* End of the shared declarations. */

#endif /* TIKU_TRK_SCRIPT_PRIVATE_H_ */
