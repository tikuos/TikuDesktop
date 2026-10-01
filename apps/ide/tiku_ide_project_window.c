/* tiku_ide_project_window.c -- project window.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"
#include "tiku_atomic_file.h"

/** @brief A project file with one line left out, for tiku_atomic_file(). */
typedef struct {
    const char *text;
    size_t      len, cut, resume;   /* the line left out is [cut, resume) */
} removal_t;

static const char *
row_name(void *ctx, int row);
static void
project_show(ide_t *st);
static char *
read_whole(const char *path, size_t *len);
static int
write_without(FILE *f, void *ctx);

tiku_rect_t
tiku_ide_project_body(const ide_t *st)
{
    tiku_rect_t r = { MARGIN, MARGIN, st->proj_w - 2 * MARGIN,
                           st->proj_h - STRIP_H - 2 * MARGIN };

    return r;
}

/* The rows sit INSIDE the sunken box, clear of its bevel: a row's
 * white fill drawn at the box edge would erase the border under it, and
 * the heading would look like it spilled onto the grey. */
tiku_rect_t
tiku_ide_project_list_area(const ide_t *st)
{
    return tiku_inset(tiku_ide_project_body(st), 2);
}

/** @brief A window of ours that grows: the project's list, the console
 *         and the messages all show more with more room. */
void
tiku_ide_window_sizeable(ide_t *st, uint32_t id)
{
    if (id != 0u && st->services->sizeable != NULL) {
        (void)st->services->sizeable(st->services->ctx, id);
    }
}

void
tiku_ide_project_sizeable(ide_t *st)
{
    tiku_ide_window_sizeable(st, st->proj_id);
}

static const char *
row_name(void *ctx, int row)
{
    ide_t *st = ctx;

    return (row >= 0 && row < st->proj.nrow) ? st->proj.row[row].name
                                             : NULL;
}

void
tiku_ide_project_paint(ide_t *st)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t all = { 0, 0, st->proj_w, st->proj_h };
    tiku_rect_t strip = { 0, st->proj_h - STRIP_H, st->proj_w, STRIP_H };
    tiku_rect_t body = tiku_ide_project_body(st);
    char says[160];

    /* Target updates can arrive while only the splash exists. A frame
     * for ID zero has no receiver and must not seed the shared-image cache. */
    if (st->proj_surface == NULL || st->proj_id == 0u) {
        return;
    }
    tiku_fill(st->proj_surface, all, TIKU_C_PANEL);
    tiku_ui_sunken(st->proj_surface, body, TIKU_C_DOC);
    tiku_list_draw(&st->tv_list, st->proj_surface, tiku_ide_project_list_area(st),
                   row_name, st);
    tiku_fill(st->proj_surface, strip, TIKU_C_PANEL);
    if (st->note[0] != '\0') {
        snprintf(says, sizeof says, "%s", st->note);
    } else if (st->proj_file[0] == '\0') {
        snprintf(says, sizeof says,
                 "no project open -- the Project menu starts one");
    } else {
        /* FILES, not rows: a group's name is furniture, and a project
         * holding one heading and nothing under it is empty. */
        int files = 0;
        int i;

        for (i = 0; i < st->proj.nrow; i++) {
            files += !st->proj.row[i].heading;
        }
                const char *name = (st->proj.title[0] != '\0') ? st->proj.title
                                                        : "a project";

        if (files == 0) {
            snprintf(says, sizeof says, "%s: empty -- New File… adds one",
                     name);
        } else {
            snprintf(says, sizeof says, "%s: %d file%s", name, files,
                     files == 1 ? "" : "s");
        }
    }
    tiku_text(st->proj_surface, f, MARGIN,
                   strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                   says, TIKU_C_TEXT);
    if (st->ask.open) {
        int aw, ah;

        tiku_alert_size(&aw, &ah);
        if (st->ask.tag == CMD_SAVEBOARD) { ah = 220; }
        if (aw > st->proj_w - 16) {
            aw = st->proj_w - 16;
        }
        st->ask_frame = (tiku_rect_t){ (st->proj_w - aw) / 2,
                                            (st->proj_h - ah) / 3, aw, ah };
        tiku_alert_draw(&st->ask, st->proj_surface, st->ask_frame);
    }
    (void)st->services->frame(st->services->ctx, st->proj_id,
                              st->proj_surface->px, st->proj_w, st->proj_h);
}

