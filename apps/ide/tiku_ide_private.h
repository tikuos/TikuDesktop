/* tiku_ide_private.h -- shared controller state and internal IDE interfaces.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_IDE_PRIVATE_H_
#define TIKU_IDE_PRIVATE_H_

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tiku_alert.h"
#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_list.h"
#include "tiku_list_paint.h"
#include "tiku_iconpaint.h"
#include "tiku_image.h"
#include "tiku_logo.h"
#include "tiku_splash.h"
#include "tiku_syntax.h"
#include "tiku_clipboard.h"
#include "tiku_field.h"
#include "tiku_textview.h"
#include "tiku_text_input.h"
#include "tiku_board_basic.h"
#include "tiku_ui.h"
#include "tiku_ide_project.h"

/* Which program this build of the source IS. */
#ifdef TIKU_IDE_C
#define APP_NAME      "TikuC"
#define APP_ABOUT     "About TikuC"
#define APP_ID        "org.tikuos.tikuc"
#define APP_NEW_FILE  "untitled.c"
#define APP_MAIN_FILE "main.c"
#else
#define APP_NAME      "TikuBasic"
#define APP_ABOUT     "About TikuBasic"
#define APP_ID        "org.tikuos.tikubasic"
#define APP_NEW_FILE  "untitled.bas"
#define APP_MAIN_FILE "main.bas"
#endif

#define IDE_W      440
#define IDE_H      300
/* An editor is where the WORK happens, so it opens at half the screen
 * rather than at a peephole somebody enlarges every single time. */
#define EDIT_W     620
#define EDIT_H     420
#define STRIP_H    22
#define TOOLBAR_H  30
#define PAGE_SB    12   /* the page's own scrollbar gutter, at the right */
#define FIND_H     26   /* the find bar, dropped under the toolbar */
#define TARGET_H   24
#define SEARCH_TOP (TOOLBAR_H + TARGET_H)
#define ASSIST_H 126
#define DEBUG_H 134
#define MARGIN     6

/* The editor toolbar's two tools, in the window's own coordinates. */
#define TB_ICON  24
#define TB_SAVE  (tiku_rect_t){ 8, 3, TB_ICON, TB_ICON }
#define TB_RUN   (tiku_rect_t){ 44, 3, TB_ICON, TB_ICON }
#define TB_STOP  (tiku_rect_t){ 72, 3, TB_ICON, TB_ICON }
#define TB_UNDO  (tiku_rect_t){ 108, 3, TB_ICON, TB_ICON }
#define TB_REDO  (tiku_rect_t){ 136, 3, TB_ICON, TB_ICON }
#define TB_FIND  (tiku_rect_t){ 172, 3, TB_ICON, TB_ICON }
#define TB_RENUM (tiku_rect_t){ 208, 3, TB_ICON, TB_ICON }

#define MSG_W      420
#define MSG_H      220
#define CON_W      560          /* the console: a program's own screen */
#define CON_H      340
#define CON_LINES  400
#define CON_COLS   120
#define CON_INK    0xd8d8d8u    /* what the program printed */
#define CON_DIM    0x8a8a8au    /* the console's own remarks */
#define CON_BACK   0x000000u
/* Wide, the way the banners those splash screens wore are wide: the
 * band takes a picture cut close to three-to-one edge to edge. */
#define SPLASH_W   560
#define SPLASH_H   300
#define SPLASH_ART 148          /* the art field's width, on the left */
/* How long the splash stands before leaving on its own. */
#define SPLASH_US  2500000

/* A session may hold four windows over the wire, and the project and
 * the messages are two of them. */
#define IDE_EDITS  2
#define IDE_MSGS   200
/* How long a run may take before it is stopped rather than trusted. */
#define IDE_RUN_LIMIT_US 30000000
/* How long a board is given to answer a Stop before the run is let go
 * on this side.  Long enough for a busy console, short enough that a
 * board pulled from its cable does not hold the page. */
#define IDE_BOARD_STOP_US 5000000

#define CMD_OPEN     1
#define CMD_QUIT     2
#define CMD_SAVE     3
#define CMD_SHUT     4
#define CMD_RUN      5
#define CMD_FOLLOW   6
#define CMD_BUILD    7
#define CMD_ABOUT    11
#define CMD_OPENFILE 12
#define CMD_NEWPROJ  8
#define CMD_NEWFILE  9
#define CMD_ADDFILE  10

