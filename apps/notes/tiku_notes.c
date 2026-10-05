/* tiku_notes.c -- Sticky Notes: small coloured windows on the desk, each
 * a few lines of text, some marked important.  Each note is a window of
 * its own and a text file of its own, under a one-line head.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_text_input.h"
#include "tiku_textview.h"

#define NOTES_MAX   24
#define NOTE_W      240
#define NOTE_H      180
#define NOTE_MIN_W  140
#define NOTE_MIN_H  80
#define MARGIN      8
#define SAVE_AFTER_US 500000    /* half a second of quiet, then the file */
#define DOUBLE_CLICK_US 400000  /* as TIKU_TRK_DOUBLE_CLICK_US           */

/* The commands a note's menus carry; the Notes menu counts on from here. */
#define CMD_NEW        1
#define CMD_DELETE     2
#define CMD_IMPORTANT  3
#define CMD_QUIT       4
#define CMD_COLOUR     10       /* + colour index                        */
#define CMD_SHOW       100      /* + note slot: raise that note          */

/** @brief The four papers, in the order the menu offers them. */
static const struct { const char *name; tiku_rgb_t face; } PAPERS[] = {
    { "Yellow", 0xFFEE8Cu },
    { "Green",  0xC8F5AAu },
    { "Blue",   0xB4DCFFu },
    { "Pink",   0xFFC8DCu },
};
#define NPAPERS ((int)(sizeof PAPERS / sizeof PAPERS[0]))

/** @brief One note: its text, its paper, whether it is important, its
 *         window and its file. */
typedef struct {
    int               used;
    uint32_t          id;           /* window, 0 while closed          */
    tiku_surface_t   *surface;
    int               w, h;
    tiku_textview_t   tv;
    tiku_text_input_t commit;
    int               paper;        /* index into PAPERS               */
    int               important;
    char              path[512];    /* where it lives on disk          */
    int64_t           edited_at;    /* last edit, for the quiet save   */
    int               dirty;        /* the file lags the text          */
    int64_t           clicked_at;   /* the last press, for a double    */
    int               click_line, click_col, clicks;
    int               dragging;     /* a selection drag is under way   */
} note_t;

/** @brief The application: the notes folder and a slot for each note. */
typedef struct {
    const tiku_app_services_t *services;
    char    dir[512];               /* the notes folder                */
    note_t  note[NOTES_MAX];
    int     next_number;            /* for a fresh file name           */
} notes_t;

/*---------------------------------------------------------------------------*/
/* Where the notes live                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief The folder the notes are kept in, made if it is not there.
 *
 * The environment variable TIKU_NOTES names it; without it, it is
 * ~/.local/share/notes.
 * @return 0, or -1 when there is no folder to use.
 */
static int
notes_dir(char *out, size_t max)
{
    const char *given = getenv("TIKU_NOTES");
    const char *home = getenv("HOME");
    struct stat sb;
    char parent[512];

    if (given != NULL && given[0] != '\0') {
        snprintf(out, max, "%s", given);
    } else if (home != NULL && home[0] != '\0') {
        snprintf(parent, sizeof parent, "%s/.local/share", home);
        if (stat(parent, &sb) != 0) { (void)mkdir(parent, 0777); }
        snprintf(out, max, "%s/notes", parent);
    } else {
        return -1;
    }
    if (stat(out, &sb) != 0 && mkdir(out, 0777) != 0) {
        return -1;
    }
    return 0;
}

/** @brief The paper named in a file's head line, or the first paper. */
static int
paper_named(const char *name)
{
    int i;

    for (i = 0; i < NPAPERS; i++) {
        if (strncasecmp(PAPERS[i].name, name, strlen(PAPERS[i].name)) == 0) {
            return i;
        }
    }
    return 0;
}

/**
 * @brief Write a note to its file: one head line, then the text as it is.
 *
 * The head is a comment any editor will show and skip:
 *   # tiku-note paper=Yellow important=1
 */
static int
note_save(note_t *n)
{
    FILE *f = fopen(n->path, "w");
    int i, lines;

    if (f == NULL) {
        return -1;
    }
    fprintf(f, "# tiku-note paper=%s important=%d\n",
            PAPERS[n->paper].name, n->important ? 1 : 0);
    lines = tiku_textview_lines(&n->tv);
    for (i = 0; i < lines; i++) {
        const char *line = tiku_textview_line(&n->tv, i);

        fprintf(f, "%s\n", (line != NULL) ? line : "");
    }
    (void)fclose(f);
    tiku_textview_saved(&n->tv);
    n->dirty = 0;
    return 0;
}

