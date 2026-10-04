/* Editor's explicit launch document and ordinary empty launch.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../../apps/editor/tiku_editor_private.h"
#include "tiku_host.h"
#include "tiku_msg.h"
#include "tiku_remote.h"
#include "tiku_codeview_private.h"

static char title_seen[128];
static uint32_t next_window;
static char *clipboard;         /* the desk's, as the services hold it */
static unsigned clipboard_stamp;
static int picker_count, picker_mode, sizeable_count;
static int shape_asked = -1, shapes_sent;  /* the last TIKU_MSG_CURSOR */

/** Native clipboard and file-panel seams for workflow tests. */
static int clip_set(void *ctx, const char *text)
{ (void)ctx; free(clipboard); clipboard = strdup(text); clipboard_stamp++; return 0; }
static const char *clip_text(void *ctx, unsigned *stamp)
{ (void)ctx; if (stamp) *stamp = clipboard_stamp; return clipboard ? clipboard : ""; }
static int pick_file(void *ctx, uint32_t id, int mode, const char *dir, const char *name)
{ (void)ctx; (void)id; (void)dir; (void)name; picker_count++; picker_mode = mode; return 0; }
static int sizeable(void *ctx, uint32_t id)
{ (void)ctx; (void)id; sizeable_count++; return 0; }

/** Feed the same input descriptor as the remote/native runtime. */
static int key(edit_state_t *st, unsigned code, unsigned mods)
{
    tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN, .key = code, .modifiers = mods};
    return tiku_editor_event(st, &e);
}

/** Choose a question button using the same measured geometry as the painter. */
static int answer(edit_state_t *st, int index)
{
    assert(st->ask.open);
    int x, y, w, h;
    tiku_alert_button_rect(&st->ask, index, st->ask_frame.w, st->ask_frame.h,
                           &x, &y, &w, &h);
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN,
        .x = st->ask_frame.x + x + w / 2, .y = st->ask_frame.y + y + h / 2};
    return tiku_editor_event(st, &e);
}

/** A byte-exact readback includes final line breaks and UTF-8 BOMs. */
static void expect_file(const char *path, const char *expected)
{
    char text[512]; FILE *f = fopen(path, "rb"); assert(f);
    size_t n = fread(text, 1, sizeof text, f); assert(!ferror(f)); fclose(f);
    assert(n == strlen(expected) && !memcmp(text, expected, n));
}

/** File fixtures never rely on the user's files. */
static void put_file(const char *path, const char *text)
{ FILE *f = fopen(path, "wb"); assert(f); assert(fputs(text, f) >= 0); assert(!fclose(f)); }

/** A fixture of exactly @p n bytes, NUL bytes included. */
static void put_bytes(const char *path, const char *bytes, size_t n)
{ FILE *f = fopen(path, "wb"); assert(f); assert(fwrite(bytes, 1, n, f) == n); assert(!fclose(f)); }

static char *line_copy;         /* what line() last returned */
static size_t line_cap;

/** Line @p y's text without its break, valid until the next call. */
static const char *line(edit_state_t *st, int64_t y)
{
    tiku_edit_range_t r = {tiku_editdoc_line_start(st->doc, y), tiku_editdoc_line_end(st->doc, y)};
    size_t got = 0;
    assert(r.start >= 0 && r.end >= r.start);
    if ((size_t)(r.end - r.start) + 1 > line_cap) {
        free(line_copy); line_cap = (size_t)(r.end - r.start) + 1;
        line_copy = malloc(line_cap); assert(line_copy);
    }
    assert(tiku_editdoc_read(st->doc, r, line_copy, line_cap, &got) == TIKU_EDIT_OK);
    return line_copy;
}

/** Lines held, and the byte length of line @p y. */
static int64_t lines(edit_state_t *st) { return tiku_editdoc_lines(st->doc); }
static int64_t line_len(edit_state_t *st, int64_t y)
{ return tiku_editdoc_line_end(st->doc, y) - tiku_editdoc_line_start(st->doc, y); }

/** The whole text, NUL-terminated; the caller frees it. */
static char *text_of(edit_state_t *st)
{
    tiku_edit_range_t all = {0, tiku_editdoc_length(st->doc)};
    size_t got = 0;
    char *text = malloc((size_t)all.end + 1); assert(text);
    assert(tiku_editdoc_read(st->doc, all, text, (size_t)all.end + 1, &got) == TIKU_EDIT_OK);
    return text;
}

/** Replace the whole text and its history, as a load does. */
static int set_text(edit_state_t *st, const char *text)
{ return tiku_editdoc_load(st->doc, text, strlen(text)) == TIKU_EDIT_OK; }

/** The caret position on line @p y nearest byte @p x, snapped back to a
 *  caret position. */
static tiku_edit_pos_t pos_of(edit_state_t *st, int64_t y, int64_t x)
{
    tiku_edit_pos_t start = tiku_editdoc_line_start(st->doc, y);
    tiku_edit_pos_t pos = start + (x < line_len(st, y) ? x : line_len(st, y));
    while (pos > start && !tiku_editdoc_is_caret(st->doc, pos)) pos--;
    return pos;
}

/** Put an empty caret at line @p y, byte @p x. */
static void place(edit_state_t *st, int64_t y, int64_t x)
{ tiku_edit_pos_t at = pos_of(st, y, x); assert(tiku_codeview_select(st->view, at, at) == TIKU_EDIT_OK); }

/** The caret's line and byte. */
static void caret_at(edit_state_t *st, int64_t *y, int64_t *x)
{
    tiku_edit_pos_t anchor, caret;
    assert(tiku_codeview_selection(st->view, &anchor, &caret) == TIKU_EDIT_OK);
    *y = tiku_editdoc_line_of(st->doc, caret);
    *x = caret - tiku_editdoc_line_start(st->doc, *y);
}

/** Whether the selection is the bytes from @p a to @p b, the caret at b. */
static int selected(edit_state_t *st, tiku_edit_pos_t a, tiku_edit_pos_t b)
{
    tiku_edit_pos_t anchor, caret;
    assert(tiku_codeview_selection(st->view, &anchor, &caret) == TIKU_EDIT_OK);
    return anchor == a && caret == b;
}

/** Step the history the way the Edit menu does. */
static int undo(edit_state_t *st) { return tiku_codeview_undo(st->view) == TIKU_EDIT_OK; }
static int redo(edit_state_t *st) { return tiku_codeview_redo(st->view) == TIKU_EDIT_OK; }

/** Every accepted replacement must round-trip through one undo and redo. */
static void undo_roundtrips(edit_state_t *st)
{
    const char *pieces[] = {"a", "café", "\n", "alpha\nbeta\n", "", "\tend"};
    unsigned seed = 19;
    for (int i = 0; i < 300; i++) {
        if (i % 15 == 0) assert(set_text(st,
            "A short paragraph with café.\nAnother line with tabs\tand text.\nLast line."));
        seed = seed * 1664525u + 1013904223u;
        int64_t y = (int64_t)(seed % (unsigned)lines(st));
        tiku_edit_pos_t anchor = pos_of(st, y, (int64_t)((seed >> 8) % (unsigned)(line_len(st, y) + 1)));
        tiku_edit_pos_t caret = anchor;
        if (seed & 1) {
            int64_t other = (int64_t)((seed >> 16) % (unsigned)lines(st));
            caret = pos_of(st, other, (int64_t)((seed >> 5) % (unsigned)(line_len(st, other) + 1)));
        }
        assert(tiku_codeview_select(st->view, anchor, caret) == TIKU_EDIT_OK);
        char *before = text_of(st);
        if (!tiku_editor_insert_checked(st, pieces[(seed >> 24) % 6])) { free(before); continue; }
        char *after = text_of(st);
        if (strcmp(before, after)) {
            assert(undo(st));
            char *actual = text_of(st);
            if (strcmp(before, actual))
                fprintf(stderr, "Roundtrip %d seed %u\nBefore: [%s]\nAfter: [%s]\nUndo: [%s]\n",
                        i, seed, before, after, actual);
            assert(!strcmp(before, actual)); free(actual);
            assert(redo(st));
            actual = text_of(st); assert(!strcmp(after, actual)); free(actual);
        }
        free(before); free(after);
    }
    assert(set_text(st, ""));
}

/** The history keeps every change within its byte budget, and a branch
 *  after undo keeps the save point accurate. */
static void history_rollover(edit_state_t *st)
{
    assert(set_text(st, ""));
    for (int i = 0; i < 400; i++) assert(tiku_editor_insert_checked(st, "x"));
    assert(line_len(st, 0) == 400);
    tiku_editdoc_saved(st->doc, tiku_editdoc_state(st->doc));
    for (int i = 0; i < 400; i++) assert(undo(st));
    assert(!undo(st) && line_len(st, 0) == 0);
    assert(tiku_editor_modified(st));
    for (int i = 0; i < 400; i++) assert(redo(st));
    assert(!redo(st) && !tiku_editor_modified(st));
    assert(undo(st));
    assert(tiku_editor_insert_checked(st, "y"));
    assert(!tiku_editdoc_can_redo(st->doc) && tiku_editor_modified(st));
    assert(undo(st) && tiku_editor_modified(st));
    assert(redo(st) && line(st, 0)[399] == 'y');
    assert(set_text(st, ""));
}

/** Long lines, many lines and multi-megabyte documents round-trip losslessly. */
static void large_documents(edit_state_t *st, const char *dir)
{
    char path[512]; snprintf(path, sizeof path, "%s/large.txt", dir);
    FILE *f = fopen(path, "wb"); assert(f);
    char row[81]; memset(row, 'a', 79); row[79] = '\n'; row[80] = 0;
    for (int i = 0; i < 70000; i++) assert(fwrite(row, 1, 80, f) == 80);
    fclose(f); assert(tiku_editor_read_document(st, path));
    assert(lines(st) == 70001 && tiku_editor_save(st));
    struct stat info; assert(!stat(path, &info) && info.st_size == 5600000);
    place(st, 65000, 40);
    assert(tiku_editor_insert_checked(st, "café"));
    assert(undo(st) && line_len(st, 65000) == 79);
    assert(redo(st) && line_len(st, 65000) == 84);
    assert(undo(st));
    size_t length = 256 * 1024;
    char *longline = malloc(length + 1); assert(longline);
    memset(longline, 'x', length); longline[length] = 0;
    put_file(path, longline); assert(tiku_editor_read_document(st, path));
    assert(lines(st) == 1 && line_len(st, 0) == (int64_t)length);
    place(st, 0, (int64_t)length - 2);
    assert(tiku_editor_insert_checked(st, "\ncafé\n"));
    assert(lines(st) == 3 && undo(st));
    assert(lines(st) == 1 && !strcmp(line(st, 0), longline));
    /* Joining two large lines and replacing all of them no longer needs a 1 KB seam. */
    assert(tiku_editor_insert_checked(st, "\n"));
    tiku_codeview_select_all(st->view); assert(tiku_editor_insert_checked(st, "done"));
    assert(undo(st) && lines(st) == 2);
    assert(undo(st) && lines(st) == 1);
    tiku_editor_reveal(st); tiku_editor_paint(st); assert(tiku_codeview_left(st->view) > 1024);
    assert(tiku_editor_save(st)); assert(!stat(path, &info) && info.st_size == (off_t)length);
    free(longline); unlink(path);
    assert(set_text(st, "Alpha alpha alphabet\nalpha\n"));
    tiku_field_start(&st->field[0], "alpha");
    tiku_field_start(&st->field[1], "beta");
    st->match_case = 0; st->whole_word = 1;
    tiku_editor_replace_all(st);
    assert(!strcmp(line(st, 0), "beta beta alphabet"));
    assert(!strcmp(line(st, 1), "beta"));
    assert(undo(st));
    assert(!strcmp(line(st, 0), "Alpha alpha alphabet"));
    st->match_case = 1; st->whole_word = 0;
    /* Find owns a different window; ordinary document input still edits text. */
    place(st, 0, 0);
    tiku_editor_pick(st, st->id, CMD_FIND);
    assert(st->search_id && st->search_id != st->id);
    /* Background exposure keeps field focus; pointer movement stays in Find. */
    tiku_event_t e = {.type = TIKU_EVENT_EXPOSE};
    tiku_editor_event_in(st, st->id, &e); assert(st->search_active);
    e = (tiku_event_t){.type = TIKU_EVENT_POINTER_MOVE, .x = 10, .y = 10};
    tiku_editor_event_in(st, st->search_id, &e); assert(st->hover_tool == -1);
    /* Menu commands target their owning window even without activation events. */
    tiku_editor_pick(st, st->id, CMD_ALL);
    assert(selected(st, 0, tiku_editdoc_length(st->doc)) && !st->search_active);
    place(st, 0, 0);
    tiku_editor_pick(st, st->search_id, CMD_ALL);
    assert(st->field[0].sel_b == st->field[0].len && st->search_active);
    int64_t before = line_len(st, 0);
    e = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = 'x'};
    tiku_editor_event_in(st, st->id, &e); assert(line_len(st, 0) == before + 1);
    e.key = TIKU_KEY_ESCAPE;
    tiku_editor_event_in(st, st->search_id, &e); assert(!st->search_id && !st->finding);
    assert(set_text(st, "")); st->path[0] = 0;
}