/* Which question the shell's panel is answering, when it answers. */
enum {
    ASK_OPEN = 0,               /* Open Project…: the path IS a project */
    ASK_NEWPROJ,                /* New Project…: make it, then open it  */
    ASK_FILE,                   /* New/Add File…: put it in the project */
    ASK_EDIT                    /* Open File…: just an editor on it     */
};
#define CMD_CUT      13
#define CMD_COPY     14
#define CMD_PASTE    15
#define CMD_SELALL   16
#define CMD_UNDO     17
#define CMD_REDO     18
#define CMD_RENUM    19
#define CMD_STOP     20
#define CMD_SAVEBOARD 23
#define CMD_FIND     21
#define CMD_REMOVEFILE 22
#define CMD_REPLACE   24
#define CMD_RECOVER   25
#define CMD_PROJECT_FIND 26
#define CMD_COMPLETE 27
#define CMD_HELPWORD 28
#define CMD_DEBUG 29
#define CMD_BREAKPOINT 30
#define CMD_DEBUG_STEP 31
#define CMD_DEBUG_CONT 32
#define CMD_DEBUG_PANEL 33
#define CMD_CLEARBREAKS 34
#define CMD_WINDOW   64         /* +i: the i'th editor of the Window menu */

/** @brief One editor window: its file, its text, and its window. */
typedef struct {
    uint32_t             id;
    tiku_surface_t *surface;    /* NULL when the seat is free */
    int                  w, h;       /* the window's CURRENT content */
    tiku_textview_t tv;
    tiku_text_input_t commit;
    tiku_syntax_lang_t   lang;
    char                 path[512];
    char                 name[64];
    int                  drag_select;   /* a press began on the page */
    int                  finding;        /* the find bar is open */
    tiku_field_t         find;           /* what is being searched for */
    tiku_field_t         replacement;
    int                  replacing, find_focus;
    char                 feedback[128];
    char                 recovery[640];
    int                  recovery_fd;
    int                  load_failed;
    struct stat          disk;           /* the file as last read or saved */
    int                  disk_known;     /* disk holds it */
    int                  disk_changed;   /* the last save found it changed */
    uint64_t             recovery_stamp;
    int                  recovery_current;
    int                  recovery_blocked;
    int                  assist_open, assist_at, assist_n;
    int                  assist_match[192];
    char                 assist_prefix[32];
    int                  assist_y, assist_x0, assist_x1;
    int                  debug_show, watch_focus;
    uint16_t             breaks[16];
    tiku_field_t         watches;
    char                 watch_value[8][112];
    int                  scroll_x;       /* the page slid left, for long lines */
    int                  tip;            /* the toolbar tool under the pointer, -1 none */
} edit_t;

/**
 * @brief One thing a run said.
 *
 * The whole of what the IDE knows about a backend: words, and -- when
 * the words were about somewhere -- the file and the line they were
 * about.  Whatever runs a program later says the same three things or
 * it does not get a window.
 */
typedef struct {
    char text[160];
    char file[512];             /* empty when it names nowhere */
    int  line;                  /* the line, or 0 for none */
    /* WHAT the line counts.  A compiler counts the lines of the file; a
     * BASIC program's lines wear their own numbers and are not in file
     * order at all.  Getting this wrong lands the caret somewhere
     * plausible and wrong, which is worse than not moving. */
    int  numbered;              /* nonzero: the line is a BASIC number */
} msg_t;

