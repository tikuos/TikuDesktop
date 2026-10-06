/* tiku_ide_private.h -- the IDE family's shared state, sizes, commands and
 * internal functions, for the TikuBasic and TikuC builds.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_IDE_PRIVATE_H_
#define TIKU_IDE_PRIVATE_H_

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
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
#include "tiku_codeview.h"
#include "tiku_text_input.h"
#include "tiku_board_basic.h"
#include "tiku_ui.h"
#include "tiku_ide_project.h"

/* Which program this build is: TikuC when TIKU_IDE_C is defined, else
 * TikuBasic. */
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

/* Window sizes and the parts of a window, in pixels. */
#define IDE_W      440  /**< the Project window's opening and least size */
#define IDE_H      300
#define EDIT_W     620  /**< the size a page opens at */
#define EDIT_H     420
#define STRIP_H    22   /**< the status strip along a window's foot */
#define TOOLBAR_H  30   /**< a page's toolbar */
#define PAGE_SB    12   /**< the page's own scrollbar gutter, at the right */
#define FIND_H     26   /**< one of the find bar's three rows */
#define TARGET_H   24   /**< the target strip under the toolbar */
#define SEARCH_TOP (TOOLBAR_H + TARGET_H)   /**< where the find bar starts */
#define DEBUG_H 134     /**< the debug panel under the page */
#define MARGIN     6    /**< the space inside a window's edge */

/* The page toolbar's tools, in the window's own coordinates;
 * tiku_ide_tb_tool_at() repeats them. */
#define TB_ICON  24     /**< a tool's width and height */
#define TB_SAVE  (tiku_rect_t){ 8, 3, TB_ICON, TB_ICON }
#define TB_RUN   (tiku_rect_t){ 44, 3, TB_ICON, TB_ICON }
#define TB_STOP  (tiku_rect_t){ 72, 3, TB_ICON, TB_ICON }
#define TB_UNDO  (tiku_rect_t){ 108, 3, TB_ICON, TB_ICON }
#define TB_REDO  (tiku_rect_t){ 136, 3, TB_ICON, TB_ICON }
#define TB_FIND  (tiku_rect_t){ 172, 3, TB_ICON, TB_ICON }
#define TB_RENUM (tiku_rect_t){ 208, 3, TB_ICON, TB_ICON }

#define MSG_W      420          /**< Messages' opening and least size */
#define MSG_H      220
#define CON_W      560          /**< the console's opening, least size */
#define CON_H      340
#define CON_LINES  400          /**< lines the console keeps */
#define CON_COLS   120          /**< bytes kept of a console line */
#define CON_INK    0xd8d8d8u    /**< what the program printed */
#define CON_DIM    0x8a8a8au    /**< the console's own remarks */
#define CON_BACK   0x000000u    /**< the console's background */
/* The splash and the About box; the art is a band across the top half,
 * SPLASH_W by SPLASH_H / 2. */
#define SPLASH_W   560
#define SPLASH_H   300
#define SPLASH_ART 148          /**< not used */
/* How long the splash stays before it closes itself, in microseconds. */
#define SPLASH_US  2500000

/* Pages the IDE holds open at once. */
#define IDE_EDITS  2
#define IDE_MSGS   200          /**< the most messages kept */
/* How long a local run may go before it is stopped, in microseconds. */
#define IDE_RUN_LIMIT_US 30000000
/* How long a board is given to answer a Stop before the run is let go
 * on this side.  Long enough for a busy console, short enough that a
 * board pulled from its cable does not hold the page. */
#define IDE_BOARD_STOP_US 5000000

/* Menu commands: what a picked menu row asks tiku_ide_pick() for. */
#define CMD_OPEN     1          /**< Open Project… */
#define CMD_QUIT     2          /**< close a page */
#define CMD_SAVE     3
#define CMD_SHUT     4          /**< Close Project */
#define CMD_RUN      5
#define CMD_FOLLOW   6          /**< go to a message's line */
#define CMD_BUILD    7
#define CMD_ABOUT    11
#define CMD_OPENFILE 12         /**< Open File… */
#define CMD_NEWPROJ  8
#define CMD_NEWFILE  9
#define CMD_ADDFILE  10

