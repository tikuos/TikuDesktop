/* Sticky Notes without a desk: notes come back from disk, typing reaches
 * the file after a quiet moment, marks and papers land in the head line,
 * and the Notes menu lists what matters first. */
#include <assert.h>
#include <sys/stat.h>
#define TIKU_APP_EMBED
#include "../../apps/notes/tiku_notes.c"

static int opens, closes, frames, raises, menus_seen;
static uint32_t next_id = 1;
static char last_title[128], last_menu_first[64], first_menu_title[32];
static int last_menu_rows;

static uint32_t open_win(void *ctx, const char *title, int w, int h)
{ (void)ctx; (void)w; (void)h; opens++; snprintf(last_title, sizeof last_title, "%s", title); return next_id++; }
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; frames++; return 1; }
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *m)
{
    (void)ctx; (void)id; menus_seen++;
    assert(m->nmenu == 2);
    snprintf(first_menu_title, sizeof first_menu_title, "%s", m->menu[0].title);
    last_menu_rows = m->menu[1].nitem;
    snprintf(last_menu_first, sizeof last_menu_first, "%s",
             m->menu[1].nitem > 0 ? m->menu[1].item[0].label : "");
    return 1;
}
static void close_win(void *ctx, uint32_t id) { (void)ctx; (void)id; closes++; }
static int configure(void *ctx, uint32_t id, const char *title, int w, int h, int r, int z)
{ (void)ctx; (void)id; (void)w; (void)h; (void)r; (void)z; snprintf(last_title, sizeof last_title, "%s", title); return 1; }
static int raise_win(void *ctx, uint32_t id) { (void)ctx; (void)id; raises++; return 1; }

static void type_text(notes_t *st, uint32_t win, const char *text, int64_t at)
{
    for (; *text; text++) {
        tiku_event_t e = { .type = TIKU_EVENT_KEY_DOWN, .time_us = at };
        e.text[0] = *text; e.key = (unsigned)(unsigned char)*text;
        assert(notes_event_in(st, win, &e) == 0);
    }
}
static int file_has(const char *path, const char *needle)
{
    char buf[2048]; FILE *f = fopen(path, "r"); size_t n;
    if (!f) return 0;
    n = fread(buf, 1, sizeof buf - 1, f); buf[n] = '\0'; fclose(f);
    return strstr(buf, needle) != NULL;
}

