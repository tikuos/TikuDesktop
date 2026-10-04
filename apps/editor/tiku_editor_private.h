/* tiku_editor_private.h -- Editor controller state and internal interfaces.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_EDITOR_PRIVATE_H_
#define TIKU_EDITOR_PRIVATE_H_

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_app.h"
#include "tiku_codeview.h"
#include "tiku_text_input.h"
#include "tiku_alert.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_dl.h"
#include "tiku_gfx.h"
#include "tiku_syntax.h"
#include "tiku_ui.h"
#include "tiku_field_event.h"
#include "tiku_scroll.h"
#include "tiku_clipboard.h"
#include "tiku_iconpaint.h"
#include "tiku_icons_tiku.h"
#include "tiku_menu.h"

#include <limits.h>

#define EDIT_W      560
#define EDIT_H      400
#define STRIP_H     20
#define MARGIN      6

#define CMD_SAVE   1
#define CMD_REVERT 2
#define CMD_QUIT   3
#define CMD_SYNTAX 4
#define CMD_OPEN   5
#define CMD_SAVEAS 6
#define CMD_RUN    7
enum { CMD_NEW = 8, CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE,
       CMD_ALL, CMD_FIND, CMD_NEXT, CMD_PREV, CMD_REPLACE, CMD_GOTO,
       CMD_NUMBERS, CMD_REPLACE_ALL, CMD_INVISIBLES, CMD_INDENT,
       CMD_UNINDENT, CMD_COMMENT, CMD_DUPLICATE, CMD_LINES_UP,
       CMD_LINES_DOWN, CMD_DELETE_LINES, CMD_BRACKET, CMD_SELECT_BRACKET,
       CMD_WORD_BACK, CMD_WORD_ON, CMD_WRAP, CMD_SPLIT, CMD_FOLD,
       CMD_UNFOLD, CMD_FOLD_ALL, CMD_UNFOLD_ALL, CMD_LINE_ENDS,
       CMD_CARET_LINE, CMD_BLOCK_CARET, CMD_INDENT_GUIDES, CMD_RULER,
       CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ZOOM_RESET,
       CMD_PREFS, CMD_LANG_AUTO, CMD_LANG_TEXT, CMD_LANG_BASIC, CMD_LANG_C,
       CMD_LANG_JSON,
       CMD_TRIM, CMD_SPACES, CMD_TABS, CMD_UPPER, CMD_LOWER, CMD_SORT,
       CMD_SELECT_MATCHES };

/* The Language menu's choices, in the menu's order: Automatic takes the
 * language from the file's name, the others name it. */
enum { EDIT_LANG_AUTO, EDIT_LANG_TEXT, EDIT_LANG_BASIC, EDIT_LANG_C,
       EDIT_LANG_JSON };

/* The defaults Preferences saves: the settings each pane takes, then
 * the View switches a window starts with, each 0 or 1. */
typedef struct {
    int tab;            /* columns between tab stops, 1 to 32            */
    int indent;         /* spaces an indent inserts; 0 inserts a tab     */
    int column;         /* the column ruler's column, 1 to 240           */
    int zoom;           /* the panes' zoom, -2 to 4                      */
    int colors;         /* code inks: 0 the theme's, 1 Muted, 2 Strong   */
    int numbers, wrap, guides, ruler;       /* View switches             */
    int invisibles, caret_line, block_caret;
} editor_preferences_t;
#define EDIT_SEARCH_MAX 1000    /* matches live search marks at most */
#define EDIT_SEARCH_KIND 7      /* the indicator kind that marks them */
#define EDIT_ZOOM_MIN (-2)      /* the least zoom the code view takes */
#define EDIT_ZOOM_MAX 4         /* and the most                       */
#define EDIT_PREF_W 560
#define EDIT_PREF_H 438

typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    /*
     * What the painting below wrote down as it went.  The editor does not
     * paint twice for it: the surface records while it is drawn into, so
     * there is one painting path and a stream cannot come to disagree
     * with the picture.  NULL when the services this is running against
     * have no use for one -- linked into a desktop, they do not.
     */
    tiku_dl_t                      *dl;
    uint32_t                        id;
    /* The text, and the view that lays it out, paints it and takes the
     * caret's keys and the pointer in the page.  The chrome around it,
     * its scrollbars included, stays the Editor's. */
    tiku_editdoc_t                 *doc;
    tiku_codeview_t                *view;
    /* The page's panes: the upper one and, while it is split, the lower,
     * each a view of doc with its own caret and scroll; view is the one
     * that has the keys, panes[pane]. */
    tiku_codeview_t                *panes[2];
    int                             pane;
    int                             split_at;   /* the upper pane's height */
    int                             split_grab; /* where the divider is
                                                   held from; -1 not held */
    /* The line break Return types: the one the file uses most, "\n" for
     * a new document. */
    char                            newline[3];
    /* The clipboard's text as the view is handed it, line breaks turned
     * into the document's; freed by the next such call or with the
     * document. */
    char                           *paste;
    tiku_text_input_t               commit;
    /* The question the close gesture asks while there are unsaved
     * changes.  The kit's own alert, held over this window's content:
     * an application no longer needs a shell to be able to ask. */
    tiku_alert_t                    ask;
    tiku_rect_t                     ask_frame;
    int                             saved_shown;
    char                            path[512];
    char                            note[128];
    /*
     * Which language the document is written in, or none.  Taken from
     * the file's name when it is opened and offered on the menu after
     * that, because the name is the only thing that can honestly say --
     * and because an editor started with nothing (which is how the
     * desktop starts this one) has no name to go on and the person does.
     */
    tiku_syntax_lang_t              lang;
    /* Which question the shell's panel is answering, when it answers:
     * the ask is not remembered by the panel and the answer arrives
     * later, so what to DO with a path is remembered here. */
    int                             pick_save;
    /*
     * What the last run said, and the run itself while it lives.  The
     * program runs in the interpreter a BOARD runs -- tiku-basic, the
     * kernel's own engine compiled for the host -- as a child on the
     * far end of a pipe, the way the terminal next door holds its
     * shell.  The pane exists while there are lines to show.
     */
    char                            out[48][96];
    int                             out_count;
    int                             running;
    int                             child_fd;
    pid_t                           child;
    char                            out_partial[96];
    int w, h, finding, find_focus, pending;
    int bom, load_failed, disk_known, numbers, allow_open;
    struct stat disk;
    tiku_field_t field[3];
    tiku_field_input_t field_input[3];
    tiku_scroll_t vertical, horizontal;
    tiku_scroll_t lower;            /* the lower pane's vertical bar   */
    tiku_menuset_t last_menus;
    char scratch[64];
    uint32_t search_id;
    tiku_surface_t *search_surface;
    int search_active, hover_tool, pressed_tool, show_invisibles, wrap;
    int caret_line, block_caret;
    int cursor;                     /* the pointer shape last asked for */
    int match_case, whole_word, regex;  /* the Find window's checkboxes */
    int indent_guides, ruler;       /* View > Indentation Guides, Ruler */
    int language_mode;              /* the Language menu's EDIT_LANG_*  */
    /* The saved defaults, and the copy the Preferences window edits
     * until Save or Cancel. */
    editor_preferences_t preferences, pref_draft;
    uint32_t prefs_id;              /* the Preferences window, or 0     */
    tiku_surface_t *prefs_surface;  /* what that window is painted on   */
    int pref_focus;                 /* its control that has the keys    */
    char pref_message[120];         /* its status line; empty for help  */
    tiku_menu_t context;            /* the right-click menu             */
    tiku_rect_t context_frame;      /* where that menu is drawn         */
    int context_open;               /* whether it is showing            */
    /* Live search for the Find window's query: the matches marked, at
     * most EDIT_SEARCH_MAX of them, and what they were found for. */
    tiku_edit_range_t matches[EDIT_SEARCH_MAX];
    size_t match_count;             /* how many are marked              */
    int match_truncated;            /* more matched than were marked    */
    uint64_t search_generation;     /* the document generation searched */
    unsigned search_flags;          /* the TIKU_EDIT_* flags searched   */
    char search_text[1024];         /* the query searched               */
    char search_message[128];       /* the count line Find shows        */
    /* Document and command changes a paint took in that no publish has
     * followed yet; the tick publishes the menus for them. */
    unsigned menus_due;
    char last_note[128];
    int64_t note_until;
} edit_state_t;


#define EDIT_TOOL_H 29
#define EDIT_FOOT_H 20
/* The divider between split panes, and the least height a pane keeps. */
#define EDIT_SPLIT_GAP 6
#define EDIT_PANE_MIN 40
#define SEARCH_W 480
#define SEARCH_H 196