/** Native toolbar activation requires releasing over the same enabled tool. */
static void toolbar_actions(edit_state_t *st)
{
    tiku_rect_t r = tiku_editor_tool_rect(5);
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .x = r.x + 8, .y = r.y + 8};
    tiku_editor_event_in(st, st->id, &e); assert(st->pressed_tool == 5);
    e.type = TIKU_EVENT_POINTER_UP; e.x = 400;
    tiku_editor_event_in(st, st->id, &e);
    assert(st->pressed_tool == -1 && !st->search_id);
    e.type = TIKU_EVENT_POINTER_DOWN; e.x = r.x + 8;
    tiku_editor_event_in(st, st->id, &e);
    e.type = TIKU_EVENT_POINTER_UP;
    tiku_editor_event_in(st, st->id, &e); assert(st->search_id);
    e = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = 'w', .modifiers = TIKU_MOD_CMD};
    assert(!tiku_editor_event_in(st, st->search_id, &e));
    assert(!st->search_id && !st->finding);
}

/** Safety, editing and navigation work through the editor rather than mocks. */
static void workflows(edit_state_t *st, const char *dir)
{
    char path[512], other[512];
    int64_t y, x;
    snprintf(path, sizeof path, "%s/work.txt", dir);
    snprintf(other, sizeof other, "%s/other.txt", dir);
    const char *samples[] = {"", "one", "one\n", "one\n\n",
        "\xef\xbb\xbf" "café\r\ntwo\r\n"};
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; i++) {
        put_file(path, samples[i]); assert(tiku_editor_read_document(st, path));
        assert(tiku_editor_save(st)); expect_file(path, samples[i]);
    }
    /* More than the old 31999-byte cap must survive a save unchanged. */
    FILE *f = fopen(path, "wb"); assert(f);
    for (int i = 0; i < 2000; i++) assert(fputs("twenty bytes of text!\n", f) >= 0);
    assert(!fclose(f)); assert(tiku_editor_read_document(st, path));
    assert(lines(st) == 2001 && tiku_editor_save(st));
    struct stat info;
    assert(!stat(path, &info) && info.st_size == (off_t)(2000 * strlen("twenty bytes of text!\n")));
    tiku_event_t resize = {.type = TIKU_EVENT_RESIZE, .width = 780, .height = 530};
    tiku_editor_event(st, &resize); assert(st->w == 780 && st->surface->w == 780);
    tiku_event_t wheel = {.type = TIKU_EVENT_WHEEL, .dy = 20};
    tiku_editor_event(st, &wheel); assert(tiku_codeview_top(st->view) == 20);
    key(st, TIKU_KEY_END, TIKU_MOD_CMD); caret_at(st, &y, &x); assert(y == 2000);
    key(st, TIKU_KEY_HOME, TIKU_MOD_CMD); caret_at(st, &y, &x);
    assert(!y && !tiku_codeview_top(st->view));
    put_file(path, "alpha beta alpha\nlast"); assert(tiku_editor_read_document(st, path));
    key(st, TIKU_KEY_RIGHT, TIKU_MOD_SHIFT);
    key(st, TIKU_KEY_RIGHT, TIKU_MOD_SHIFT);
    key(st, 'c', TIKU_MOD_CMD); assert(!strcmp(clipboard, "al"));
    key(st, 'x', TIKU_MOD_CMD); assert(!strcmp(line(st, 0), "pha beta alpha"));
    key(st, 'z', TIKU_MOD_CMD); assert(!tiku_editor_modified(st));
    key(st, 'y', TIKU_MOD_CMD); assert(tiku_editor_modified(st));
    key(st, 'z', TIKU_MOD_CMD);
    place(st, 0, 0);
    key(st, 'h', TIKU_MOD_CMD); assert(st->finding == 1);
    tiku_field_start(&st->field[0], "alpha");
    tiku_editor_find_match(st, 0); caret_at(st, &y, &x); assert(x == 5);
    tiku_editor_find_match(st, 0); caret_at(st, &y, &x); assert(x == 16);
    tiku_editor_find_match(st, 0); caret_at(st, &y, &x);
    assert(x == 5 && strstr(st->note, "wrapped"));
    tiku_field_start(&st->field[1], "omega");
    tiku_editor_replace_match(st); assert(!strcmp(line(st, 0), "omega beta alpha"));
    key(st, TIKU_KEY_ESCAPE, 0); key(st, 'z', TIKU_MOD_CMD);
    assert(!strcmp(line(st, 0), "alpha beta alpha"));
    assert(!tiku_editor_modified(st));
    key(st, 'g', TIKU_MOD_CMD); tiku_field_start(&st->field[2], "2");
    key(st, TIKU_KEY_RETURN, 0); caret_at(st, &y, &x); assert(y == 1 && !x && !st->finding);
    tiku_rect_t page = tiku_editor_page(st);
    tiku_event_t click = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = page.x, .y = page.y + 2};
    tiku_editor_event(st, &click); caret_at(st, &y, &x); assert(!y && !x);
    int64_t px, py;
    assert(tiku_codeview_point(st->view, 5, &px, &py) == TIKU_EDIT_OK);
    click.type = TIKU_EVENT_POINTER_MOVE; click.button = 0; click.x = (int)px;
    tiku_editor_event(st, &click); assert(selected(st, 0, 5));
    click.type = TIKU_EVENT_POINTER_UP; click.button = 1; tiku_editor_event(st, &click);
    key(st, 'c', TIKU_MOD_CMD); assert(!strcmp(clipboard, "alpha"));
    /* Clipboard supports multiple lines and UTF-8, as one undo operation. */
    clip_set(NULL, "café\nnotes"); key(st, 'v', TIKU_MOD_CMD);
    assert(!strcmp(line(st, 0), "café"));
    key(st, 'z', TIKU_MOD_CMD); assert(!tiku_editor_modified(st));
    key(st, '!', 0); assert(tiku_editor_modified(st));
    assert(!key(st, 'w', TIKU_MOD_CMD) && st->ask.open);
    key(st, TIKU_KEY_RETURN, 0); assert(!st->ask.open && tiku_editor_modified(st));
    tiku_editor_closed(st, st->id); assert(st->ask.open);
    key(st, TIKU_KEY_ESCAPE, 0);
    int before = picker_count;
    key(st, 'o', TIKU_MOD_CMD); assert(st->ask.open && picker_count == before);
    key(st, TIKU_KEY_ESCAPE, 0);
    /* Refused reads and writes keep the current path, data and undo state. */
    put_bytes(other, "nul\0inside", 10); assert(!tiku_editor_read_document(st, other));
    assert(!strcmp(st->path, path) && tiku_editor_modified(st));
    assert(!strcmp(st->note, "Not a UTF-8 text file. Use TikuHex for binary data."));
    put_file(path, "changed externally"); assert(!tiku_editor_save(st));
    expect_file(path, "changed externally");
    char impossible[512]; snprintf(impossible, sizeof impossible, "%s/no-folder/file.txt", dir);
    assert(!tiku_editor_save_document(st, impossible) && !strcmp(st->path, path));
    assert(tiku_editor_save_document(st, other) && !tiku_editor_modified(st));
    assert(!strcmp(st->path, other));
    key(st, 'n', TIKU_MOD_CMD); assert(!st->path[0] && lines(st) == 1);
    key(st, 's', TIKU_MOD_CMD); assert(picker_mode == TIKU_APP_PICK_SAVE);
    /* Typing while an Open panel is outstanding must not be lost. */
    key(st, 'o', TIKU_MOD_CMD); key(st, 'x', 0);
    tiku_editor_picked(st, st->id, path); assert(!st->path[0]);
    assert(!strcmp(line(st, 0), "x"));
    /* Discard approval does not mark an unsaved file clean if Open is cancelled. */
    key(st, 'o', TIKU_MOD_CMD); answer(st, 1);
    assert(tiku_editor_modified(st));
    tiku_editor_picked(st, st->id, path); assert(!strcmp(st->path, path));
    /* Saving through a guarded transition continues only after success. */
    key(st, '!', 0); key(st, 'n', TIKU_MOD_CMD); answer(st, 2);
    assert(!st->path[0] && !tiku_editor_modified(st));
    key(st, 'x', 0); key(st, 'n', TIKU_MOD_CMD); answer(st, 2);
    assert(st->pending == CMD_NEW && picker_mode == TIKU_APP_PICK_SAVE);
    tiku_editor_picked(st, st->id, other);
    assert(!st->path[0] && !tiku_editor_modified(st));
    assert(tiku_editor_read_document(st, other));
    assert(!chmod(other, 0444)); assert(!tiku_editor_save(st)); assert(!chmod(other, 0644));
    char linkpath[512]; snprintf(linkpath, sizeof linkpath, "%s/link.txt", dir);
    assert(!symlink(other, linkpath)); assert(!tiku_editor_save_document(st, linkpath));
    assert(!unlink(linkpath));
    /* Empty replacement is undoable, and clipboard overflow never cuts text. */
    tiku_codeview_select_all(st->view); assert(tiku_editor_insert_checked(st, ""));
    assert(!line_len(st, 0)); key(st, 'z', TIKU_MOD_CMD);
    assert(!strcmp(line(st, 0), "x"));
    char longline[1024]; memset(longline, 'q', 1023); longline[1023] = 0;
    assert(set_text(st, longline));
    place(st, 0, 1023);
    assert(tiku_editor_insert_checked(st, "a") && line_len(st, 0) == 1024);
    put_file(other, "bad\xff"); assert(!tiku_editor_read_document(st, other));
    assert(line_len(st, 0) == 1024);
    assert(!strcmp(st->note, "Invalid UTF-8; the current document was kept."));
    assert(set_text(st, ""));
    for (int i = 0; i < 10; i++) {
        assert(tiku_editor_insert_checked(st, "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\n"));
    }
    tiku_codeview_select_all(st->view); key(st, 'c', TIKU_MOD_CMD);
    assert(strlen(clipboard) == 530);
    assert(tiku_editor_insert_checked(st, "")); key(st, 'v', TIKU_MOD_CMD);
    assert(lines(st) == 11);
    assert(set_text(st, ""));
    char block[502]; memset(block, 'q', 500); block[500] = '\n'; block[501] = 0;
    for (int i = 0; i < 10; i++) assert(tiku_editor_insert_checked(st, block));
    tiku_codeview_select_all(st->view);
    key(st, 'x', TIKU_MOD_CMD);             /* 5010 bytes: past the old 4 KB */
    assert(strlen(clipboard) == 5010 && lines(st) == 1);
    key(st, 'v', TIKU_MOD_CMD);
    assert(lines(st) == 11 && line_len(st, 9) == 500);
    tiku_editdoc_saved(st->doc, tiku_editdoc_state(st->doc));
    assert(!unlink(path) && !unlink(other));
}

/** A panel answer during the unsaved-changes question leaves its action. */
static void panel_during_question(edit_state_t *st, const char *dir)
{
    char path[512]; snprintf(path, sizeof path, "%s/panel.txt", dir);
    put_file(path, "kept\n");
    assert(set_text(st, "")); st->path[0] = 0;
    /* A Save panel from before New asked is ignored; Discard still runs New. */
    key(st, 'y', 0); key(st, 's', TIKU_MOD_CMD);
    assert(picker_mode == TIKU_APP_PICK_SAVE);
    key(st, 'n', TIKU_MOD_CMD); assert(st->ask.open && st->pending == CMD_NEW);
    tiku_editor_picked(st, st->id, path);
    assert(st->ask.open && st->pending == CMD_NEW && !st->path[0]);
    assert(strstr(st->note, "Save cancelled")); expect_file(path, "kept\n");
    answer(st, 1);
    assert(!st->ask.open && !tiku_editor_modified(st));
    assert(lines(st) == 1 && !line_len(st, 0) && !st->note[0]);
    /* An Open panel answered or cancelled while Close asks leaves Close. */
    key(st, 'o', TIKU_MOD_CMD); assert(picker_mode == TIKU_APP_PICK_OPEN);
    key(st, 'y', 0); key(st, 'w', TIKU_MOD_CMD);
    assert(st->ask.open && st->pending == CMD_QUIT);
    tiku_editor_picked(st, st->id, ""); assert(!st->note[0]);
    tiku_editor_picked(st, st->id, path);
    assert(st->ask.open && st->pending == CMD_QUIT && !st->path[0]);
    assert(strstr(st->note, "Open cancelled"));
    assert(answer(st, 1) == 1 && !tiku_editor_modified(st));
    assert(!unlink(path));
}

/** Replace All is one step however large the document, so the history
 *  before it survives; undo and redo put back the selection each side
 *  of it, which here is the caret where Replace All ran. */
