/* Editor's explicit launch document and ordinary empty launch.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#define TIKU_APP_EMBED
#include "../../apps/editor/tiku_edit.c"

static char title_seen[128];
static uint32_t next_window;
static char clipboard[TIKU_CLIPBOARD_MAX];
static int picker_count, picker_mode, sizeable_count;

/** Native clipboard and file-panel seams for workflow tests. */
static int clip_set(void *ctx, const char *text)
{ (void)ctx; snprintf(clipboard, sizeof clipboard, "%s", text); return 0; }
static int clip_get(void *ctx, char *out, size_t max)
{ (void)ctx; snprintf(out, max, "%s", clipboard); return (int)strlen(out); }
static int pick_file(void *ctx, uint32_t id, int mode, const char *dir, const char *name)
{ (void)ctx; (void)id; (void)dir; (void)name; picker_count++; picker_mode = mode; return 0; }
static int sizeable(void *ctx, uint32_t id)
{ (void)ctx; (void)id; sizeable_count++; return 0; }

/** Feed the same input descriptor as the remote/native runtime. */
static int key(edit_state_t *st, unsigned code, unsigned mods)
{
    tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN, .key = code, .modifiers = mods};
    return edit_event(st, &e);
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
    return edit_event(st, &e);
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

/** Every accepted replacement must round-trip through one undo and redo. */
static void undo_roundtrips(edit_state_t *st)
{
    char before[8192], after[8192], actual[8192];
    const char *pieces[] = {"a", "café", "\n", "alpha\nbeta\n", "", "\tend"};
    unsigned seed = 19;
    for (int i = 0; i < 300; i++) {
        if (i % 15 == 0) edit_buffer_set(&st->tv,
            "A short paragraph with café.\nAnother line with tabs\tand text.\nLast line.");
        seed = seed * 1664525u + 1013904223u;
        int y = (int)(seed % (unsigned)st->tv.nline);
        edit_buffer_select_none(&st->tv);
        edit_buffer_place(&st->tv, y, (int)((seed >> 8) % (unsigned)(st->tv.line[y].len + 1)));
        if (seed & 1) {
            edit_buffer_select_begin(&st->tv);
            int other = (int)((seed >> 16) % (unsigned)st->tv.nline);
            edit_buffer_place(&st->tv, other, (int)((seed >> 5) % (unsigned)(st->tv.line[other].len + 1)));
        }
        assert(edit_buffer_text(&st->tv, before, sizeof before) < sizeof before);
        if (!insert_checked(st, pieces[(seed >> 24) % 6])) continue;
        assert(edit_buffer_text(&st->tv, after, sizeof after) < sizeof after);
        if (!strcmp(before, after)) continue;
        assert(edit_buffer_undo(&st->tv));
        edit_buffer_text(&st->tv, actual, sizeof actual);
        if (strcmp(before, actual)) {
            fprintf(stderr, "Roundtrip %d seed %u\nBefore: [%s]\nAfter: [%s]\nUndo: [%s]\n",
                i, seed, before, after, actual);
        }
        assert(!strcmp(before, actual));
        assert(edit_buffer_redo(&st->tv));
        edit_buffer_text(&st->tv, actual, sizeof actual);
        assert(!strcmp(after, actual));
    }
    edit_buffer_free(&st->tv);
}

/** History pruning and a branch after undo retain accurate save points. */
static void history_rollover(edit_state_t *st)
{
    assert(edit_buffer_set(&st->tv, ""));
    for (int i = 0; i < 400; i++) assert(edit_buffer_replace(&st->tv, "x"));
    assert(st->tv.undo_n == 256 && st->tv.line[0].len == 400);
    edit_buffer_saved(&st->tv);
    for (int i = 0; i < 256; i++) assert(edit_buffer_undo(&st->tv));
    assert(!edit_buffer_undo(&st->tv) && st->tv.line[0].len == 144);
    assert(edit_buffer_modified(&st->tv));
    for (int i = 0; i < 256; i++) assert(edit_buffer_redo(&st->tv));
    assert(!edit_buffer_redo(&st->tv) && !edit_buffer_modified(&st->tv));
    assert(edit_buffer_undo(&st->tv));
    assert(edit_buffer_replace(&st->tv, "y"));
    assert(!edit_buffer_can_redo(&st->tv) && edit_buffer_modified(&st->tv));
    assert(edit_buffer_undo(&st->tv) && edit_buffer_modified(&st->tv));
    assert(edit_buffer_redo(&st->tv) && st->tv.line[0].text[399] == 'y');
    assert(edit_buffer_set(&st->tv, ""));
}