/** @brief Read a note back from its file.  @return 0 when it was one. */
static int
note_load(note_t *n, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[TIKU_TEXTVIEW_LINE_MAX + 2];
    char *body = NULL;
    size_t used = 0, cap = 0;
    int first = 1;

    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        size_t len = strlen(line);

        if (first) {
            const char *p;

            first = 0;
            if (strncmp(line, "# tiku-note", 11) != 0) {
                (void)fclose(f);
                free(body);
                return -1;      /* somebody else's file */
            }
            p = strstr(line, "paper=");
            n->paper = (p != NULL) ? paper_named(p + 6) : 0;
            p = strstr(line, "important=");
            n->important = (p != NULL && p[10] == '1');
            continue;
        }
        if (used + len + 1 > cap) {
            char *grown;

            cap = (cap == 0) ? 1024 : cap * 2;
            while (used + len + 1 > cap) { cap *= 2; }
            grown = realloc(body, cap);
            if (grown == NULL) { break; }
            body = grown;
        }
        memcpy(body + used, line, len);
        used += len;
        body[used] = '\0';
    }
    (void)fclose(f);
    if (body != NULL && used > 0 && body[used - 1] == '\n') {
        body[used - 1] = '\0';  /* the newline each line was written with */
    }
    tiku_textview_set(&n->tv, (body != NULL) ? body : "");
    tiku_textview_saved(&n->tv);
    free(body);
    snprintf(n->path, sizeof n->path, "%s", path);
    n->dirty = 0;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Drawing                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief The title the tab and the Notes menu show: the first line, or
 *         "Note" when it is blank, after "! " for an important note. */
static void
note_title(const note_t *n, char *out, size_t max)
{
    const char *first = tiku_textview_line(&n->tv, 0);
    const char *p = (first != NULL) ? first : "";

    while (*p == ' ' || *p == '\t') { p++; }
    if (*p == '\0') {
        snprintf(out, max, "%sNote", n->important ? "! " : "");
    } else {
        snprintf(out, max, "%s%s", n->important ? "! " : "", p);
    }
}

/** @brief Paint the note and hand the frame over. */
static void
note_paint(notes_t *st, note_t *n)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t all = { 0, 0, n->w, n->h };
    tiku_rgb_t ink = TIKU_C_TEXT;
    tiku_rgb_t face = PAPERS[n->paper].face;
    int rows = (n->h - 2 * MARGIN) / f->height;
    int top, i, cy, cx;

    if (n->surface == NULL) {
        return;
    }
    if (rows < 1) { rows = 1; }
    (void)tiku_textview_reveal(&n->tv, rows);
    top = tiku_textview_top(&n->tv);
    tiku_fill(n->surface, all, face);
    tiku_vline(n->surface, 0, 0, n->h, tiku_tint(face, TIKU_DARKEN_2));
    for (i = 0; i < rows; i++) {
        const char *line = tiku_textview_line(&n->tv, top + i);

        if (line == NULL) { break; }
        tiku_text(n->surface, f, MARGIN, MARGIN + i * f->height + f->ascent,
                  line, ink);
    }
    /* The caret, as a hairline where the next letter goes. */
    tiku_textview_caret(&n->tv, &cy, &cx);
    if (cy >= top && cy < top + rows) {
        const char *line = tiku_textview_line(&n->tv, cy);
        char head[TIKU_TEXTVIEW_LINE_MAX];
        int cut = (line != NULL) ? (int)strlen(line) : 0;

        if (cut > cx) { cut = cx; }
        if (cut > (int)sizeof head - 1) { cut = (int)sizeof head - 1; }
        memcpy(head, (line != NULL) ? line : "", (size_t)cut);
        head[cut] = '\0';
        tiku_vline(n->surface, MARGIN + tiku_text_width(f, head),
                   MARGIN + (cy - top) * f->height, f->height, ink);
    }
    if (n->important) {
        /* An important note has a dark triangle in its top right
         * corner. */
        int s = 14, k;

        for (k = 0; k < s; k++) {
            tiku_hline(n->surface, n->w - s + k, k, s - k,
                       tiku_tint(face, TIKU_DARKEN_2));
        }
    }
    (void)st->services->frame(st->services->ctx, n->id, n->surface->px,
                              n->w, n->h);
}