static void replace_all_history(edit_state_t *st)
{
    size_t size = 17u << 20;
    int64_t y, x;
    char *text = malloc(size + 1); assert(text);
    for (size_t i = 0; i < size; i++) text[i] = i % 80 == 79 ? '\n' : (char)('a' + i % 7);
    memcpy(text + 1000, "needle", 6); memcpy(text + size - 1000, "Needle", 6);
    text[size] = 0;
    assert(set_text(st, text));
    place(st, 0, 0);
    assert(tiku_editor_insert_checked(st, "first "));
    place(st, 5, 3);
    tiku_field_start(&st->field[0], "needle");
    tiku_field_start(&st->field[1], "pin");
    st->match_case = 0; st->whole_word = 0;
    tiku_editor_replace_all(st);
    assert(strstr(st->note, "2 replacements"));
    caret_at(st, &y, &x); assert(y == 5 && x == 3);
    int64_t last = (int64_t)((size - 1000) / 80);
    assert(!strncmp(line(st, last) + (size - 1000) % 80, "pin", 3));
    tiku_edit_pos_t at = pos_of(st, 5, 3);
    assert(undo(st) && selected(st, at, at) && tiku_editor_modified(st));
    assert(!strncmp(line(st, last) + (size - 1000) % 80, "Needle", 6));
    assert(undo(st) && !tiku_editor_modified(st));
    assert(!strncmp(line(st, 0), text, 79));
    assert(redo(st) && redo(st));
    caret_at(st, &y, &x); assert(y == 5 && x == 3);
    assert(!strncmp(line(st, 12) + 40, "pin", 3));
    /* A selection past the desk clipboard's limit is neither copied nor
     * cut, and says why. */
    clip_set(NULL, "kept");
    key(st, 'a', TIKU_MOD_CMD); key(st, 'x', TIKU_MOD_CMD);
    assert(!strcmp(clipboard, "kept") && tiku_editdoc_length(st->doc) > (1 << 24));
    assert(!strcmp(st->note, "Selection exceeds the desktop clipboard's 16 MB limit."));
    free(text);
    assert(set_text(st, "")); st->path[0] = 0;
}

/** A Find field redoes from Edit > Redo and from Cmd+Y, and Copy or Cut
 *  with nothing selected there leaves the desk clipboard as it was. */
static void find_field_edits(edit_state_t *st)
{
    tiku_editor_pick(st, st->id, CMD_FIND);
    assert(st->search_id && st->search_active && st->find_focus == 0);
    tiku_field_start(&st->field[0], "");
    tiku_field_input_init(&st->field_input[0]);
    tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN, .key = 'a', .text = "a"};
    tiku_editor_event_in(st, st->search_id, &e);
    e.key = 'b'; strcpy(e.text, "b");
    tiku_editor_event_in(st, st->search_id, &e);
    assert(!strcmp(st->field[0].text, "ab"));
    tiku_editor_pick(st, st->search_id, CMD_UNDO);
    assert(!strcmp(st->field[0].text, "a"));
    tiku_editor_pick(st, st->search_id, CMD_REDO);
    assert(!strcmp(st->field[0].text, "ab"));
    tiku_event_t chord = {.type = TIKU_EVENT_KEY_DOWN, .key = 'z', .modifiers = TIKU_MOD_CMD};
    tiku_editor_event_in(st, st->search_id, &chord);
    assert(!strcmp(st->field[0].text, "a"));
    chord.key = 'y';
    tiku_editor_event_in(st, st->search_id, &chord);
    assert(!strcmp(st->field[0].text, "ab"));
    clip_set(NULL, "kept");
    st->field[0].sel_a = st->field[0].sel_b = st->field[0].caret;
    chord.key = 'c';
    tiku_editor_event_in(st, st->search_id, &chord);
    assert(!strcmp(clipboard, "kept"));
    tiku_editor_pick(st, st->search_id, CMD_COPY);
    tiku_editor_pick(st, st->search_id, CMD_CUT);
    assert(!strcmp(clipboard, "kept") && !strcmp(st->field[0].text, "ab"));
    chord = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_ESCAPE};
    tiku_editor_event_in(st, st->search_id, &chord);
    assert(!st->search_id && !st->finding);
}

/** A press on the footer message dismisses it rather than scrolling the
 *  scrollbar it covers; the next press scrolls. */
static void footer_message_press(edit_state_t *st)
{
    char wide[2001]; memset(wide, 'w', 2000); wide[2000] = 0;
    assert(set_text(st, wide));
    tiku_codeview_scroll_to(st->view, 0, 0); tiku_editor_paint(st);
    snprintf(st->note, sizeof st->note, "Saved");
    tiku_event_t press = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = 208 + (st->w - 224) - 7, .y = st->h - EDIT_FOOT_H + 8};
    tiku_editor_event(st, &press);
    assert(!tiku_codeview_left(st->view) && !st->note[0]);
    tiku_editor_event(st, &press);
    assert(tiku_codeview_left(st->view) > 0);
    tiku_event_t up = press; up.type = TIKU_EVENT_POINTER_UP; tiku_editor_event(st, &up);
    tiku_codeview_scroll_to(st->view, 0, 0); assert(set_text(st, ""));
}

/** A NUL in a run's output is dropped, and the bytes after it are kept. */
static void run_output_nul(edit_state_t *st)
{
    int fds[2]; assert(!pipe(fds));
    assert(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
    static const char said[] = "a\0b\nc\n";
    assert(write(fds[1], said, sizeof said - 1) == (ssize_t)(sizeof said - 1));
    st->child_fd = fds[0]; st->running = 1; st->out_count = 0; st->out_partial[0] = 0;
    tiku_editor_tick(st, 0);
    assert(st->out_count == 2 && !strcmp(st->out[0], "ab") && !strcmp(st->out[1], "c"));
    st->running = 0; st->child_fd = -1; st->out_count = 0;
    close(fds[0]); close(fds[1]);
}

/** The file keeps its bytes: every line break, mixed ones and control
 *  characters load and save back exactly.  Return types the break the
 *  file uses most, ties going to the first; the clipboard gets LF, and a
 *  paste takes the document's break. */
static void kept_bytes(edit_state_t *st, const char *dir)
{
    char path[512]; snprintf(path, sizeof path, "%s/bytes.txt", dir);
    static const char mixed[] = "a\r\nb\nc\rd\x01" "e\x7f" "f\x1b[0m\n";
    put_bytes(path, mixed, sizeof mixed - 1);
    assert(tiku_editor_read_document(st, path));
    assert(tiku_editor_save(st)); expect_file(path, mixed);
    static const struct { const char *file, *newline; } breaks[] = {
        {"a\r\nb\r\nc\n", "\r\n"}, {"a\nb\r\n", "\n"}, {"a\rb", "\r"},
        {"none", "\n"}, {"a\r\nb\nc\nd\r\n", "\r\n"}, {"a\nb\r\nc\r\n", "\r\n"}};
    for (unsigned i = 0; i < sizeof breaks / sizeof breaks[0]; i++) {
        put_file(path, breaks[i].file); assert(tiku_editor_read_document(st, path));
        assert(!strcmp(st->newline, breaks[i].newline));
    }
    put_file(path, "one\r\ntwo\r\n"); assert(tiku_editor_read_document(st, path));
    place(st, 0, 3); key(st, TIKU_KEY_RETURN, 0);
    char *text = text_of(st); assert(!strcmp(text, "one\r\n\r\ntwo\r\n")); free(text);
    assert(tiku_editor_save(st)); expect_file(path, "one\r\n\r\ntwo\r\n");
    assert(tiku_codeview_select(st->view, 0, pos_of(st, 2, 3)) == TIKU_EDIT_OK);
    key(st, 'c', TIKU_MOD_CMD); assert(!strcmp(clipboard, "one\n\ntwo"));
    tiku_editdoc_saved(st->doc, tiku_editdoc_state(st->doc));
    key(st, 'n', TIKU_MOD_CMD); key(st, TIKU_KEY_RETURN, 0);
    text = text_of(st); assert(!strcmp(text, "\n") && !strcmp(st->newline, "\n")); free(text);
    assert(tiku_editor_read_document(st, path));
    clip_set(NULL, "x\ny\r\nz\rw");
    tiku_codeview_select_all(st->view); key(st, 'v', TIKU_MOD_CMD);
    text = text_of(st); assert(!strcmp(text, "x\r\ny\r\nz\r\nw")); free(text);
    tiku_editdoc_saved(st->doc, tiku_editdoc_state(st->doc));
    assert(!unlink(path));
    /* The name says the language, and Language > Plain Text overrides it
     * until Automatic goes back to the name. */
    snprintf(path, sizeof path, "%s/prog.bas", dir);
    put_file(path, "10 PRINT 1\n"); assert(tiku_editor_read_document(st, path));
    assert(tiku_codeview_language(st->view) == TIKU_SYNTAX_BASIC);
    tiku_editor_pick(st, st->id, CMD_LANG_TEXT);
    assert(tiku_codeview_language(st->view) == TIKU_SYNTAX_NONE);
    tiku_editor_pick(st, st->id, CMD_LANG_AUTO);
    assert(tiku_codeview_language(st->view) == TIKU_SYNTAX_BASIC);
    assert(!unlink(path));
}

/** The view's caret rules: typed keys join into one undo step, a key's
 *  UTF-8 text is typed, Up and Down keep an x across a tab, Left over a
 *  selection stops at its start, two clicks select a word, undo puts the
 *  selection back, and Replace All leaves the selection on its text. */
static void caret_rules(edit_state_t *st)
{
    int64_t y, x, px, py;
    assert(set_text(st, ""));
    key(st, 'a', 0); key(st, 'b', 0); key(st, 'c', 0);
    assert(!strcmp(line(st, 0), "abc"));
    key(st, 'z', TIKU_MOD_CMD); assert(!line_len(st, 0) && !tiku_editor_modified(st));
    tiku_event_t typed = {.type = TIKU_EVENT_KEY_DOWN, .text = "é"};
    tiku_editor_event(st, &typed); assert(!strcmp(line(st, 0), "é"));
    assert(set_text(st, "\tx\nabcdefgh"));
    place(st, 0, 1); key(st, TIKU_KEY_DOWN, 0);
    caret_at(st, &y, &x); assert(y == 1 && x == 4);
    assert(set_text(st, "alpha beta"));
    assert(tiku_codeview_select(st->view, 6, 10) == TIKU_EDIT_OK);
    key(st, TIKU_KEY_LEFT, 0); assert(selected(st, 6, 6));
    tiku_editor_paint(st);
    assert(tiku_codeview_point(st->view, 7, &px, &py) == TIKU_EDIT_OK);
    tiku_event_t press = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = (int)px + 2, .y = (int)py + 4};
    tiku_event_t release = press; release.type = TIKU_EVENT_POINTER_UP;
    tiku_editor_event(st, &press); tiku_editor_event(st, &release);
    tiku_editor_event(st, &press); tiku_editor_event(st, &release);
    assert(selected(st, 6, 10));
    key(st, 'X', 0); assert(!strcmp(line(st, 0), "alpha X"));
    key(st, 'z', TIKU_MOD_CMD);
    assert(!strcmp(line(st, 0), "alpha beta") && selected(st, 6, 10));
    assert(set_text(st, "alpha\nlast alpha"));
    assert(tiku_codeview_select(st->view, 6, 10) == TIKU_EDIT_OK);
    tiku_field_start(&st->field[0], "alpha");
    tiku_field_start(&st->field[1], "omega12");
    tiku_editor_replace_all(st);
    assert(!strcmp(line(st, 0), "omega12") && !strcmp(line(st, 1), "last omega12"));
    assert(selected(st, 8, 12));
    assert(set_text(st, ""));
}

/** Send @p text as one text-input commit, the way the transport does. */
static void commit_text(edit_state_t *st, const char *text)
{
    tiku_text_input_t in;
    tiku_event_t e;
    tiku_text_input_reset(&in);
    assert(tiku_text_input_set(&in, text, strlen(text)));
    while (tiku_text_input_next(&in, &e)) tiku_editor_event(st, &e);
}