/** Long lines, many lines and multi-megabyte documents round-trip losslessly. */
static void large_documents(edit_state_t *st, const char *dir)
{
    char path[512]; snprintf(path, sizeof path, "%s/large.txt", dir);
    FILE *f = fopen(path, "wb"); assert(f);
    char row[81]; memset(row, 'a', 79); row[79] = '\n'; row[80] = 0;
    for (int i = 0; i < 70000; i++) assert(fwrite(row, 1, 80, f) == 80);
    fclose(f); assert(read_document(st, path));
    assert(st->tv.nline == 70001 && save(st));
    struct stat info; assert(!stat(path, &info) && info.st_size == 5600000);
    edit_buffer_place(&st->tv, 65000, 40);
    assert(insert_checked(st, "café"));
    assert(edit_buffer_undo(&st->tv) && st->tv.line[65000].len == 79);
    assert(edit_buffer_redo(&st->tv) && st->tv.line[65000].len == 84);
    assert(edit_buffer_undo(&st->tv));
    size_t length = 256 * 1024;
    char *longline = malloc(length + 1); assert(longline);
    memset(longline, 'x', length); longline[length] = 0;
    put_file(path, longline); assert(read_document(st, path));
    assert(st->tv.nline == 1 && st->tv.line[0].len == (int)length);
    edit_buffer_place(&st->tv, 0, (int)length - 2);
    assert(insert_checked(st, "\ncafé\n"));
    assert(st->tv.nline == 3 && edit_buffer_undo(&st->tv));
    assert(st->tv.nline == 1 && !strcmp(st->tv.line[0].text, longline));
    /* Joining two large lines and replacing all of them no longer needs a 1 KB seam. */
    assert(insert_checked(st, "\n"));
    edit_buffer_select_all(&st->tv); assert(insert_checked(st, "done"));
    assert(edit_buffer_undo(&st->tv) && st->tv.nline == 2);
    assert(edit_buffer_undo(&st->tv) && st->tv.nline == 1);
    reveal(st); paint(st); assert(st->left > 1024);
    assert(save(st)); assert(!stat(path, &info) && info.st_size == (off_t)length);
    free(longline); unlink(path);
    assert(edit_buffer_set(&st->tv, "Alpha alpha alphabet\nalpha\n"));
    tiku_field_start(&st->field[0], "alpha");
    tiku_field_start(&st->field[1], "beta");
    st->match_case = 0; st->whole_word = 1;
    replace_all(st);
    assert(!strcmp(st->tv.line[0].text, "beta beta alphabet"));
    assert(!strcmp(st->tv.line[1].text, "beta"));
    assert(edit_buffer_undo(&st->tv));
    assert(!strcmp(st->tv.line[0].text, "Alpha alpha alphabet"));
    st->match_case = 1; st->whole_word = 0;
    /* Find owns a different window; ordinary document input still edits text. */
    edit_buffer_place(&st->tv, 0, 0);
    edit_pick(st, st->id, CMD_FIND);
    assert(st->search_id && st->search_id != st->id);
    /* Background exposure keeps field focus; pointer movement stays in Find. */
    tiku_event_t e = {.type = TIKU_EVENT_EXPOSE};
    edit_event_in(st, st->id, &e); assert(st->search_active);
    e = (tiku_event_t){.type = TIKU_EVENT_POINTER_MOVE, .x = 10, .y = 10};
    edit_event_in(st, st->search_id, &e); assert(st->hover_tool == -1);
    /* Menu commands target their owning window even without activation events. */
    edit_pick(st, st->id, CMD_ALL);
    assert(st->tv.selecting && !st->search_active);
    edit_buffer_select_none(&st->tv); edit_buffer_place(&st->tv, 0, 0);
    edit_pick(st, st->search_id, CMD_ALL);
    assert(st->field[0].sel_b == st->field[0].len && st->search_active);
    int before = st->tv.line[0].len;
    e = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = 'x'};
    edit_event_in(st, st->id, &e); assert(st->tv.line[0].len == before + 1);
    e.key = TIKU_KEY_ESCAPE;
    edit_event_in(st, st->search_id, &e); assert(!st->search_id && !st->finding);
    edit_buffer_set(&st->tv, ""); st->path[0] = 0; st->width_dirty = 1;
}