/*---------------------------------------------------------------------------*/
/* Menus                                                                     */
/*---------------------------------------------------------------------------*/

static note_t *note_of(notes_t *st, uint32_t id);

/**
 * @brief This note's menus: what to do with it, and every note there is.
 *
 * The Notes menu lists every open note, the important ones first; each
 * row raises its note, and this note's own row is disabled.
 */
static void
note_publish(notes_t *st, note_t *n)
{
    tiku_menuset_t menus;
    int i, k, pass;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 2;
#define NOTE_MENU  menus.menu[0]
#define NOTES_MENU menus.menu[1]
    snprintf(NOTE_MENU.title, sizeof NOTE_MENU.title, "Note");
    k = 0;
    snprintf(NOTE_MENU.item[k].label, sizeof NOTE_MENU.item[k].label,
             "New note");
    NOTE_MENU.item[k].command = CMD_NEW;
    NOTE_MENU.item[k].enabled = 1;
    k++;
    snprintf(NOTE_MENU.item[k].label, sizeof NOTE_MENU.item[k].label,
             n->important ? "Not important" : "Important");
    NOTE_MENU.item[k].command = CMD_IMPORTANT;
    NOTE_MENU.item[k].enabled = 1;
    k++;
    NOTE_MENU.item[k].label[0] = '\0';          /* a rule */
    k++;
    for (i = 0; i < NPAPERS; i++) {
        snprintf(NOTE_MENU.item[k].label, sizeof NOTE_MENU.item[k].label,
                 "%s", PAPERS[i].name);
        NOTE_MENU.item[k].command = CMD_COLOUR + i;
        NOTE_MENU.item[k].enabled = (unsigned char)(i != n->paper);
        k++;
    }
    NOTE_MENU.item[k].label[0] = '\0';
    k++;
    snprintf(NOTE_MENU.item[k].label, sizeof NOTE_MENU.item[k].label,
             "Delete note");
    NOTE_MENU.item[k].command = CMD_DELETE;
    NOTE_MENU.item[k].enabled = 1;
    k++;
    snprintf(NOTE_MENU.item[k].label, sizeof NOTE_MENU.item[k].label,
             "Quit");
    NOTE_MENU.item[k].command = CMD_QUIT;
    NOTE_MENU.item[k].sc = 'q';
    NOTE_MENU.item[k].mods = TIKU_MOD_CMD;
    NOTE_MENU.item[k].enabled = 1;
    k++;
    NOTE_MENU.nitem = k;

    snprintf(NOTES_MENU.title, sizeof NOTES_MENU.title, "Notes");
    k = 0;
    for (pass = 1; pass >= 0; pass--) {
        int room = (int)(sizeof NOTES_MENU.item / sizeof NOTES_MENU.item[0]);

        for (i = 0; i < NOTES_MAX && k < room - 1; i++) {
            note_t *o = &st->note[i];

            if (!o->used || o->id == 0 || o->important != pass) { continue; }
            note_title(o, NOTES_MENU.item[k].label,
                       sizeof NOTES_MENU.item[k].label);
            NOTES_MENU.item[k].command = CMD_SHOW + i;
            NOTES_MENU.item[k].enabled = (unsigned char)(o != n);
            k++;
        }
    }
    NOTES_MENU.nitem = k;
#undef NOTE_MENU
#undef NOTES_MENU
    (void)st->services->menus(st->services->ctx, n->id, &menus);
}

/*---------------------------------------------------------------------------*/
/* Notes coming and going                                                    */
/*---------------------------------------------------------------------------*/

/** @brief The note whose window is @p id, or NULL. */
static note_t *
note_of(notes_t *st, uint32_t id)
{
    int i;

    for (i = 0; i < NOTES_MAX; i++) {
        if (st->note[i].used && st->note[i].id == id) {
            return &st->note[i];
        }
    }
    return NULL;
}