/** Editor's own key, message and search rules, kept on the view. */
static void editor_rows(edit_state_t *st)
{
    int64_t y, x, before;
    char *text;
    assert(set_text(st, "alpha beta\n"));
    place(st, 0, 5);
    commit_text(st, "\tnaïve\nnew");
    assert(!strcmp(line(st, 0), "alpha\tnaïve") && !strcmp(line(st, 1), "new beta"));
    key(st, 'z', TIKU_MOD_CMD); assert(!strcmp(line(st, 0), "alpha beta"));
    /* Shift+Tab takes a level off; Option keeps Tab and Return plain. */
    place(st, 0, 0); key(st, TIKU_KEY_TAB, 0);
    assert(!strcmp(line(st, 0), "\talpha beta"));
    key(st, TIKU_KEY_TAB, TIKU_MOD_SHIFT);
    assert(!strcmp(line(st, 0), "alpha beta"));
    key(st, TIKU_KEY_TAB, TIKU_MOD_OPTION);
    key(st, TIKU_KEY_RETURN, TIKU_MOD_OPTION);
    assert(!strcmp(line(st, 0), "\t") && !strcmp(line(st, 1), "alpha beta"));
    assert(set_text(st, "ab\ncd"));
    place(st, 1, 0); key(st, TIKU_KEY_BACKSPACE, 0); key(st, TIKU_KEY_BACKSPACE, 0);
    assert(!strcmp(line(st, 0), "acd"));
    key(st, 'z', TIKU_MOD_CMD); assert(lines(st) == 2 && !strcmp(line(st, 0), "ab"));
    place(st, 0, 0); key(st, TIKU_KEY_BACKSPACE, 0); assert(!strcmp(line(st, 0), "ab"));
    tiku_codeview_select(st->view, tiku_editdoc_length(st->doc), tiku_editdoc_length(st->doc));
    key(st, TIKU_KEY_DELETE, 0); assert(!strcmp(line(st, 1), "cd"));
    /* A message clears on an edit, even one with nothing to remove, and
     * stays through navigation, Copy and Select All. */
    snprintf(st->note, sizeof st->note, "Saved");
    key(st, TIKU_KEY_LEFT, 0); key(st, 'a', TIKU_MOD_CMD); key(st, 'c', TIKU_MOD_CMD);
    assert(!strcmp(st->note, "Saved"));
    place(st, 0, 0); key(st, TIKU_KEY_BACKSPACE, 0); assert(!st->note[0]);
    text = malloc(200 * 4 + 1); assert(text); text[0] = 0;
    for (int i = 0; i < 200; i++) strcat(text, "abc\n");
    assert(set_text(st, text)); free(text);
    tiku_editor_paint(st);
    int64_t rows = tiku_codeview_rows_visible(st->view);
    place(st, 0, 2); key(st, TIKU_KEY_PAGE_DOWN, 0);
    caret_at(st, &y, &x); assert(y == rows && x == 2);
    key(st, TIKU_KEY_DOWN, TIKU_MOD_OPTION); key(st, TIKU_KEY_DOWN, TIKU_MOD_OPTION);
    key(st, TIKU_KEY_UP, TIKU_MOD_OPTION);
    caret_at(st, &y, &x); assert(y == rows + 1 && x == 2);
    /* A press in the gutter leaves the caret, and Select All shows its end. */
    tiku_rect_t box = tiku_editor_view_box(st);
    tiku_event_t press = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = box.x + 2, .y = box.y + 20};
    tiku_editor_event(st, &press);
    caret_at(st, &y, &x); assert(y == rows + 1 && x == 2);
    key(st, 'a', TIKU_MOD_CMD);
    assert(selected(st, 0, tiku_editdoc_length(st->doc)));
    assert(tiku_codeview_top(st->view) + rows > 200);
    /* An edit leaves the caret after it with nothing selected; one that
     * leaves the text as it was still marks it modified. */
    assert(set_text(st, "same"));
    assert(tiku_codeview_select(st->view, 0, 4) == TIKU_EDIT_OK);
    assert(tiku_editor_insert_checked(st, "same"));
    assert(selected(st, 4, 4) && tiku_editor_modified(st));
    key(st, 'z', TIKU_MOD_CMD); assert(!tiku_editor_modified(st));
    /* Undo and Redo clear the message even with nothing to step. */
    snprintf(st->note, sizeof st->note, "Saved");
    tiku_editor_pick(st, st->id, CMD_UNDO); assert(!st->note[0]);
    snprintf(st->note, sizeof st->note, "Saved");
    tiku_editor_pick(st, st->id, CMD_REDO); assert(!st->note[0]);
    /* Copy with nothing selected leaves the clipboard. */
    clip_set(NULL, "kept"); place(st, 0, 1); key(st, 'c', TIKU_MOD_CMD);
    assert(!strcmp(clipboard, "kept"));
    /* Find with an empty query asks for one; Find Previous takes the last
     * match before the selection, wrapping to the document's last. */
    assert(set_text(st, "one two one two one"));
    tiku_field_start(&st->field[0], "");
    tiku_editor_find_match(st, 0); assert(!strcmp(st->note, "Enter text to find."));
    tiku_field_start(&st->field[0], "one");
    assert(tiku_codeview_select(st->view, 8, 11) == TIKU_EDIT_OK);
    tiku_editor_find_match(st, 1);
    assert(selected(st, 0, 3) && !strcmp(st->note, "Match selected."));
    tiku_editor_find_match(st, 1);
    assert(selected(st, 16, 19) && !strcmp(st->note, "Search wrapped."));
    before = tiku_editdoc_length(st->doc);
    tiku_field_start(&st->field[0], "absent");
    tiku_editor_replace_all(st);
    assert(!strcmp(st->note, "No matches.") && tiku_editdoc_length(st->doc) == before);
    tiku_field_start(&st->field[0], "");
    tiku_field_start(&st->field[1], "");
    /* The vertical scrollbar's arrow moves the view a line, not the caret,
     * and the View menu's toggles reach the view. */
    text = malloc(200 * 4 + 1); assert(text); text[0] = 0;
    for (int i = 0; i < 200; i++) strcat(text, "abc\n");
    assert(set_text(st, text)); free(text);
    tiku_codeview_scroll_to(st->view, 0, 0); tiku_editor_paint(st);
    box = tiku_editor_view_box(st);
    press = (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = st->w - 8, .y = box.y + box.h - 7};
    tiku_editor_event(st, &press);
    press.type = TIKU_EVENT_POINTER_UP; tiku_editor_event(st, &press);
    assert(tiku_codeview_top(st->view) == 1 && selected(st, 0, 0));
    tiku_editor_pick(st, st->id, CMD_INVISIBLES);
    assert(tiku_codeview_options(st->view) & TIKU_CODEVIEW_INVISIBLES);
    tiku_editor_pick(st, st->id, CMD_NUMBERS);
    assert(!(tiku_codeview_options(st->view) & TIKU_CODEVIEW_LINE_NUMBERS));
    tiku_editor_pick(st, st->id, CMD_INVISIBLES); tiku_editor_pick(st, st->id, CMD_NUMBERS);
    assert(tiku_codeview_options(st->view) ==
           (TIKU_CODEVIEW_LINE_NUMBERS | TIKU_CODEVIEW_BRACES |
            TIKU_CODEVIEW_FOLDS));
    assert(set_text(st, ""));
}

/** @brief Capture the title without opening a real desktop window. */
static uint32_t window_open(void *ctx, const char *title, int w, int h)
{
    (void)ctx; (void)w; (void)h;
    snprintf(title_seen, sizeof title_seen, "%s", title);
    return ++next_window;
}

static uint32_t counted_window; /* the window whose frames are counted */
static int frames_counted;      /* how many frames it has been sent    */

/** @brief The headless host accepts the frame, counting those sent to
 *         the window a test watches. */
static int window_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    (void)ctx; (void)px; (void)w; (void)h;
    if (counted_window && id == counted_window) frames_counted++;
    return 0;
}

/** @brief The headless host accepts the application's menus. */
static int window_menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    (void)ctx; (void)id; (void)set;
    return 0;
}

/** @brief The headless host keeps the pointer shape asked for. */
static int send_message(void *ctx, const struct tiku_msg *m)
{
    int32_t shape = -1;
    (void)ctx;
    if (tiku_msg_what(m) == TIKU_MSG_CURSOR &&
        tiku_msg_find_int32(m, "shape", 0, &shape)) {
        shape_asked = shape;
        shapes_sent++;
    }
    return 1;
}

/** The Text menu: its rows and shortcuts, and its commands by key and by
 *  menu through the view's line commands; Return keeps the indentation
 *  and a C closer goes back to its opener's. */
static void text_menu(edit_state_t *st)
{
    const tiku_menu_list_t *text = &st->last_menus.menu[4];
    int64_t y, x;
    st->lang = TIKU_SYNTAX_C; tiku_codeview_set_language(st->view, st->lang);
    assert(set_text(st, "int f() {"));
    place(st, 0, 9); key(st, TIKU_KEY_RETURN, 0);
    assert(lines(st) == 2 && !strcmp(line(st, 1), "\t"));
    key(st, '}', 0); assert(!strcmp(line(st, 1), "}"));
    tiku_editor_publish(st);
    assert(st->last_menus.nmenu == 6 && !strcmp(text->title, "Text") &&
           text->nitem == 20);
    assert(text->item[2].command == CMD_COMMENT && text->item[2].sc == '/' &&
           text->item[2].enabled && text->item[3].sc == 'd');
    assert(text->item[6].command == CMD_DELETE_LINES &&
           text->item[6].sc == 'k' &&
           text->item[6].mods == (TIKU_MOD_CMD | TIKU_MOD_SHIFT));
    assert(set_text(st, "a();\nb();"));
    place(st, 0, 0); key(st, '/', TIKU_MOD_CMD);
    assert(!strcmp(line(st, 0), "// a();"));
    snprintf(st->note, sizeof st->note, "Saved");
    tiku_editor_pick(st, st->id, CMD_COMMENT);
    assert(!strcmp(line(st, 0), "a();") && !st->note[0]);
    key(st, 'd', TIKU_MOD_CMD);
    assert(lines(st) == 3 && !strcmp(line(st, 1), "a();"));
    key(st, TIKU_KEY_DOWN, TIKU_MOD_CMD | TIKU_MOD_SHIFT);
    assert(!strcmp(line(st, 1), "b();") && !strcmp(line(st, 2), "a();"));
    tiku_editor_pick(st, st->id, CMD_LINES_UP);
    assert(!strcmp(line(st, 1), "a();") && !strcmp(line(st, 2), "b();"));
    key(st, 'k', TIKU_MOD_CMD | TIKU_MOD_SHIFT);
    assert(lines(st) == 2 && !strcmp(line(st, 1), "b();"));
    tiku_editor_pick(st, st->id, CMD_INDENT);
    assert(!strcmp(line(st, 1), "\tb();"));
    tiku_editor_pick(st, st->id, CMD_UNINDENT);
    assert(!strcmp(line(st, 1), "b();"));
    assert(set_text(st, "f(alpha)"));
    place(st, 0, 2); key(st, 'm', TIKU_MOD_CMD);
    caret_at(st, &y, &x); assert(x == 8);
    tiku_editor_pick(st, st->id, CMD_SELECT_BRACKET);
    assert(selected(st, 8, 1));
    place(st, 0, 7); key(st, TIKU_KEY_BACKSPACE, TIKU_MOD_CMD);
    assert(!strcmp(line(st, 0), "f()"));
    st->lang = TIKU_SYNTAX_NONE; tiku_codeview_set_language(st->view, st->lang);
    tiku_editor_publish(st); assert(!text->item[2].enabled);
    key(st, '/', TIKU_MOD_CMD); assert(!strcmp(line(st, 0), "f()"));
}

/** View > Wrap Lines breaks lines at the page's width: marked while on,
 *  nothing to scroll across, End stopping at a row's end before the
 *  line's, and the window's tick counting the rows of a long document. */
static void wrap_lines(edit_state_t *st)
{
    const tiku_menu_list_t *view = &st->last_menus.menu[3];
    size_t big = 70000 * 30;
    char *text = malloc(big + 1);
    int64_t total, shown, at, y, x;
    int i;
    assert(text);
    for (i = 0; i < 400; i++) memcpy(text + 5 * i, "word ", 5);
    text[2000] = 0;
    assert(set_text(st, text));
    tiku_editor_publish(st); tiku_editor_paint(st);
    assert(view->nitem == 13 && view->item[4].command == CMD_WRAP &&
           !strcmp(view->item[4].label, "Wrap Lines") &&
           view->item[4].enabled && !view->item[4].marked);
    assert(tiku_scroll_max(&st->horizontal) > 0);
    tiku_editor_pick(st, st->id, CMD_WRAP);
    assert(view->item[4].marked &&
           (tiku_codeview_options(st->view) & TIKU_CODEVIEW_WRAP));
    tiku_codeview_extent(st->view, 0, &total, &shown, &at);
    assert(tiku_scroll_max(&st->horizontal) == 0 && total > 10);
    place(st, 0, 0); key(st, TIKU_KEY_END, 0);
    caret_at(st, &y, &x); assert(y == 0 && x > 0 && x < 2000);
    key(st, TIKU_KEY_END, 0); caret_at(st, &y, &x); assert(x == 2000);
    for (i = 0; i < 70000; i++)
        memcpy(text + 30 * i, "word word word word word word\n", 30);
    text[big] = 0;
    assert(set_text(st, text));
    tiku_editor_paint(st);
    assert(tiku_codeview_wrapping(st->view));
    for (i = 0; i < 50 && tiku_codeview_wrapping(st->view); i++)
        tiku_editor_tick(st, 0);
    assert(!tiku_codeview_wrapping(st->view));
    for (i = 0; i < 400; i++) memcpy(text + 5 * i, "word ", 5);
    text[2000] = 0;
    assert(set_text(st, text));
    tiku_editor_pick(st, st->id, CMD_WRAP);
    assert(!view->item[4].marked &&
           !(tiku_codeview_options(st->view) & TIKU_CODEVIEW_WRAP) &&
           tiku_scroll_max(&st->horizontal) > 0);
    free(text);
    assert(set_text(st, ""));
}

