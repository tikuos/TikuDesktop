/*
 * TikuDesktop application tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_notes.c - embedded Rich View, plain files and save failure recovery.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gfx.h"
#include "tiku_text_input.h"
#include <assert.h>
#include <time.h>

static int fail_surface;
/** @brief Inject a replacement-surface allocation failure. */
static tiku_surface_t *
test_surface(int w, int h, tiku_rgb_t background)
{
    return fail_surface ? NULL : tiku_surface_new(w, h, background);
}

#define TIKU_APP_EMBED
#define tiku_surface_new test_surface
#include "../../apps/notes/tiku_notes.c"
#undef tiku_surface_new

static int opens, closes, frames, raises, menus_seen, fail_clip;
static uint32_t next_id = 1;
static char last_title[128], last_menu_first[64], clipboard[32768];
static int last_menu_rows;

/** @brief Record a host window request without opening a native display. */
static uint32_t
open_win(void *ctx, const char *title, int w, int h)
{
    (void)ctx;
    assert(w >= NOTE_MIN_W && h >= NOTE_MIN_H);
    opens++;
    snprintf(last_title, sizeof last_title, "%s", title);
    return next_id++;
}

/** @brief Validate a complete initialized frame. */
static int
frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    (void)ctx;
    assert(id && px && w >= NOTE_MIN_W && h >= NOTE_MIN_H);
    frames++;
    return 1;
}

/** @brief Record the published menus and their note ordering. */
static int
menus(void *ctx, uint32_t id, const tiku_menuset_t *m)
{
    (void)ctx;
    assert(id && m->nmenu == 3);
    assert(!strcmp(m->menu[0].title, "Note"));
    assert(!strcmp(m->menu[1].title, "Edit"));
    assert(!strcmp(m->menu[2].title, "Notes"));
    menus_seen++;
    last_menu_rows = m->menu[2].nitem;
    snprintf(last_menu_first, sizeof last_menu_first, "%s",
             last_menu_rows ? m->menu[2].item[0].label : "");
    return 1;
}

/** @brief Count host window closures. */
static void
close_win(void *ctx, uint32_t id)
{
    (void)ctx;
    assert(id);
    closes++;
}

/** @brief Record title updates and require working maximize controls. */
static int
configure(void *ctx, uint32_t id, const char *title, int w, int h, int r, int z)
{
    (void)ctx; (void)w; (void)h;
    assert(id && r && z);
    snprintf(last_title, sizeof last_title, "%s", title);
    return 1;
}

/** @brief Count requests to activate an existing note. */
static int
raise_win(void *ctx, uint32_t id)
{
    (void)ctx;
    assert(id);
    raises++;
    return 1;
}

/** @brief Publish exact text unless the clipboard fault is enabled. */
static int
clip_set(void *ctx, const char *text)
{
    (void)ctx;
    if (fail_clip || strlen(text) >= sizeof clipboard) return 0;
    strcpy(clipboard, text);
    return 1;
}

/** @brief Return the test host's owned clipboard mirror. */
static const char *
clip_text(void *ctx, unsigned *stamp)
{
    (void)ctx;
    if (stamp) *stamp = 0;
    return clipboard;
}

/** @brief Deliver ordinary typing using host key events. */
static void
type_text(notes_t *st, note_t *n, const char *text, int64_t at)
{
    for (; *text; text++) {
        tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN, .time_us = at};

        e.text[0] = *text;
        e.key = (unsigned char)*text;
        assert(!notes_event_in(st, n->id, &e));
    }
}

/** @brief Read the saved bytes without consulting the application model. */
static int
file_has(const char *path, const char *needle)
{
    char buf[32768];
    FILE *f = fopen(path, "r");
    size_t size;

    if (!f) return 0;
    size = fread(buf, 1, sizeof buf - 1, f);
    buf[size] = '\0';
    fclose(f);
    return strstr(buf, needle) != NULL;
}

/** @brief Assert the complete live text, including paragraph separators. */
static void
text_is(note_t *n, const char *wanted)
{
    tiku_richsnapshot_t *snapshot;
    char text[32768];
    size_t length;

    assert(tiku_richdoc_snapshot(n->doc, &snapshot) == TIKU_RICH_OK);
    assert(tiku_richsnapshot_text(snapshot, text, sizeof text, &length) ==
           TIKU_RICH_OK);
    assert(length == strlen(wanted) && !strcmp(text, wanted));
    tiku_richsnapshot_release(snapshot);
}