typedef struct {
    const tiku_app_services_t *services;
    uint32_t                        proj_id;
    tiku_surface_t            *proj_surface;
    int                             proj_w, proj_h; /* IDE_W x IDE_H at least */
    tiku_list_t                tv_list;
    project_t                       proj;
    edit_t                          edit[IDE_EDITS];
    /* The question closing asks while there is work nothing has saved.
     * ONE question for the whole project, naming what is at stake: a
     * question per window is a person pressing the same button three
     * times without reading it. */
    tiku_alert_t               ask;
    tiku_rect_t                ask_frame;
    char                            note[128];
    /* The project FILE itself, because adding to a project means
     * appending to the file that describes it -- appending, so the
     * lines this build does not know survive being edited by it. */
    char                            proj_file[512];
    int                             asked;      /* which question, ASK_* */
    /* The click before this one, because opening by mouse is two of
     * them: the same row, close together.  The host synthesises no
     * double-click event, and the row's own clock is the event's. */
    int                             click_row;
    int64_t                         click_at;
    /* The splash: the application's name, shown while it arrives and
     * gone before it matters.  A key or a click sends it away early,
     * because a person who is already working is not an audience. */
    uint32_t                        splash_id;
    tiku_surface_t            *splash_surface;
    int64_t                         splash_from;
    int                             splash_stage;
    /* The person's own banner, when they have put one where this
     * program looks; NULL wears the built-in sweep. */
    tiku_surface_t            *splash_art;
    /* The About box: the same announcement, standing until somebody
     * has read it.  One of it, raised when asked for again. */
    uint32_t                        about_id;
    tiku_surface_t            *about_surface;
    /* What the last run said, and the window it says it in.  The
     * window arrives when there is something to put in it: a message
     * window standing empty is furniture. */
    uint32_t                        msg_id;
        tiku_surface_t            *msg_surface;
    int                             msg_w, msg_h;   /* MSG_W x MSG_H at least */
    /* The console: opened for a board run, black, the mono face; every
     * line the program printed in order, the run's states as the
     * console's own dim remarks, and the INPUT line at the bottom. */
    uint32_t                        con_id;
    tiku_surface_t            *con_surface;
    int                             con_w, con_h;   /* CON_W x CON_H at least */
    char                            con[CON_LINES][CON_COLS];
    unsigned char                   con_dim[CON_LINES];
    int                             ncon;
    int                             con_top;    /* first line shown, or -1 for the tail */
    char                            con_title[96];
    tiku_list_t                msg_list;
    msg_t                           msg[IDE_MSGS];
    int                             nmsg;
    /* The run itself: a child on the far end of a pipe, pumped from
     * tick, which is called wherever this application is hosted. */
    int                             running;
    int                             child_fd;
    /* The board a run was sent to, empty when it ran here.  A run on a
     * board has no child and no pipe: the shell owns it and says what it
     * printed. */
    char                            board[64];
    /* A program on the board at an INPUT: what it asked, and the line
     * being typed for it, in the message window's strip. */
    int                             asking;
    char                            ask_prompt[160];
    tiku_field_t                    answer;
    tiku_text_input_t               answer_commit;
    int                             saving;     /* a save, not a run  */
    pid_t                           child;
    char                            child_file[512];
    char                            partial[160];
    int                             child_numbered; /* a BASIC run */
    int64_t                         run_from;   /* when it started */
    int64_t                         stop_asked; /* a board stop, unanswered */
    int64_t                         recovery_at;
    char                            target_status[160];
    char                            upload_board[64], upload_name[8];
    char                            upload_program[8192];
    uint32_t                        closing_editor;
    uint32_t                        overwrite_editor; /* the save-over question's page */
    int                             debugging, debug_paused, debug_pc;
    int                             debug_values, debug_socket, debug_eof;
    tiku_board_basic_t              *local_debug;
} ide_t;


extern const tiku_app_descriptor_t tiku_ide_app;


#pragma GCC visibility push(hidden)

/* Editor view. */
tiku_syntax_lang_t tiku_ide_lang_of(const char *name);
int tiku_ide_tb_tool_at(const edit_t *e, int x, int y);
int tiku_ide_edit_page_top(const edit_t *e);
int tiku_ide_edit_page_bottom(const edit_t *e);
void tiku_ide_edit_paint(ide_t *st, edit_t *e);
int tiku_ide_edit_rows(const edit_t *e);
void tiku_ide_edit_place_from_xy(edit_t *e, int x, int y);

/* Editor. */
edit_t * tiku_ide_edit_of_path(ide_t *st, const char *path);
edit_t * tiku_ide_edit_of_id(ide_t *st, uint32_t id);
int tiku_ide_edit_clip(ide_t *st, edit_t *e, int command);
void tiku_ide_mrow(tiku_menu_list_t *m, const char *label, int command, char sc, unsigned mods, int enabled);
void tiku_ide_window_menu(ide_t *st, tiku_menu_list_t *m);
void tiku_ide_edit_publish(ide_t *st, edit_t *e);
void tiku_ide_edit_load(edit_t *e);
int tiku_ide_edit_save(edit_t *e);
int tiku_ide_edit_save_or_ask(ide_t *st, edit_t *e);
void tiku_ide_type_number(tiku_textview_t *tv, int n);
int tiku_ide_next_number(const tiku_textview_t *tv, int at);
void tiku_ide_edit_open(ide_t *st, const prow_t *row);
void tiku_ide_edit_drop(ide_t *st, edit_t *e);
int tiku_ide_document_fits(const char *text);

/* Search. */
void tiku_ide_edit_select_span(edit_t *e, int y, int x0, int x1);
void tiku_ide_search_project(ide_t *st, edit_t *e);
void tiku_ide_search_open(edit_t *e, int replacing);
tiku_rect_t tiku_ide_search_button(int i);
void tiku_ide_search_paint(edit_t *e);
void tiku_ide_search_replace(edit_t *e, int all);
int tiku_ide_search_pointer(edit_t *e, int x, int y);
void tiku_ide_search_key(ide_t *st, edit_t *e, const tiku_event_t *ev);

/* Language. */
int tiku_ide_norm_is_word(int c);
void tiku_ide_edit_normalize_word(edit_t *e);
void tiku_ide_renumber_basic(edit_t *e);