/** The surface row @p y of pane @p pane's caret, or -1 when it is not
 *  drawn there. */
static int64_t caret_row_y(edit_state_t *st, int pane)
{
    tiku_edit_pos_t anchor, caret;
    int64_t x, y;
    assert(tiku_codeview_selection(st->panes[pane], &anchor, &caret) == TIKU_EDIT_OK);
    if (tiku_codeview_point(st->panes[pane], caret, &x, &y) != TIKU_EDIT_OK) return -1;
    return y;
}

/** View > Split Pane: two panes of the one document, each with its own
 *  caret, scroll and scrollbar; a press gives a pane the keys, the
 *  divider shares the height, the wheel scrolls the pane under it, and
 *  taking the item again keeps the pane with the keys. */
static void split_panes(edit_state_t *st)
{
    const tiku_menu_list_t *view = &st->last_menus.menu[3];
    char *text = malloc(200 * 6 + 1);
    tiku_rect_t upper, lower, divider;
    tiku_edit_pos_t anchor, caret;
    int64_t y, x;
    assert(text); text[0] = 0;
    for (int i = 0; i < 200; i++) strcat(text, "line\n");
    assert(set_text(st, text)); free(text);
    place(st, 50, 2); tiku_editor_reveal(st);
    tiku_editor_publish(st);
    assert(view->item[5].command == CMD_SPLIT &&
           !strcmp(view->item[5].label, "Split Pane") &&
           view->item[5].enabled && !view->item[5].marked && !st->panes[1]);
    tiku_editor_pick(st, st->id, CMD_SPLIT);
    assert(st->panes[1] && st->view == st->panes[0] && view->item[5].marked);
    upper = tiku_editor_pane_box(st, 0); lower = tiku_editor_pane_box(st, 1);
    divider = tiku_editor_divider(st);
    assert(divider.y == upper.y + upper.h && divider.h == EDIT_SPLIT_GAP &&
           lower.y == divider.y + divider.h &&
           lower.y + lower.h == tiku_editor_view_box(st).y + tiku_editor_view_box(st).h &&
           upper.h - lower.h <= 1 && lower.h - upper.h <= 1);
    assert(tiku_codeview_selection(st->panes[1], &anchor, &caret) == TIKU_EDIT_OK &&
           caret == pos_of(st, 50, 2) && caret_row_y(st, 1) >= lower.y &&
           caret_row_y(st, 1) < lower.y + lower.h);
    assert(tiku_codeview_focused(st->panes[0]) && !tiku_codeview_focused(st->panes[1]));
    /* Typing goes to the pane with the keys and shows in both; the other
     * pane's caret stays on its text. */
    place(st, 0, 0);
    commit_text(st, "top ");
    assert(!strcmp(line(st, 0), "top line"));
    assert(tiku_codeview_selection(st->panes[1], &anchor, &caret) == TIKU_EDIT_OK &&
           caret == pos_of(st, 50, 2));
    /* A press in the lower pane gives it the keys. */
    tiku_event_t press = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = 80, .y = lower.y + 5, .time_us = 50000000};
    tiku_editor_event(st, &press);
    press.type = TIKU_EVENT_POINTER_UP; tiku_editor_event(st, &press);
    assert(st->pane == 1 && st->view == st->panes[1] &&
           tiku_codeview_focused(st->panes[1]) && !tiku_codeview_focused(st->panes[0]));
    caret_at(st, &y, &x);
    assert(y == tiku_codeview_top(st->panes[1]) && y > 0);
    /* Undo in either pane steps the one history; each caret stays on its
     * text. */
    int64_t lower_line = y, lower_col = x;
    key(st, 'z', TIKU_MOD_CMD);
    assert(!strcmp(line(st, 0), "line"));
    caret_at(st, &y, &x); assert(y == lower_line && x == lower_col);
    key(st, 'z', TIKU_MOD_CMD | TIKU_MOD_SHIFT);
    assert(!strcmp(line(st, 0), "top line"));
    key(st, TIKU_KEY_HOME, TIKU_MOD_CMD);
    caret_at(st, &y, &x); assert(y == 0 && x == 0);
    assert(tiku_codeview_selection(st->panes[0], &anchor, &caret) == TIKU_EDIT_OK &&
           caret == 4);
    /* The divider shares the height; each pane keeps its least. */
    press = (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = 200, .y = divider.y + 2};
    tiku_editor_event(st, &press);
    press.type = TIKU_EVENT_POINTER_MOVE; press.y = upper.y + 60 + 2;
    tiku_editor_event(st, &press);
    press.type = TIKU_EVENT_POINTER_UP; tiku_editor_event(st, &press);
    assert(tiku_editor_pane_box(st, 0).h == 60 && st->pane == 1);
    press = (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = 200, .y = tiku_editor_divider(st).y + 1};
    tiku_editor_event(st, &press);
    press.type = TIKU_EVENT_POINTER_MOVE; press.y = 0;
    tiku_editor_event(st, &press);
    press.type = TIKU_EVENT_POINTER_UP; tiku_editor_event(st, &press);
    assert(tiku_editor_pane_box(st, 0).h == EDIT_PANE_MIN);
    press = (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = 200, .y = tiku_editor_divider(st).y + 1};
    tiku_editor_event(st, &press);
    press.type = TIKU_EVENT_POINTER_MOVE; press.y = st->h;
    tiku_editor_event(st, &press);
    press.type = TIKU_EVENT_POINTER_UP; tiku_editor_event(st, &press);
    assert(tiku_editor_pane_box(st, 1).h == EDIT_PANE_MIN);
    st->split_at = 60;
    /* The wheel and a scrollbar arrow move the pane under them alone. */
    tiku_codeview_scroll_to(st->panes[0], 0, 0);
    tiku_codeview_scroll_to(st->panes[1], 0, 0);
    tiku_event_t wheel = {.type = TIKU_EVENT_WHEEL, .dy = 3,
        .x = 100, .y = tiku_editor_pane_box(st, 1).y + 10};
    tiku_editor_event(st, &wheel);
    assert(tiku_codeview_top(st->panes[1]) == 3 && tiku_codeview_top(st->panes[0]) == 0);
    upper = tiku_editor_pane_box(st, 0);
    press = (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = st->w - 8, .y = upper.y + upper.h - 7};
    tiku_editor_event(st, &press);
    press.type = TIKU_EVENT_POINTER_UP; tiku_editor_event(st, &press);
    assert(tiku_codeview_top(st->panes[0]) == 1 && tiku_codeview_top(st->panes[1]) == 3 &&
           st->pane == 1);
    /* Taken again, the pane with the keys stays, with its place. */
    place(st, 5, 1);
    tiku_editor_pick(st, st->id, CMD_SPLIT);
    assert(!st->panes[1] && st->view == st->panes[0] && st->pane == 0 &&
           !view->item[5].marked && tiku_codeview_top(st->view) == 3 &&
           tiku_codeview_focused(st->view));
    caret_at(st, &y, &x); assert(y == 5 && x == 1);
    assert(tiku_editor_pane_box(st, 0).h == tiku_editor_view_box(st).h);
    assert(set_text(st, ""));
}

/** A press through Editor's own event path at position @p pos, with
 *  @p mods; @p type says down, move or up. */
static void press_at(edit_state_t *st, tiku_event_type_t type,
                     tiku_edit_pos_t pos, unsigned mods)
{
    int64_t x = 0, y = 0;
    assert(tiku_codeview_point(st->view, pos, &x, &y) == TIKU_EDIT_OK);
    tiku_event_t e = {.type = type, .button = 1, .x = (int)x + 1,
        .y = (int)y + tiku_codeview_row_height(st->view) / 2,
        .modifiers = mods};
    tiku_editor_event(st, &e);
}

/** Text > Carets to Line Ends (Cmd+Shift+L) puts a caret at each line's
 *  end; a key types at every caret as one undo step; Escape keeps the
 *  main caret instead of closing; a Command press adds a caret and an
 *  Option drag selects a rectangle. */
static void carets(edit_state_t *st)
{
    const tiku_menu_list_t *text = &st->last_menus.menu[4];
    tiku_edit_selection_t s[8];
    size_t main = 9;
    assert(set_text(st, "one\ntwo\nthree\n"));
    tiku_editor_publish(st); tiku_editor_paint(st);
    assert(text->nitem == 20 && text->item[13].command == CMD_LINE_ENDS &&
           text->item[13].sc == 'l' &&
           text->item[13].mods == (TIKU_MOD_CMD | TIKU_MOD_SHIFT) &&
           !strcmp(text->item[13].label, "Carets to Line Ends"));
    assert(tiku_codeview_select(st->view, 1, 14) == TIKU_EDIT_OK);
    key(st, 'l', TIKU_MOD_CMD | TIKU_MOD_SHIFT);
    assert(tiku_codeview_selections(st->view, s, 8, &main) == 3 &&
           main == 2 && s[0].caret == 3 && s[1].caret == 7 &&
           s[2].caret == 13);
    key(st, '!', 0);
    assert(!strcmp(line(st, 0), "one!") && !strcmp(line(st, 1), "two!") &&
           !strcmp(line(st, 2), "three!") && tiku_editor_modified(st));
    key(st, TIKU_KEY_ESCAPE, 0);
    assert(tiku_codeview_selections(st->view, NULL, 0, NULL) == 1 &&
           !st->ask.open && st->pending == 0);
    assert(undo(st) && !strcmp(line(st, 0), "one") &&
           !strcmp(line(st, 2), "three") &&
           tiku_codeview_selections(st->view, NULL, 0, NULL) == 3);
    assert(tiku_codeview_select(st->view, 0, 7) == TIKU_EDIT_OK);
    tiku_editor_pick(st, st->id, CMD_LINE_ENDS);
    assert(tiku_codeview_selections(st->view, s, 8, &main) == 2 &&
           s[0].caret == 3 && s[1].caret == 7);
    place(st, 0, 0);
    press_at(st, TIKU_EVENT_POINTER_DOWN, 5, TIKU_MOD_CMD);
    press_at(st, TIKU_EVENT_POINTER_UP, 5, TIKU_MOD_CMD);
    assert(tiku_codeview_selections(st->view, s, 8, &main) == 2 &&
           main == 1 && s[0].caret == 0 && s[1].caret == 5);
    press_at(st, TIKU_EVENT_POINTER_DOWN, 1, TIKU_MOD_OPTION);
    press_at(st, TIKU_EVENT_POINTER_MOVE, 10, TIKU_MOD_OPTION);
    press_at(st, TIKU_EVENT_POINTER_UP, 10, TIKU_MOD_OPTION);
    assert(tiku_codeview_selections(st->view, s, 8, &main) == 3 &&
           main == 2 && s[0].anchor == 1 && s[0].caret == 2 &&
           s[1].anchor == 5 && s[2].caret == 10);
    key(st, 'X', 0);
    assert(!strcmp(line(st, 0), "oXe") && !strcmp(line(st, 1), "tXo") &&
           !strcmp(line(st, 2), "tXree"));
    assert(undo(st) && !strcmp(line(st, 1), "two"));
    assert(set_text(st, ""));
}

/** View > Highlight Caret Line and Block Caret, off by default, reach
 *  each pane, and the caret's line is filled on the window. */
static void caret_looks(edit_state_t *st)
{
    const tiku_menu_list_t *view = &st->last_menus.menu[3];
    const unsigned both = TIKU_CODEVIEW_CARET_LINE | TIKU_CODEVIEW_BLOCK_CARET;
    tiku_rect_t box = tiku_editor_view_box(st);
    int64_t x, y;
    assert(set_text(st, "one\ntwo\nthree"));
    tiku_editor_publish(st); tiku_editor_paint(st);
    assert(view->nitem == 13 && view->item[6].command == CMD_CARET_LINE &&
           !strcmp(view->item[6].label, "Highlight Caret Line") &&
           view->item[6].enabled && !view->item[6].marked &&
           view->item[7].command == CMD_BLOCK_CARET &&
           !strcmp(view->item[7].label, "Block Caret") &&
           view->item[7].enabled && !view->item[7].marked);
    assert(!(tiku_codeview_options(st->view) & both));
    place(st, 1, 1);
    tiku_editor_pick(st, st->id, CMD_CARET_LINE);
    assert(view->item[6].marked && !view->item[7].marked &&
           (tiku_codeview_options(st->view) & both) ==
           TIKU_CODEVIEW_CARET_LINE);
    assert(tiku_codeview_point(st->view, 5, &x, &y) == TIKU_EDIT_OK);
    assert((tiku_peek(st->surface, box.x + box.w - 20, (int)y + 2) &
            0xffffffu) == (TIKU_C_NOTE & 0xffffffu));
    assert((tiku_peek(st->surface, box.x + box.w - 20, (int)y - 4) &
            0xffffffu) == (TIKU_C_DOC & 0xffffffu));
    tiku_editor_pick(st, st->id, CMD_BLOCK_CARET);
    assert(view->item[7].marked &&
           (tiku_codeview_options(st->view) & both) == both);
    assert((tiku_peek(st->surface, (int)x + 1, (int)y + 1) & 0xffffffu) ==
           (TIKU_C_TEXT & 0xffffffu));
    tiku_editor_pick(st, st->id, CMD_SPLIT);
    assert(st->panes[1] &&
           (tiku_codeview_options(st->panes[0]) & both) == both &&
           (tiku_codeview_options(st->panes[1]) & both) == both);
    tiku_editor_pick(st, st->id, CMD_CARET_LINE);
    tiku_editor_pick(st, st->id, CMD_BLOCK_CARET);
    assert(!view->item[6].marked && !view->item[7].marked &&
           !(tiku_codeview_options(st->panes[0]) & both) &&
           !(tiku_codeview_options(st->panes[1]) & both));
    tiku_editor_pick(st, st->id, CMD_SPLIT);
    assert(!st->panes[1]);
    assert(set_text(st, ""));
}

