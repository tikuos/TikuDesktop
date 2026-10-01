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
#include "tiku_textview.h"
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

#include "tiku_editor_buffer.h"
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
       CMD_NUMBERS, CMD_REPLACE_ALL, CMD_INVISIBLES };

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
    /* Desktop-sized dynamic document; the legacy shared textview remains
     * unchanged for other applications with different compatibility needs. */
    edit_buffer_t                 tv;
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
    int w, h, left, dragging, finding, find_focus, pending;
    int crlf, bom, load_failed, disk_known, numbers, allow_open;
    struct stat disk;
    tiku_field_t field[3];
    tiku_field_input_t field_input[3];
    tiku_scroll_t vertical, horizontal;
    int width_dirty, max_width, measured_cell, measured_height;
    tiku_menuset_t last_menus;
    char scratch[64];
    uint32_t search_id;
    tiku_surface_t *search_surface;
    int search_active, hover_tool, pressed_tool, show_invisibles;
    int match_case, whole_word;
    char last_note[128];
    int64_t note_until;
} edit_state_t;


#define EDIT_TOOL_H 29
#define EDIT_FOOT_H 20
#define SEARCH_W 480
#define SEARCH_H 166

extern const tiku_app_descriptor_t tiku_edit_app;


#pragma GCC visibility push(hidden)

/* Standalone launch document; the runtime's explicit document takes priority. */
int tiku_editor_startup_path(const char *path);

/* App. */
int tiku_editor_start(void **state, const tiku_app_services_t *services);
void tiku_editor_stop(void *state);

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
tiku_rect_t tiku_editor_page(const edit_state_t *st);
tiku_rect_t tiku_editor_note_box(const edit_state_t *st);
int tiku_editor_rows_visible_of(const edit_state_t *st);
int tiku_editor_glyph_at(const char *text, int at, int x, char glyph[5], int *advance);
int tiku_editor_prefix_width(const char *text, int n);
void tiku_editor_reveal(edit_state_t *st);
tiku_rect_t tiku_editor_tool_rect(int i);
int tiku_editor_tool_enabled(edit_state_t *st, int i);
tiku_rect_t tiku_editor_search_field(const edit_state_t *st, int i);
tiku_rect_t tiku_editor_search_button(int i);
void tiku_editor_search_close(edit_state_t *st);
void tiku_editor_search_open_window(edit_state_t *st, int mode);
void tiku_editor_search_paint(edit_state_t *st);
void tiku_editor_paint(edit_state_t *st);

/* Commands. */
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
