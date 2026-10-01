/* tiku_editor_buffer.h -- dynamic UTF-8 document and undo-journal interface.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_EDITOR_BUFFER_H_
#define TIKU_EDITOR_BUFFER_H_

#include <stddef.h>
#include <stdint.h>
#include "tiku_textview.h"

typedef struct { char *text; int len; int width; } edit_line_t;
typedef struct { int y, x; } edit_hit_t;
/* One undo record: a range replacement, or a Replace All that keeps each
 * match's place and text.  For Replace All, y and x are the caret when it
 * ran, before holds the matched text of every hit and after the
 * replacement once. */
typedef struct {
    int y, x, old_y, old_x, new_y, new_x;
    char *before, *after;
    size_t bytes;
    uint64_t previous, revision;
    edit_hit_t *hit;            /* Replace All: each match, in order */
    int nhit;                   /* 0 for a range replacement */
    size_t match;               /* the bytes each hit replaced */
} edit_change_t;
typedef struct {
    edit_line_t *line;
    int nline, cy, cx, ay, ax, selecting, top;
    edit_change_t undo[256];
    int undo_n, undo_at;
    size_t history_bytes;
    uint64_t revision, saved_revision, serial;
} edit_buffer_t;


#pragma GCC visibility push(hidden)
/* Buffer. */
void tiku_editor_buffer_free(edit_buffer_t *b);
int tiku_editor_buffer_set(edit_buffer_t *b, const char *text);
int tiku_editor_buffer_init(edit_buffer_t *b);
const char *tiku_editor_buffer_line(const edit_buffer_t *b, int y);
int tiku_editor_buffer_modified(const edit_buffer_t *b);
void tiku_editor_buffer_saved(edit_buffer_t *b);
int tiku_editor_buffer_can_undo(const edit_buffer_t *b);
int tiku_editor_buffer_can_redo(const edit_buffer_t *b);
void tiku_editor_buffer_place(edit_buffer_t *b, int y, int x);
void tiku_editor_buffer_select_none(edit_buffer_t *b);
void tiku_editor_buffer_select_begin(edit_buffer_t *b);
void tiku_editor_buffer_select_all(edit_buffer_t *b);
int tiku_editor_buffer_selection(const edit_buffer_t *b, int *y0, int *x0, int *y1, int *x1);
size_t tiku_editor_buffer_text(const edit_buffer_t *b, char *out, size_t cap);
size_t tiku_editor_buffer_selected(const edit_buffer_t *b, char *out, size_t cap);
int tiku_editor_buffer_replace(edit_buffer_t *b, const char *text);
int tiku_editor_buffer_replace_hits(edit_buffer_t *b, edit_hit_t *hit, int n,
                                    size_t match, const char *with);
int tiku_editor_buffer_undo(edit_buffer_t *b);
int tiku_editor_buffer_redo(edit_buffer_t *b);
int tiku_editor_buffer_scroll_to(edit_buffer_t *b, int top, int rows);
void tiku_editor_buffer_reveal(edit_buffer_t *b, int rows);
void tiku_editor_buffer_move(edit_buffer_t *b, tiku_textview_move_t move);

#pragma GCC visibility pop

#endif /* TIKU_EDITOR_BUFFER_H_ */