/** A pointer event over the middle of the character at @p pos. */
static void point_at(edit_state_t *st, tiku_event_type_t type,
                     tiku_edit_pos_t pos, unsigned mods)
{
    int64_t x = 0, y = 0;
    assert(tiku_codeview_point(st->view, pos, &x, &y) == TIKU_EDIT_OK);
    tiku_event_t e = {.type = type,
        .button = type == TIKU_EVENT_POINTER_MOVE ? 0 : 1,
        .x = (int)x + tiku_font_mono_cell(0) / 2,
        .y = (int)y + tiku_codeview_row_height(st->view) / 2,
        .modifiers = mods};
    tiku_editor_event(st, &e);
}

/** Coloured BASIC shows reserved words bold, remarks italic and jump
 *  targets underlined; with Command held a target under the pointer is
 *  live, the pointer a hand, and a click on it goes to the line it
 *  names, a number or a label, or says there is none. */
static void jumps(edit_state_t *st)
{
    const char *program = "10 GOTO 30\n20 GOTO 99\n30 GOSUB done\n40 END\n"
                          "done: RETURN\n";
    tiku_edit_pos_t anchor, caret;
    assert(set_text(st, program));
    tiku_editor_pick(st, st->id, CMD_SYNTAX);
    assert(st->lang == TIKU_SYNTAX_BASIC);
    assert(tiku_codeview_ink_style(st->view, TIKU_INK_WORD) ==
           TIKU_CODEVIEW_BOLD &&
           tiku_codeview_ink_style(st->view, TIKU_INK_REMARK) ==
           TIKU_CODEVIEW_ITALIC &&
           tiku_codeview_ink_style(st->view, TIKU_INK_LINK) ==
           (TIKU_CODEVIEW_UNDERLINE | TIKU_CODEVIEW_HOTSPOT));
    place(st, 0, 0);
    tiku_editor_paint(st);
    point_at(st, TIKU_EVENT_POINTER_MOVE, 8, 0);
    assert(!tiku_codeview_hotspot(st->view, NULL) &&
           st->cursor == TIKU_CURSOR_ARROW);
    point_at(st, TIKU_EVENT_POINTER_MOVE, 8, TIKU_MOD_CMD);
    assert(tiku_codeview_hotspot(st->view, NULL) &&
           st->cursor == TIKU_CURSOR_HAND && shape_asked == TIKU_CURSOR_HAND);
    point_at(st, TIKU_EVENT_POINTER_DOWN, 8, TIKU_MOD_CMD);
    point_at(st, TIKU_EVENT_POINTER_UP, 8, TIKU_MOD_CMD);
    assert(tiku_codeview_selection(st->view, &anchor, &caret) == TIKU_EDIT_OK &&
           anchor == 22 && caret == 22);           /* "30 GOSUB done" */
    point_at(st, TIKU_EVENT_POINTER_DOWN, 32, TIKU_MOD_CMD);
    point_at(st, TIKU_EVENT_POINTER_UP, 32, TIKU_MOD_CMD);
    assert(tiku_codeview_selection(st->view, &anchor, &caret) == TIKU_EDIT_OK &&
           anchor == 43 && caret == 43);           /* "done: RETURN" */
    point_at(st, TIKU_EVENT_POINTER_DOWN, 19, TIKU_MOD_CMD);
    point_at(st, TIKU_EVENT_POINTER_UP, 19, TIKU_MOD_CMD);
    assert(tiku_codeview_selection(st->view, &anchor, &caret) == TIKU_EDIT_OK &&
           caret == 43 && !strcmp(st->note, "No line 99 in this program."));
    point_at(st, TIKU_EVENT_POINTER_DOWN, 8, 0);
    point_at(st, TIKU_EVENT_POINTER_UP, 8, 0);
    assert(tiku_codeview_selection(st->view, &anchor, &caret) == TIKU_EDIT_OK &&
           caret >= 8 && caret <= 9);              /* a plain click edits */
    point_at(st, TIKU_EVENT_POINTER_MOVE, 8, 0);
    assert(st->cursor == TIKU_CURSOR_ARROW);
    int sent = shapes_sent;
    tiku_event_t cmd = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_CMD};
    tiku_editor_event(st, &cmd);
    assert(tiku_codeview_hotspot(st->view, NULL) &&
           st->cursor == TIKU_CURSOR_HAND && shapes_sent == sent + 1);
    assert(tiku_codeview_selection(st->view, &anchor, &caret) == TIKU_EDIT_OK &&
           caret >= 8 && caret <= 9);              /* nothing moved */
    cmd.type = TIKU_EVENT_KEY_UP; cmd.modifiers = TIKU_MOD_CMD;
    tiku_editor_event(st, &cmd);
    assert(!tiku_codeview_hotspot(st->view, NULL) &&
           st->cursor == TIKU_CURSOR_ARROW &&
           shape_asked == TIKU_CURSOR_ARROW);
    tiku_editor_pick(st, st->id, CMD_SYNTAX);
    assert(st->lang == TIKU_SYNTAX_NONE);
    assert(set_text(st, ""));
}

/** The Text menu's Fold and Unfold fold the region the caret is in and
 *  unfold its line, by menu and by Cmd+Shift+[ and ]; Fold All and Unfold
 *  All take every region; a press on the fold column's box folds. */
static void folds(edit_state_t *st)
{
    const tiku_menu_list_t *text = &st->last_menus.menu[4];
    tiku_rect_t box;
    int64_t y, x;
    st->lang = TIKU_SYNTAX_C; tiku_codeview_set_language(st->view, st->lang);
    assert(set_text(st, "int f(void) {\n\ta();\n\tb();\n}\nint g;\n"));
    tiku_editor_publish(st); tiku_editor_paint(st);
    assert(text->item[9].command == CMD_FOLD && text->item[9].sc == '[' &&
           text->item[9].mods == (TIKU_MOD_CMD | TIKU_MOD_SHIFT) &&
           !strcmp(text->item[9].label, "Fold") &&
           text->item[10].command == CMD_UNFOLD && text->item[10].sc == ']' &&
           text->item[11].command == CMD_FOLD_ALL &&
           text->item[12].command == CMD_UNFOLD_ALL);
    place(st, 1, 1); key(st, '[', TIKU_MOD_CMD | TIKU_MOD_SHIFT);
    assert(tiku_codeview_folded(st->view, 0) &&
           !tiku_codeview_shown(st->view, 1) && !tiku_codeview_shown(st->view, 2));
    caret_at(st, &y, &x); assert(y == 0);
    key(st, ']', TIKU_MOD_CMD | TIKU_MOD_SHIFT);
    assert(!tiku_codeview_folded(st->view, 0) && tiku_codeview_shown(st->view, 1));
    key(st, '[', TIKU_MOD_CMD);
    assert(!tiku_codeview_folded(st->view, 0));
    tiku_editor_pick(st, st->id, CMD_FOLD_ALL);
    assert(tiku_codeview_folded(st->view, 0));
    tiku_editor_pick(st, st->id, CMD_UNFOLD_ALL);
    assert(!tiku_codeview_folded(st->view, 0) && tiku_codeview_shown(st->view, 2));
    /* The fold column ends 4 pixels before the text starts. */
    box = tiku_editor_pane_box(st, 0);
    assert(tiku_codeview_point(st->view, 0, &x, NULL) == TIKU_EDIT_OK);
    tiku_event_t press = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = (int)x - 10, .y = box.y + 3 + tiku_codeview_row_height(st->view) / 2};
    tiku_editor_event(st, &press);
    press.type = TIKU_EVENT_POINTER_UP; tiku_editor_event(st, &press);
    assert(tiku_codeview_folded(st->view, 0));
    tiku_editor_pick(st, st->id, CMD_UNFOLD_ALL);
    st->lang = TIKU_SYNTAX_NONE; tiku_codeview_set_language(st->view, st->lang);
    assert(set_text(st, ""));
}

/** A non-main selection can be copied and cut even when the main one is
 *  an empty caret. The menu and keyboard must agree. */
static void secondary_clipboard(edit_state_t *st)
{
    const tiku_edit_selection_t selections[] = {{0, 3}, {7, 7}};
    assert(set_text(st, "one two"));
    assert(tiku_codeview_select_many(st->view, selections, 2, 1) ==
           TIKU_EDIT_OK);
    tiku_editor_publish(st);
    assert(st->last_menus.menu[1].item[2].enabled &&
           st->last_menus.menu[1].item[3].enabled);
    key(st, 'c', TIKU_MOD_CMD);
    assert(clipboard && !strcmp(clipboard, "one"));
    tiku_editor_pick(st, st->id, CMD_CUT);
    assert(!strcmp(line(st, 0), " two"));
    assert(undo(st) && !strcmp(line(st, 0), "one two") &&
           tiku_codeview_selections(st->view, NULL, 0, NULL) == 2);
    assert(set_text(st, ""));
}

/** JSON highlighting follows the path in both split panes. */
static void json_provider(edit_state_t *st)
{
    char old_path[sizeof st->path];
    tiku_syntax_lang_t old_lang = st->lang;
    strcpy(old_path, st->path);
    assert(set_text(st, "{\"answer\":42}"));
    strcpy(st->path, "/tmp/example.JSON"); st->lang = TIKU_SYNTAX_NONE;
    if (!st->panes[1]) assert(!tiku_editor_split(st));
    tiku_editor_view_sync(st);
    for (int i = 0; i < 2; i++) {
        size_t n; int number = 0;
        const tiku_span_t *runs = tiku_cv_style_line(st->panes[i], 0, 0, &n);
        assert(runs &&
               tiku_codeview_lexer_status(st->panes[i]) == TIKU_EDIT_OK);
        for (size_t j = 0; j < n; j++) number |= runs[j].ink == TIKU_INK_NUM;
        assert(number);
    }
    strcpy(st->path, "/tmp/example.txt");
    tiku_editor_view_sync(st);
    for (int i = 0; i < 2; i++) {
        size_t n;
        assert(!tiku_cv_style_line(st->panes[i], 0, 0, &n));
    }
    assert(!tiku_editor_split(st));
    strcpy(st->path, old_path); st->lang = old_lang;
    tiku_editor_view_sync(st);
}

/** Find's Regular expression box, Replace All with capture groups and
 *  its undo, and the View menu's guides, ruler and zoom reaching the
 *  view. */
static void advanced_controls(edit_state_t *st)
{
    assert(set_text(st, "a=12\nb=34\n"));
    tiku_editor_pick(st, st->id, CMD_FIND);
    tiku_event_t toggle = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                           .x = 95, .y = 111};
    (void)tiku_editor_event_in(st, st->search_id, &toggle);
    assert(st->regex);
    tiku_field_start(&st->field[0], "([a-z])=([0-9]+)");
    tiku_field_start(&st->field[1], "$2:$1");
    tiku_editor_pick(st, st->id, CMD_REPLACE_ALL);
    assert(!strcmp(line(st, 0), "12:a") && !strcmp(line(st, 1), "34:b"));
    assert(tiku_codeview_undo(st->view) == TIKU_EDIT_OK);
    assert(!strcmp(line(st, 0), "a=12"));
    (void)tiku_editor_event_in(st, st->search_id, &toggle);
    assert(!st->regex);
    tiku_editor_search_close(st);
    tiku_editor_pick(st, st->id, CMD_INDENT_GUIDES);
    tiku_editor_pick(st, st->id, CMD_RULER);
    assert(tiku_codeview_options(st->view) & TIKU_CODEVIEW_INDENT_GUIDES);
    assert(tiku_codeview_ruler(st->view) == 80);
    tiku_editor_pick(st, st->id, CMD_ZOOM_IN);
    assert(tiku_codeview_zoom(st->view) == 1);
    tiku_editor_pick(st, st->id, CMD_ZOOM_RESET);
    assert(tiku_codeview_zoom(st->view) == 0);
    tiku_editor_pick(st, st->id, CMD_INDENT_GUIDES);
    tiku_editor_pick(st, st->id, CMD_RULER);
}