/** @brief Advance jobs with a ten-second deadline and no real board access. */
static void
settle(notes_t *st, int64_t now)
{
    struct timespec pause = {0, 1000000};
    int turn, i, pending;

    for (turn = 0; turn < 10000; turn++) {
        notes_tick(st, now + turn * 1000);
        pending = 0;
        for (i = 0; i < NOTES_MAX; i++) {
            note_t *n = &st->note[i];

            if (n->used && (n->save || n->queue_count || note_dirty(n) ||
                tiku_richview_status(n->view) == TIKU_RICH_PENDING))
                pending = 1;
        }
        if (!pending) return;
        nanosleep(&pause, NULL);
    }
    assert(!"Notes failed to settle");
}

/** @brief Obtain a click coordinate from the actual retained glyph geometry. */
static tiku_event_t
click_at(note_t *n, size_t byte, int64_t time)
{
    tiku_rich_pos_t first, last;
    tiku_rich_rect_t caret;
    const tiku_richlayout_t *layout = tiku_richview_layout(n->view);

    assert(layout);
    assert(tiku_richsession_range(tiku_richview_session(n->view),
                                  &first, &last) == TIKU_RICH_OK);
    first.byte = byte;
    assert(tiku_richlayout_caret(layout, first, &caret) == TIKU_RICH_OK);
    return (tiku_event_t){.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
        .x = MARGIN + (int)(caret.x + 0.5),
        .y = MARGIN + (int)(caret.y + caret.height / 2), .time_us = time};
}

/** @brief Double/triple click and drag use the same geometry as painting. */
static void
selection(notes_t *st, note_t *n)
{
    tiku_event_t event = click_at(n, 5, 2000000);
    char held[64];
    size_t length;
    tiku_richsession_t *session = tiku_richview_session(n->view);

    notes_event_in(st, n->id, &event);
    assert(tiku_richsession_text(session, held, sizeof held, &length) == 0);
    assert(!length);
    event.time_us += 100000;
    notes_event_in(st, n->id, &event);
    assert(tiku_richsession_text(session, held, sizeof held, &length) == 0);
    assert(!strcmp(held, "milk"));
    event.time_us += 100000;
    notes_event_in(st, n->id, &event);
    assert(tiku_richsession_text(session, held, sizeof held, &length) == 0);
    assert(!strcmp(held, "Buy milk"));
    event = click_at(n, 0, 4000000);
    notes_event_in(st, n->id, &event);
    event = click_at(n, 4, 4000100);
    event.type = TIKU_EVENT_POINTER_MOVE;
    notes_event_in(st, n->id, &event);
    event.type = TIKU_EVENT_POINTER_UP;
    notes_event_in(st, n->id, &event);
    event.type = TIKU_EVENT_POINTER_MOVE;
    event.x = 200;
    notes_event_in(st, n->id, &event);
    assert(tiku_richsession_text(session, held, sizeof held, &length) == 0);
    assert(!strcmp(held, "Buy "));
    type_text(st, n, "Get ", 4000200);
    text_is(n, "Get milk");
}

/** @brief Refused cut, Unicode deletion, plain-only formatting and undo. */
static void
editing(notes_t *st, note_t *n)
{
    tiku_event_t event = {.type = TIKU_EVENT_KEY_DOWN, .key = 'b',
        .modifiers = TIKU_MOD_CMD};
    tiku_richcmd_state_t state;
    tiku_richsession_t *session = tiku_richview_session(n->view);

    notes_pick(st, n->id, CMD_SELECT_ALL);
    fail_clip = 1;
    notes_pick(st, n->id, CMD_CUT);
    text_is(n, "Get milk");
    fail_clip = 0;
    notes_pick(st, n->id, CMD_COPY);
    assert(!strcmp(clipboard, "Get milk"));
    strcpy(clipboard, "a\xcc\x81\xf0\x9f\x98\x80");
    notes_pick(st, n->id, CMD_PASTE);
    text_is(n, clipboard);
    event.key = TIKU_KEY_BACKSPACE;
    event.modifiers = 0;
    notes_event_in(st, n->id, &event);
    text_is(n, "a\xcc\x81");
    notes_event_in(st, n->id, &event);
    text_is(n, "");
    notes_pick(st, n->id, CMD_UNDO);
    text_is(n, "a\xcc\x81");
    notes_pick(st, n->id, CMD_UNDO);
    text_is(n, clipboard);
    notes_pick(st, n->id, CMD_UNDO);
    text_is(n, "Get milk");
    event.key = 'b';
    event.modifiers = TIKU_MOD_CMD;
    notes_event_in(st, n->id, &event);
    assert(tiku_richsession_query(session, TIKU_RICHCMD_BOLD, &state) == 0);
    assert(state.check != TIKU_RICHCMD_CHECK_ON);
    notes_pick(st, n->id, CMD_CUT);
    text_is(n, "");
    assert(!strcmp(clipboard, "Get milk"));
    notes_pick(st, n->id, CMD_UNDO);
    text_is(n, "Get milk");
    event = (tiku_event_t){.type = TIKU_EVENT_TEXT_INPUT,
        .button = TIKU_TEXT_BEGIN, .dy = 8, .text = "ignored"};
    notes_event_in(st, n->id, &event);
    text_is(n, "Get milk");
    notes_pick(st, n->id, CMD_COPY);
    event.button = TIKU_TEXT_END;
    event.dx = 7;
    strcpy(event.text, "!");
    notes_event_in(st, n->id, &event);
    text_is(n, "Get milk");
}