/* Which question the shell's panel is answering, when it answers. */
enum {
    ASK_OPEN = 0,               /**< Open Project…: a project file   */
    ASK_NEWPROJ,                /**< New Project…: make it, open it  */
    ASK_FILE,                   /**< New/Add File…: into the project */
    ASK_EDIT                    /**< Open File…: open it in a page   */
};
#define CMD_CUT      13
#define CMD_COPY     14
#define CMD_PASTE    15
#define CMD_SELALL   16
#define CMD_UNDO     17
#define CMD_REDO     18
#define CMD_RENUM    19
#define CMD_STOP     20
#define CMD_SAVEBOARD 23        /**< Save on the default device */
#define CMD_FIND     21
#define CMD_REMOVEFILE 22
#define CMD_REPLACE   24
#define CMD_RECOVER   25        /**< Recover unsaved files */
#define CMD_PROJECT_FIND 26
#define CMD_COMPLETE 27         /**< Complete keyword */
#define CMD_HELPWORD 28         /**< Keyword help */
#define CMD_DEBUG 29            /**< Start debugging */
#define CMD_BREAKPOINT 30
#define CMD_DEBUG_STEP 31
#define CMD_DEBUG_CONT 32
#define CMD_DEBUG_PANEL 33      /**< Show debugger / watches */
#define CMD_CLEARBREAKS 34
#define CMD_COMMENT  35         /**< Toggle comment */
#define CMD_NEXT_PROBLEM 36
#define CMD_PREV_PROBLEM 37
#define CMD_WINDOW   64         /**< +i: page i of the Window menu */

/* The line marker a breakpoint is, and how many lines may hold one. */
#define IDE_MARK_BREAK 0
#define IDE_BREAKS     16
/* What the BASIC interpreter takes of a program line: the bytes it reads
 * of a line, and the bytes after the line's number that it keeps. */
#define IDE_BASIC_LINE 159
#define IDE_BASIC_BODY 143
/* The bytes tiku_ide_caret_text() reads of the caret's line. */
#define IDE_CARET_TEXT 512

/** @brief One page: an editor window, its file and its text. */
typedef struct {
    uint32_t             id;             /**< the page's window */
    tiku_surface_t *surface;    /**< NULL when the slot is free */
    int                  w, h;       /**< the window's content size now */
    const tiku_app_services_t *services; /**< the desktop's calls, the
                                              clipboard among them */
    tiku_editdoc_t      *doc;            /**< the page's text */
    tiku_codeview_t     *view;           /**< its view, which holds it */
    char                 newline[3];     /**< the file's commonest break */
    /** the clipboard's text made ready to paste, freed by the next paste
     *  or with the text */
    char                *paste;
    tiku_text_input_t commit;            /**< stages text input that comes
                                              in pieces */
    tiku_syntax_lang_t   lang;           /**< the language the name implies */
    char                 path[512];      /**< the file, made absolute */
    char                 name[64];       /**< the file's name, as shown */
    int                  finding;        /**< the find bar is open */
    tiku_field_t         find;           /**< what is being searched for */
    tiku_field_t         replacement;    /**< the replace field */
    /** replace is in use; which field has the keys, 1 the replace field */
    int                  replacing, find_focus;
    char                 feedback[128];  /**< the status strip's message */
    char                 recovery[640];  /**< the draft's path; empty for
                                              none */
    int                  recovery_fd;    /**< the draft's lock; -1 none */
    int                  load_failed;    /**< unreadable: no save or run */
    struct stat          disk;           /**< the file as last read or saved */
    int                  disk_known;     /**< disk holds it */
    int                  disk_changed;   /**< the last save found it changed */
    uint64_t             recovery_state; /**< the text the draft holds */
    int                  recovery_current; /**< the draft holds it now */
    int                  recovery_blocked; /**< an unrestored draft is kept;
                                                none is written */
    int                  assist_open;    /**< 1 keyword help, 2 completion,
                                              3 a call's signature tip */
    int                  assist_n;       /**< the keywords offered */
    int                  assist_match[192]; /**< their keyword indexes */
    size_t               assist_at;      /**< the one chosen */
    char                 assist_prefix[32]; /**< the word they match */
    /** the debug panel is shown; its watch field has the keys */
    int                  debug_show, watch_focus;
    tiku_field_t         watches;        /**< the watch names */
    char                 watch_value[8][112]; /**< their values, shown */
    int                  tip;            /**< tool under the pointer; -1 none */
    int                  cursor;         /**< the pointer shape last set */
    /** the scrollbar's thumb is held, and how far into it the press was */
    int                  sb_held, sb_grab;
} edit_t;