/** Send key @p code to the Preferences window. */
static void pref_key(edit_state_t *st, unsigned code)
{
    tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN, .key = code};
    tiku_editor_event_in(st, st->prefs_id, &e);
}

/** Preferences: the steppers by press and by key, Escape writing
 *  nothing, Save writing and applying, a split pane and a new window
 *  taking the saved values, malformed files read as the defaults, and a
 *  folder refused as the file to save. */
static void preferences_window(edit_state_t *st, const char *dir)
{
    char path[512];
    tiku_codeview_style_t style;
    void *fresh = NULL;
    int font;

    snprintf(path, sizeof path, "%s/preferences", dir);
    assert(!setenv("TIKU_EDITOR_PREFERENCES", path, 1));
    tiku_editor_pick(st, st->id, CMD_PREFS);
    assert(st->prefs_id);
    tiku_event_t plus = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                         .x = 510, .y = 64};
    tiku_editor_event_in(st, st->prefs_id, &plus);
    assert(st->pref_draft.tab == 5);
    pref_key(st, TIKU_KEY_LEFT);
    assert(st->pref_draft.tab == 4);
    pref_key(st, TIKU_KEY_RIGHT);
    assert(st->pref_draft.tab == 5 && st->view->tab_width == 4);
    pref_key(st, TIKU_KEY_ESCAPE);
    assert(!st->prefs_id && st->view->tab_width == 4);
    assert(access(path, F_OK));             /* Cancel writes nothing */
    tiku_editor_preferences_open(st);
    st->pref_draft.tab = 8;
    st->pref_draft.indent = 2;
    st->pref_draft.column = 100;
    st->pref_draft.zoom = 1;
    st->pref_draft.colors = 2;
    st->pref_draft.guides = 1;
    st->pref_focus = 14;
    pref_key(st, TIKU_KEY_LEFT);
    assert(st->prefs_id);                   /* Left on Save does not save */
    pref_key(st, TIKU_KEY_RETURN);
    assert(!st->prefs_id && st->view->tab_width == 8);
    assert(st->preferences.column == 100 && tiku_codeview_zoom(st->view) == 1);
    assert(!tiku_editor_split(st));
    assert(st->panes[1]->tab_width == 8 &&
           tiku_codeview_zoom(st->panes[1]) == 1);
    assert(tiku_codeview_get_style(st->panes[1], TIKU_INK_WORD, &style) ==
           TIKU_EDIT_OK);
    assert(style.colors & TIKU_CODEVIEW_STYLE_FORE);
    assert(!tiku_editor_split(st));
    tiku_editor_preferences_load(st);
    assert(st->preferences.tab == 8 && st->preferences.indent == 2);
    assert(!tiku_editor_start(&fresh, st->services));
    assert(((edit_state_t *)fresh)->preferences.tab == 8);
    tiku_editor_stop(fresh);
    tiku_editor_preferences_open(st);
    tiku_editor_preferences_paint(st);
    assert(!tiku_surface_png(st->prefs_surface,
                             "/tmp/tiku-editor-preferences.png"));
    font = tiku_font_size();
    tiku_font_set_size(16);
    tiku_editor_preferences_paint(st);
    assert(!tiku_surface_png(st->prefs_surface,
                             "/tmp/tiku-editor-preferences-large.png"));
    tiku_font_set_size(font);
    tiku_editor_preferences_close(st);
    put_file(path, "TikuEditorPreferences 1\n"
                   "99999999999999999999999999999 0 80 0 0 1 0 0 0 0 0 0\n");
    tiku_editor_preferences_load(st);
    assert(st->preferences.tab == 4);
    put_file(path, "TikuEditorPreferences 1\n8 0 80 0 0 1 0 0 0 0 0 0 junk\n");
    tiku_editor_preferences_load(st);
    assert(st->preferences.tab == 4);
    assert(!unlink(path));
    /* A folder is not a file to save to. */
    assert(!setenv("TIKU_EDITOR_PREFERENCES", dir, 1));
    tiku_editor_preferences_open(st);
    st->pref_draft.tab = 9;
    pref_key(st, TIKU_KEY_RETURN);
    assert(st->prefs_id && st->pref_message[0] && st->preferences.tab == 4);
    pref_key(st, TIKU_KEY_ESCAPE);
    assert(!setenv("TIKU_EDITOR_PREFERENCES", "", 1));
}

/** The Language menu: JSON and Plain Text colour or leave the text, C
 *  names its language, Save As keeps a named language, and Automatic
 *  goes back to the file's name. */
static void language_menu(edit_state_t *st, const char *dir)
{
    char path[512];
    size_t n;

    assert(set_text(st, "{\"sample\": 12}\n"));
    tiku_editor_pick(st, st->id, CMD_LANG_JSON);
    assert(tiku_cv_style_line(st->view, 0, strlen(line(st, 0)), &n) && n);
    tiku_editor_pick(st, st->id, CMD_LANG_TEXT);
    assert(!tiku_cv_style_line(st->view, 0, strlen(line(st, 0)), &n));
    tiku_editor_pick(st, st->id, CMD_LANG_C);
    assert(st->lang == TIKU_SYNTAX_C);
    snprintf(path, sizeof path, "%s/manual-language.txt", dir);
    assert(tiku_editor_save_document(st, path));
    assert(st->lang == TIKU_SYNTAX_C && st->language_mode == EDIT_LANG_C);
    assert(!unlink(path));
    tiku_editor_pick(st, st->id, CMD_LANG_AUTO);
    assert(st->language_mode == EDIT_LANG_AUTO);
}

/** Find's live count and outlines, Select All Matches, and the context
 *  menu that keeps the selection, greys what cannot run and goes on
 *  Escape, close and resize. */
static void live_search(edit_state_t *st)
{
    tiku_event_t close = {.type = TIKU_EVENT_CLOSE};

    assert(set_text(st, "one two one\n"));
    st->regex = st->whole_word = st->match_case = 0;
    tiku_editor_pick(st, st->id, CMD_FIND);
    tiku_field_start(&st->field[0], "one");
    tiku_editor_paint(st);
    assert(st->match_count == 2 && strstr(st->search_message, "2 matches"));
    tiku_editor_find_match(st, 0);
    tiku_editor_paint(st);
    assert(strstr(st->search_message, "of 2"));
    tiku_editor_pick(st, st->id, CMD_SELECT_MATCHES);
    assert(tiku_codeview_selections(st->view, NULL, 0, NULL) == 2);
    tiku_editor_context_open(st, 80, 80);
    assert(st->context_open &&
           tiku_codeview_selections(st->view, NULL, 0, NULL) == 2);
    tiku_editor_paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-editor-context.png"));
    key(st, TIKU_KEY_ESCAPE, 0);
    assert(!st->context_open);
    tiku_editor_context_open(st, 80, 80);
    assert(!tiku_editor_context_event(st, &close) && !st->context_open);
    tiku_editor_context_open(st, 80, 80);
    tiku_event_t resize = {.type = TIKU_EVENT_RESIZE, .width = st->w,
                           .height = st->h};
    tiku_editor_event(st, &resize);
    assert(!st->context_open);
    tiku_editdoc_set_read_only(st->doc, 1);
    tiku_editor_context_open(st, 80, 80);
    for (int i = 0; i < st->context.count; i++) {
        if (st->context.item[i].command == CMD_TRIM ||
            st->context.item[i].command == CMD_CUT)
            assert(!st->context.item[i].enabled);
    }
    key(st, TIKU_KEY_ESCAPE, 0);
    tiku_editdoc_set_read_only(st->doc, 0);
    st->regex = 1;
    tiku_field_start(&st->field[0], "[");
    tiku_editor_paint(st);
    assert(!st->match_count && strstr(st->search_message, "Invalid"));
    tiku_field_start(&st->field[0], "two");
    tiku_editor_paint(st);
    assert(st->match_count == 1 && !strstr(st->search_message, "Invalid"));
    assert(set_text(st, "two two"));
    tiku_editor_paint(st);
    assert(st->match_count == 2);
    tiku_field_start(&st->field[0], "missing");
    tiku_editor_paint(st);
    assert(!st->match_count && !strcmp(st->search_message, "0 matches"));
    tiku_editor_search_close(st);
    assert(!st->match_count && !st->search_generation);
}

/** Live search marks at most EDIT_SEARCH_MAX matches and Select All
 *  Matches refuses more; above 2 MiB there is no live count. */
static void search_limits(edit_state_t *st)
{
    char many[2201], *large;

    for (int i = 0; i < 1100; i++) {
        many[i * 2] = 'x';
        many[i * 2 + 1] = ' ';
    }
    many[2200] = 0;
    assert(set_text(st, many));
    st->regex = 0;
    tiku_editor_pick(st, st->id, CMD_FIND);
    tiku_field_start(&st->field[0], "x");
    tiku_editor_paint(st);
    assert(st->match_count == EDIT_SEARCH_MAX && st->match_truncated);
    assert(strstr(st->search_message, "More than 1000 matches"));
    tiku_codeview_select(st->view, 0, 0);
    tiku_editor_pick(st, st->id, CMD_SELECT_MATCHES);
    assert(strstr(st->note, "Too many") &&
           tiku_codeview_selections(st->view, NULL, 0, NULL) == 1);
    large = malloc(2 * 1024 * 1024 + 2);
    assert(large);
    memset(large, 'a', 2 * 1024 * 1024 + 1);
    large[2 * 1024 * 1024 + 1] = 0;
    assert(set_text(st, large));
    free(large);
    tiku_editor_paint(st);
    assert(!st->match_count && strstr(st->search_message, "2 MiB"));
    tiku_editor_search_close(st);
    st->regex = 0;
}

/** Text > Trim Trailing Whitespace is one undo step, and the menu bar
 *  holds the system menu, six application menus and Devices. */
static void text_cleanup(edit_state_t *st)
{
    tiku_menubar_t bar = {0};
    tiku_menu_t menu = {0};

    assert(set_text(st, "b  \na \n"));
    tiku_editor_pick(st, st->id, CMD_TRIM);
    assert(!strcmp(line(st, 0), "b"));
    assert(tiku_codeview_undo(st->view) == TIKU_EDIT_OK &&
           !strcmp(line(st, 0), "b  "));
    for (int i = 0; i < 8; i++)
        assert(tiku_menubar_add(&bar, &menu) >= 0);
    assert(bar.count == 8);
}

/** An edit through the window's own event path is painted once: the
 *  tick after it paints nothing, and repaints only for a change made
 *  after the last frame. */
static void tick_paints_once(edit_state_t *st)
{
    int before;

    assert(set_text(st, "alpha beta\n"));
    place(st, 0, 0);
    st->note[0] = st->last_note[0] = 0;
    tiku_editor_paint(st);
    tiku_editor_tick(st, 0);
    counted_window = st->id;
    before = frames_counted;
    key(st, 'x', 0);
    assert(frames_counted == before + 1 && !strcmp(line(st, 0), "xalpha beta"));
    tiku_editor_tick(st, 0);
    assert(frames_counted == before + 1);
    assert(tiku_codeview_select(st->view, 0, 5) == TIKU_EDIT_OK);
    tiku_editor_tick(st, 0);
    assert(frames_counted == before + 2);
    tiku_editor_tick(st, 0);
    assert(frames_counted == before + 2);
    counted_window = 0;
    assert(set_text(st, ""));
}

/** The horizontal scrollbar's arrow steps one character of the face the
 *  pane is zoomed to. */
static void zoomed_scroll_step(edit_state_t *st)
{
    char wide[2001];
    int cell;

    memset(wide, 'w', 2000);
    wide[2000] = 0;
    assert(set_text(st, wide));
    tiku_codeview_scroll_to(st->view, 0, 0);
    tiku_editor_pick(st, st->id, CMD_ZOOM_IN);
    tiku_editor_pick(st, st->id, CMD_ZOOM_IN);
    cell = tiku_text_advance(tiku_codeview_font(st->view), ' ');
    assert(tiku_codeview_zoom(st->view) == 2 &&
           cell != tiku_font_mono_cell(0));
    st->note[0] = 0;
    st->hover_tool = -1;
    tiku_editor_paint(st);
    tiku_event_t press = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = 208 + (st->w - 224) - 7, .y = st->h - EDIT_FOOT_H + 8};
    tiku_event_t up = press;
    up.type = TIKU_EVENT_POINTER_UP;
    tiku_editor_event(st, &press);
    tiku_editor_event(st, &up);
    assert(tiku_codeview_left(st->view) == cell);
    tiku_editor_pick(st, st->id, CMD_ZOOM_RESET);
    tiku_codeview_scroll_to(st->view, 0, 0);
    assert(set_text(st, ""));
}

/** Uppercase and Lowercase change the main selection, so they are
 *  offered only while it holds text, whatever the others hold. */