/** @brief Give a note its window, titled by its first line. */
static int
note_show(notes_t *st, note_t *n)
{
    char title[TIKU_TEXTVIEW_LINE_MAX];

    note_title(n, title, sizeof title);
    n->w = NOTE_W;
    n->h = NOTE_H;
    n->surface = tiku_surface_new(n->w, n->h, PAPERS[n->paper].face);
    if (n->surface == NULL) {
        return -1;
    }
    n->id = st->services->open(st->services->ctx, title, n->w, n->h);
    if (n->id == 0u) {
        tiku_surface_free(n->surface);
        n->surface = NULL;
        return -1;
    }
    if (st->services->configure != NULL) {
        (void)st->services->configure(st->services->ctx, n->id, title,
                                      0, 0, 1, 0);
    }
    if (st->services->sizeable != NULL) {
        (void)st->services->sizeable(st->services->ctx, n->id);
    }
    note_paint(st, n);
    note_publish(st, n);
    return 0;
}

/** @brief A free slot, prepared, or NULL when every one is taken. */
static note_t *
note_alloc(notes_t *st)
{
    int i;

    for (i = 0; i < NOTES_MAX; i++) {
        if (!st->note[i].used) {
            note_t *n = &st->note[i];

            memset(n, 0, sizeof *n);
            n->used = 1;
            tiku_textview_init(&n->tv);
            return n;
        }
    }
    return NULL;
}

/** @brief A new empty note, on the paper of the note it was asked from. */
static note_t *
note_new(notes_t *st, int paper)
{
    note_t *n = note_alloc(st);
    struct stat sb;

    if (n == NULL) {
        return NULL;
    }
    n->paper = paper;
    do {
        snprintf(n->path, sizeof n->path, "%s/note-%03d.txt", st->dir,
                 ++st->next_number);
    } while (stat(n->path, &sb) == 0);
    tiku_textview_set(&n->tv, "");
    (void)note_save(n);
    if (note_show(st, n) != 0) {
        (void)unlink(n->path);
        tiku_textview_free(&n->tv);
        n->used = 0;
        return NULL;
    }
    return n;
}

/** @brief Let go of a note's window, keeping or removing its file. */
static void
note_drop(notes_t *st, note_t *n, int remove_file)
{
    if (n->dirty && !remove_file) {
        (void)note_save(n);
    }
    if (n->id != 0u) {
        st->services->close(st->services->ctx, n->id);
        n->id = 0u;
    }
    if (n->surface != NULL) {
        tiku_surface_free(n->surface);
        n->surface = NULL;
    }
    if (remove_file) {
        (void)unlink(n->path);
    }
    tiku_textview_free(&n->tv);
    n->used = 0;
}

/** @brief Republish every open note's menus, since each Notes menu lists
 *         them all. */