/** @brief One message: a line a run, a build or a search printed, and the
 *         place it names, if any. */
typedef struct {
    char text[160];             /**< the line, as shown */
    char file[512];             /**< empty when it names nowhere */
    int  line;                  /**< the line, or 0 for none */
    int  col;                   /**< the byte column, or 0 for none */
    /* A compiler's line counts the file's lines; a BASIC line is a line
     * number, and BASIC lines need not be in file order. */
    int  numbered;              /**< nonzero: the line is a BASIC number */
} msg_t;

/** @brief The IDE's state: the Project window, the pages, the Messages
 *         window, the console, and the run. */
typedef struct {
    const tiku_app_services_t *services;        /**< the desktop's calls */
    uint32_t                        proj_id;    /**< the Project window */
    tiku_surface_t            *proj_surface;    /**< NULL once it closed */
    /** the window's size, IDE_W x IDE_H at least */
    int                             proj_w, proj_h;
    tiku_list_t                tv_list;         /**< the project's rows */
    project_t                       proj;       /**< the description */
    edit_t                          edit[IDE_EDITS];  /**< the pages */
    /* The question open on the Project window, tagged by the command it
     * answers: Close Project (one question naming every unsaved page),
     * closing a page, saving over a file changed on disk, or saving on
     * the device. */
    tiku_alert_t               ask;
    tiku_rect_t                ask_frame;       /**< where it is drawn */
    char                            note[128];  /**< the status message */
    /** the description's path; empty with no project open */
    char                            proj_file[512];
    int                             asked;      /**< which question, ASK_* */
    /* The last click on the project list: the desktop sends no
     * double-click event, so a second click on the same row within
     * 400 ms, timed by the events' clock, opens it. */
    int                             click_row;  /**< -1 for none */
    int64_t                         click_at;   /**< its time */
    /* The splash: the name, shown at start for SPLASH_US; a key or a
     * click closes it early. */
    uint32_t                        splash_id;  /**< its window */
    tiku_surface_t            *splash_surface;  /**< NULL once closed */
    int64_t                         splash_from; /**< its first tick */
    int                             splash_stage; /**< the label shown */
    /** the person's banner art; NULL for the built-in picture */
    tiku_surface_t            *splash_art;
    /* The About box: the splash's picture, open until it is closed; asked
     * for again, it is raised. */
    uint32_t                        about_id;   /**< its window */
    tiku_surface_t            *about_surface;   /**< NULL while closed */
    /* The Messages window, opened once there are messages. */
    uint32_t                        msg_id;     /**< its window */
        tiku_surface_t            *msg_surface; /**< NULL while closed */
    /** the window's size, MSG_W x MSG_H at least */
    int                             msg_w, msg_h;
    /* The console, opened for a board run or a debug run: the lines the
     * program printed, the run's states as dim remarks, and the INPUT
     * strip at its foot. */
    uint32_t                        con_id;     /**< its window */
    tiku_surface_t            *con_surface;     /**< NULL while closed */
    /** the window's size, CON_W x CON_H at least */
    int                             con_w, con_h;
    char                            con[CON_LINES][CON_COLS]; /**< lines */
    unsigned char                   con_dim[CON_LINES]; /**< remarks */
    int                             ncon;       /**< lines held */
    int                             con_top;    /**< first shown; -1 tail */
    char                            con_title[96]; /**< "file on board" */
    tiku_list_t                msg_list;        /**< the Messages list */
    msg_t                           msg[IDE_MSGS]; /**< the messages */
    int                             nmsg;       /**< how many */
    /* The run: a local one is a child on the far end of a pipe, read by
     * tiku_ide_tick(). */
    int                             running;    /**< a run, save or debug run */
    int                             child_fd;   /**< its pipe; -1 none */
    /* The board a run was sent to, empty when it ran here.  A run on a
     * board has no child and no pipe: the shell owns it and says what it
     * printed. */
    char                            board[64];
    /* A program waiting at INPUT: its prompt, and the answer being typed
     * in the console's or the Messages window's strip. */
    int                             asking;     /**< it is waiting */
    char                            ask_prompt[160]; /**< its prompt */
    tiku_field_t                    answer;     /**< the answer typed */
    tiku_text_input_t               answer_commit; /**< stages its text */
    int                             saving;     /**< a save, not a run */
    pid_t                           child;      /**< a local run's process */
    char                            child_file[512]; /**< the file run */
    char                            partial[160]; /**< an unended line */
    int                             child_numbered; /**< a BASIC run */
    int                             msg_problems; /**< the messages are a
                                                       run's or build's,
                                                       not search hits */
    int64_t                         run_from;   /**< when it started */
    /** when a board run's Stop was sent: -1 until the next tick, 0 none */
    int64_t                         stop_asked;
    int64_t                         recovery_at; /**< last draft write */
    char                            target_status[160]; /**< strip text */
    /** the device and name the save-on-device question is about */
    char                            upload_board[64], upload_name[8];
    char                            upload_program[8192]; /**< its text */
    /** the page the close question is about */
    uint32_t                        closing_editor;
    /** the page the save-over question is about */
    uint32_t                        overwrite_editor;
    /** a debug run goes; it is paused; the BASIC line it paused before */
    int                             debugging, debug_paused, debug_pc;
    /** watch values since the pause; the local interpreter's socket, and
     *  whether it has closed */
    int                             debug_values, debug_socket, debug_eof;
    uint64_t                        debug_state;    /**< the source text the
                                                         debug run was given */
    /** the interpreter of a local debug run; NULL otherwise */
    tiku_board_basic_t              *local_debug;
} ide_t;