/**
 * @brief The project window's menus, the Window menu among them.
 *
 * A row per open editor, marked when what it holds is unsaved -- so the
 * one place that lists the windows is also the one place that says
 * which of them has work in it.
 */
void
tiku_ide_project_publish(ide_t *st)
{
    tiku_menuset_t menus;

    memset(&menus, 0, sizeof menus);
        menus.nmenu = 3;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Project");
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, APP_ABOUT);
    menus.menu[0].item[0].command = CMD_ABOUT;
    menus.menu[0].item[0].enabled = 1;
    /* The groups a person reads at a glance: what this IS, then the
     * projects, then the work, then the leaving. */
    menus.menu[0].item[1].separator = 1;
    snprintf(menus.menu[0].item[2].label,
             sizeof menus.menu[0].item[2].label, "New Project…");
    menus.menu[0].item[2].command = CMD_NEWPROJ;
    menus.menu[0].item[2].enabled = 1;
    snprintf(menus.menu[0].item[3].label,
             sizeof menus.menu[0].item[3].label, "Open Project…");
    menus.menu[0].item[3].command = CMD_OPEN;
    menus.menu[0].item[3].enabled = 1;
    {
    int mrow = 4;
    snprintf(menus.menu[0].item[mrow].label,
             sizeof menus.menu[0].item[mrow].label, "Recover unsaved files");
    menus.menu[0].item[mrow].command = CMD_RECOVER;
    menus.menu[0].item[mrow++].enabled = 1;
    menus.menu[0].item[mrow].separator = 1;
    mrow++;
#ifndef TIKU_IDE_C
    {
        /* Run is offered for the row the list is standing on, and only
         * when its NAME says the interpreter would know what to do
         * with it -- the same thing the editor's own Run asks.  TikuC
         * does not offer it: nothing here runs a C program yet, and a
         * row that could never be taken is furniture. */
        int row = tiku_list_chosen(&st->tv_list);
        int can = (row >= 0 && row < st->proj.nrow &&
                   !st->proj.row[row].heading &&
                   tiku_ide_lang_of(st->proj.row[row].name) == TIKU_SYNTAX_BASIC &&
                   !st->running);

        snprintf(menus.menu[0].item[mrow].label,
                 sizeof menus.menu[0].item[mrow].label, "Run%s%s",
                 (row >= 0 && row < st->proj.nrow &&
                  !st->proj.row[row].heading) ? " " : "",
                 (row >= 0 && row < st->proj.nrow &&
                  !st->proj.row[row].heading) ? st->proj.row[row].name
                                              : "");
        menus.menu[0].item[mrow].command = CMD_RUN;
        menus.menu[0].item[mrow].enabled = (unsigned char)can;
        {
            /* The desktop chooses the destination; Run names it here. */
            char dest[64];

            if (tiku_ide_dest_board(st, dest, sizeof dest)) {
                snprintf(menus.menu[0].item[mrow].label,
                         sizeof menus.menu[0].item[mrow].label, "Run%s%s on %s",
                         (row >= 0 && row < st->proj.nrow &&
                          !st->proj.row[row].heading) ? " " : "",
                         (row >= 0 && row < st->proj.nrow &&
                          !st->proj.row[row].heading) ? st->proj.row[row].name
                                                      : "",
                         dest);
                mrow++;
                snprintf(menus.menu[0].item[mrow].label,
                         sizeof menus.menu[0].item[mrow].label,
                         "Save on %s", dest);
                menus.menu[0].item[mrow].command = CMD_SAVEBOARD;
                menus.menu[0].item[mrow].enabled =
                    (unsigned char)(st->proj.nrow > 0 && !st->running);
            }
            if (st->running) {
                /* A run started from here has to be stoppable from here:
                 * the editor's Stop is on a window this run never opened. */
                mrow++;
                snprintf(menus.menu[0].item[mrow].label,
                         sizeof menus.menu[0].item[mrow].label, "Stop");
                menus.menu[0].item[mrow].command = CMD_STOP;
                menus.menu[0].item[mrow].enabled = 1;
            }
        }
        mrow++;
    }
#endif
    /* Build names what it is FOR, because that is the decision the
     * project already made and the one a person needs to see before
     * they trust what comes back. */
    {
        /* BASIC first: Build appears only when a project SAYS what it
         * is being built for.  A menu offering a road nobody asked for
         * is a generic tool wearing a focused name.  TikuC builds by
         * DEFAULT -- asking the compiler is what it is here for -- and
         * a project naming no target is asked about on this machine. */
        int at = mrow;

#ifndef TIKU_IDE_C
        if (st->proj.target[0] != '\0')
#endif
        {
            snprintf(menus.menu[0].item[at].label,
                     sizeof menus.menu[0].item[at].label, "Build for %s",
                     (st->proj.target[0] != '\0') ? st->proj.target
                                                  : "host");
            menus.menu[0].item[at].command = CMD_BUILD;
            menus.menu[0].item[at].enabled =
                (unsigned char)(st->proj.nrow > 0 && !st->running);
            at++;
        }
        menus.menu[0].item[at].separator = 1;
        at++;
        snprintf(menus.menu[0].item[at].label,
                 sizeof menus.menu[0].item[at].label, "Close Project");
        menus.menu[0].item[at].command = CMD_SHUT;
        menus.menu[0].item[at].enabled = 1;
        menus.menu[0].nitem = at + 1;
    }
    }

    /* The two roads a file takes INTO a project, in a menu of their
     * own: File is where hands look for files, and Project is about
     * the project.  Neither road means anything without a project for
     * it to land in, and the rows say so by dimming. */
    snprintf(menus.menu[1].title, sizeof menus.menu[1].title, "File");
    snprintf(menus.menu[1].item[0].label,
             sizeof menus.menu[1].item[0].label, "New File…");
    menus.menu[1].item[0].command = CMD_NEWFILE;
    menus.menu[1].item[0].enabled =
        (unsigned char)(st->proj_file[0] != '\0');
    /* Open File… asks for no project at all: an editor on the file,
     * and nothing written anywhere.  Reading is not joining. */
    snprintf(menus.menu[1].item[1].label,
             sizeof menus.menu[1].item[1].label, "Open File…");
    menus.menu[1].item[1].command = CMD_OPENFILE;
    menus.menu[1].item[1].enabled = 1;
    snprintf(menus.menu[1].item[2].label,
             sizeof menus.menu[1].item[2].label, "Add File…");
    menus.menu[1].item[2].command = CMD_ADDFILE;
    menus.menu[1].item[2].enabled =
        (unsigned char)(st->proj_file[0] != '\0');
    menus.menu[1].item[3].separator = 1;
    {
        /* The road OUT that Add File… promised a partner for: the
         * selected row leaves the DESCRIPTION and only that -- the
         * file stays on the disk, exactly as BeIDE meant it. */
        int row = tiku_list_chosen(&st->tv_list);

        snprintf(menus.menu[1].item[4].label,
                 sizeof menus.menu[1].item[4].label, "Remove File");
        menus.menu[1].item[4].command = CMD_REMOVEFILE;
        menus.menu[1].item[4].enabled =
            (unsigned char)(st->proj_file[0] != '\0' &&
                            row >= 0 && row < st->proj.nrow &&
                            !st->proj.row[row].heading);
    }
    menus.menu[1].nitem = 5;

    tiku_ide_window_menu(st, &menus.menu[2]);
    (void)st->services->menus(st->services->ctx, st->proj_id, &menus);
}