/** @brief Snapshot saves cannot clear newer text or metadata changes. */
static void
save_races(notes_t *st, note_t *n)
{
    tiku_event_t end = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_END,
        .modifiers = TIKU_MOD_CMD};

    notes_event_in(st, n->id, &end);
    type_text(st, n, " A", 7000000);
    assert(!note_save_start(n) && n->save);
    type_text(st, n, " B", 7000001);
    n->important = 1;
    n->metadata++;
    assert(note_save_finish(n, 1) == 1);
    assert(note_dirty(n));
    assert(tiku_richdoc_dirty(n->doc));
    assert(n->metadata != n->saved_metadata);
    assert(file_has(n->path, "Get milk A\n"));
    assert(!file_has(n->path, " A B"));
    settle(st, 8000000);
    assert(file_has(n->path, "Get milk A B\n"));
    assert(file_has(n->path, "important=1"));
}

/** @brief Failed saves preserve the old file and keep the dirty note open. */
static void
save_failure(notes_t *st, note_t *n)
{
    char old[600];
    char recovery[64];
    struct stat info;
    uint32_t id = n->id;

    snprintf(old, sizeof old, "%s.saved", n->path);
    assert(!rename(n->path, old));
    assert(!mkdir(n->path, 0700));
    type_text(st, n, " unsaved", 9000000);
    assert(!note_save_start(n));
    assert(note_save_finish(n, 1) == -1);
    assert(note_dirty(n) && strstr(n->error, "Save failed"));
    assert(file_has(old, "Get milk A B\n"));
    assert(!note_recover(n, recovery, sizeof recovery));
    assert(!stat(recovery, &info) && (info.st_mode & 0777) == 0600);
    assert(file_has(recovery, "Get milk A B unsaved\n"));
    assert(note_dirty(n) && !unlink(recovery));
    notes_closed(st, id);
    assert(n->used && n->id && n->id != id);
    assert(note_save_finish(n, 1) == -1);
    assert(n->used && note_dirty(n));
    assert(!rmdir(n->path));
    assert(!rename(old, n->path));
    n->closing = 0;
    settle(st, 10000000);
    assert(file_has(n->path, "unsaved"));
}

/** @brief Preserve the surface on resize failure and input order in reflow. */
static void
resizing(notes_t *st, note_t *n)
{
    tiku_surface_t *old = n->surface;
    tiku_event_t event = {.type = TIKU_EVENT_RESIZE, .width = 500,
        .height = 320};
    int i;

    fail_surface = 1;
    notes_event_in(st, n->id, &event);
    assert(n->surface == old && n->w == NOTE_W && n->h == NOTE_H);
    fail_surface = 0;
    notes_event_in(st, n->id, &event);
    assert(n->w == 500 && n->h == 320 && n->surface != old);
    for (i = 0; i < 24; i++) {
        event.width = i % 2 ? 140 : 1000;
        event.height = i % 2 ? 80 : 700;
        notes_event_in(st, n->id, &event);
        assert(tiku_richview_layout(n->view));
    }
    notes_pick(st, n->id, CMD_SELECT_ALL);
    memset(clipboard, 'x', 6000);
    clipboard[6000] = 0;
    notes_pick(st, n->id, CMD_PASTE);
    event = (tiku_event_t){.type = TIKU_EVENT_KEY_DOWN,
        .key = TIKU_KEY_HOME, .modifiers = TIKU_MOD_CMD};
    notes_event_in(st, n->id, &event);
    type_text(st, n, "A", 11000000);
    settle(st, 12000000);
    assert(!n->queue_count && tiku_richview_layout(n->view));
    memmove(clipboard + 1, clipboard, 6001);
    clipboard[0] = 'A';
    text_is(n, clipboard);
}