/** @brief The IDE's descriptor, built as TikuBasic or TikuC. */
extern const tiku_app_descriptor_t tiku_ide_app;


#pragma GCC visibility push(hidden)

/* tiku_ide_text.c */
int tiku_ide_text_new(edit_t *e);
void tiku_ide_text_free(edit_t *e);
tiku_edit_status_t tiku_ide_text_load(edit_t *e, const char *text, size_t n);
char *tiku_ide_line_dup(const edit_t *e, int64_t line, size_t *length);
long tiku_ide_line_number(const edit_t *e, int64_t line, int blanks);
int tiku_ide_caret_text(const edit_t *e, char *text,
                        tiku_edit_pos_t *start, size_t *got);
tiku_edit_pos_t tiku_ide_caret(const edit_t *e, int64_t *line,
                               int64_t *column);
void tiku_ide_place(edit_t *e, int64_t line, int64_t column);
tiku_edit_status_t tiku_ide_replace(edit_t *e, tiku_edit_pos_t start,
                                    tiku_edit_pos_t end, const char *text,
                                    size_t n, int typing);

/* tiku_ide_editor_view.c */
tiku_syntax_lang_t tiku_ide_lang_of(const char *name);
int tiku_ide_tb_tool_at(const edit_t *e, int x, int y);
int tiku_ide_edit_page_top(const edit_t *e);
int tiku_ide_edit_page_bottom(const edit_t *e);
int tiku_ide_edit_scrollbar(edit_t *e, tiku_rect_t *bar, tiku_rect_t *thumb);
void tiku_ide_edit_bounds(edit_t *e);
void tiku_ide_edit_reveal(edit_t *e);
void tiku_ide_edit_lock(ide_t *st, edit_t *e);
void tiku_ide_edit_paint(ide_t *st, edit_t *e);

/* tiku_ide_editor.c */
edit_t * tiku_ide_edit_of_path(ide_t *st, const char *path);
edit_t * tiku_ide_edit_of_id(ide_t *st, uint32_t id);
int tiku_ide_edit_clip(ide_t *st, edit_t *e, int command);
void tiku_ide_mrow(tiku_menu_list_t *m, const char *label, int command, char sc, unsigned mods, int enabled);
void tiku_ide_window_menu(ide_t *st, tiku_menu_list_t *m);
void tiku_ide_edit_publish(ide_t *st, edit_t *e);
void tiku_ide_edit_load(edit_t *e);
int tiku_ide_edit_save(edit_t *e);
int tiku_ide_edit_save_or_ask(ide_t *st, edit_t *e);
int tiku_ide_next_number(const edit_t *e, int64_t line);
void tiku_ide_edit_open(ide_t *st, const prow_t *row);
void tiku_ide_edit_drop(ide_t *st, edit_t *e);

/* tiku_ide_search.c */
void tiku_ide_search_project(ide_t *st, edit_t *e);
void tiku_ide_search_open(edit_t *e, int replacing);
tiku_rect_t tiku_ide_search_button(int i);
void tiku_ide_search_paint(edit_t *e);
void tiku_ide_search_replace(edit_t *e, int all);
int tiku_ide_search_pointer(edit_t *e, int x, int y);
void tiku_ide_search_key(ide_t *st, edit_t *e, const tiku_event_t *ev);
void tiku_ide_edit_select(edit_t *e, tiku_edit_pos_t anchor,
                          tiku_edit_pos_t caret);