/* Project window. */
tiku_rect_t tiku_ide_project_body(const ide_t *st);
tiku_rect_t tiku_ide_project_list_area(const ide_t *st);
void tiku_ide_window_sizeable(ide_t *st, uint32_t id);
void tiku_ide_project_sizeable(ide_t *st);
void tiku_ide_project_paint(ide_t *st);
void tiku_ide_project_publish(ide_t *st);
void tiku_ide_project_take(ide_t *st, const char *path);
void tiku_ide_project_add_file(ide_t *st, const char *path);
void tiku_ide_project_remove_file(ide_t *st);
void tiku_ide_project_create(ide_t *st, const char *path);
void tiku_ide_open_chosen(ide_t *st);

/* Device. */
int tiku_ide_dest_board(ide_t *st, char *out, size_t max);
void tiku_ide_run_file_where(ide_t *st, const char *path);
void tiku_ide_run_file_on_board(ide_t *st, const char *path, const char *board);
void tiku_ide_save_file_on_board(ide_t *st, const char *path);
void tiku_ide_upload_confirm(ide_t *st);
void tiku_ide_board_run_released(ide_t *st);
void tiku_ide_board_said(void *state, int what, const char *text);
int tiku_ide_board_connected(ide_t *st, const char *board);
void tiku_ide_target_text(ide_t *st, char *out, size_t max);
void tiku_ide_target_paint(ide_t *st, edit_t *e);
int tiku_ide_upload_read(ide_t *st, const char *path, char *out, size_t max);

/* Console. */
void tiku_ide_messages_paint(ide_t *st);
void tiku_ide_messages_publish(ide_t *st);
void tiku_ide_messages_open(ide_t *st);
void tiku_ide_message_take(ide_t *st, const char *line);
int tiku_ide_line_of_number(const tiku_textview_t *tv, int want);
void tiku_ide_console_take(ide_t *st, const char *line, int dim);
void tiku_ide_console_paint(ide_t *st);
void tiku_ide_console_publish(ide_t *st);
void tiku_ide_console_open(ide_t *st, const char *file, const char *board);
void tiku_ide_message_follow(ide_t *st);
void tiku_ide_messages_show(ide_t *st);

/* Run. */
void tiku_ide_run_stop(ide_t *st, const char *why);
void tiku_ide_run_file(ide_t *st, const char *path);
void tiku_ide_build_project(ide_t *st);

/* Recovery. */
void tiku_ide_recovery_scan(ide_t *st);
int tiku_ide_recovery_write(edit_t *e);
void tiku_ide_recovery_restore(edit_t *e);
void tiku_ide_recovery_open(edit_t *e);
void tiku_ide_recovery_release(edit_t *e, int discard);

/* App. */
void tiku_ide_tick(void *state, int64_t now_us);
int tiku_ide_start(void **state, const tiku_app_services_t *services);
void tiku_ide_stop(void *state);

/* Events. */
int tiku_ide_unsaved(const ide_t *st, char *out, size_t max);
int tiku_ide_event(void *state, uint32_t window, const tiku_event_t *event);

/* Commands. */
int tiku_ide_pick(void *state, uint32_t window, int command);
void tiku_ide_picked(void *state, uint32_t window, const char *path);

/* Splash. */
tiku_surface_t * tiku_ide_splash_art_load(void);
void tiku_ide_announce_license(tiku_surface_t *sf);
void tiku_ide_announce_paint_art(tiku_surface_t *sf, const char *status, const tiku_surface_t *art);
void tiku_ide_about_open(ide_t *st);
void tiku_ide_about_drop(ide_t *st);
void tiku_ide_splash_drop(ide_t *st);

/* Assist. */
void tiku_ide_assist_open(edit_t *e, int complete);
void tiku_ide_assist_paint(edit_t *e);
void tiku_ide_assist_accept(edit_t *e);
int tiku_ide_assist_event(edit_t *e, const tiku_event_t *event);

/* Debug. */
int tiku_ide_debug_locked(ide_t *st, edit_t *e);
void tiku_ide_debug_show(edit_t *e);
void tiku_ide_debug_break(edit_t *e);
void tiku_ide_debug_start(ide_t *st, edit_t *e);
void tiku_ide_debug_command(ide_t *st, const char *command);
int tiku_ide_debug_line(ide_t *st, const char *line);
void tiku_ide_debug_cleanup(ide_t *st);
void tiku_ide_debug_tick(ide_t *st);
void tiku_ide_debug_paint(ide_t *st, edit_t *e);
int tiku_ide_debug_event(ide_t *st, edit_t *e, const tiku_event_t *event);

#pragma GCC visibility pop

#endif /* TIKU_IDE_PRIVATE_H_ */