int main(void)
{
    char dir[] = "/tmp/tiku-notes-test-XXXXXX";
    char path[600];
    tiku_app_services_t sv = { 0 };
    void *state = NULL;
    notes_t *st;

    assert(mkdtemp(dir) != NULL);
    setenv("TIKU_NOTES", dir, 1);
    sv.open = open_win; sv.frame = frame; sv.menus = menus; sv.close = close_win;
    sv.configure = configure; sv.raise_window = raise_win;

    /* An empty folder: one note to write on, its file already there. */
    assert(notes_start(&state, &sv) == 0); st = state;
    assert(opens == 1 && frames >= 1);
    assert(!strcmp(last_title, "Note"));
    snprintf(path, sizeof path, "%s/note-001.txt", dir);
    assert(file_has(path, "# tiku-note paper=Yellow important=0"));

    /* Typing: the title follows the first line at once, the file after
     * half a second of quiet, and not before. */
    type_text(st, 1, "Buy milk", 1000);
    assert(!strcmp(last_title, "Buy milk"));
    assert(!file_has(path, "Buy milk"));
    notes_tick(state, 1000 + SAVE_AFTER_US - 1);
    assert(!file_has(path, "Buy milk"));
    notes_tick(state, 1000 + SAVE_AFTER_US);
    assert(file_has(path, "Buy milk"));

    /* The hand: two clicks on a word take the word, three take the line,
     * a drag takes what it crosses -- and typing replaces what is held. */
    {
        tiku_event_t e = { .type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                           .x = 40, .y = 12, .time_us = 2000000 };
        char held[64];
        int y0, x0, y1, x1;

        assert(notes_event_in(st, 1, &e) == 0);
        assert(!tiku_textview_selection(&st->note[0].tv, &y0, &x0, &y1, &x1));
        e.time_us += 100000;
        assert(notes_event_in(st, 1, &e) == 0);
        assert(tiku_textview_selected(&st->note[0].tv, held, sizeof held) > 0);
        assert(!strcmp(held, "milk"));
        e.time_us += 100000;
        assert(notes_event_in(st, 1, &e) == 0);
        assert(tiku_textview_selected(&st->note[0].tv, held, sizeof held) > 0);
        assert(!strcmp(held, "Buy milk"));
        e.time_us += 1000000;             /* long after: a plain click */
        e.x = 4;
        assert(notes_event_in(st, 1, &e) == 0);
        assert(!tiku_textview_selection(&st->note[0].tv, &y0, &x0, &y1, &x1));
        e.type = TIKU_EVENT_POINTER_MOVE; e.x = 40;
        assert(notes_event_in(st, 1, &e) == 0);
        assert(tiku_textview_selected(&st->note[0].tv, held, sizeof held) > 0);
        assert(!strncmp(held, "Buy", 3) && strlen(held) >= 4);
        e.type = TIKU_EVENT_POINTER_UP;
        assert(notes_event_in(st, 1, &e) == 0);
        e.type = TIKU_EVENT_POINTER_MOVE; e.x = 200;
        assert(notes_event_in(st, 1, &e) == 0);
        {
            char after[64];
            assert(tiku_textview_selected(&st->note[0].tv, after, sizeof after) > 0);
            assert(!strcmp(after, held));     /* the hand is up: no growth */
        }
        type_text(st, 1, "Get ", 4000000);  /* over "Buy " */
        assert(!strcmp(last_title, "Get milk"));
        notes_tick(state, 4000000 + SAVE_AFTER_US);
        assert(file_has(path, "Get milk"));
    }

    /* Important: the title wears the mark, the head line says so, and the
     * Notes menu puts it first. */
    assert(notes_pick(state, 1, CMD_IMPORTANT) == 0);
    assert(!strcmp(last_title, "! Get milk"));
    assert(file_has(path, "important=1"));
    assert(notes_pick(state, 1, CMD_NEW) == 0);
    assert(opens == 2 && !strcmp(last_title, "Note"));
    type_text(st, 2, "Later", 5000);
    assert(last_menu_rows == 2 && !strcmp(last_menu_first, "! Get milk"));
    assert(!strcmp(first_menu_title, "Note"));

    /* Paper: the head line names it. */
    assert(notes_pick(state, 2, CMD_COLOUR + 2) == 0);
    snprintf(path, sizeof path, "%s/note-002.txt", dir);
    assert(file_has(path, "paper=Blue"));

    /* Raising another note from the Notes menu, and deleting one. */
    assert(notes_pick(state, 2, CMD_SHOW + 0) == 0 && raises == 1);
    assert(notes_pick(state, 2, CMD_DELETE) == 0);
    { struct stat sb; assert(stat(path, &sb) != 0); }
    assert(closes == 1);

    /* Only Quit ends the application; nothing else a pick or a key does
     * may, since the client reads nonzero as the end. */
    assert(notes_pick(state, 1, CMD_QUIT) == 1);

    /* The desk taking a window keeps the note on disk. */
    notes_closed(state, 1);
    snprintf(path, sizeof path, "%s/note-001.txt", dir);
    assert(file_has(path, "Get milk"));
    notes_stop(state);

    /* And a fresh start brings the survivor back, titled by its line. */
    opens = 0; state = NULL;
    assert(notes_start(&state, &sv) == 0);
    assert(opens == 1 && !strcmp(last_title, "! Get milk"));
    notes_stop(state);

    /* A notes path that names a regular file is no folder to use: Notes
     * does not start. */
    {
        FILE *f;

        snprintf(path, sizeof path, "%s/not-a-folder", dir);
        f = fopen(path, "w");
        assert(f != NULL && fclose(f) == 0);
        setenv("TIKU_NOTES", path, 1);
        opens = 0; state = NULL;
        if (notes_start(&state, &sv) != -1) {
            puts("FAIL: Notes started on a regular file as its folder");
            return 1;
        }
        assert(opens == 0 && state == NULL);
    }
    puts("PASS: notes persist, select by hand, save after quiet, mark, paper, list, delete, reopen, refuse a file as the folder");
    return 0;
}
