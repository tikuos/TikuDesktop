/* Editor's explicit launch document and ordinary empty launch.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../../apps/editor/tiku_editor_private.h"

static char title_seen[128];
static uint32_t next_window;
static char *clipboard;         /* the desk's, as the services hold it */
static unsigned clipboard_stamp;
static int picker_count, picker_mode, sizeable_count;

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
    /* The name says the language, and View > Colour as BASIC changes it. */
    snprintf(path, sizeof path, "%s/prog.bas", dir);
    put_file(path, "10 PRINT 1\n"); assert(tiku_editor_read_document(st, path));
    assert(tiku_codeview_language(st->view) == TIKU_SYNTAX_BASIC);
    tiku_editor_pick(st, st->id, CMD_SYNTAX);
    assert(tiku_codeview_language(st->view) == TIKU_SYNTAX_NONE);
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
           (TIKU_CODEVIEW_LINE_NUMBERS | TIKU_CODEVIEW_BRACES));
    assert(set_text(st, ""));
}

/** @brief Capture the title without opening a real desktop window. */
static uint32_t window_open(void *ctx, const char *title, int w, int h)
{
    (void)ctx; (void)w; (void)h;
    snprintf(title_seen, sizeof title_seen, "%s", title);
    return ++next_window;
}

/** @brief The headless host accepts the frame. */
static int window_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    (void)ctx; (void)id; (void)px; (void)w; (void)h;
    return 0;
}

/** @brief The headless host accepts the application's menus. */
static int window_menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    (void)ctx; (void)id; (void)set;
    return 0;
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
    assert(st->last_menus.nmenu == 5 && !strcmp(text->title, "Text") &&
           text->nitem == 9);
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
    assert(view->nitem == 5 && view->item[4].command == CMD_WRAP &&
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

/** @brief A launch argument loads its contents, never a remembered document. */
int main(void)
{
    char dir[] = "/tmp/tiku-editor-document-XXXXXX", path[512];
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/notes literal;name.TXT", dir);
    FILE *f = fopen(path, "w"); assert(f);
    assert(fputs("Sensor notes\nSecond line\n", f) >= 0);
    assert(!fclose(f));
    tiku_app_services_t services = {.open = window_open, .frame = window_frame,
        .menus = window_menus, .startup_document = path,
        .clip_set = clip_set, .clip_text = clip_text, .pick = pick_file,
        .sizeable = sizeable};
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
    undo_roundtrips(st);
    history_rollover(st);
    large_documents(st, dir);
    toolbar_actions(st);
    panel_during_question(st, dir);
    replace_all_history(st);
    find_field_edits(st);
    footer_message_press(st);
    run_output_nul(st);
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