/** @brief Show @p pj: the rows, with the groups among them as headings. */
static void
project_show(ide_t *st)
{
    int i;

    tiku_list_init(&st->tv_list, 1);
    (void)tiku_list_set_count(&st->tv_list, st->proj.nrow);
    for (i = 0; i < st->proj.nrow; i++) {
        tiku_list_set_heading(&st->tv_list, i, st->proj.row[i].heading);
    }
    st->note[0] = '\0';
    if (st->proj.unknown > 0) {
        snprintf(st->note, sizeof st->note,
                 "%d line%s this build does not know", st->proj.unknown,
                 st->proj.unknown == 1 ? "" : "s");
    }
    tiku_ide_project_paint(st);
    tiku_ide_project_publish(st);
}

/** @brief Open what the person picked in the shell's panel. */
void
tiku_ide_project_take(ide_t *st, const char *path)
{
    static char whole[64 * 1024];
    FILE *f = fopen(path, "r");
    char dir[512];
    char *slash;
    size_t got;

    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "cannot read %s", path);
        tiku_ide_project_paint(st);
        return;
    }
    got = fread(whole, 1u, sizeof whole - 1u, f);
    whole[got] = '\0';
    (void)fclose(f);
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    (void)tiku_ide_project_parse(&st->proj, whole, dir);
    /* Copied ASIDE first: a caller may hand this the very buffer it is
     * about to be written into, and printf over itself is nothing. */
        if (path != st->proj_file) {
        snprintf(st->proj_file, sizeof st->proj_file, "%s", path);
    }
    project_show(st);
}