extern const tiku_app_descriptor_t tiku_edit_app;


#pragma GCC visibility push(hidden)

/* Standalone launch document; the runtime's explicit document takes priority. */
int tiku_editor_startup_path(const char *path);

/* App. */
int tiku_editor_start(void **state, const tiku_app_services_t *services);
void tiku_editor_stop(void *state);
int tiku_editor_split(edit_state_t *st);

/* Run. */
void tiku_editor_run_program(edit_state_t *st);
void tiku_editor_tick(void *state, int64_t now_us);


/* File. */
int tiku_editor_read_document(edit_state_t *st, const char *path);
void tiku_editor_load(edit_state_t *st);
int tiku_editor_write_document(edit_state_t *st, FILE *f);
int tiku_editor_save_document(edit_state_t *st, const char *path);
int tiku_editor_save(edit_state_t *st);

/* View. */
tiku_rect_t tiku_editor_view_box(const edit_state_t *st);
tiku_rect_t tiku_editor_pane_box(const edit_state_t *st, int pane);
tiku_rect_t tiku_editor_divider(const edit_state_t *st);
void tiku_editor_focus_pane(edit_state_t *st, int pane);
tiku_rect_t tiku_editor_page(const edit_state_t *st);
tiku_rect_t tiku_editor_note_box(const edit_state_t *st);
void tiku_editor_view_sync(edit_state_t *st);
void tiku_editor_reveal(edit_state_t *st);
void tiku_editor_follow(edit_state_t *st, tiku_codeview_t *view,
                        tiku_edit_range_t target);
tiku_rect_t tiku_editor_tool_rect(int i);
int tiku_editor_tool_enabled(edit_state_t *st, int i);
tiku_rect_t tiku_editor_search_field(const edit_state_t *st, int i);
tiku_rect_t tiku_editor_search_button(int i);
void tiku_editor_search_close(edit_state_t *st);
void tiku_editor_search_open_window(edit_state_t *st, int mode);
void tiku_editor_search_paint(edit_state_t *st);
void tiku_editor_paint(edit_state_t *st);

void tiku_editor_search_refresh(edit_state_t *st);
void tiku_editor_search_clear(edit_state_t *st);
void tiku_editor_select_matches(edit_state_t *st);

void tiku_editor_preferences_load(edit_state_t *st);
void tiku_editor_preferences_apply(edit_state_t *st,
                                   const editor_preferences_t *prefs);
void tiku_editor_preferences_pane(tiku_codeview_t *view,
                                  const editor_preferences_t *prefs);
void tiku_editor_preferences_colours(tiku_codeview_t *view, int preset);
void tiku_editor_preferences_open(edit_state_t *st);
void tiku_editor_preferences_close(edit_state_t *st);
void tiku_editor_preferences_paint(edit_state_t *st);
int tiku_editor_preferences_event(edit_state_t *st,
                                  const tiku_event_t *event);

void tiku_editor_context_open(edit_state_t *st, int x, int y);
int tiku_editor_context_event(edit_state_t *st, const tiku_event_t *event);

/* Commands. */
int tiku_editor_modified(const edit_state_t *st);
void tiku_editor_edited(edit_state_t *st);
void tiku_editor_refused(edit_state_t *st, tiku_edit_status_t status);
int tiku_editor_insert_checked(edit_state_t *st, const char *text);
void tiku_editor_find_match(edit_state_t *st, int backward);
void tiku_editor_replace_match(edit_state_t *st);
void tiku_editor_replace_all(edit_state_t *st);
void tiku_editor_file_panel(edit_state_t *st, int saving);
int tiku_editor_document_action(edit_state_t *st, int command);
int tiku_editor_guarded_action(edit_state_t *st, int command);
void tiku_editor_publish(edit_state_t *st);
int tiku_editor_pick(void *state, uint32_t window, int command);
void tiku_editor_picked(void *state, uint32_t window, const char *path);

/* Input. */
int tiku_editor_event(void *state, const tiku_event_t *e);
int tiku_editor_event_in(void *state, uint32_t window, const tiku_event_t *e);
void tiku_editor_closed(void *state, uint32_t window);

#pragma GCC visibility pop

#endif /* TIKU_EDITOR_PRIVATE_H_ */