/** @brief Reject malformed, oversized and symlink files without altering them. */
static void
bad_files(notes_t *st, const char *dir)
{
    char path[600], link[600];
    FILE *file;
    note_t *n = note_alloc(st);

    assert(n);
    snprintf(path, sizeof path, "%s/bad.txt", dir);
    file = fopen(path, "w");
    assert(file);
    fputs("# tiku-note paper=Yellow important=0\nbad\xff\n", file);
    assert(!fclose(file));
    assert(note_load(n, path) == -1);
    text_is(n, "");
    snprintf(link, sizeof link, "%s/symlink.txt", dir);
    assert(!symlink(path, link));
    assert(note_load(n, link) == -1);
    assert(!unlink(link));
    file = fopen(path, "w");
    assert(file && !ftruncate(fileno(file), NOTE_FILE_MAX + 1));
    assert(!fclose(file));
    assert(note_load(n, path) == -1);
    assert(!unlink(path));
    note_free(st, n);
}

/** @brief Run the app lifecycle and its shared-view regression cases. */
int
main(void)
{
    char dir[] = "/tmp/tiku-notes-test-XXXXXX", path[600];
    tiku_app_services_t services = {.open = open_win, .close = close_win,
        .frame = frame, .menus = menus, .configure = configure,
        .raise_window = raise_win, .clip_set = clip_set, .clip_text = clip_text};
    void *state = NULL;
    notes_t *st;
    note_t *n;

    assert(mkdtemp(dir));
    assert(!setenv("TIKU_NOTES", dir, 1));
    assert(!notes_start(&state, &services));
    st = state;
    n = &st->note[0];
    settle(st, 500000);
    assert(opens == 1 && frames && !strcmp(last_title, "Note"));
    assert(file_has(n->path, "# tiku-note paper=Yellow important=0\n\n"));
    type_text(st, n, "Buy milk", 1000000);
    assert(!strcmp(last_title, "Buy milk"));
    notes_tick(st, 1000000 + SAVE_AFTER_US - 1);
    assert(!n->save && !file_has(n->path, "Buy milk"));
    settle(st, 1500000);
    assert(file_has(n->path, "Buy milk"));
    selection(st, n);
    editing(st, n);
    settle(st, 6000000);
    notes_pick(st, n->id, CMD_IMPORTANT);
    settle(st, 6500000);
    assert(!strcmp(last_title, "! Get milk"));
    assert(file_has(n->path, "important=1"));
    notes_pick(st, n->id, CMD_NEW);
    assert(opens == 2 && last_menu_rows == 2);
    assert(!strcmp(last_menu_first, "! Get milk"));
    type_text(st, &st->note[1], "Later", 6500001);
    notes_pick(st, st->note[1].id, CMD_COLOUR + 2);
    settle(st, 7000000);
    assert(file_has(st->note[1].path, "paper=Blue"));
    notes_pick(st, st->note[1].id, CMD_SHOW);
    assert(raises == 1);
    strcpy(path, st->note[1].path);
    notes_pick(st, st->note[1].id, CMD_DELETE);
    assert(access(path, F_OK) && closes == 1);
    save_races(st, n);
    save_failure(st, n);
    resizing(st, n);
    bad_files(st, dir);
    strcpy(path, n->path);
    assert(notes_pick(st, n->id, CMD_QUIT) == 1);
    notes_closed(st, n->id);
    assert(!n->used && file_has(path, "Axxxx"));
    notes_stop(st);
    state = NULL;
    assert(!notes_start(&state, &services));
    st = state;
    text_is(&st->note[0], clipboard);
    assert(!note_dirty(&st->note[0]));
    assert(!tiku_richdoc_can_undo(st->note[0].doc));
    notes_stop(st);
    assert(!unlink(path) && !rmdir(dir));
    puts("PASS Notes: Rich View, selection, Unicode, clipboard, history, "
         "plain files, concurrent saves, failure retention, resize, reflow");
    return 0;
}