/** Native toolbar activation requires releasing over the same enabled tool. */
static void toolbar_actions(edit_state_t *st)
{
    tiku_rect_t r = tool_rect(5);
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .x = r.x + 8, .y = r.y + 8};
    edit_event_in(st, st->id, &e); assert(st->pressed_tool == 5);
    e.type = TIKU_EVENT_POINTER_UP; e.x = 400;
    edit_event_in(st, st->id, &e);
    assert(st->pressed_tool == -1 && !st->search_id);
    e.type = TIKU_EVENT_POINTER_DOWN; e.x = r.x + 8;
    edit_event_in(st, st->id, &e);
    e.type = TIKU_EVENT_POINTER_UP;
    edit_event_in(st, st->id, &e); assert(st->search_id);
    e = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN, .key = 'w', .modifiers = TIKU_MOD_CMD};
    assert(!edit_event_in(st, st->search_id, &e));
    assert(!st->search_id && !st->finding);
}

/** Safety, editing and navigation work through the editor rather than mocks. */
static void workflows(edit_state_t *st, const char *dir)
{
    char path[512], other[512];
    snprintf(path, sizeof path, "%s/work.txt", dir);
    snprintf(other, sizeof other, "%s/other.txt", dir);
    const char *samples[] = {"", "one", "one\n", "one\n\n",
        "\xef\xbb\xbf" "café\r\ntwo\r\n"};
    for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; i++) {
        put_file(path, samples[i]); assert(read_document(st, path));
        assert(save(st)); expect_file(path, samples[i]);
    }
    /* More than the old 31999-byte cap must survive a save unchanged. */
    FILE *f = fopen(path, "wb"); assert(f);
    for (int i = 0; i < 2000; i++) assert(fputs("twenty bytes of text!\n", f) >= 0);
    assert(!fclose(f)); assert(read_document(st, path));
    assert(st->tv.nline == 2001 && save(st));
    struct stat info;
    assert(!stat(path, &info) && info.st_size == (off_t)(2000 * strlen("twenty bytes of text!\n")));
    tiku_event_t resize = {.type = TIKU_EVENT_RESIZE, .width = 780, .height = 530};
    edit_event(st, &resize); assert(st->w == 780 && st->surface->w == 780);
    tiku_event_t wheel = {.type = TIKU_EVENT_WHEEL, .dy = 20};
    edit_event(st, &wheel); assert(st->tv.top == 20);
    key(st, TIKU_KEY_END, TIKU_MOD_CMD); assert(st->tv.cy == 2000);
    key(st, TIKU_KEY_HOME, TIKU_MOD_CMD); assert(!st->tv.cy && !st->tv.top);
    put_file(path, "alpha beta alpha\nlast"); assert(read_document(st, path));
    key(st, TIKU_KEY_RIGHT, TIKU_MOD_SHIFT);
    key(st, TIKU_KEY_RIGHT, TIKU_MOD_SHIFT);
    key(st, 'c', TIKU_MOD_CMD); assert(!strcmp(clipboard, "al"));
    key(st, 'x', TIKU_MOD_CMD); assert(!strcmp(edit_buffer_line(&st->tv, 0), "pha beta alpha"));
    key(st, 'z', TIKU_MOD_CMD); assert(!edit_buffer_modified(&st->tv));
    key(st, 'y', TIKU_MOD_CMD); assert(edit_buffer_modified(&st->tv));
    key(st, 'z', TIKU_MOD_CMD);
    edit_buffer_place(&st->tv, 0, 0);
    key(st, 'h', TIKU_MOD_CMD); assert(st->finding == 1);
    tiku_field_start(&st->field[0], "alpha");
    find_match(st, 0); assert(st->tv.cx == 5);
    find_match(st, 0); assert(st->tv.cx == 16);
    find_match(st, 0); assert(st->tv.cx == 5 && strstr(st->note, "wrapped"));
    tiku_field_start(&st->field[1], "omega");
    replace_match(st); assert(!strcmp(edit_buffer_line(&st->tv, 0), "omega beta alpha"));
    key(st, TIKU_KEY_ESCAPE, 0); key(st, 'z', TIKU_MOD_CMD);
    assert(!strcmp(edit_buffer_line(&st->tv, 0), "alpha beta alpha"));
    assert(!edit_buffer_modified(&st->tv));
    key(st, 'g', TIKU_MOD_CMD); tiku_field_start(&st->field[2], "2");
    key(st, TIKU_KEY_RETURN, 0); assert(st->tv.cy == 1 && !st->finding);
    tiku_rect_t page = edit_page(st);
    tiku_event_t click = {.type = TIKU_EVENT_POINTER_DOWN, .x = page.x, .y = page.y + 2};
    edit_event(st, &click); assert(!st->tv.cy && !st->tv.cx);
    click.type = TIKU_EVENT_POINTER_MOVE; click.x += prefix_width("alpha", 5);
    edit_event(st, &click);
    click.type = TIKU_EVENT_POINTER_UP; edit_event(st, &click);
    key(st, 'c', TIKU_MOD_CMD); assert(!strcmp(clipboard, "alpha"));
    /* Clipboard supports multiple lines and UTF-8, as one undo operation. */
    clip_set(NULL, "café\nnotes"); key(st, 'v', TIKU_MOD_CMD);
    assert(!strcmp(edit_buffer_line(&st->tv, 0), "café"));
    key(st, 'z', TIKU_MOD_CMD); assert(!edit_buffer_modified(&st->tv));
    key(st, '!', 0); assert(edit_buffer_modified(&st->tv));
    assert(!key(st, 'w', TIKU_MOD_CMD) && st->ask.open);
    key(st, TIKU_KEY_RETURN, 0); assert(!st->ask.open && edit_buffer_modified(&st->tv));
    edit_closed(st, st->id); assert(st->ask.open);
    key(st, TIKU_KEY_ESCAPE, 0);
    int before = picker_count;
    key(st, 'o', TIKU_MOD_CMD); assert(st->ask.open && picker_count == before);
    key(st, TIKU_KEY_ESCAPE, 0);
    /* Refused reads and writes keep the current path, data and undo state. */
    put_file(other, "invalid\rbare"); assert(!read_document(st, other));
    assert(!strcmp(st->path, path) && edit_buffer_modified(&st->tv));
    put_file(other, "x\r\ny\n"); assert(!read_document(st, other));
    put_file(path, "changed externally"); assert(!save(st));
    expect_file(path, "changed externally");
    char impossible[512]; snprintf(impossible, sizeof impossible, "%s/no-folder/file.txt", dir);
    assert(!save_document(st, impossible) && !strcmp(st->path, path));
    assert(save_document(st, other) && !edit_buffer_modified(&st->tv));
    assert(!strcmp(st->path, other));
    key(st, 'n', TIKU_MOD_CMD); assert(!st->path[0] && st->tv.nline == 1);
    key(st, 's', TIKU_MOD_CMD); assert(picker_mode == TIKU_APP_PICK_SAVE);
    /* Typing while an Open panel is outstanding must not be lost. */
    key(st, 'o', TIKU_MOD_CMD); key(st, 'x', 0);
    edit_picked(st, st->id, path); assert(!st->path[0]);
    assert(!strcmp(edit_buffer_line(&st->tv, 0), "x"));
    /* Discard approval does not mark an unsaved file clean if Open is cancelled. */
    key(st, 'o', TIKU_MOD_CMD); answer(st, 1);
    assert(edit_buffer_modified(&st->tv));
    edit_picked(st, st->id, path); assert(!strcmp(st->path, path));
    /* Saving through a guarded transition continues only after success. */
    key(st, '!', 0); key(st, 'n', TIKU_MOD_CMD); answer(st, 2);
    assert(!st->path[0] && !edit_buffer_modified(&st->tv));
    key(st, 'x', 0); key(st, 'n', TIKU_MOD_CMD); answer(st, 2);
    assert(st->pending == CMD_NEW && picker_mode == TIKU_APP_PICK_SAVE);
    edit_picked(st, st->id, other);
    assert(!st->path[0] && !edit_buffer_modified(&st->tv));
    assert(read_document(st, other));
    assert(!chmod(other, 0444)); assert(!save(st)); assert(!chmod(other, 0644));
    char linkpath[512]; snprintf(linkpath, sizeof linkpath, "%s/link.txt", dir);
    assert(!symlink(other, linkpath)); assert(!save_document(st, linkpath));
    assert(!unlink(linkpath));
    /* Empty replacement is undoable, and clipboard overflow never cuts text. */
    edit_buffer_select_all(&st->tv); assert(insert_checked(st, ""));
    assert(!st->tv.line[0].len); key(st, 'z', TIKU_MOD_CMD);
    assert(!strcmp(edit_buffer_line(&st->tv, 0), "x"));
    char longline[1024]; memset(longline, 'q', 1023); longline[1023] = 0;
    edit_buffer_set(&st->tv, longline);
    edit_buffer_place(&st->tv, 0, 1023);
    assert(insert_checked(st, "a") && st->tv.line[0].len == 1024);
    put_file(other, "bad\xff"); assert(!read_document(st, other));
    assert(st->tv.line[0].len == 1024);
    edit_buffer_set(&st->tv, "");
    for (int i = 0; i < 10; i++) {
        assert(insert_checked(st, "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\n"));
    }
    edit_buffer_select_all(&st->tv); key(st, 'c', TIKU_MOD_CMD);
    assert(strlen(clipboard) == 530);
    assert(insert_checked(st, "")); key(st, 'v', TIKU_MOD_CMD);
    assert(st->tv.nline == 11);
    edit_buffer_set(&st->tv, "");
    char block[502]; memset(block, 'q', 500); block[500] = '\n'; block[501] = 0;
    for (int i = 0; i < 10; i++) assert(insert_checked(st, block));
    edit_buffer_select_all(&st->tv);
    key(st, 'x', TIKU_MOD_CMD);
    assert(strstr(st->note, "4095") && st->tv.nline == 11 && strlen(clipboard) == 530);
    edit_buffer_saved(&st->tv);
    assert(!unlink(path) && !unlink(other));
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
        .clip_set = clip_set, .clip_get = clip_get, .pick = pick_file,
        .sizeable = sizeable};
    void *state = NULL;
    assert(!edit_start(&state, &services));
    edit_state_t *st = state;
    assert(!strcmp(st->path, path));
    assert(!strcmp(title_seen, "notes literal;name.TXT"));
    assert(!strcmp(edit_buffer_line(&st->tv, 0), "Sensor notes"));
    assert(!edit_buffer_modified(&st->tv) && !st->running);
    edit_stop(state);
    services.startup_document = NULL;
    assert(!edit_start(&state, &services));
    st = state;
    assert(!st->path[0] && !strcmp(title_seen, "Edit"));
    workflows(st, dir);
    undo_roundtrips(st);
    history_rollover(st);
    large_documents(st, dir);
    toolbar_actions(st);
    assert(sizeable_count >= 2);
    edit_buffer_free(&st->tv);
    assert(insert_checked(st, "Sensor capture notes\n\nSample interval: 250 ms\nTemperature: 24.8 C\nHumidity: 48 percent\n\nNext steps\n\tCheck the calibration values\n\tExport the next capture\n"));
    edit_buffer_saved(&st->tv);
    edit_buffer_place(&st->tv, 0, 0);
    edit_pick(st, st->id, CMD_FIND);
    tiku_field_start(&st->field[0], "capture");
    tiku_field_start(&st->field[1], "session");
    find_match(st, 0); paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-editor-preview.png"));
    assert(!tiku_surface_png(st->search_surface, "/tmp/tiku-editor-find-preview.png"));
    edit_stop(state);
    assert(!unlink(path) && !rmdir(dir));
    puts("PASS: Editor lossless IO, atomic saves, clipboard, undo, search, navigation, resize and guards");
    return 0;
}