static void
notes_republish(notes_t *st)
{
    int i;

    for (i = 0; i < NOTES_MAX; i++) {
        if (st->note[i].used && st->note[i].id != 0u) {
            note_publish(st, &st->note[i]);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* The application                                                           */
/*---------------------------------------------------------------------------*/

/** @brief Open every note the folder holds, in name order, or one new
 *         note when it holds none. */
static int
notes_start(void **state, const tiku_app_services_t *services)
{
    notes_t *st = calloc(1, sizeof *st);
    DIR *d;
    struct dirent *e;
    int opened = 0;

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    if (notes_dir(st->dir, sizeof st->dir) != 0) {
        free(st);
        return -1;
    }
    /* The notes the folder holds come back, NOTES_MAX at most, in name
     * order. */
    d = opendir(st->dir);
    if (d != NULL) {
        char names[NOTES_MAX][64];
        int count = 0, i, j;

        while ((e = readdir(d)) != NULL && count < NOTES_MAX) {
            size_t len = strlen(e->d_name);

            if (strncmp(e->d_name, "note-", 5) == 0 && len > 4 &&
                strcmp(e->d_name + len - 4, ".txt") == 0 && len < 64) {
                snprintf(names[count++], 64, "%s", e->d_name);
            }
        }
        (void)closedir(d);
        for (i = 1; i < count; i++) {          /* insertion sort, by name */
            char key[64];

            snprintf(key, sizeof key, "%s", names[i]);
            for (j = i; j > 0 && strcmp(names[j - 1], key) > 0; j--) {
                snprintf(names[j], 64, "%s", names[j - 1]);
            }
            snprintf(names[j], 64, "%s", key);
        }
        for (i = 0; i < count; i++) {
            note_t *n = note_alloc(st);
            char path[600];
            int number = 0;

            if (n == NULL) { break; }
            snprintf(path, sizeof path, "%s/%s", st->dir, names[i]);
            if (note_load(n, path) != 0 || note_show(st, n) != 0) {
                tiku_textview_free(&n->tv);
                n->used = 0;
                continue;
            }
            if (sscanf(names[i], "note-%d.txt", &number) == 1 &&
                number > st->next_number) {
                st->next_number = number;
            }
            opened++;
        }
    }
    /* A desk with no notes gets one to write on. */
    if (opened == 0 && note_new(st, 0) == NULL) {
        free(st);
        return -1;
    }
    notes_republish(st);
    *state = st;
    return 0;
}

/** @brief Close every note, saving what is unsaved. */
static void
notes_stop(void *state)
{
    notes_t *st = state;
    int i;

    if (st == NULL) {
        return;
    }
    for (i = 0; i < NOTES_MAX; i++) {
        if (st->note[i].used) {
            note_drop(st, &st->note[i], 0);
        }
    }
    free(st);
}

/** @brief Where the pointer landed, as a line and a column of it. */
static int
note_locate(note_t *n, int x, int y, int *line_out, int *col_out)
{
    const tiku_font_t *f = tiku_font_plain();
    int top = tiku_textview_top(&n->tv);
    int line = top + (y - MARGIN) / f->height;
    int lines = tiku_textview_lines(&n->tv);
    const char *text;
    int col = 0, len;
    char head[TIKU_TEXTVIEW_LINE_MAX];

    if (line < 0) { line = 0; }
    if (line >= lines) { line = lines - 1; }
    if (line < 0) { return 0; }
    text = tiku_textview_line(&n->tv, line);
    len = (text != NULL) ? (int)strlen(text) : 0;
    if (len > (int)sizeof head - 1) { len = (int)sizeof head - 1; }
    /* The column of the glyph under the pointer. */
    while (col < len) {
        memcpy(head, text, (size_t)(col + 1));
        head[col + 1] = '\0';
        if (tiku_text_width(f, head) > x - MARGIN) { break; }
        col++;
    }
    *line_out = line;
    *col_out = col;
    return 1;
}

/** @brief A byte a word is made of: a letter, a digit, '_', or a byte
 *         beyond ASCII. */
static int
word_byte(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80;
}

/** @brief Select the span from one column to another on a line. */
static void
note_select_span(note_t *n, int line, int from, int to)
{
    tiku_textview_select_none(&n->tv);
    tiku_textview_place(&n->tv, line, from);
    tiku_textview_select_begin(&n->tv);
    tiku_textview_place(&n->tv, line, to);
}

/**
 * @brief A press in the text.
 *
 * One click places the caret and arms a drag; a second within
 * DOUBLE_CLICK_US on the same spot takes the word under it, a third the
 * line; with Shift held the caret's old place stays the anchor, so the
 * selection grows to the click.
 */
static void
note_press(note_t *n, const tiku_event_t *ev)
{
    const char *text;
    int line, col, len, from, to;

    if (!note_locate(n, ev->x, ev->y, &line, &col)) {
        return;
    }
    if (ev->time_us - n->clicked_at <= DOUBLE_CLICK_US &&
        line == n->click_line && abs(col - n->click_col) <= 1) {
        n->clicks++;
    } else {
        n->clicks = 1;
    }
    n->clicked_at = ev->time_us;
    n->click_line = line;
    n->click_col = col;
    text = tiku_textview_line(&n->tv, line);
    len = (text != NULL) ? (int)strlen(text) : 0;
    if (n->clicks == 2) {
        from = col;
        to = col;
        while (from > 0 && word_byte((unsigned char)text[from - 1])) { from--; }
        while (to < len && word_byte((unsigned char)text[to])) { to++; }
        if (from == to) {          /* on a gap: the run of non-word bytes */
            while (from > 0 && !word_byte((unsigned char)text[from - 1])) {
                from--;
            }
            while (to < len && !word_byte((unsigned char)text[to])) { to++; }
        }
        note_select_span(n, line, from, to);
        return;
    }
    if (n->clicks >= 3) {
        note_select_span(n, line, 0, len);
        return;
    }
    if (ev->modifiers & TIKU_MOD_SHIFT) {
        tiku_textview_select_begin(&n->tv);
    } else {
        tiku_textview_select_none(&n->tv);
    }
    tiku_textview_place(&n->tv, line, col);
    n->dragging = 1;
}

/** @brief A drag in the text: the selection follows the pointer. */
static int
note_drag(note_t *n, int x, int y)
{
    int line, col;

    if (!n->dragging || !note_locate(n, x, y, &line, &col)) {
        return 0;
    }
    tiku_textview_select_begin(&n->tv);
    tiku_textview_place(&n->tv, line, col);
    return 1;
}

/** @brief A key typed into a note: Ctrl+A selects all; the arrows, Home
 *         and End move, Shift extending the selection; Delete, Backspace,
 *         Return and printable characters edit.  @return 1 when taken. */
static int
note_key(note_t *n, const tiku_event_t *ev)
{
    unsigned ch = (unsigned char)ev->text[0];
    int shift = (ev->modifiers & TIKU_MOD_SHIFT) != 0;
    int is_move = ev->key == TIKU_KEY_LEFT || ev->key == TIKU_KEY_RIGHT ||
                  ev->key == TIKU_KEY_UP || ev->key == TIKU_KEY_DOWN ||
                  ev->key == TIKU_KEY_HOME || ev->key == TIKU_KEY_END;

    if ((ev->modifiers & TIKU_MOD_CMD) && ev->key == 'a') {
        tiku_textview_select_all(&n->tv);
        return 1;
    }
    if (is_move) {
        if (shift) { tiku_textview_select_begin(&n->tv); }
        else { tiku_textview_select_none(&n->tv); }
    }
    switch (ev->key) {
    case TIKU_KEY_LEFT:  tiku_textview_move(&n->tv, TIKU_TEXTVIEW_LEFT);  break;
    case TIKU_KEY_RIGHT: tiku_textview_move(&n->tv, TIKU_TEXTVIEW_RIGHT); break;
    case TIKU_KEY_UP:    tiku_textview_move(&n->tv, TIKU_TEXTVIEW_UP);    break;
    case TIKU_KEY_DOWN:  tiku_textview_move(&n->tv, TIKU_TEXTVIEW_DOWN);  break;
    case TIKU_KEY_HOME:  tiku_textview_move(&n->tv, TIKU_TEXTVIEW_HOME);  break;
    case TIKU_KEY_END:   tiku_textview_move(&n->tv, TIKU_TEXTVIEW_END);   break;
    case TIKU_KEY_DELETE:
        tiku_textview_delete(&n->tv);    n->dirty = 1; break;
    case TIKU_KEY_BACKSPACE:
        tiku_textview_backspace(&n->tv); n->dirty = 1; break;
    case TIKU_KEY_RETURN:
        tiku_textview_newline(&n->tv);   n->dirty = 1; break;
    default:
        if (ch >= 32u && ch < 127u && !(ev->modifiers & TIKU_MOD_CMD)) {
            tiku_textview_insert(&n->tv, (char)ch);
            n->dirty = 1;
        } else {
            return 0;
        }
    }
    return 1;
}

/** @brief A note window's event: a resize, the pointer, keys and composed
 *         text; Ctrl+N makes a new note.  The title follows the first
 *         line as it is typed. */
static int
notes_event_in(void *state, uint32_t window, const tiku_event_t *ev)
{
    notes_t *st = state;
    note_t *n = note_of(st, window);
    int changed = 0;

    if (n == NULL) {
        return 0;
    }
    if (ev->type == TIKU_EVENT_RESIZE) {
        int w = (ev->width < NOTE_MIN_W) ? NOTE_MIN_W : ev->width;
        int h = (ev->height < NOTE_MIN_H) ? NOTE_MIN_H : ev->height;

        if (w != n->w || h != n->h) {
            tiku_surface_free(n->surface);
            n->surface = tiku_surface_new(w, h, PAPERS[n->paper].face);
            n->w = w;
            n->h = h;
            changed = 1;
        }
    } else if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        note_press(n, ev);
        changed = 1;
    } else if (ev->type == TIKU_EVENT_POINTER_MOVE) {
        changed = note_drag(n, ev->x, ev->y);
    } else if (ev->type == TIKU_EVENT_POINTER_UP && ev->button == 1) {
        n->dragging = 0;
    } else if (ev->type == TIKU_EVENT_KEY_DOWN) {
        if ((ev->modifiers & TIKU_MOD_CMD) && ev->key == 'n') {
            note_t *fresh = note_new(st, n->paper);

            if (fresh != NULL) { notes_republish(st); }
            return 0;
        }
        changed = note_key(n, ev);
    } else if (ev->type == TIKU_EVENT_TEXT_INPUT) {
        /* Text an input method composed is inserted whole. */
        int r = tiku_text_input_event(&n->commit, ev);

        if (r == TIKU_TEXT_COMPLETE && n->commit.text[0] != '\0') {
            (void)tiku_textview_commit(&n->tv, n->commit.text);
            n->dirty = 1;
            changed = 1;
        }
    }
    if (changed) {
        if (n->dirty) {
            n->edited_at = ev->time_us;
        }
        note_paint(st, n);
        if (n->dirty) {
            /* The first line is the title: it changes as it is typed. */
            char title[TIKU_TEXTVIEW_LINE_MAX];

            note_title(n, title, sizeof title);
            if (st->services->configure != NULL) {
                (void)st->services->configure(st->services->ctx, n->id, title,
                                              0, 0, 1, 0);
            }
        }
    }
    return 0;                   /* nonzero ends the app */
}

/** @brief Each turn: save every note left unedited for SAVE_AFTER_US. */
static void
notes_tick(void *state, int64_t now_us)
{
    notes_t *st = state;
    int i;

    /* A note is written after half a second without edits, not on every
     * keystroke. */
    for (i = 0; i < NOTES_MAX; i++) {
        note_t *n = &st->note[i];

        if (n->used && n->dirty && now_us - n->edited_at >= SAVE_AFTER_US) {
            (void)note_save(n);
            notes_republish(st);   /* its title may have changed */
        }
    }
}

/** @brief A menu pick from a note's window.  @return 1 for Quit. */
static int
notes_pick(void *state, uint32_t window, int command)
{
    notes_t *st = state;
    note_t *n = note_of(st, window);

    if (n == NULL) {
        return 0;
    }
    if (command == CMD_QUIT) {
        return 1;               /* 1 ends the app */
    }
    if (command == CMD_NEW) {
        if (note_new(st, n->paper) != NULL) { notes_republish(st); }
        return 0;
    }
    if (command == CMD_DELETE) {
        note_drop(st, n, 1);
        notes_republish(st);
        return 0;
    }
    if (command == CMD_IMPORTANT) {
        char title[TIKU_TEXTVIEW_LINE_MAX];

        n->important = !n->important;
        (void)note_save(n);
        note_title(n, title, sizeof title);
        if (st->services->configure != NULL) {
            (void)st->services->configure(st->services->ctx, n->id, title,
                                          0, 0, 1, 0);
        }
        note_paint(st, n);
        notes_republish(st);
        return 0;
    }
    if (command >= CMD_COLOUR && command < CMD_COLOUR + NPAPERS) {
        n->paper = command - CMD_COLOUR;
        (void)note_save(n);
        note_paint(st, n);
        note_publish(st, n);
        return 0;
    }
    if (command >= CMD_SHOW && command < CMD_SHOW + NOTES_MAX) {
        note_t *o = &st->note[command - CMD_SHOW];

        if (o->used && o->id != 0u && st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx, o->id);
        }
        return 0;
    }
    return 0;
}

/** @brief The desk took a note's window: the note stays on disk. */
static void
notes_closed(void *state, uint32_t window)
{
    notes_t *st = state;
    note_t *n = note_of(st, window);

    if (n == NULL) {
        return;
    }
    n->id = 0u;                 /* the window is already gone */
    note_drop(st, n, 0);
    notes_republish(st);
}

/** @brief Sticky Notes' descriptor: exported to a loader, or run by
 *         main(). */
const tiku_app_descriptor_t tiku_notes_app = {
    .id = "org.tikuos.notes",
    .name = "Sticky Notes",
    .start = notes_start,
    .stop = notes_stop,
    .tick = notes_tick,
    .pick = notes_pick,
    .event_in = notes_event_in,
    .closed = notes_closed
};

#ifdef TIKU_APP_SO
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_notes_app
};
#endif

#ifndef TIKU_APP_EMBED
/** @brief Run Sticky Notes as a client of the desktop. */
int
main(void)
{
    return tiku_client_run(&tiku_notes_app);
}
#endif