/**
 * @brief Put @p path into the project, making the file when it is new.
 *
 * The description is APPENDED to, never rewritten: a rewrite from what
 * this build parsed would silently drop every line it did not know,
 * which is exactly the forward road the skipping rule keeps open.
 */
void
tiku_ide_project_add_file(ide_t *st, const char *path)
{
    size_t dirlen = strlen(st->proj.dir);
    const char *leaf;
    FILE *f;
    int i;

    if (st->proj_file[0] == '\0') {
        return;                 /* no project to put it in */
    }
    /* Inside the project's own folder, because the description names
     * its files RELATIVELY -- a project is a folder somebody can move,
     * and a file outside it would be left behind. */
    if (strncmp(path, st->proj.dir, dirlen) != 0 ||
        path[dirlen] != '/') {
        snprintf(st->note, sizeof st->note,
                 "a project's files live in its folder");
        tiku_ide_project_paint(st);
        return;
    }
    leaf = path + dirlen + 1;
    for (i = 0; i < st->proj.nrow; i++) {
        if (!st->proj.row[i].heading &&
            strcmp(st->proj.row[i].path, path) == 0) {
            tiku_ide_edit_open(st, &st->proj.row[i]);
            return;             /* already listed: adding means opening */
        }
    }
    f = fopen(path, "a");       /* made when new, kept when not */
    if (f != NULL) {
        (void)fclose(f);
    }
    f = fopen(st->proj_file, "a+");
    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "cannot write the project");
        tiku_ide_project_paint(st);
        return;
    }
    /* A last line with no break would run into the new entry. */
    if (fseek(f, -1L, SEEK_END) == 0 && fgetc(f) != '\n') {
        (void)fputc('\n', f);
    }
    (void)fprintf(f, "file %s\n", leaf);
    (void)fclose(f);
    tiku_ide_project_take(st, st->proj_file);
    for (i = 0; i < st->proj.nrow; i++) {
        if (!st->proj.row[i].heading &&
            strcmp(st->proj.row[i].path, path) == 0) {
            tiku_list_select_only(&st->tv_list, i);
            tiku_ide_edit_open(st, &st->proj.row[i]);
            return;
        }
    }
}

/** @brief All of the file at @p path, NUL-closed; NULL when it cannot be
 *         read or memory runs out. */
static char *
read_whole(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    size_t cap = 4096u, n = 0u;
    char *text = malloc(cap);

    if (f == NULL || text == NULL) {
        if (f != NULL) { (void)fclose(f); }
        free(text);
        return NULL;
    }
    for (;;) {
        size_t got = fread(text + n, 1u, cap - n - 1u, f);
        char *grown;

        n += got;
        if (n + 1u < cap) {
            break;
        }
        grown = (cap <= SIZE_MAX / 2u) ? realloc(text, cap * 2u) : NULL;
        if (grown == NULL) {
            free(text);
            (void)fclose(f);
            return NULL;
        }
        text = grown;
        cap *= 2u;
    }
    if (ferror(f)) {
        free(text);
        (void)fclose(f);
        return NULL;
    }
    (void)fclose(f);
    text[n] = '\0';
    *len = n;
    return text;
}

/** @brief Write the project file without its one removed line. */
static int
write_without(FILE *f, void *ctx)
{
    const removal_t *r = ctx;

    return fwrite(r->text, 1u, r->cut, f) != r->cut ||
           fwrite(r->text + r->resume, 1u, r->len - r->resume, f) !=
               r->len - r->resume;
}