static void case_needs_main_selection(edit_state_t *st)
{
    const tiku_menu_list_t *text = &st->last_menus.menu[4];
    const tiku_edit_selection_t two[] = {{0, 3}, {7, 7}};

    assert(set_text(st, "one two"));
    assert(tiku_codeview_select_many(st->view, two, 2, 1) == TIKU_EDIT_OK);
    tiku_editor_publish(st);
    assert(text->item[17].command == CMD_UPPER &&
           text->item[18].command == CMD_LOWER);
    assert(!text->item[17].enabled && !text->item[18].enabled);
    tiku_editor_pick(st, st->id, CMD_UPPER);
    assert(!strcmp(line(st, 0), "one two"));
    assert(tiku_codeview_select_many(st->view, two, 2, 0) == TIKU_EDIT_OK);
    tiku_editor_publish(st);
    assert(text->item[17].enabled && text->item[18].enabled);
    tiku_editor_pick(st, st->id, CMD_UPPER);
    assert(!strcmp(line(st, 0), "ONE two"));
    assert(undo(st) && !strcmp(line(st, 0), "one two"));
    assert(set_text(st, ""));
}

/** Revert reads the same file again and keeps a language named on the
 *  Language menu; opening another file goes back to Automatic. */
static void revert_keeps_language(edit_state_t *st, const char *dir)
{
    char path[512], other[512];

    snprintf(path, sizeof path, "%s/revert.txt", dir);
    snprintf(other, sizeof other, "%s/other.txt", dir);
    put_file(path, "10 PRINT 1\n");
    put_file(other, "20 END\n");
    assert(tiku_editor_read_document(st, path));
    assert(st->lang == TIKU_SYNTAX_NONE);
    tiku_editor_pick(st, st->id, CMD_LANG_BASIC);
    assert(tiku_codeview_language(st->view) == TIKU_SYNTAX_BASIC);
    tiku_editor_pick(st, st->id, CMD_REVERT);
    assert(st->language_mode == EDIT_LANG_BASIC &&
           st->lang == TIKU_SYNTAX_BASIC &&
           tiku_codeview_language(st->view) == TIKU_SYNTAX_BASIC);
    assert(tiku_editor_read_document(st, other));
    tiku_editor_reveal(st);
    assert(st->language_mode == EDIT_LANG_AUTO &&
           st->lang == TIKU_SYNTAX_NONE &&
           tiku_codeview_language(st->view) == TIKU_SYNTAX_NONE);
    assert(!unlink(path) && !unlink(other));
    st->path[0] = 0;
    assert(set_text(st, ""));
}

/** Preferences opens on the saved defaults, not on the window's View
 *  switches and zoom of the moment, so Save keeps those out of the file;
 *  Save then gives the window the saved values. */
static void preferences_seed(edit_state_t *st, const char *dir)
{
    char path[512];

    snprintf(path, sizeof path, "%s/seed-preferences", dir);
    assert(!setenv("TIKU_EDITOR_PREFERENCES", path, 1));
    tiku_editor_preferences_load(st);
    tiku_editor_pick(st, st->id, CMD_WRAP);
    tiku_editor_pick(st, st->id, CMD_ZOOM_IN);
    assert(st->wrap && tiku_codeview_zoom(st->view) == 1);
    tiku_editor_preferences_open(st);
    assert(st->prefs_id && !st->pref_draft.wrap && !st->pref_draft.zoom);
    st->pref_draft.tab = 6;
    pref_key(st, TIKU_KEY_RETURN);
    assert(!st->prefs_id);
    expect_file(path, "TikuEditorPreferences 1\n6 0 80 0 0\n1 0 0 0 0 0 0\n");
    assert(!st->wrap && !tiku_codeview_zoom(st->view));
    assert(!unlink(path));
    assert(!setenv("TIKU_EDITOR_PREFERENCES", "", 1));
    tiku_editor_preferences_load(st);
}

/** Save tab width @p tab from the Preferences window. */
static void save_tab(edit_state_t *st, int tab)
{
    tiku_editor_preferences_open(st);
    st->pref_draft.tab = tab;
    pref_key(st, TIKU_KEY_RETURN);
}

/** Where preferences are kept: a relative XDG_CONFIG_HOME is passed over
 *  for ~/.config, a symbolic link is saved through to its file, and a
 *  save whose folder cannot be synced is applied with that noted. */
static void preferences_places(edit_state_t *st, const char *dir)
{
    char home[512], path[600], link[512], locked[512];
    char *old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    struct stat info;
    int fd;

    assert(!setenv("TIKU_EDITOR_PREFERENCES", "", 1));
    tiku_editor_preferences_load(st);
    snprintf(home, sizeof home, "%s/home", dir);
    snprintf(path, sizeof path, "%s/.config/TikuEditorPreferences", home);
    assert(!mkdir(home, 0700) && !unsetenv("TIKU_EDITOR_PREFERENCES"));
    assert(!setenv("HOME", home, 1));
    assert(!setenv("XDG_CONFIG_HOME", "relative/config", 1));
    save_tab(st, 7);
    assert(!st->prefs_id && st->preferences.tab == 7);
    expect_file(path, "TikuEditorPreferences 1\n7 0 80 0 0\n1 0 0 0 0 0 0\n");
    assert(!unlink(path));
    snprintf(path, sizeof path, "%s/.config", home);
    assert(!rmdir(path) && !rmdir(home));
    assert(!unsetenv("XDG_CONFIG_HOME"));
    assert(old_home ? !setenv("HOME", old_home, 1) : !unsetenv("HOME"));
    free(old_home);

    snprintf(path, sizeof path, "%s/linked-preferences", dir);
    snprintf(link, sizeof link, "%s/preferences-link", dir);
    put_file(path, "TikuEditorPreferences 1\n4 0 80 0 0\n1 0 0 0 0 0 0\n");
    assert(!symlink(path, link));
    assert(!setenv("TIKU_EDITOR_PREFERENCES", link, 1));
    save_tab(st, 3);
    assert(!st->prefs_id && st->preferences.tab == 3);
    assert(!lstat(link, &info) && S_ISLNK(info.st_mode));
    expect_file(path, "TikuEditorPreferences 1\n3 0 80 0 0\n1 0 0 0 0 0 0\n");
    assert(!unlink(link) && !unlink(path));

    /* A folder that can be written but not read: the rename lands and
     * only the sync that follows it fails.  Root reads every folder, so
     * there the case cannot be made. */
    snprintf(locked, sizeof locked, "%s/locked", dir);
    snprintf(path, sizeof path, "%s/preferences", locked);
    assert(!mkdir(locked, 0700) && !chmod(locked, 0300));
    fd = open(locked, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        assert(!setenv("TIKU_EDITOR_PREFERENCES", path, 1));
        st->note[0] = 0;
        save_tab(st, 9);
        assert(!st->prefs_id && st->preferences.tab == 9);
        assert(strstr(st->note, "could not be synced"));
        expect_file(path,
                    "TikuEditorPreferences 1\n9 0 80 0 0\n1 0 0 0 0 0 0\n");
        assert(!unlink(path));
    } else {
        close(fd);
    }
    assert(!chmod(locked, 0700) && !rmdir(locked));
    assert(!setenv("TIKU_EDITOR_PREFERENCES", "", 1));
    tiku_editor_preferences_load(st);
    st->note[0] = 0;
}

/** Whether every code ink stands at least a quarter of the range away
 *  from the page in lightness, as Rec. 601 grey. */
static int inks_stand_out(edit_state_t *st)
{
    static const tiku_ink_t roles[] = {TIKU_INK_WORD, TIKU_INK_STR,
                                       TIKU_INK_NUM, TIKU_INK_REMARK};
    int page = (int)(tiku_grey(TIKU_C_DOC) & 0xFFu);

    for (size_t k = 0; k < sizeof roles / sizeof roles[0]; k++) {
        tiku_codeview_style_t style;
        int ink;

        assert(tiku_codeview_get_style(st->view, roles[k], &style) ==
               TIKU_EDIT_OK && (style.colors & TIKU_CODEVIEW_STYLE_FORE));
        ink = (int)(tiku_grey(style.foreground) & 0xFFu);
        if (abs(ink - page) < 64)
            return 0;
    }
    return 1;
}

/** The Muted and Strong colour presets are made from the live theme's
 *  code inks, so they stand out from the page on the dark theme too and
 *  follow a change of theme. */
static void preset_colours(edit_state_t *st)
{
    editor_preferences_t p = st->preferences;
    tiku_codeview_style_t style;
    tiku_rgb_t word;

    assert(set_text(st, "10 PRINT 1\n"));
    for (p.colors = 1; p.colors <= 2; p.colors++) {
        tiku_editor_preferences_apply(st, &p);
        tiku_theme_set(tiku_theme_dark());
        tiku_editor_view_sync(st);
        assert(inks_stand_out(st));
        tiku_theme_set(NULL);
        tiku_editor_view_sync(st);
        assert(inks_stand_out(st));
    }
    word = tiku_ink(TIKU_INK_WORD);
    p.colors = 1;
    tiku_editor_preferences_apply(st, &p);
    assert(tiku_codeview_get_style(st->view, TIKU_INK_WORD, &style) ==
           TIKU_EDIT_OK && style.foreground == tiku_dim(word, tiku_grey(word)));
    p.colors = 0;
    tiku_editor_preferences_apply(st, &p);
    assert(tiku_codeview_get_style(st->view, TIKU_INK_WORD, &style) ==
           TIKU_EDIT_OK && !(style.colors & TIKU_CODEVIEW_STYLE_FORE));
    assert(set_text(st, ""));
}

/** @brief A launch argument loads its contents, never a remembered document. */
int main(void)
{
    assert(!setenv("TIKU_EDITOR_PREFERENCES","",1));
    char dir[] = "/tmp/tiku-editor-document-XXXXXX", path[512];
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/notes literal;name.TXT", dir);
    FILE *f = fopen(path, "w"); assert(f);
    assert(fputs("Sensor notes\nSecond line\n", f) >= 0);
    assert(!fclose(f));
    tiku_app_services_t services = {.open = window_open, .frame = window_frame,
        .menus = window_menus, .startup_document = path,
        .clip_set = clip_set, .clip_text = clip_text, .pick = pick_file,
        .sizeable = sizeable, .send_message = send_message};
    void *state = NULL;
    assert(!tiku_editor_start(&state, &services));
    edit_state_t *st = state;
    assert(!strcmp(st->path, path));
    assert(!strcmp(title_seen, "notes literal;name.TXT"));
    assert(!strcmp(line(st, 0), "Sensor notes"));
    assert(!tiku_editor_modified(st) && !st->running);
    tiku_editor_stop(state);
    services.startup_document = NULL;
    /* A command-line path too long to hold is refused, never cut short. */
    char longpath[600]; memset(longpath, 'p', sizeof longpath - 1);
    longpath[sizeof longpath - 1] = 0;
    assert(tiku_editor_startup_path(path));
    assert(!tiku_editor_startup_path(longpath));
    assert(!tiku_editor_start(&state, &services));
    st = state;
    assert(!st->path[0] && !strcmp(title_seen, "Edit"));
    workflows(st, dir);
    kept_bytes(st, dir);
    caret_rules(st);
    editor_rows(st);
    text_menu(st);
    wrap_lines(st);
    split_panes(st);
    caret_looks(st);
    jumps(st);
    folds(st);
    carets(st);
    secondary_clipboard(st);
    undo_roundtrips(st);
    history_rollover(st);
    large_documents(st, dir);
    toolbar_actions(st);
    panel_during_question(st, dir);
    replace_all_history(st);
    find_field_edits(st);
    footer_message_press(st);
    run_output_nul(st);
    json_provider(st);
    advanced_controls(st);
    preferences_window(st, dir);
    language_menu(st, dir);
    live_search(st);
    search_limits(st);
    text_cleanup(st);
    tick_paints_once(st);
    zoomed_scroll_step(st);
    case_needs_main_selection(st);
    revert_keeps_language(st, dir);
    preferences_seed(st, dir);
    preferences_places(st, dir);
    preset_colours(st);
    assert(sizeable_count >= 2);
    assert(set_text(st, ""));
    assert(tiku_editor_insert_checked(st, "Sensor capture notes\n\nSample interval: 250 ms\nTemperature: 24.8 C\nHumidity: 48 percent\n\nNext steps\n\tCheck the calibration values\n\tExport the next capture\n"));
    tiku_editdoc_saved(st->doc, tiku_editdoc_state(st->doc));
    place(st, 0, 0);
    tiku_editor_pick(st, st->id, CMD_FIND);
    tiku_field_start(&st->field[0], "capture");
    tiku_field_start(&st->field[1], "session");
    tiku_editor_find_match(st, 0); tiku_editor_paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-editor-preview.png"));
    assert(!tiku_surface_png(st->search_surface, "/tmp/tiku-editor-find-preview.png"));
    tiku_editor_stop(state);
    free(line_copy);
    assert(!unlink(path) && !rmdir(dir));
    puts("PASS: Editor lossless IO, atomic saves, clipboard, undo, search, navigation, resize and guards");
    return 0;
}