/* tiku_ide_language.c */
int tiku_ide_norm_is_word(int c);
void tiku_ide_edit_normalize_word(edit_t *e);
void tiku_ide_renumber_basic(edit_t *e);

/* tiku_ide_project_window.c */
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

/* tiku_ide_device.c */
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
int tiku_ide_basic_fits(ide_t *st, const char *text, size_t n);
int tiku_ide_basic_file_fits(ide_t *st, const char *path);

/* tiku_ide_console.c */
void tiku_ide_messages_paint(ide_t *st);
void tiku_ide_messages_publish(ide_t *st);
void tiku_ide_messages_open(ide_t *st);
void tiku_ide_message_take(ide_t *st, const char *line);
int64_t tiku_ide_line_of_number(const edit_t *e, long want);
void tiku_ide_console_take(ide_t *st, const char *line, int dim);
void tiku_ide_console_paint(ide_t *st);
void tiku_ide_console_publish(ide_t *st);
void tiku_ide_console_open(ide_t *st, const char *file, const char *board);
void tiku_ide_message_follow(ide_t *st);
void tiku_ide_messages_show(ide_t *st);

/* tiku_ide_problems.c */
void tiku_ide_problems_style(edit_t *e);
void tiku_ide_problems_page(ide_t *st, edit_t *e);
void tiku_ide_problems_show(ide_t *st);
int tiku_ide_problems_any(const edit_t *e);
int tiku_ide_problems_go(ide_t *st, edit_t *e, int backward);

/* tiku_ide_run.c */
void tiku_ide_run_stop(ide_t *st, const char *why);
void tiku_ide_run_file(ide_t *st, const char *path);
void tiku_ide_build_project(ide_t *st);

/* tiku_ide_recovery.c */
void tiku_ide_recovery_scan(ide_t *st);
int tiku_ide_recovery_write(edit_t *e);
void tiku_ide_recovery_restore(edit_t *e);
void tiku_ide_recovery_open(edit_t *e);
void tiku_ide_recovery_release(edit_t *e, int discard);

/* tiku_ide_app.c */
void tiku_ide_tick(void *state, int64_t now_us);
int tiku_ide_start(void **state, const tiku_app_services_t *services);
void tiku_ide_stop(void *state);

/* tiku_ide_events.c */
int tiku_ide_unsaved(const ide_t *st, char *out, size_t max);
int tiku_ide_event(void *state, uint32_t window, const tiku_event_t *event);

/* tiku_ide_commands.c */
int tiku_ide_pick(void *state, uint32_t window, int command);
void tiku_ide_picked(void *state, uint32_t window, const char *path);

/* tiku_ide_splash.c */
tiku_surface_t * tiku_ide_splash_art_load(void);
void tiku_ide_announce_paint_art(tiku_surface_t *sf, const char *status, const tiku_surface_t *art);
void tiku_ide_about_open(ide_t *st);
void tiku_ide_about_drop(ide_t *st);
void tiku_ide_splash_drop(ide_t *st);

/* tiku_ide_assist.c */
void tiku_ide_assist_open(edit_t *e, int complete);
void tiku_ide_assist_close(edit_t *e);
void tiku_ide_assist_sync(edit_t *e);
void tiku_ide_assist_accept(edit_t *e);
int tiku_ide_assist_event(edit_t *e, const tiku_event_t *event);

/* tiku_ide_debug.c */
int tiku_ide_debug_locked(ide_t *st, edit_t *e);
void tiku_ide_debug_show(edit_t *e);
void tiku_ide_debug_break(edit_t *e);
void tiku_ide_debug_clear(edit_t *e);
void tiku_ide_debug_start(ide_t *st, edit_t *e);
void tiku_ide_debug_command(ide_t *st, const char *command);
int tiku_ide_debug_line(ide_t *st, const char *line);
void tiku_ide_debug_cleanup(ide_t *st);
void tiku_ide_debug_tick(ide_t *st);
void tiku_ide_debug_paint(ide_t *st, edit_t *e);
int tiku_ide_debug_event(ide_t *st, edit_t *e, const tiku_event_t *event);

#pragma GCC visibility pop

#endif /* TIKU_IDE_PRIVATE_H_ */