/**
 * @brief Take the SELECTED row out of the project's description.
 *
 * Out of the PROJECT, not off the DISK: the file stays where it is,
 * exactly as BeIDE meant it.  The first `file` line naming the row, read
 * the way the project is read, is the one left out; every other byte
 * rides through untouched, and the description is replaced whole, so a
 * failure leaves it as it was.
 */
void
tiku_ide_project_remove_file(ide_t *st)
{
    int row = tiku_list_chosen(&st->tv_list);
    removal_t r = { NULL, 0u, 0u, 0u };
    char *text;
    size_t at;

    if (st->proj_file[0] == '\0' ||
        row < 0 || row >= st->proj.nrow || st->proj.row[row].heading) {
        return;
    }
    text = read_whole(st->proj_file, &r.len);
    if (text == NULL) {
        return;
    }
    for (at = 0u; at < r.len; ) {
        const char *nl = memchr(text + at, '\n', r.len - at);
        size_t end = (nl != NULL) ? (size_t)(nl - text) + 1u : r.len;
        char name[sizeof st->proj.row[0].name];

        if (tiku_ide_project_file_entry(text + at, name, sizeof name) &&
            strcmp(name, st->proj.row[row].name) == 0) {
            r.cut = at;
            r.resume = end;
            break;
        }
        at = end;
    }
    if (r.resume == 0u) {
        free(text);
        return;                 /* no line names it */
    }
    r.text = text;
    if (tiku_atomic_file(st->proj_file, write_without, &r) != 0) {
        snprintf(st->note, sizeof st->note, "cannot write the project");
        tiku_ide_project_paint(st);
        free(text);
        return;
    }
    free(text);
    tiku_ide_project_take(st, st->proj_file);
}

/**
 * @brief Make a project where the person said, and open it.
 *
 * A project IS a folder: the name the person typed becomes a folder of
 * that name, holding the description and the program's first source
 * file -- which opens, because a person who just made a project is
 * there to write it.  A name that already means something is OPENED
 * rather than emptied: an existing description as itself, an existing
 * folder by the description inside it.
 */
void
tiku_ide_project_create(ide_t *st, const char *path)
{
    char projfile[560];
    char mainfile[560];
    struct stat sb;
    const char *leaf = strrchr(path, '/');
    int i;

    leaf = (leaf != NULL) ? leaf + 1 : path;
    if (stat(path, &sb) == 0 && !S_ISDIR(sb.st_mode)) {
        /* An existing FILE: the person picked a description itself. */
        tiku_ide_project_take(st, path);
        return;
    }
    if (stat(path, &sb) != 0 && mkdir(path, 0777) != 0) {
        snprintf(st->note, sizeof st->note, "cannot make %s", path);
        tiku_ide_project_paint(st);
        return;
    }
    snprintf(projfile, sizeof projfile, "%s/project.tiku", path);
    snprintf(mainfile, sizeof mainfile, "%s/%s", path, APP_MAIN_FILE);
    if (stat(projfile, &sb) != 0) {
        FILE *f = fopen(projfile, "w");

        if (f == NULL) {
            snprintf(st->note, sizeof st->note, "cannot write %s",
                     projfile);
            tiku_ide_project_paint(st);
            return;
        }
        (void)fprintf(f, "title %s\ngroup Sources\nfile %s\n", leaf,
                      APP_MAIN_FILE);
        (void)fclose(f);
        f = fopen(mainfile, "a");
        if (f != NULL) {
            (void)fclose(f);
        }
    }
    tiku_ide_project_take(st, projfile);
    for (i = 0; i < st->proj.nrow; i++) {
        if (!st->proj.row[i].heading &&
            strcmp(st->proj.row[i].name, APP_MAIN_FILE) == 0) {
            tiku_list_select_only(&st->tv_list, i);
            tiku_ide_edit_open(st, &st->proj.row[i]);
            break;
        }
    }
}

/** @brief Open whatever row the list is standing on. */
void
tiku_ide_open_chosen(ide_t *st)
{
    int row = tiku_list_chosen(&st->tv_list);

    if (row >= 0 && row < st->proj.nrow &&
        !st->proj.row[row].heading) {
        tiku_ide_edit_open(st, &st->proj.row[row]);
    }
}
