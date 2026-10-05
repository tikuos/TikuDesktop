/* tiku_editor_private.h -- the Editor family's shared state, sizes,
 * command ids and internal functions.
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

#define EDIT_W      560         /**< the window's opening width */
#define EDIT_H      400         /**< and height */
#define STRIP_H     20          /**< not used */
#define MARGIN      6           /**< not used */

/* Menu command ids. */
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
    int tab;            /**< columns between tab stops, 1 to 32          */
    int indent;         /**< spaces an indent inserts; 0 inserts a tab   */
    int column;         /**< the column ruler's column, 1 to 240         */
    int zoom;           /**< the panes' zoom, -2 to 4                    */
    int colors;         /**< code inks: 0 the theme's, 1 Muted, 2 Strong */
    int numbers, wrap, guides, ruler;       /**< View switches           */
    int invisibles, caret_line, block_caret;    /**< View switches       */
} editor_preferences_t;
#define EDIT_SEARCH_MAX 1000    /* matches live search marks at most */
#define EDIT_SEARCH_KIND 7      /* the indicator kind that marks them */
#define EDIT_ZOOM_MIN (-2)      /* the least zoom the code view takes */
#define EDIT_ZOOM_MAX 4         /* and the most                       */
#define EDIT_PREF_W 560         /**< the Preferences window's width */
#define EDIT_PREF_H 438         /**< and height */

/** @brief The Editor's state: the document, its panes, the window, the
 *         Find and Preferences windows, and a run. */
typedef struct {
    const tiku_app_services_t *services;    /**< the desktop's calls */
    tiku_surface_t            *surface;     /**< the window's pixels */
    /** the display list the surface records into while it is painted,
     *  sent with each frame; NULL when the host has no present() */
    tiku_dl_t                      *dl;
    uint32_t                        id;     /**< the document window */
    /* The text, and the view that lays it out, paints it and takes the
     * keys and the pointer in the page; the scrollbars and the rest of
     * the window are the Editor's. */
    tiku_editdoc_t                 *doc;    /**< the text */
    tiku_codeview_t                *view;   /**< the pane with the keys */
    /* The page's panes: the upper one and, while it is split, the lower,
     * each a view of doc with its own caret and scroll; view is the one
     * that has the keys, panes[pane]. */
    tiku_codeview_t                *panes[2];
    int                             pane;       /**< the pane with the keys */
    int                             split_at;   /**< the upper pane's height */
    /** where the divider is held, from its top; -1 when it is not held */
    int                             split_grab;
    /* The line break Return types: the one the file uses most, "\n" for
     * a new document. */
    char                            newline[3];
    /* The clipboard's text as the view is handed it, line breaks turned
     * into the document's; freed by the next such call or with the
     * document. */
    char                           *paste;
    tiku_text_input_t               commit;     /**< stages typed text */
    /* The unsaved-changes question, the kit's alert drawn over this
     * window. */
    tiku_alert_t                    ask;
    tiku_rect_t                     ask_frame;  /**< where it is drawn */
    int                             saved_shown;    /**< not used */
    char                            path[512];  /**< the file; empty for none */
    char                            note[128];  /**< the footer message */
    /* The document's language: from the file's name when it is read
     * (.bas BASIC, .c and .h C, else none), then as the Language menu
     * sets it. */
    tiku_syntax_lang_t              lang;
    /* The file panel's question, 1 Save or 0 Open; its answer comes later,
     * to tiku_editor_picked(). */
    int                             pick_save;
    /* The run: the last lines tiku-basic printed, read from a pipe; the
     * run pane shows while a run goes or has output. */
    char                            out[48][96];    /**< the output lines */
    int                             out_count;  /**< lines held */
    int                             running;    /**< a run goes */
    int                             child_fd;   /**< the pipe; -1 for none */
    pid_t                           child;      /**< tiku-basic's process */
    char                            out_partial[96];    /**< an unended line */
    /** the window's size; the Find window's mode (0 closed, 1 Find, 2 Go
     *  to Line) and its focused field, -1 for none; the command waiting on
     *  the unsaved-changes question */
    int w, h, finding, find_focus, pending;
    /** the file had a BOM; it failed to load; disk holds its state; line
     *  numbers are shown; an Open that Discard allowed */
    int bom, load_failed, disk_known, numbers, allow_open;
    struct stat disk;               /**< the file as last read or saved */
    tiku_field_t field[3];          /**< Find, Replace and Go to Line */
    tiku_field_input_t field_input[3];  /**< their input state */
    tiku_scroll_t vertical, horizontal; /**< the upper pane's bar; the
                                             horizontal bar */
    tiku_scroll_t lower;            /**< the lower pane's vertical bar */
    tiku_menuset_t last_menus;      /**< the menus last published */
    char scratch[64];               /**< an unnamed run's file; empty none */
    uint32_t search_id;             /**< the Find window, or 0 */
    tiku_surface_t *search_surface; /**< what it is painted on */
    /** the Find window has the keys; the toolbar tool hovered and the one
     *  pressed, -1 for none; View: Show Invisibles, Wrap Lines */
    int search_active, hover_tool, pressed_tool, show_invisibles, wrap;
    int caret_line, block_caret;    /**< View: Highlight Caret Line, Block
                                         Caret */
    int cursor;                     /**< the pointer shape last asked for */
    int match_case, whole_word, regex;  /**< the Find window's checkboxes */
    int indent_guides, ruler;       /**< View > Indentation Guides, Ruler */
    int language_mode;              /**< the Language menu's EDIT_LANG_* */
    /* The saved defaults, and the copy the Preferences window edits
     * until Save or Cancel. */
    editor_preferences_t preferences, pref_draft;
    uint32_t prefs_id;              /**< the Preferences window, or 0 */
    tiku_surface_t *prefs_surface;  /**< what that window is painted on */
    int pref_focus;                 /**< its control that has the keys */
    char pref_message[120];         /**< its status line; empty for help */
    tiku_menu_t context;            /**< the right-click menu */
    tiku_rect_t context_frame;      /**< where that menu is drawn */
    int context_open;               /**< whether it is showing */
    /* Live search for the Find window's query: the matches marked, at
     * most EDIT_SEARCH_MAX of them, and what they were found for. */
    tiku_edit_range_t matches[EDIT_SEARCH_MAX];
    size_t match_count;             /**< how many are marked */
    int match_truncated;            /**< more matched than were marked */
    uint64_t search_generation;     /**< the document generation searched */
    unsigned search_flags;          /**< the TIKU_EDIT_* flags searched */
    char search_text[1024];         /**< the query searched */
    char search_message[128];       /**< the count line Find shows */
    /* Document and command changes a paint took in since the menus were
     * last published; the tick publishes the menus for them. */
    unsigned menus_due;
    char last_note[128];            /**< the note the 8 s clock is for */
    int64_t note_until;             /**< when that note is cleared */
} edit_state_t;


#define EDIT_TOOL_H 29                  /**< the toolbar's height */
#define EDIT_FOOT_H 20                  /**< the footer's height */
/* The divider between split panes, and the least height a pane keeps. */
#define EDIT_SPLIT_GAP 6
#define EDIT_PANE_MIN 40
#define SEARCH_W 480                    /**< the Find window's width */
#define SEARCH_H 196                    /**< and height */

/** @brief The Editor's descriptor. */
extern const tiku_app_descriptor_t tiku_edit_app;


#pragma GCC visibility push(hidden)

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
