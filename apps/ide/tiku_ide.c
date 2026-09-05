/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ide.c - TikuBasic and TikuC: a place to work on a PROGRAM.
 *
 * One source, two personalities: built plain it is TikuBasic, built
 * with TIKU_IDE_C it is TikuC.  The difference is what each puts
 * forward, Run for the interpreter and Build for the compiler, and
 * nothing else.  The project window lists what the program is made of,
 * grouped as the project file groups it; opening a row opens an editor
 * window of its own, the Window menu reaches every one, and a file
 * already open is raised rather than opened twice.  It is the same kit
 * machinery as the simple editor, a text view, the syntax tables and
 * the shell's file panel, held in several windows at once.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tiku_alert.h"
#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_list.h"
#include "tiku_iconpaint.h"
#include "tiku_image.h"
#include "tiku_logo.h"
#include "tiku_splash.h"
#include "tiku_syntax.h"
#include "tiku_clipboard.h"
#include "tiku_field.h"
#include "tiku_textview.h"
#include "tiku_ui.h"

/* Which program this build of the source IS. */
#ifdef TIKU_IDE_C
#define APP_NAME      "TikuC"
#define APP_ABOUT     "About TikuC"
#define APP_ID        "org.tikuos.tikuc"
#define APP_NEW_FILE  "untitled.c"
#define APP_MAIN_FILE "main.c"
#else
#define APP_NAME      "TikuBasic"
#define APP_ABOUT     "About TikuBasic"
#define APP_ID        "org.tikuos.tikubasic"
#define APP_NEW_FILE  "untitled.bas"
#define APP_MAIN_FILE "main.bas"
#endif

#define IDE_W      320
#define IDE_H      300
/* An editor is where the WORK happens, so it opens at half the screen
 * rather than at a peephole somebody enlarges every single time. */
#define EDIT_W     620
#define EDIT_H     420
#define STRIP_H    22
#define TOOLBAR_H  30
#define PAGE_SB    12   /* the page's own scrollbar gutter, at the right */
#define FIND_H     26   /* the find bar, dropped under the toolbar */
#define MARGIN     6

/* The editor toolbar's two tools, in the window's own coordinates. */
#define TB_ICON  24
#define TB_SAVE  (tiku_rect_t){ 8, 3, TB_ICON, TB_ICON }
#define TB_RUN   (tiku_rect_t){ 44, 3, TB_ICON, TB_ICON }
#define TB_STOP  (tiku_rect_t){ 72, 3, TB_ICON, TB_ICON }
#define TB_UNDO  (tiku_rect_t){ 108, 3, TB_ICON, TB_ICON }
#define TB_REDO  (tiku_rect_t){ 136, 3, TB_ICON, TB_ICON }
#define TB_FIND  (tiku_rect_t){ 172, 3, TB_ICON, TB_ICON }
#define TB_RENUM (tiku_rect_t){ 208, 3, TB_ICON, TB_ICON }

#define MSG_W      420
#define MSG_H      220
/* Wide, the way the banners those splash screens wore are wide: the
 * band takes a picture cut close to three-to-one edge to edge. */
#define SPLASH_W   560
#define SPLASH_H   300
#define SPLASH_ART 148          /* the art field's width, on the left */
/* How long the splash stands before leaving on its own. */
#define SPLASH_US  2500000

#define IDE_ROWS   64
/* A session may hold four windows over the wire, and the project and
 * the messages are two of them. */
#define IDE_EDITS  2
#define IDE_MSGS   200
/* How long a run may take before it is stopped rather than trusted. */
#define IDE_RUN_LIMIT_US 30000000

#define CMD_OPEN     1
#define CMD_QUIT     2
#define CMD_SAVE     3
#define CMD_SHUT     4
#define CMD_RUN      5
#define CMD_FOLLOW   6
#define CMD_BUILD    7
#define CMD_ABOUT    11
#define CMD_OPENFILE 12
#define CMD_NEWPROJ  8
#define CMD_NEWFILE  9
#define CMD_ADDFILE  10

/* Which question the shell's panel is answering, when it answers. */
enum {
    ASK_OPEN = 0,               /* Open Project…: the path IS a project */
    ASK_NEWPROJ,                /* New Project…: make it, then open it  */
    ASK_FILE,                   /* New/Add File…: put it in the project */
    ASK_EDIT                    /* Open File…: just an editor on it     */
};
#define CMD_CUT      13
#define CMD_COPY     14
#define CMD_PASTE    15
#define CMD_SELALL   16
#define CMD_UNDO     17
#define CMD_REDO     18
#define CMD_RENUM    19
#define CMD_STOP     20
#define CMD_FIND     21
#define CMD_REMOVEFILE 22
#define CMD_WINDOW   64         /* +i: the i'th editor of the Window menu */

/** @brief One line of the project window: a file, or a group's name. */
typedef struct {
    char name[64];
    char path[512];             /* empty for a heading */
    int  heading;
} prow_t;

typedef struct {
    char  title[64];
    char  dir[512];             /* what the file's own paths are from */
    char  target[32];           /* what the program is being built FOR */
    int   nrow;
    int   unknown;              /* lines this build did not know */
    prow_t row[IDE_ROWS];
} project_t;

/**
 * @brief The compiler a target is built with.
 *
 * The names the tree's own Makefile already detects, so a project says
 * what it is FOR and the toolchain follows from that rather than from
 * a path somebody typed.  A target this build has never heard of gets
 * no compiler and is told so; a compiler that is not installed says so
 * itself, through the same road every other failure takes.
 */
static const char *
compiler_for(const char *target)
{
    static const struct { const char *target, *cc; } WHICH[] = {
        { "host",      "cc"                 },
        { "rp2350",    "arm-none-eabi-gcc"  },
        { "ambiq",     "arm-none-eabi-gcc"  },
        { "nordic",    "arm-none-eabi-gcc"  },
        { "stm32n6",   "arm-none-eabi-gcc"  },
        { "ra8p1",     "arm-none-eabi-gcc"  },
        { "msp430",    "msp430-elf-gcc"     }
    };
    size_t i;

    for (i = 0; i < sizeof WHICH / sizeof WHICH[0]; i++) {
        if (strcmp(target, WHICH[i].target) == 0) {
            return WHICH[i].cc;
        }
    }
    return NULL;
}

/** @brief One editor window: its file, its text, and its window. */
typedef struct {
    uint32_t             id;
    tiku_surface_t *surface;    /* NULL when the seat is free */
    int                  w, h;       /* the window's CURRENT content */
    tiku_textview_t tv;
    tiku_syntax_lang_t   lang;
    char                 path[512];
    char                 name[64];
    int                  drag_select;   /* a press began on the page */
    int                  finding;        /* the find bar is open */
    tiku_field_t         find;           /* what is being searched for */
    int                  scroll_x;       /* the page slid left, for long lines */
    int                  tip;            /* the toolbar tool under the pointer, -1 none */
} edit_t;

/**
 * @brief One thing a run said.
 *
 * The whole of what the IDE knows about a backend: words, and -- when
 * the words were about somewhere -- the file and the line they were
 * about.  Whatever runs a program later says the same three things or
 * it does not get a window.
 */
typedef struct {
    char text[160];
    char file[512];             /* empty when it names nowhere */
    int  line;                  /* the line, or 0 for none */
    /* WHAT the line counts.  A compiler counts the lines of the file; a
     * BASIC program's lines wear their own numbers and are not in file
     * order at all.  Getting this wrong lands the caret somewhere
     * plausible and wrong, which is worse than not moving. */
    int  numbered;              /* nonzero: the line is a BASIC number */
} msg_t;

typedef struct {
    const tiku_app_services_t *services;
    uint32_t                        proj_id;
    tiku_surface_t            *proj_surface;
    tiku_list_t                tv_list;
    project_t                       proj;
    edit_t                          edit[IDE_EDITS];
    /* The question closing asks while there is work nothing has saved.
     * ONE question for the whole project, naming what is at stake: a
     * question per window is a person pressing the same button three
     * times without reading it. */
    tiku_alert_t               ask;
    tiku_rect_t                ask_frame;
    char                            note[128];
    /* The project FILE itself, because adding to a project means
     * appending to the file that describes it -- appending, so the
     * lines this build does not know survive being edited by it. */
    char                            proj_file[512];
    int                             asked;      /* which question, ASK_* */
    /* The click before this one, because opening by mouse is two of
     * them: the same row, close together.  The host synthesises no
     * double-click event, and the row's own clock is the event's. */
    int                             click_row;
    int64_t                         click_at;
    /* The splash: the application's name, shown while it arrives and
     * gone before it matters.  A key or a click sends it away early,
     * because a person who is already working is not an audience. */
    uint32_t                        splash_id;
    tiku_surface_t            *splash_surface;
    int64_t                         splash_from;
    int                             splash_stage;
    /* The person's own banner, when they have put one where this
     * program looks; NULL wears the built-in sweep. */
    tiku_surface_t            *splash_art;
    /* The About box: the same announcement, standing until somebody
     * has read it.  One of it, raised when asked for again. */
    uint32_t                        about_id;
    tiku_surface_t            *about_surface;
    /* What the last run said, and the window it says it in.  The
     * window arrives when there is something to put in it: a message
     * window standing empty is furniture. */
    uint32_t                        msg_id;
    tiku_surface_t            *msg_surface;
    tiku_list_t                msg_list;
    msg_t                           msg[IDE_MSGS];
    int                             nmsg;
    /* The run itself: a child on the far end of a pipe, pumped from
     * tick, which is called wherever this application is hosted. */
    int                             running;
    int                             child_fd;
    pid_t                           child;
    char                            child_file[512];
    char                            partial[160];
    int                             child_numbered; /* a BASIC run */
    int64_t                         run_from;   /* when it started */
} ide_t;

static void project_paint(ide_t *st);
static void project_publish(ide_t *st);
static void messages_show(ide_t *st);
static void splash_drop(ide_t *st);
static void announce_paint_art(tiku_surface_t *sf, const char *status,
                               const tiku_surface_t *art);

/*---------------------------------------------------------------------------*/
/* The project file                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Take the next blank-separated word of @p p into @p out. */
static const char *
word(const char *p, char *out, size_t max)
{
    size_t n = 0u;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    while (*p != '\0' && *p != '\n' && *p != ' ' && *p != '\t') {
        if (n + 1u < max) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
    return p;
}

/** @brief The rest of the line, blanks trimmed off both ends. */
static const char *
tail(const char *p, char *out, size_t max)
{
    size_t n = 0u;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    while (*p != '\0' && *p != '\n') {
        if (n + 1u < max) {
            out[n++] = *p;
        }
        p++;
    }
    while (n > 0u && (out[n - 1u] == ' ' || out[n - 1u] == '\t' ||
                      out[n - 1u] == '\r')) {
        n--;
    }
    out[n] = '\0';
    return p;
}

static const char *
line_end(const char *p)
{
    while (*p != '\0' && *p != '\n') {
        p++;
    }
    return (*p == '\n') ? p + 1 : p;
}

/**
 * @brief Read a project description into @p pj.
 *
 * A line this build does not know is skipped and counted rather than
 * refusing the project: the lines a BUILD will need were written before
 * anything here could act on them, and a project that opens showing
 * what it can is worth more than one that refuses to open at all.
 *
 * @return the number of rows read.
 */
static int
project_parse(project_t *pj, const char *text, const char *dir)
{
    const char *p = text;

    memset(pj, 0, sizeof *pj);
    snprintf(pj->dir, sizeof pj->dir, "%s", (dir != NULL) ? dir : ".");
    if (text == NULL) {
        return 0;
    }
    while (*p != '\0') {
        char kind[16];
        const char *rest = word(p, kind, sizeof kind);
        prow_t *row;

        if (kind[0] == '\0' || kind[0] == '#') {
            p = line_end(p);
            continue;               /* a blank line, or a remark */
        }
        if (strcmp(kind, "title") == 0) {
            (void)tail(rest, pj->title, sizeof pj->title);
            p = line_end(p);
            continue;
        }
        if (strcmp(kind, "target") == 0) {
            /* What the program is FOR.  It was an unknown line until
             * something could act on it, which is exactly what the
             * skipping rule is for. */
            (void)word(rest, pj->target, sizeof pj->target);
            p = line_end(p);
            continue;
        }
        if (pj->nrow >= IDE_ROWS) {
            pj->unknown++;
            p = line_end(p);
            continue;
        }
        row = &pj->row[pj->nrow];
        memset(row, 0, sizeof *row);
        if (strcmp(kind, "group") == 0) {
            row->heading = 1;
            (void)tail(rest, row->name, sizeof row->name);
        } else if (strcmp(kind, "file") == 0) {
            char at[sizeof row->path];

            (void)word(rest, row->name, sizeof row->name);
            /* Paths are from the project's own directory, so a project
             * is a FOLDER somebody can move.  Built aside and copied,
             * because the two halves live in the same object. */
            snprintf(at, sizeof at, "%s/%s", pj->dir, row->name);
            snprintf(row->path, sizeof row->path, "%s", at);
        } else {
            pj->unknown++;
            p = line_end(p);
            continue;
        }
        pj->nrow++;
        p = line_end(p);
    }
    return pj->nrow;
}

/*---------------------------------------------------------------------------*/
/* Editors                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief The editor holding @p path, or NULL. */
static edit_t *
edit_of_path(ide_t *st, const char *path)
{
    int i;

    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface != NULL &&
            strcmp(st->edit[i].path, path) == 0) {
            return &st->edit[i];
        }
    }
    return NULL;
}

/** @brief The editor in window @p id, or NULL. */
static edit_t *
edit_of_id(ide_t *st, uint32_t id)
{
    int i;

    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface != NULL && st->edit[i].id == id) {
            return &st->edit[i];
        }
    }
    return NULL;
}

/** @brief What a name says the language is; only the name can say. */
static tiku_syntax_lang_t
lang_of(const char *name)
{
    return tiku_syntax_of_path(name);
}

/**
 * @brief Draw the toolbar's own icons -- pictures, like the run arrow,
 *        because a worded button is a second menu.
 *
 * Undo and redo are curved arrows, back and forward, DIMMED when there
 * is nothing to take back or put again; find is a glass.  Drawn from
 * the same lines the rest of the window is, so they carry no baked art.
 */
static void
tb_arrow(tiku_surface_t *s, int x, int back, int on)
{
    tiku_rgb_t c = on ? TIKU_RGB(64, 64, 64) : TIKU_RGB(178, 178, 178);
    int mid = 3 + TB_ICON / 2;          /* the toolbar y of the middle */
    int hx = back ? x + 7 : x + 16;     /* the arrowhead's tip */
    int dir = back ? 1 : -1;

    /* the shaft, and a hook rising at the tail so it reads as a RETURN
     * rather than a plain move */
    tiku_hline(s, x + 7, mid, 10, c);
    tiku_vline(s, back ? x + 16 : x + 7, mid - 4, 5, c);
    tiku_hline(s, back ? x + 13 : x + 7, mid - 4, 4, c);
    /* the arrowhead */
    tiku_pixel(s, hx + dir, mid - 2, c);
    tiku_pixel(s, hx + dir * 2, mid - 1, c);
    tiku_pixel(s, hx + dir, mid + 2, c);
    tiku_pixel(s, hx + dir * 2, mid + 1, c);
}

static void
tb_glass(tiku_surface_t *s, int x)
{
    tiku_rgb_t c = TIKU_RGB(64, 64, 64);
    int i;

    /* the ring, an octagon of the lines available (toolbar y = 3 + n) */
    tiku_hline(s, x + 8, 3 + 6, 4, c);
    tiku_hline(s, x + 8, 3 + 13, 4, c);
    tiku_vline(s, x + 6, 3 + 8, 4, c);
    tiku_vline(s, x + 13, 3 + 8, 4, c);
    tiku_pixel(s, x + 7, 3 + 7, c);
    tiku_pixel(s, x + 12, 3 + 7, c);
    tiku_pixel(s, x + 7, 3 + 12, c);
    tiku_pixel(s, x + 12, 3 + 12, c);
    /* the handle, out of the lower right */
    for (i = 0; i < 4; i++) {
        tiku_pixel(s, x + 14 + i, 3 + 14 + i, c);
        tiku_pixel(s, x + 15 + i, 3 + 14 + i, c);
    }
}

/**
 * @brief Which toolbar tool sits at @p x,@p y for this page, or -1.
 *
 * One table for the pointer and the tip, so a tool cannot be named
 * without being hittable or hit without wearing a name.
 */
static int
tb_tool_at(const edit_t *e, int x, int y)
{
    static const tiku_rect_t R[] = {
        { 8, 3, TB_ICON, TB_ICON },     /* 0 Save */
        { 44, 3, TB_ICON, TB_ICON },    /* 1 Run / Build */
        { 72, 3, TB_ICON, TB_ICON },    /* 2 Stop */
        { 108, 3, TB_ICON, TB_ICON },   /* 3 Undo */
        { 136, 3, TB_ICON, TB_ICON },   /* 4 Redo */
        { 172, 3, TB_ICON, TB_ICON },   /* 5 Find */
        { 208, 3, TB_ICON, TB_ICON }    /* 6 Renumber */
    };
    int i;

    for (i = 0; i < (int)(sizeof R / sizeof R[0]); i++) {
        if (x >= R[i].x && x < R[i].x + R[i].w &&
            y >= R[i].y && y < R[i].y + R[i].h) {
            if ((i == 2 || i == 6) && e->lang != TIKU_SYNTAX_BASIC) {
                return -1;      /* stop and renumber are BASIC's */
            }
            if (i == 1 && e->lang != TIKU_SYNTAX_BASIC &&
                e->lang != TIKU_SYNTAX_C) {
                return -1;
            }
            return i;
        }
    }
    return -1;
}

/** @brief The name a hovered tool wears on its tip. */
static const char *
tb_tool_name(const edit_t *e, int tool)
{
    static const char *const N[] = {
        "Save", "Run", "Stop", "Undo", "Redo", "Find", "Renumber"
    };

    if (tool == 1 && e->lang == TIKU_SYNTAX_C) {
        return "Build";
    }
    return (tool >= 0 && tool < 7) ? N[tool] : "";
}

/** @brief The stop square, red while a run is there to stop. */
static void
tb_stop(tiku_surface_t *s, int x, int on)
{
    tiku_rect_t sq = { x + 6, 3 + 6, 12, 12 };

    tiku_fill(s, sq, on ? TIKU_RGB(178, 52, 52)
                        : TIKU_RGB(178, 178, 178));
}

/** @brief The build mallet: a head and a handle, from the lines available. */
static void
tb_mallet(tiku_surface_t *s, int x)
{
    tiku_rgb_t c = TIKU_RGB(64, 64, 64);
    tiku_rect_t head = { x + 5, 3 + 5, 14, 5 };
    int i;

    tiku_fill(s, head, c);
    tiku_vline(s, x + 11, 3 + 10, 9, c);
    tiku_vline(s, x + 12, 3 + 10, 9, c);
    (void)i;
}

/** @brief Renumber: the ten every line is counted by, and an arrow on. */
static void
tb_renum(tiku_surface_t *s, int x)
{
    const tiku_font_t *f = tiku_font_at(11);
    tiku_rgb_t c = TIKU_RGB(64, 64, 64);
    int mid = 3 + TB_ICON / 2;

    tiku_text(s, f, x + 3, 3 + 6 + f->ascent, "10", c);
    tiku_hline(s, x + 15, mid + 1, 5, c);
    tiku_pixel(s, x + 18, mid, c);
    tiku_pixel(s, x + 18, mid + 2, c);
    tiku_pixel(s, x + 17, mid - 1, c);
    tiku_pixel(s, x + 17, mid + 3, c);
}

/**
 * @brief Where the PAGE begins: below the toolbar, and below the find
 *        bar when it is open, which drops between them.
 */
static int
edit_page_top(const edit_t *e)
{
    return TOOLBAR_H + (e->finding ? FIND_H : 0);
}

static void
edit_paint(ide_t *st, edit_t *e)
{
    const tiku_font_t *f = tiku_font_plain();
    int ptop = edit_page_top(e);
    tiku_rect_t bar = { 0, 0, e->w, TOOLBAR_H };
    tiku_rect_t page = { 0, ptop, e->w, e->h - STRIP_H - ptop };
    tiku_rect_t strip = { 0, e->h - STRIP_H, e->w, STRIP_H };
    int rows = (e->h - STRIP_H - ptop - MARGIN) / (f->height + 2);
    tiku_syntax_state_t carry = TIKU_SYNTAX_OPEN;
    int top = tiku_textview_top(&e->tv);
    int i;
    char where[160];

    if (e->surface == NULL) {
        return;
    }
    {
        /* The page follows the caret sideways on a line longer than the
         * window: the caret's own x is measured, and the slide kept just
         * enough that it stays inside the text area.  Home takes the
         * caret to column zero, whose x is zero, which pulls the slide
         * back to nothing -- so the view comes home on its own. */
        int cy, cx, cut, caret_x;
        const char *ct;
        int room = (e->w - PAGE_SB) - MARGIN;
        char head[TIKU_TEXTVIEW_LINE_MAX];

        tiku_textview_caret(&e->tv, &cy, &cx);
        ct = tiku_textview_line(&e->tv, cy);
        cut = tiku_textview_line_len(&e->tv, cy);
        if (cut > cx) { cut = cx; }
        if (cut > (int)sizeof head - 1) { cut = (int)sizeof head - 1; }
        memcpy(head, (ct != NULL) ? ct : "", (size_t)cut);
        head[cut] = '\0';
        caret_x = tiku_text_width(f, head);
        if (caret_x - e->scroll_x < 0) {
            e->scroll_x = caret_x;
        }
        if (room > 16 && caret_x - e->scroll_x > room - 8) {
            e->scroll_x = caret_x - (room - 8);
        }
        if (e->scroll_x < 0) {
            e->scroll_x = 0;
        }
    }
    /*
     * C carries a remark between lines, so the first line SHOWN cannot
     * be told apart on its own: the walk starts at the document's own
     * beginning, where the state is known, and comes forward.  Nothing
     * is cached, because a cache of this would have to be invalidated
     * by every edit and the arithmetic of that is worth more than the
     * walk -- a document is bounded, and a window is redrawn by hand.
     */
    if (e->lang == TIKU_SYNTAX_C) {
        tiku_span_t span[64];

        for (i = 0; i < top; i++) {
            const char *text = tiku_textview_line(&e->tv, i);

            if (text == NULL) {
                break;
            }
            (void)tiku_syntax_spans_on(e->lang, text, carry, &carry, span,
                                       (int)(sizeof span / sizeof span[0]));
        }
    }
    /* The toolbar: the two gestures a person reaches for while a hand
     * is on the mouse, as PICTURES -- a toolbar of worded buttons is a
     * second menu, and the reach was the point. */
    tiku_fill(e->surface, bar, TIKU_C_PANEL);
    {
        tiku_rect_t sv = TB_SAVE;

        (void)tiku_icon_paint(e->surface, "floppy", sv.x, sv.y,
                                  TB_ICON);
    }
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* The run arrow, drawn rather than baked: rows of the triangle
         * every player has worn since tape. */
        tiku_rect_t rn = TB_RUN;
        tiku_rgb_t go = TIKU_RGB(52, 140, 62);
        int i;

        for (i = 0; i < TB_ICON - 6; i++) {
            int half = (i < (TB_ICON - 6) / 2)
                           ? i
                           : (TB_ICON - 7 - i);

            tiku_hline(e->surface, rn.x + 4,
                            rn.y + 3 + i, 2 + half * 2, go);
        }
    }
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* Stop stands beside Run, red only while something runs. */
        tb_stop(e->surface, TB_STOP.x, st->running);
    }
    if (e->lang == TIKU_SYNTAX_C) {
        /* The C page's run is a BUILD: the mallet where the arrow is. */
        tb_mallet(e->surface, TB_RUN.x);
    }
    {
        /* Undo, redo and find join the reach: the edits a hand on the
         * mouse wants most, beside Save and Run. */
        tb_arrow(e->surface, TB_UNDO.x, 1,
                 tiku_textview_can_undo(&e->tv));
        tb_arrow(e->surface, TB_REDO.x, 0,
                 tiku_textview_can_redo(&e->tv));
        tb_glass(e->surface, TB_FIND.x);
    }
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* The tidy that renumbers by tens, for the language whose
         * lines wear their numbers. */
        tb_renum(e->surface, TB_RENUM.x);
    }
    {
        /* A thin rule under the tools, so the bar reads as furniture
         * rather than as the top of the page. */
        tiku_hline(e->surface, 0, TOOLBAR_H - 1, e->w,
                        tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    }
    if (e->finding) {
        /* The find bar, dropped between the toolbar and the page: a
         * word to look for, Return to walk the hits, Escape to put it
         * away.  For a BASIC file, a number typed here goes to that
         * LINE -- the label says so, because the language's lines wear
         * their numbers and "find 250" reads two ways. */
        tiku_rect_t fbar = { 0, TOOLBAR_H, e->w, FIND_H };
        tiku_rect_t fld;
        const char *label = (e->lang == TIKU_SYNTAX_BASIC)
                                ? "Find / line:" : "Find:";
        int lw = tiku_text_width(f, label);

        tiku_fill(e->surface, fbar, TIKU_C_PANEL);
        tiku_hline(e->surface, 0, TOOLBAR_H + FIND_H - 1, e->w,
                        tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
        tiku_text(e->surface, f, MARGIN,
                       TOOLBAR_H + (FIND_H - f->height) / 2 + f->ascent,
                       label, TIKU_C_TEXT);
        fld = (tiku_rect_t){ MARGIN + lw + 6, TOOLBAR_H + 3,
                             e->w - (MARGIN + lw + 6) - MARGIN,
                             FIND_H - 6 };
        tiku_ui_textfield(e->surface, fld, e->find.text,
                          e->find.caret, 0u);
    }
    tiku_ui_sunken(e->surface, page, TIKU_C_DOC);
    for (i = 0; i < rows; i++) {
        int line = top + i;
        const char *text = tiku_textview_line(&e->tv, line);
        int y = ptop + MARGIN + i * (f->height + 2);

        if (text == NULL) {
            break;
        }
        if (e->lang != TIKU_SYNTAX_NONE) {
            tiku_span_t span[64];
            int n = tiku_syntax_spans_on(e->lang, text, carry, &carry,
                                      span,
                                      (int)(sizeof span / sizeof span[0]));

            (void)tiku_ui_text_spans(e->surface, f, MARGIN - e->scroll_x, y + f->ascent,
                                     text, span, n);
        } else {
            tiku_text(e->surface, f, MARGIN - e->scroll_x, y + f->ascent, text,
                           TIKU_C_TEXT);
        }
    }
    {
        /* The selection's ground, behind the text it covers: the blue
         * the panels and lists give a chosen thing, and the text on it
         * turned white so it reads.  A line wholly inside the span
         * carries a nib past its end, so the line break it holds is
         * shown to be part of what is selected. */
        int sy0, sx0, sy1, sx1;

        if (tiku_textview_selection(&e->tv, &sy0, &sx0, &sy1, &sx1)) {
            for (i = 0; i < rows; i++) {
                int line = top + i;
                const char *text;
                int len, c0, c1, px0, px1, y;
                char head[TIKU_TEXTVIEW_LINE_MAX];
                tiku_rect_t selr;

                if (line < sy0 || line > sy1) {
                    continue;
                }
                text = tiku_textview_line(&e->tv, line);
                if (text == NULL) {
                    continue;
                }
                len = tiku_textview_line_len(&e->tv, line);
                c0 = (line == sy0) ? sx0 : 0;
                c1 = (line == sy1) ? sx1 : len;
                if (c0 > len) { c0 = len; }
                if (c1 > len) { c1 = len; }
                memcpy(head, text, (size_t)c0);
                head[c0] = '\0';
                px0 = MARGIN - e->scroll_x + tiku_text_width(f, head);
                memcpy(head, text, (size_t)c1);
                head[c1] = '\0';
                px1 = MARGIN - e->scroll_x + tiku_text_width(f, head);
                y = ptop + MARGIN + i * (f->height + 2);
                selr = (tiku_rect_t){ px0, y,
                                      (px1 > px0) ? px1 - px0 : 2,
                                      f->height + 2 };
                if (line < sy1) {
                    selr.w += 4;    /* the line break, shown as taken */
                }
                tiku_fill(e->surface, selr, TIKU_C_SELECT);
                tiku_clip_set(e->surface, selr);
                tiku_text(e->surface, f, MARGIN - e->scroll_x, y + f->ascent, text,
                               TIKU_C_SELTEXT);
                tiku_clip_reset(e->surface);
            }
        }
    }
    {
        /* The caret, without which a person cannot know where their
         * typing will land.  It sits where the text before it ends,
         * which is the only way a proportional face can place it. */
        int cy, cx, row;
        char head[256];
        int cut;

        tiku_textview_caret(&e->tv, &cy, &cx);
        row = cy - top;
        cut = cx;
        if (row >= 0 && row < rows) {
            const char *text = tiku_textview_line(&e->tv, cy);
            int len = tiku_textview_line_len(&e->tv, cy);

            if (cut > len) {
                cut = len;
            }
            if (cut > (int)sizeof head - 1) {
                cut = (int)sizeof head - 1;
            }
            memcpy(head, (text != NULL) ? text : "", (size_t)cut);
            head[cut] = '\0';
            tiku_vline(e->surface,
                            MARGIN - e->scroll_x + tiku_text_width(f, head),
                            ptop + MARGIN + row * (f->height + 2),
                            f->height + 2, TIKU_C_TEXT);
        }
    }
    {
        /* The same scrollbar the folder windows and the panels wear,
         * in a gutter of its own at the page's right: a listing longer
         * than the window SAYS so, rather than hiding its own tail. */
        int total = tiku_textview_lines(&e->tv);

        if (total > rows && rows > 0) {
            tiku_rect_t sb = { e->w - PAGE_SB, ptop, PAGE_SB,
                               e->h - STRIP_H - ptop };

            tiku_ui_scrollbar(e->surface, sb,
                (float)top / (float)(total - rows),
                (float)rows / (float)total, 0);
        }
    }
    tiku_fill(e->surface, strip, TIKU_C_PANEL);
    {
        /* The strip says where the caret is -- the line and column an
         * editor is always asked -- and, for a BASIC line wearing its
         * number, the NUMBER the interpreter will name it by. */
        int cy, cx, num = 0;
        const char *cline;
        char bas[24];

        tiku_textview_caret(&e->tv, &cy, &cx);
        cline = tiku_textview_line(&e->tv, cy);
        bas[0] = '\0';
        if (e->lang == TIKU_SYNTAX_BASIC && cline != NULL) {
            num = atoi(cline);
        }
        if (num > 0) {
            snprintf(bas, sizeof bas, "   [%d]", num);
        }
        snprintf(where, sizeof where, "%s%s   line %d, col %d%s   %d lines",
                 e->name,
                 tiku_textview_modified(&e->tv) ? " (changed)" : "",
                 cy + 1, cx + 1, bas, tiku_textview_lines(&e->tv));
    }
    tiku_text(e->surface, f, MARGIN,
                   strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                   where, TIKU_C_TEXT);
    if (e->tip >= 0) {
        /* The hovered tool's name, on the kit's own tip, below the
         * button so the pointer does not sit on the words. */
        const char *name = tb_tool_name(e, e->tip);
        tiku_rect_t tr = tiku_ui_tip_size(name);

        tr.x = 8 + e->tip * 4 + e->tip * 28;   /* near its button */
        tr.y = TOOLBAR_H + 2;
        if (tr.x + tr.w > e->w) {
            tr.x = e->w - tr.w - 2;
        }
        tiku_ui_tip(e->surface, tr, name);
    }
    (void)st->services->frame(st->services->ctx, e->id, e->surface->px,
                              e->w, e->h);
}

/** @brief How many text rows the page shows at this window's height. */
static int
edit_rows(const edit_t *e)
{
    const tiku_font_t *f = tiku_font_plain();

    return (e->h - STRIP_H - edit_page_top(e) - MARGIN) / (f->height + 2);
}

/**
 * @brief Put the caret under the pointer.
 *
 * The row is which screenful line the y falls on, offset by the line
 * the page starts at; the column is found the way the caret is PAINTED
 * -- by measuring the prefix through the proportional face and taking
 * the character edge the x is nearest -- so the caret lands exactly
 * where it will be drawn, never a glyph off.
 */
static void
edit_place_from_xy(edit_t *e, int x, int y)
{
    const tiku_font_t *f = tiku_font_plain();
    int rows = edit_rows(e);
    int top = tiku_textview_top(&e->tv);
    int nline = tiku_textview_lines(&e->tv);
    int row = (y - edit_page_top(e) - MARGIN) / (f->height + 2);
    int line, len, col, k, prevw;
    const char *text;
    char head[TIKU_TEXTVIEW_LINE_MAX];

    if (row < 0) {
        row = 0;
    }
    if (row >= rows) {
        row = rows - 1;
    }
    line = top + row;
    if (line >= nline) {
        line = nline - 1;
    }
    if (line < 0) {
        line = 0;
    }
    text = tiku_textview_line(&e->tv, line);
    len = tiku_textview_line_len(&e->tv, line);
    col = len;
    prevw = MARGIN - e->scroll_x;
    for (k = 0; k <= len && k < (int)sizeof head - 1; k++) {
        int w;

        memcpy(head, (text != NULL) ? text : "", (size_t)k);
        head[k] = '\0';
        w = MARGIN - e->scroll_x + tiku_text_width(f, head);
        if (w >= x) {
            /* Between edge k-1 and k: the nearer edge wins. */
            col = (k > 0 && (x - prevw) > (w - x)) ? k : (k > 0 ? k - 1 : 0);
            break;
        }
        prevw = w;
    }
    tiku_textview_place(&e->tv, line, col);
}

/**
 * @brief Cut, copy or paste at this editor, over the DESK's clipboard.
 *
 * Copy puts the selection on the desk; cut is a copy and then the one
 * backspace that removes a held selection; paste types what the desk
 * holds, replacing a selection the way any typing does.  All three
 * through the services, so the same page behaves the same hosted in
 * the shell or across the wire.
 */
static int
edit_clip(ide_t *st, edit_t *e, int command)
{
    char text[TIKU_CLIPBOARD_MAX];

    switch (command) {
    case CMD_COPY:
    case CMD_CUT:
        if (tiku_textview_selected(&e->tv, text, sizeof text) == 0u) {
            return 0;           /* nothing held: nothing to take */
        }
        if (st->services->clip_set != NULL) {
            (void)st->services->clip_set(st->services->ctx, text);
        }
        if (command == CMD_CUT) {
            tiku_textview_backspace(&e->tv);
        }
        return 1;
    case CMD_PASTE:
        if (st->services->clip_get == NULL ||
            st->services->clip_get(st->services->ctx, text,
                                   sizeof text) <= 0) {
            return 0;
        }
        tiku_textview_insert_text(&e->tv, text);
        return 1;
    case CMD_SELALL:
        tiku_textview_select_all(&e->tv);
        return 1;
    case CMD_UNDO:
        return tiku_textview_undo(&e->tv);
    case CMD_REDO:
        return tiku_textview_redo(&e->tv);
    default:
        return 0;
    }
}

/**
 * @brief The first hit of @p q at or after (sy,sx), wrapping once.
 *
 * Single-line matches -- a find is one line's worth of text -- swept
 * from the caret to the end and then, if nothing, from the top back to
 * the caret, so the second Return after the last hit lands on the
 * first.  @return 1 and the place, 0 for no hit.
 */
static int
find_forward(const tiku_textview_t *tv, const char *q,
             int sy, int sx, int *oy, int *ox)
{
    int qlen = (int)strlen(q);
    int total = tiku_textview_lines(tv);
    int L, i;

    if (qlen == 0) {
        return 0;
    }
    for (L = sy; L < total; L++) {
        const char *t = tiku_textview_line(tv, L);
        int ll = tiku_textview_line_len(tv, L);
        int start = (L == sy) ? sx : 0;

        for (i = start; i + qlen <= ll; i++) {
            if (memcmp(t + i, q, (size_t)qlen) == 0) {
                *oy = L; *ox = i; return 1;
            }
        }
    }
    for (L = 0; L <= sy && L < total; L++) {
        const char *t = tiku_textview_line(tv, L);
        int ll = tiku_textview_line_len(tv, L);
        int limit = (L == sy) ? sx : ll;

        for (i = 0; i + qlen <= ll && i < limit; i++) {
            if (memcmp(t + i, q, (size_t)qlen) == 0) {
                *oy = L; *ox = i; return 1;
            }
        }
    }
    return 0;
}

/** @brief Select the span [ (y,x0) , (y,x1) ] and bring it into view. */
static void
edit_select_span(edit_t *e, int y, int x0, int x1)
{
    tiku_textview_place(&e->tv, y, x0);
    tiku_textview_select_none(&e->tv);
    tiku_textview_select_begin(&e->tv);
    tiku_textview_place(&e->tv, y, x1);
    (void)tiku_textview_reveal(&e->tv, edit_rows(e));
}

/**
 * @brief Run the find field: a hit becomes the selection.
 *
 * For a BASIC file a search that is ALL DIGITS is first tried as a
 * line NUMBER -- the caret goes to that line and its number is
 * selected -- and only if no line wears it is it looked for as text.
 * Everything else is text, found forward from the caret with a wrap,
 * so Return walks the hits.
 */
static void
edit_find_run(edit_t *e)
{
    const char *q = e->find.text;
    int cy, cx, hy, hx;

    if (q[0] == '\0') {
        return;
    }
    if (e->lang == TIKU_SYNTAX_BASIC) {
        int alldig = 1, i;

        for (i = 0; q[i] != '\0'; i++) {
            if (q[i] < '0' || q[i] > '9') { alldig = 0; break; }
        }
        if (alldig) {
            int total = tiku_textview_lines(&e->tv);
            int L, n = (int)strlen(q);

            for (L = 0; L < total; L++) {
                const char *t = tiku_textview_line(&e->tv, L);
                int d = 0;

                while (t[d] >= '0' && t[d] <= '9') {
                    d++;
                }
                if (d == n && memcmp(t, q, (size_t)n) == 0) {
                    edit_select_span(e, L, 0, d);
                    return;
                }
            }
            /* no line wears it: fall through and find it as text */
        }
    }
    tiku_textview_caret(&e->tv, &cy, &cx);
    if (find_forward(&e->tv, q, cy, cx, &hy, &hx)) {
        edit_select_span(e, hy, hx, hx + (int)strlen(q));
    }
}

static int
norm_is_word(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '$';
}

/**
 * @brief Uppercase the reserved word the caret just finished, 1982-style.
 *
 * The spelling authority is the CLASSIFIER's -- the same table the
 * colours come from -- so a word is uppercased exactly when it would be
 * painted as reserved, which also means a keyword typed inside a string
 * or a remark is left alone for free.  The rewrite is one journal
 * batch, so a single Undo puts back what the hands typed.  BASIC only:
 * C is not a language of shouted keywords.
 */
static void
edit_normalize_word(edit_t *e)
{
    int cy, cx, s, i, off, ink = -1, span_s = -1, span_len = 0, n;
    const char *line;
    tiku_span_t span[64];
    char up[64];

    if (e->lang != TIKU_SYNTAX_BASIC) {
        return;
    }
    tiku_textview_caret(&e->tv, &cy, &cx);
    line = tiku_textview_line(&e->tv, cy);
    if (line == NULL || cx <= 0 || !norm_is_word((unsigned char)line[cx - 1])) {
        return;                 /* no word just ended here */
    }
    s = cx;
    while (s > 0 && norm_is_word((unsigned char)line[s - 1])) {
        s--;
    }
    /* Which run the finished word falls in: only a RESERVED word is
     * touched, and the classifier already knows a keyword in a string
     * from one in the code. */
    n = tiku_syntax_spans(TIKU_SYNTAX_BASIC, line, span,
                          (int)(sizeof span / sizeof span[0]));
    for (i = 0, off = 0; i < n; i++) {
        if (cx - 1 >= off && cx - 1 < off + span[i].len) {
            ink = span[i].ink;
            span_s = off;
            span_len = span[i].len;
            break;
        }
        off += span[i].len;
    }
    if (ink != TIKU_INK_WORD || span_len <= 0 ||
        span_len >= (int)sizeof up) {
        return;
    }
    for (i = 0; i < span_len; i++) {
        char c = line[span_s + i];

        up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    up[span_len] = '\0';
    if (memcmp(up, line + span_s, (size_t)span_len) == 0) {
        return;                 /* already shouting: nothing to do */
    }
    /* One batch: the word out, the word back in upper -- a single Undo
     * away from exactly what was typed. */
    tiku_textview_group_begin(&e->tv);
    tiku_textview_place(&e->tv, cy, span_s + span_len);
    for (i = 0; i < span_len; i++) {
        tiku_textview_backspace(&e->tv);
    }
    tiku_textview_insert_text(&e->tv, up);
    tiku_textview_group_end(&e->tv);
    tiku_textview_place(&e->tv, cy, cx);   /* case is length: caret stands */
}

static int
renum_is_word(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

/** @brief The length of a target keyword starting here, or 0. */
static int
renum_keyword(const char *s)
{
    static const char *const K[] = { "goto", "gosub", "then", "else" };
    size_t i;

    for (i = 0; i < sizeof K / sizeof K[0]; i++) {
        int n = (int)strlen(K[i]);

        if (strncasecmp(s, K[i], (size_t)n) == 0 && !renum_is_word(s[n])) {
            return n;
        }
    }
    return 0;
}

/** @brief The new number an old one maps to, or the old if none. */
static int
renum_map(int old, const int *olds, const int *news, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (olds[i] == old) {
            return news[i];
        }
    }
    return old;
}

/**
 * @brief Copy @p s, rewriting a number after GOTO/GOSUB/THEN/ELSE.
 *
 * A jump has to follow the line it names when the numbers move, or
 * renumbering would quietly break every branch.  Only a number that
 * FOLLOWS one of the branch words is touched; a number sitting in a
 * string or a REM is left as it stands.
 */
static void
renum_emit(char *out, size_t *u, const char *s,
           const int *olds, const int *news, int n)
{
    int prev_word = 0;

    while (*s != '\0') {
        int kw = prev_word ? 0 : renum_keyword(s);

        if (kw > 0) {
            memcpy(out + *u, s, (size_t)kw);
            *u += (size_t)kw;
            s += kw;
            while (*s == ' ' || *s == '\t') {
                out[(*u)++] = *s++;
            }
            if (*s >= '0' && *s <= '9') {
                int old = 0;
                char nb[16];
                int nl;

                while (*s >= '0' && *s <= '9') {
                    old = old * 10 + (*s - '0');
                    s++;
                }
                nl = snprintf(nb, sizeof nb, "%d",
                              renum_map(old, olds, news, n));
                memcpy(out + *u, nb, (size_t)nl);
                *u += (size_t)nl;
            }
            prev_word = 1;
            continue;
        }
        prev_word = renum_is_word((unsigned char)*s);
        out[(*u)++] = *s++;
    }
}

/**
 * @brief Renumber a BASIC program by tens, branches following.
 *
 * Every line that WEARS a number is given the next ten; every branch to
 * an old number is rewritten to the new one.  The whole rewrite is ONE
 * journal batch, so a single Undo takes it all back -- which is the
 * only thing standing in the way of a person trying it.
 */
static void
renumber_basic(edit_t *e)
{
    int total = tiku_textview_lines(&e->tv);
    int *olds, *news;
    int nmap = 0, next = 10, L;
    size_t cap = 64, used = 0;
    char *out;

    if (total <= 0) {
        return;
    }
    olds = malloc((size_t)total * sizeof *olds);
    news = malloc((size_t)total * sizeof *news);
    for (L = 0; L < total; L++) {
        cap += (size_t)tiku_textview_line_len(&e->tv, L) + 16u;
    }
    out = malloc(cap);
    if (olds == NULL || news == NULL || out == NULL) {
        free(olds); free(news); free(out);
        return;
    }
    for (L = 0; L < total; L++) {
        const char *t = tiku_textview_line(&e->tv, L);

        if (t != NULL && t[0] >= '0' && t[0] <= '9') {
            olds[nmap] = atoi(t);
            news[nmap] = next;
            nmap++;
            next += 10;
        }
    }
    for (L = 0; L < total; L++) {
        const char *t = tiku_textview_line(&e->tv, L);
        int d = 0;

        if (t == NULL) {
            t = "";
        }
        while (t[d] >= '0' && t[d] <= '9') {
            d++;
        }
        if (d > 0) {
            char nb[16];
            int nl = snprintf(nb, sizeof nb, "%d",
                              renum_map(atoi(t), olds, news, nmap));

            memcpy(out + used, nb, (size_t)nl);
            used += (size_t)nl;
            renum_emit(out, &used, t + d, olds, news, nmap);
        } else {
            renum_emit(out, &used, t, olds, news, nmap);
        }
        if (L < total - 1) {
            out[used++] = '\n';
        }
    }
    out[used] = '\0';
    tiku_textview_group_begin(&e->tv);
    tiku_textview_select_all(&e->tv);
    tiku_textview_insert_text(&e->tv, out);
    tiku_textview_group_end(&e->tv);
    (void)tiku_textview_reveal(&e->tv, edit_rows(e));
    free(olds); free(news); free(out);
}

/** @brief Append one row: the label, its command, and the KEY it says. */
static void
mrow(tiku_menu_list_t *m, const char *label, int command,
     char sc, unsigned mods, int enabled)
{
    tiku_menu_entry_t *it = &m->item[m->nitem];

    if (m->nitem >= TIKU_MENUSET_ITEMS) {
        return;
    }
    memset(it, 0, sizeof *it);
    snprintf(it->label, sizeof it->label, "%s", label);
    it->command = command;
    it->sc = sc;
    it->mods = mods;
    it->enabled = (unsigned char)enabled;
    m->nitem++;
}

/** @brief A line between the groups, the way the desk's menus keep them. */
static void
msep(tiku_menu_list_t *m)
{
    if (m->nitem >= TIKU_MENUSET_ITEMS) {
        return;
    }
    memset(&m->item[m->nitem], 0, sizeof m->item[0]);
    m->item[m->nitem].separator = 1;
    m->nitem++;
}

/**
 * @brief The Window menu, the same on EVERY window of the program.
 *
 * A person in one editor reaches the other from where they are; making
 * them return to the project window first was a detour every proving
 * script had memorised, which is how a missing road shows itself.
 */
static void
window_menu(ide_t *st, tiku_menu_list_t *m)
{
    int i, n = 0;
    char row[64];

    snprintf(m->title, sizeof m->title, "Window");
    for (i = 0; i < IDE_EDITS; i++) {
        edit_t *e = &st->edit[i];

        if (e->surface == NULL) {
            continue;
        }
        /* The mark is the DOCUMENT's, not the window's: a person
         * looking for where their unsaved work is should find it in the
         * list of windows rather than by opening each one. */
        snprintf(row, sizeof row, "%s%s", e->name,
                 tiku_textview_modified(&e->tv) ? " (changed)" : "");
        mrow(m, row, CMD_WINDOW + i, 0, 0u, 1);
        n++;
    }
    if (n == 0) {
        mrow(m, "No open files", 0, 0, 0u, 0);
    }
}

static void
edit_publish(ide_t *st, edit_t *e)
{
    tiku_menuset_t menus;
    char row[64];

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 3;
    /* File: the page's own file, its run, and its leaving -- each row
     * saying the key that is the same gesture. */
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "File");
    snprintf(row, sizeof row, "Save %s", e->name);
    mrow(&menus.menu[0], row, CMD_SAVE, 's', TIKU_MOD_CMD,
         tiku_textview_modified(&e->tv));
    if (e->lang == TIKU_SYNTAX_BASIC) {
        snprintf(row, sizeof row, "Run %s", e->name);
        mrow(&menus.menu[0], row, CMD_RUN, 'r', TIKU_MOD_CMD,
             !st->running);
        mrow(&menus.menu[0], "Stop", CMD_STOP, '.', TIKU_MOD_CMD,
             st->running);
    }
    msep(&menus.menu[0]);
    snprintf(row, sizeof row, "Close %s", e->name);
    mrow(&menus.menu[0], row, CMD_QUIT, 'w', TIKU_MOD_CMD, 1);
    /* Edit: the desk's editing rows, grouped, each wearing its key. */
    snprintf(menus.menu[1].title, sizeof menus.menu[1].title, "Edit");
    mrow(&menus.menu[1], "Undo", CMD_UNDO, 'z', TIKU_MOD_CMD,
         tiku_textview_can_undo(&e->tv));
    mrow(&menus.menu[1], "Redo", CMD_REDO, 'z',
         TIKU_MOD_CMD | TIKU_MOD_SHIFT, tiku_textview_can_redo(&e->tv));
    msep(&menus.menu[1]);
    mrow(&menus.menu[1], "Cut", CMD_CUT, 'x', TIKU_MOD_CMD, 1);
    mrow(&menus.menu[1], "Copy", CMD_COPY, 'c', TIKU_MOD_CMD, 1);
    mrow(&menus.menu[1], "Paste", CMD_PASTE, 'v', TIKU_MOD_CMD, 1);
    mrow(&menus.menu[1], "Select all", CMD_SELALL, 'a', TIKU_MOD_CMD, 1);
    msep(&menus.menu[1]);
    mrow(&menus.menu[1], "Find…", CMD_FIND, 'f', TIKU_MOD_CMD, 1);
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* The tidy that renumbers by tens, for the language whose
         * lines wear their numbers. */
        mrow(&menus.menu[1], "Renumber", CMD_RENUM, 0, 0u, 1);
    }
    window_menu(st, &menus.menu[2]);
    (void)st->services->menus(st->services->ctx, e->id, &menus);
}

/** @brief Read @p e's file in.  A missing file is an empty one. */
static void
edit_load(edit_t *e)
{
    static char whole[TIKU_TEXTVIEW_LINES_MAX * 8];
    FILE *f = fopen(e->path, "r");

    tiku_textview_init(&e->tv);
    if (f == NULL) {
        tiku_textview_set(&e->tv, "");
        return;
    }
    {
        size_t got = fread(whole, 1u, sizeof whole - 1u, f);

        whole[got] = '\0';
    }
    (void)fclose(f);
    tiku_textview_set(&e->tv, whole);
}

static int
edit_save(edit_t *e)
{
    FILE *f = fopen(e->path, "w");
    int i;

    if (f == NULL) {
        return 0;
    }
    for (i = 0; i < tiku_textview_lines(&e->tv); i++) {
        (void)fprintf(f, "%s\n", tiku_textview_line(&e->tv, i));
    }
    if (fclose(f) != 0) {
        return 0;
    }
    tiku_textview_saved(&e->tv);
    return 1;
}

/**
 * @brief Put the digits of a line number, and its blank, into @p tv.
 */
static void
type_number(tiku_textview_t *tv, int n)
{
    char digits[16];
    int i;

    snprintf(digits, sizeof digits, "%d ", n);
    for (i = 0; digits[i] != '\0'; i++) {
        tiku_textview_insert(tv, digits[i]);
    }
}

/**
 * @brief The number the line AFTER line @p at should wear.
 *
 * The interpreter's own habit: count by tens, so a line can always be
 * put between two others later.  A line wearing no number offers none.
 *
 * @return the next number, or 0 for none.
 */
static int
next_number(const tiku_textview_t *tv, int at)
{
    const char *p = tiku_textview_line(tv, at);
    long n;
    char *end;

    if (p == NULL) {
        return 0;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    n = strtol(p, &end, 10);
    return (end != p && n > 0 && n < 99990) ? (int)n + 10 : 0;
}

/**
 * @brief Open @p row's file, or bring its window forward.
 *
 * A file already open is RAISED: opening it twice would give a person
 * two windows onto one document, and whichever they typed into last
 * would be the one the other quietly forgot.
 */
static void
edit_open(ide_t *st, const prow_t *row)
{
    edit_t *e = edit_of_path(st, row->path);
    int i;

    if (e != NULL) {
        if (st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx, e->id);
        }
        return;
    }
    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface == NULL) {
            e = &st->edit[i];
            break;
        }
    }
    if (e == NULL) {
        snprintf(st->note, sizeof st->note,
                 "no room for another window");
        project_paint(st);
        return;
    }
    memset(e, 0, sizeof *e);
    e->tip = -1;
    snprintf(e->path, sizeof e->path, "%s", row->path);
    snprintf(e->name, sizeof e->name, "%s", row->name);
    e->lang = lang_of(e->name);
    edit_load(e);
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* A program is CONTINUED, not reread from the top: the caret
         * lands at the end of the last line, so the first Return is
         * already the next numbered line. */
        int last = tiku_textview_lines(&e->tv) - 1;

        tiku_textview_place(&e->tv, last,
                            tiku_textview_line_len(&e->tv, last));
    }
    if (e->lang == TIKU_SYNTAX_BASIC &&
        tiku_textview_lines(&e->tv) <= 1 &&
        tiku_textview_line_len(&e->tv, 0) == 0) {
        /* A fresh page of BASIC begins where every line of it must:
         * with a number.  Counted by tens, the interpreter's own
         * habit, so a line can always be put between two others. */
        type_number(&e->tv, 10);
        tiku_textview_saved(&e->tv);    /* offered, not yet work */
    }
    e->w = EDIT_W;
    e->h = EDIT_H;
    /* The caret was placed at the end of a file that may run past a
     * screenful; bring the page to it, so opening a long program does
     * not begin with the caret lost below the bottom edge. */
    (void)tiku_textview_reveal(&e->tv, edit_rows(e));
    e->surface = tiku_surface_new(EDIT_W, EDIT_H, TIKU_C_PANEL);
    if (e->surface == NULL) {
        return;
    }
    e->id = st->services->open(st->services->ctx, e->name, EDIT_W,
                               EDIT_H);
    if (e->id == 0u) {
        tiku_surface_free(e->surface);
        e->surface = NULL;
        snprintf(st->note, sizeof st->note, "no room for another window");
        project_paint(st);
        return;
    }
    edit_paint(st, e);
    edit_publish(st, e);
    if (st->services->sizeable != NULL) {
        /* An editor's page is the one window a person grows. */
        (void)st->services->sizeable(st->services->ctx, e->id);
    }
    project_paint(st);          /* the Window menu grew */
    project_publish(st);
}

/** @brief Let one editor go; the project window stays. */
static void
edit_drop(ide_t *st, edit_t *e)
{
    int i;

    st->services->close(st->services->ctx, e->id);
    tiku_surface_free(e->surface);
    e->surface = NULL;
    project_paint(st);
    project_publish(st);
    /* Every surviving editor's Window menu named the one that left:
     * republished, or the stale row would offer a window that is not
     * there to raise. */
    for (i = 0; i < IDE_EDITS; i++) {
        if (st->edit[i].surface != NULL) {
            edit_publish(st, &st->edit[i]);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* The project window                                                        */
/*---------------------------------------------------------------------------*/

static tiku_rect_t
project_body(void)
{
    tiku_rect_t r = { MARGIN, MARGIN, IDE_W - 2 * MARGIN,
                           IDE_H - STRIP_H - 2 * MARGIN };

    return r;
}

static const char *
row_name(void *ctx, int row)
{
    ide_t *st = ctx;

    return (row >= 0 && row < st->proj.nrow) ? st->proj.row[row].name
                                             : NULL;
}

static void
project_paint(ide_t *st)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t all = { 0, 0, IDE_W, IDE_H };
    tiku_rect_t strip = { 0, IDE_H - STRIP_H, IDE_W, STRIP_H };
    tiku_rect_t body = project_body();
    char says[160];

    if (st->proj_surface == NULL) {
        return;
    }
    tiku_fill(st->proj_surface, all, TIKU_C_PANEL);
    tiku_ui_sunken(st->proj_surface, body, TIKU_C_DOC);
    tiku_list_draw(&st->tv_list, st->proj_surface, body, row_name, st);
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
        if (files == 0) {
            snprintf(says, sizeof says,
                     "%s: empty -- New File… adds one",
                     (st->proj.title[0] != '\0') ? st->proj.title
                                                 : "a project");
        } else {
            snprintf(says, sizeof says, "%d file%s", files,
                     files == 1 ? "" : "s");
        }
    }
    tiku_text(st->proj_surface, f, MARGIN,
                   strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                   says, TIKU_C_TEXT);
    if (st->ask.open) {
        int aw, ah;

        tiku_alert_size(&aw, &ah);
        if (aw > IDE_W - 16) {
            aw = IDE_W - 16;
        }
        st->ask_frame = (tiku_rect_t){ (IDE_W - aw) / 2,
                                            (IDE_H - ah) / 3, aw, ah };
        tiku_alert_draw(&st->ask, st->proj_surface, st->ask_frame);
    }
    (void)st->services->frame(st->services->ctx, st->proj_id,
                              st->proj_surface->px, IDE_W, IDE_H);
}

/**
 * @brief The project window's menus, the Window menu among them.
 *
 * A row per open editor, marked when what it holds is unsaved -- so the
 * one place that lists the windows is also the one place that says
 * which of them has work in it.
 */
static void
project_publish(ide_t *st)
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
                   lang_of(st->proj.row[row].name) == TIKU_SYNTAX_BASIC &&
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

    window_menu(st, &menus.menu[2]);
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
    project_paint(st);
    project_publish(st);
}

/** @brief Open what the person picked in the shell's panel. */
static void
project_take(ide_t *st, const char *path)
{
    static char whole[64 * 1024];
    FILE *f = fopen(path, "r");
    char dir[512];
    char *slash;
    size_t got;

    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "cannot read %s", path);
        project_paint(st);
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
    (void)project_parse(&st->proj, whole, dir);
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
static void
project_add_file(ide_t *st, const char *path)
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
        project_paint(st);
        return;
    }
    leaf = path + dirlen + 1;
    for (i = 0; i < st->proj.nrow; i++) {
        if (!st->proj.row[i].heading &&
            strcmp(st->proj.row[i].path, path) == 0) {
            edit_open(st, &st->proj.row[i]);
            return;             /* already listed: adding means opening */
        }
    }
    f = fopen(path, "a");       /* made when new, kept when not */
    if (f != NULL) {
        (void)fclose(f);
    }
    f = fopen(st->proj_file, "a");
    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "cannot write the project");
        project_paint(st);
        return;
    }
    (void)fprintf(f, "file %s\n", leaf);
    (void)fclose(f);
    project_take(st, st->proj_file);
    for (i = 0; i < st->proj.nrow; i++) {
        if (!st->proj.row[i].heading &&
            strcmp(st->proj.row[i].path, path) == 0) {
            tiku_list_select_only(&st->tv_list, i);
            edit_open(st, &st->proj.row[i]);
            return;
        }
    }
}

/**
 * @brief Take the SELECTED row out of the project's description.
 *
 * Out of the PROJECT, not off the DISK: the file stays where it is,
 * exactly as BeIDE meant it.  The description is rewritten keeping
 * every line as it stands except the one `file` line being dropped --
 * so the lines this build does not know (a target, a board) ride
 * through untouched, the same promise Add File keeps by appending.
 */
static void
project_remove_file(ide_t *st)
{
    int row = tiku_list_chosen(&st->tv_list);
    char whole[8192];
    char kept[8192];
    char want[96];
    size_t used = 0, w = 0;
    FILE *f;
    const char *leaf, *slash;
    char *line;
    int dropped = 0;

    if (st->proj_file[0] == '\0' ||
        row < 0 || row >= st->proj.nrow || st->proj.row[row].heading) {
        return;
    }
    slash = strrchr(st->proj.row[row].path, '/');
    leaf = (slash != NULL) ? slash + 1 : st->proj.row[row].path;
    snprintf(want, sizeof want, "file %s", leaf);
    f = fopen(st->proj_file, "r");
    if (f == NULL) {
        return;
    }
    used = fread(whole, 1u, sizeof whole - 1u, f);
    (void)fclose(f);
    whole[used] = '\0';
    line = whole;
    while (*line != '\0') {
        char *nl = strchr(line, '\n');
        size_t n = (nl != NULL) ? (size_t)(nl - line) + 1u
                                : strlen(line);

        if (!dropped && strncmp(line, want, strlen(want)) == 0 &&
            (line[strlen(want)] == '\n' ||
             line[strlen(want)] == '\0')) {
            dropped = 1;        /* the one line being taken out */
        } else if (w + n < sizeof kept) {
            memcpy(kept + w, line, n);
            w += n;
        }
        line += n;
    }
    if (!dropped) {
        return;
    }
    f = fopen(st->proj_file, "w");
    if (f == NULL) {
        snprintf(st->note, sizeof st->note, "cannot write the project");
        project_paint(st);
        return;
    }
    (void)fwrite(kept, 1u, w, f);
    (void)fclose(f);
    project_take(st, st->proj_file);
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
static void
project_create(ide_t *st, const char *path)
{
    char projfile[560];
    char mainfile[560];
    struct stat sb;
    const char *leaf = strrchr(path, '/');
    int i;

    leaf = (leaf != NULL) ? leaf + 1 : path;
    if (stat(path, &sb) == 0 && !S_ISDIR(sb.st_mode)) {
        /* An existing FILE: the person picked a description itself. */
        project_take(st, path);
        return;
    }
    if (stat(path, &sb) != 0 && mkdir(path, 0777) != 0) {
        snprintf(st->note, sizeof st->note, "cannot make %s", path);
        project_paint(st);
        return;
    }
    snprintf(projfile, sizeof projfile, "%s/project.tiku", path);
    snprintf(mainfile, sizeof mainfile, "%s/%s", path, APP_MAIN_FILE);
    if (stat(projfile, &sb) != 0) {
        FILE *f = fopen(projfile, "w");

        if (f == NULL) {
            snprintf(st->note, sizeof st->note, "cannot write %s",
                     projfile);
            project_paint(st);
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
    project_take(st, projfile);
    for (i = 0; i < st->proj.nrow; i++) {
        if (!st->proj.row[i].heading &&
            strcmp(st->proj.row[i].name, APP_MAIN_FILE) == 0) {
            tiku_list_select_only(&st->tv_list, i);
            edit_open(st, &st->proj.row[i]);
            break;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* What a run said                                                           */
/*---------------------------------------------------------------------------*/

static const char *
msg_name(void *ctx, int row)
{
    ide_t *st = ctx;

    return (row >= 0 && row < st->nmsg) ? st->msg[row].text : NULL;
}

static void
messages_paint(ide_t *st)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t all = { 0, 0, MSG_W, MSG_H };
    tiku_rect_t strip = { 0, MSG_H - STRIP_H, MSG_W, STRIP_H };
    tiku_rect_t body = { MARGIN, MARGIN, MSG_W - 2 * MARGIN,
                              MSG_H - STRIP_H - 2 * MARGIN };
    char says[160];

    if (st->msg_surface == NULL) {
        return;
    }
    tiku_fill(st->msg_surface, all, TIKU_C_PANEL);
    tiku_ui_sunken(st->msg_surface, body, TIKU_C_DOC);
    tiku_list_draw(&st->msg_list, st->msg_surface, body, msg_name, st);
    tiku_fill(st->msg_surface, strip, TIKU_C_PANEL);
    snprintf(says, sizeof says, "%s",
             st->running ? "running…" : "the run has finished");
    tiku_text(st->msg_surface, f, MARGIN,
                   strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                   says, TIKU_C_TEXT);
    (void)st->services->frame(st->services->ctx, st->msg_id,
                              st->msg_surface->px, MSG_W, MSG_H);
}

/**
 * @brief The message window's menus.
 *
 * One row, and it is the gesture the window exists for: take me to
 * where this was about.
 */
static void
messages_publish(ide_t *st)
{
    tiku_menuset_t menus;
    int row = tiku_list_chosen(&st->msg_list);
    int links = (row >= 0 && row < st->nmsg && st->msg[row].line > 0);

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Message");
    menus.menu[0].nitem = 1;
    if (links) {
        snprintf(menus.menu[0].item[0].label,
                 sizeof menus.menu[0].item[0].label, "Go to line %d",
                 st->msg[row].line);
    } else {
        snprintf(menus.menu[0].item[0].label,
                 sizeof menus.menu[0].item[0].label,
                 "This one is not about anywhere");
    }
    menus.menu[0].item[0].command = CMD_FOLLOW;
    menus.menu[0].item[0].enabled = (unsigned char)links;
    (void)st->services->menus(st->services->ctx, st->msg_id, &menus);
}

/** @brief Make the message window when there is first something to say. */
static void
messages_open(ide_t *st)
{
    if (st->msg_surface != NULL) {
        return;
    }
    st->msg_surface = tiku_surface_new(MSG_W, MSG_H, TIKU_C_PANEL);
    if (st->msg_surface == NULL) {
        return;
    }
    st->msg_id = st->services->open(st->services->ctx, "Messages",
                                    MSG_W, MSG_H);
    if (st->msg_id == 0u) {
        tiku_surface_free(st->msg_surface);
        st->msg_surface = NULL;
        snprintf(st->note, sizeof st->note, "no room for the messages");
        project_paint(st);
        return;
    }
    tiku_list_init(&st->msg_list, 1);
}

/**
 * @brief Take one line a run said.
 *
 * The interpreter names where an error was on the line after saying
 * what it was, "? syntax" then "at line 20"; the two become one row,
 * since a row that is only a number is one nobody can act on.  The
 * annotation is folded only onto a row that was an error, the one place
 * the interpreter puts it, so a program printing those words keeps them.
 */
static void
message_take(ide_t *st, const char *line)
{
    unsigned n = 0u;
    msg_t *m;

    if (st->nmsg >= IDE_MSGS) {
        return;
    }
    /* The marker is not always at the head of the row: a PRINT that
     * ended in a semicolon leaves the line open, and the error is
     * written onto the end of it -- "value: ? division by zero". */
    if (sscanf(line, "at line %u", &n) == 1 && st->nmsg > 0 &&
        strstr(st->msg[st->nmsg - 1].text, "? ") != NULL) {
        m = &st->msg[st->nmsg - 1];
        m->line = (int)n;
        m->numbered = 1;
        snprintf(m->file, sizeof m->file, "%s", st->child_file);
        {
            char whole[160];

            snprintf(whole, sizeof whole, "%s, at line %u", m->text, n);
            snprintf(m->text, sizeof m->text, "%s", whole);
        }
        return;
    }
    m = &st->msg[st->nmsg++];
    memset(m, 0, sizeof *m);
    snprintf(m->text, sizeof m->text, "%s", line);
    if (st->child_numbered) {
        return;                 /* BASIC says where on its own line */
    }
    /*
     * A compiler says where at the HEAD of what it says --
     * "file.c:12:5: error: ..." -- so the place and the words arrive
     * together and there is nothing to fold.  The file is taken as
     * written: a compiler asked about a path answers about that path.
     */
    {
        const char *colon = strchr(line, ':');
        unsigned at = 0u;

        if (colon != NULL && colon != line &&
            sscanf(colon + 1, "%u", &at) == 1 && at > 0u) {
            size_t len = (size_t)(colon - line);

            if (len < sizeof m->file) {
                memcpy(m->file, line, len);
                m->file[len] = '\0';
                m->line = (int)at;
            }
        }
    }
}

/**
 * @brief Which text line holds the program's line @p want.
 *
 * A BASIC line WEARS its number, so the file itself says where the
 * interpreter meant -- there is no table to keep in step.
 *
 * @return the line, or -1 when the file has no such number.
 */
static int
line_of_number(const tiku_textview_t *tv, int want)
{
    int i, found = -1;

    for (i = 0; i < tiku_textview_lines(tv); i++) {
        const char *p = tiku_textview_line(tv, i);
        long got;
        char *end;

        if (p == NULL) {
            break;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        got = strtol(p, &end, 10);
        if (end != p && got == (long)want) {
            /* The LAST of them: storing a line REPLACES one already
             * numbered the same, so a file with the number twice ran
             * the second and the first was never in the program. */
            found = i;
        }
    }
    return found;
}

/**
 * @brief Run @p path on the interpreter a BOARD runs.
 *
 * The same engine, as a child on the far end of a pipe: what the
 * editor's Run does, for a file the project names.  The sandbox is the
 * file's own folder, so what a program writes lands beside its source.
 */
/**
 * @brief End a run by hand: the same road the time bound takes.
 *
 * A program waiting on INPUT, or printing for ever, is somebody's to
 * stop NOW rather than when the bound gives up on it; the message says
 * which way it ended.
 */
static void
run_stop(ide_t *st, const char *why)
{
    if (!st->running) {
        return;
    }
    (void)kill(st->child, SIGTERM);
    (void)waitpid(st->child, NULL, 0);
    st->running = 0;
    (void)close(st->child_fd);
    st->child_fd = -1;
    message_take(st, why);
    messages_show(st);
    if (st->services->raise_window != NULL) {
        (void)st->services->raise_window(st->services->ctx, st->msg_id);
    }
}

static void
run_file(ide_t *st, const char *path)
{
    char box[560];
    const char *tool;
    char *slash;
    int fds[2];

    if (st->running) {
        snprintf(st->note, sizeof st->note, "already running");
        project_paint(st);
        return;
    }
    snprintf(st->child_file, sizeof st->child_file, "%s", path);
    snprintf(box, sizeof box, "%s", path);
    slash = strrchr(box, '/');
    if (slash != NULL && slash != box) {
        *slash = '\0';
    } else {
        snprintf(box, sizeof box, "/tmp");
    }
    /* ABSOLUTE, because a runner starts an application in the person's
     * home rather than wherever the desktop was built. */
    tool = getenv("TIKU_BASIC");
    if (tool == NULL || tool[0] == '\0') {
        tool = "tiku-basic";
    }
    if (pipe(fds) != 0) {
        return;
    }
    st->child = fork();
    if (st->child < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return;
    }
    if (st->child == 0) {
        (void)dup2(fds[1], 1);
        (void)dup2(fds[1], 2);
        (void)close(fds[0]);
        (void)close(fds[1]);
        (void)execlp(tool, tool, path, box, (char *)NULL);
        /* A tool that is not installed says so through the same pipe
         * everything else does, and becomes a message like any other. */
        fprintf(stderr, "cannot run %s\n", tool);
        _exit(127);
    }
    st->child_numbered = 1;     /* BASIC names its own lines */
    (void)close(fds[1]);
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    st->child_fd = fds[0];
    st->running = 1;
    st->run_from = 0;           /* the first tick says when */
    st->nmsg = 0;
    st->partial[0] = '\0';
    /* No window yet: a run that says nothing has nothing to show, and
     * a message window standing empty is furniture. */
    (void)tiku_list_set_count(&st->msg_list, 0);
    messages_paint(st);
}

/**
 * @brief Ask the compiler the project's target names whether its C is
 *        good, and say whatever it says.
 *
 * What BUILD means, today: the check, not the image.  Making an image
 * for a board needs a toolchain for that board, and a project asking
 * for one this machine has not got is told so rather than shown a
 * silence it could mistake for success.
 */
static void
build_project(ide_t *st)
{
    const char *cc;

#ifdef TIKU_IDE_C
    /* No target line means this machine: the check is the point. */
    if (st->proj.target[0] == '\0') {
        snprintf(st->proj.target, sizeof st->proj.target, "host");
    }
#endif
    cc = compiler_for(st->proj.target);
    char inc[560];
    const char *argv[IDE_ROWS + 8];
    int argc = 0;
    int fds[2];
    int i, any = 0;

    if (st->running) {
        return;
    }
    st->nmsg = 0;
    st->partial[0] = '\0';
    st->child_numbered = 0;     /* a compiler counts the file's lines */
    (void)tiku_list_set_count(&st->msg_list, 0);
    if (st->proj.target[0] == '\0') {
        message_take(st, "? this project does not say what it is for");
        messages_show(st);
        return;
    }
    if (cc == NULL) {
        char says[160];

        snprintf(says, sizeof says,
                 "? nothing here knows how to build for %s",
                 st->proj.target);
        message_take(st, says);
        messages_show(st);
        return;
    }
    snprintf(inc, sizeof inc, "-I%s", st->proj.dir);
    argv[argc++] = cc;
    argv[argc++] = "-fsyntax-only";
    argv[argc++] = inc;
    for (i = 0; i < st->proj.nrow && argc < IDE_ROWS + 6; i++) {
        const prow_t *row = &st->proj.row[i];
        size_t n = strlen(row->name);

        if (row->heading || n < 3u ||
            strcmp(row->name + n - 2, ".c") != 0) {
            continue;
        }
        argv[argc++] = row->path;
        any = 1;
    }
    argv[argc] = NULL;
    if (!any) {
        message_take(st, "? this project has no C in it to build");
        messages_show(st);
        return;
    }
    if (pipe(fds) != 0) {
        return;
    }
    st->child = fork();
    if (st->child < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return;
    }
    if (st->child == 0) {
        (void)dup2(fds[1], 1);
        (void)dup2(fds[1], 2);
        (void)close(fds[0]);
        (void)close(fds[1]);
        (void)execvp(cc, (char *const *)argv);
        fprintf(stderr, "cannot run %s\n", cc);
        _exit(127);
    }
    (void)close(fds[1]);
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    st->child_fd = fds[0];
    st->running = 1;
    st->run_from = 0;
    st->child_file[0] = '\0';
    messages_paint(st);
}

/** @brief Open the file a message was about, at the line it named. */
static void
message_follow(ide_t *st)
{
    int row = tiku_list_chosen(&st->msg_list);
    const msg_t *m;
    edit_t *e;
    int at;

    if (row < 0 || row >= st->nmsg) {
        return;
    }
    m = &st->msg[row];
    if (m->line <= 0 || m->file[0] == '\0') {
        return;             /* a message about nowhere leads nowhere */
    }
    e = edit_of_path(st, m->file);
    if (e == NULL) {
        prow_t want;

        memset(&want, 0, sizeof want);
        snprintf(want.path, sizeof want.path, "%s", m->file);
        {
            const char *slash = strrchr(m->file, '/');

            snprintf(want.name, sizeof want.name, "%s",
                     (slash != NULL) ? slash + 1 : m->file);
        }
        edit_open(st, &want);
        e = edit_of_path(st, m->file);
    }
    if (e == NULL) {
        return;
    }
    /* A compiler counted the lines of the file; BASIC named one. */
    at = m->numbered ? line_of_number(&e->tv, m->line) : m->line - 1;
    if (at >= 0) {
        tiku_textview_place(&e->tv, at, 0);
        /* Placing the caret does not move the PAGE, and a caret below
         * the window is a jump that looks like nothing happening. */
        (void)tiku_textview_reveal(&e->tv, edit_rows(e));
        edit_paint(st, e);
    }
    if (st->services->raise_window != NULL) {
        (void)st->services->raise_window(st->services->ctx, e->id);
    }
}

static void
messages_show(ide_t *st)
{
    if (st->nmsg > 0) {
        messages_open(st);      /* now there is something to put in it */
    }
    if (st->msg_surface == NULL) {
        return;
    }
    (void)tiku_list_set_count(&st->msg_list, st->nmsg);
    messages_paint(st);
    messages_publish(st);
}

/*---------------------------------------------------------------------------*/
/* The application                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Pump the run.
 *
 * Called wherever this application is hosted -- linked in, or on the
 * far end of the desk socket -- which is what lets a project run a
 * program from a process of its own.
 */
static void
ide_tick(void *state, int64_t now_us)
{
    ide_t *st = state;
    char buf[256];
    ssize_t n = -1;
    int heard = 0;

    if (st == NULL) {
        return;
    }
    if (st->splash_surface != NULL) {
        /* The splash leaves on its own: an announcement is not a door
         * somebody should have to close.  While it stands, the status
         * line along its bottom says what is being gathered -- which is
         * what those splash screens were FOR, and the one part of them that
         * was never decoration. */
        static const char *const STAGE[] = {
#ifdef TIKU_IDE_C
            "finding the compilers…",
#else
            "waking the interpreter…",
#endif
            "reading the kits…",
            "opening the project window…"
        };
        int stage;

        if (st->splash_from == 0) {
            st->splash_from = now_us;
        } else if (now_us - st->splash_from > SPLASH_US) {
            splash_drop(st);
        } else {
            stage = (int)((now_us - st->splash_from) /
                          (SPLASH_US / 3 + 1));
            if (stage > 2) {
                stage = 2;
            }
            if (stage != st->splash_stage) {
                st->splash_stage = stage;
                announce_paint_art(st->splash_surface, STAGE[stage],
                                   st->splash_art);
                (void)st->services->frame(st->services->ctx,
                                          st->splash_id,
                                          st->splash_surface->px,
                                          SPLASH_W, SPLASH_H);
            }
        }
    }
    if (!st->running) {
        return;
    }
    if (st->run_from == 0) {
        st->run_from = now_us;
    }
    /*
     * BOUNDED, because a program is something somebody may have written
     * wrong: the interpreter stops its own endless loops, but a program
     * waiting for INPUT waits for a person who is not there, and an
     * application that sat on that would say "running…" until it was
     * killed.
     */
    if (now_us - st->run_from > IDE_RUN_LIMIT_US) {
        run_stop(st, "? the run was stopped: it did not finish");
        return;
    }
    for (;;) {
        n = read(st->child_fd, buf, sizeof buf - 1u);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR) {
            n = 0;              /* a broken pipe is a finished run */
            break;
        }
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';
        {
            char *p = buf;

            while (*p != '\0') {
                size_t used = strlen(st->partial);

                if (*p == '\n') {
                    message_take(st, st->partial);
                    st->partial[0] = '\0';
                } else if (used + 1u < sizeof st->partial) {
                    st->partial[used] = *p;
                    st->partial[used + 1u] = '\0';
                }
                p++;
            }
        }
        heard = 1;
    }
    if (n == 0) {
        int status = 0;

        if (st->partial[0] != '\0') {
            message_take(st, st->partial);
            st->partial[0] = '\0';
        }
        (void)waitpid(st->child, &status, 0);
        st->running = 0;
        (void)close(st->child_fd);
        st->child_fd = -1;
        heard = 1;
        /* The run is OVER: what it said comes forward, or a build
         * pressed while the messages stood buried behind the page
         * looks like nothing happening.  Once, at the end -- never on
         * the chunks in between, which would fight the person's hands. */
        if (st->nmsg > 0 && st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             st->msg_id);
        }
    }
    if (heard) {
        messages_show(st);
    }
}

/**
 * @brief The banner the person chose, when they chose one.
 *
 * The picture is looked for where the desktop already keeps a person's
 * replacements -- ~/.config/<name>/splash.bmp (or .png) -- so choosing
 * a banner is putting a file there, the same gesture that replaces an
 * icon.  A picture that will not load is simply not worn.
 */
static tiku_surface_t *
splash_art_load(void)
{
    static const char *const KIND[] = { "bmp", "png" };
    const char *home = getenv("HOME");
    size_t k;

    for (k = 0; home != NULL && k < sizeof KIND / sizeof KIND[0]; k++) {
        char at[512];
        tiku_image_t img;
        tiku_surface_t *art;
        int i;

        snprintf(at, sizeof at, "%s/.config/%s/splash.%s", home,
                 APP_NAME, KIND[k]);
        if (tiku_image_load(at, &img) != TIKU_IMG_OK) {
            continue;
        }
        art = tiku_surface_new(img.w, img.h, TIKU_C_DOC);
        if (art != NULL) {
            for (i = 0; i < img.w * img.h; i++) {
                art->px[i] = img.px[i] & 0x00ffffffu;
            }
        }
        tiku_image_free(&img);
        return art;
    }
    return NULL;
}

/** @brief This program's splash screen: its name, and the words that are true. */
static void
announce_paint_art(tiku_surface_t *sf, const char *status,
                   const tiku_surface_t *art)
{
    static const char *const LINES[] = {
        "by Ambuj Varshney",
#ifdef TIKU_IDE_C
        "the compiler is the target's own",
#else
        "the interpreter is the board's own",
#endif
        "drawn with the six kits"
    };

    if (status != NULL) {
        /* The SPLASH says only what a passing card should: the art,
         * the name, and what is being gathered.  The credits and the
         * version belong to the About box, which is the same card
         * held still for somebody who ASKED -- that is what asking
         * is for. */
        tiku_splash_paint(sf, APP_NAME, NULL, 0, NULL, status, art);
        return;
    }
    tiku_splash_paint(sf, APP_NAME, LINES,
                           (int)(sizeof LINES / sizeof LINES[0]),
                           "TikuOS 0.06", status, art);
}

/**
 * @brief The About box: the announcement again, for as long as it is
 *        wanted this time.
 *
 * Asked for while already standing, it comes FORWARD rather than
 * doubling -- the same rule every window of this application keeps.
 */
static void
about_open(ide_t *st)
{
    if (st->about_surface != NULL) {
        if (st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             st->about_id);
        }
        return;
    }
    st->about_surface = tiku_surface_new(SPLASH_W, SPLASH_H,
                                              TIKU_C_PANEL);
    if (st->about_surface == NULL) {
        return;
    }
    st->about_id = st->services->open(st->services->ctx,
                                      APP_ABOUT, SPLASH_W,
                                      SPLASH_H);
    if (st->about_id == 0u) {
        tiku_surface_free(st->about_surface);
        st->about_surface = NULL;
        return;
    }
    announce_paint_art(st->about_surface, NULL, st->splash_art);
    (void)st->services->frame(st->services->ctx, st->about_id,
                              st->about_surface->px, SPLASH_W,
                              SPLASH_H);
    if (st->services->place != NULL) {
        (void)st->services->place(st->services->ctx, st->about_id,
                                  TIKU_APP_PLACE_ANNOUNCE);
    }
}

static void
about_drop(ide_t *st)
{
    if (st->about_surface == NULL) {
        return;
    }
    st->services->close(st->services->ctx, st->about_id);
    tiku_surface_free(st->about_surface);
    st->about_surface = NULL;
}

/** @brief Let the splash go, whoever asked. */
static void
splash_drop(ide_t *st)
{
    if (st->splash_surface == NULL) {
        return;
    }
    st->services->close(st->services->ctx, st->splash_id);
    tiku_surface_free(st->splash_surface);
    st->splash_surface = NULL;
    /* The splash leaving is the project window arriving: its session
     * row frees first, so the project stands exactly where it would
     * have without the announcement. */
    if (st->proj_id == 0u) {
        st->proj_id = st->services->open(st->services->ctx, "Project",
                                         IDE_W, IDE_H);
        project_paint(st);
        project_publish(st);
    }
}

static int
ide_start(void **state, const tiku_app_services_t *services)
{
    ide_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    tiku_list_init(&st->tv_list, 1);
    st->click_row = -1;
    st->proj_surface = tiku_surface_new(IDE_W, IDE_H, TIKU_C_PANEL);
    if (st->proj_surface == NULL) {
        free(st);
        return -1;
    }
    /* The announcement first, ALONE: the project window arrives when
     * the splash leaves (splash_drop), so what a person sees is a
     * sequence rather than a pile -- and the splash's session row
     * frees before the project takes one, so every window stands where
     * it would have without the announcement. */
    st->splash_art = splash_art_load();
    st->splash_surface = tiku_surface_new(SPLASH_W, SPLASH_H,
                                               TIKU_C_PANEL);
    if (st->splash_surface != NULL) {
        st->splash_id = services->open(services->ctx, APP_NAME,
                                       SPLASH_W, SPLASH_H);
        announce_paint_art(st->splash_surface,
#ifdef TIKU_IDE_C
                           "finding the compilers…",
#else
                           "waking the interpreter…",
#endif
                           st->splash_art);
        (void)services->frame(services->ctx, st->splash_id,
                              st->splash_surface->px, SPLASH_W,
                              SPLASH_H);
        if (services->place != NULL) {
            (void)services->place(services->ctx, st->splash_id,
                                  TIKU_APP_PLACE_ANNOUNCE);
        }
    }
    if (st->splash_surface == NULL) {
        /* No announcement to wait for: the project arrives now. */
        st->proj_id = services->open(services->ctx, "Project", IDE_W,
                                     IDE_H);
        project_paint(st);
        project_publish(st);
    }
    *state = st;
    return 0;
}

static void
ide_stop(void *state)
{
    ide_t *st = state;
    int i;

    if (st == NULL) {
        return;
    }
    if (st->running) {
        /* A run outliving the application it was started from is a
         * program nobody is listening to. */
        (void)kill(st->child, SIGTERM);
        (void)waitpid(st->child, NULL, 0);
        (void)close(st->child_fd);
        st->running = 0;
    }
    for (i = 0; i < IDE_EDITS; i++) {
        tiku_surface_free(st->edit[i].surface);
    }
    tiku_surface_free(st->msg_surface);
    tiku_surface_free(st->splash_surface);
    tiku_surface_free(st->about_surface);
    tiku_surface_free(st->splash_art);
    tiku_surface_free(st->proj_surface);
    free(st);
}

/** @brief Open whatever row the list is standing on. */
static void
open_chosen(ide_t *st)
{
    int row = tiku_list_chosen(&st->tv_list);

    if (row >= 0 && row < st->proj.nrow &&
        !st->proj.row[row].heading) {
        edit_open(st, &st->proj.row[row]);
    }
}

/**
 * @brief Name what closing would throw away, or say there is nothing.
 *
 * @return the number of documents holding unsaved work.
 */
static int
unsaved(const ide_t *st, char *out, size_t max)
{
    int i, n = 0;
    size_t used = 0u;

    if (max > 0u) {
        out[0] = '\0';
    }
    for (i = 0; i < IDE_EDITS; i++) {
        const edit_t *e = &st->edit[i];

        if (e->surface == NULL || !tiku_textview_modified(&e->tv)) {
            continue;
        }
        used += (size_t)snprintf(out + used, (used < max) ? max - used : 0u,
                                 "%s%s", (n > 0) ? ", " : "", e->name);
        n++;
    }
    return n;
}

static int
ide_event(void *state, uint32_t window, const tiku_event_t *event)
{
    ide_t *st = state;
    edit_t *e = edit_of_id(st, window);

    if (event->type != TIKU_EVENT_KEY_DOWN &&
        event->type != TIKU_EVENT_POINTER_DOWN &&
        event->type != TIKU_EVENT_POINTER_MOVE &&
        event->type != TIKU_EVENT_POINTER_UP &&
        event->type != TIKU_EVENT_RESIZE &&
        event->type != TIKU_EVENT_WHEEL) {
        return 0;
    }
    if (window == st->about_id && st->about_surface != NULL) {
        if (event->type == TIKU_EVENT_POINTER_DOWN) {
            /* A plain panel wears no close button: a click IS the
             * "read it, done" the old About boxes answered to. */
            st->services->close(st->services->ctx, st->about_id);
            return 0;
        }
        if (event->type == TIKU_EVENT_KEY_DOWN &&
            (event->key == TIKU_KEY_ESCAPE ||
             event->key == TIKU_KEY_RETURN)) {
            about_drop(st);     /* read, and put away */
        }
        return 0;
    }
    if (window == st->splash_id && st->splash_surface != NULL) {
        splash_drop(st);        /* a person already working is not an
                                 * audience */
        return 0;
    }
    if (st->ask.open && window == st->proj_id) {
        int at = -1;

        if (event->type == TIKU_EVENT_KEY_DOWN) {
            at = tiku_alert_key(&st->ask, event->key);
        } else {
            at = tiku_alert_button_at(&st->ask, st->ask_frame.w,
                     st->ask_frame.h, event->x - st->ask_frame.x,
                     event->y - st->ask_frame.y);
            if (at >= 0) {
                st->ask.chosen = at;
            }
        }
        if (at < 0) {
            return 0;           /* not the question's key: ignored */
        }
        tiku_alert_reset(&st->ask);
        if (at == 1) {
            return 1;           /* Discard: the project, and all of it */
        }
        project_paint(st);      /* Cancel: everything stands */
        return 0;
    }
    if (e != NULL && event->type == TIKU_EVENT_RESIZE) {
        /* The person let go of the corner: repaint at the size the
         * window now is, and the next frame hands it over. */
        int w = (event->width > 160) ? event->width : 160;
        int h = (event->height > 100) ? event->height : 100;
        tiku_surface_t *grown = tiku_surface_new(w, h,
                                                      TIKU_C_PANEL);

        if (grown != NULL) {
            tiku_surface_free(e->surface);
            e->surface = grown;
            e->w = w;
            e->h = h;
            edit_paint(st, e);
        }
        return 0;
    }
    if (e != NULL && event->type == TIKU_EVENT_WHEEL) {
        /* The wheel scrolls the page and leaves the caret where it is,
         * the way the scrollbar's track does: a person reading ahead
         * has not moved where they type. */
        int rows = edit_rows(e);
        int top = tiku_textview_top(&e->tv);

        (void)tiku_textview_scroll_to(&e->tv, top + event->dy, rows);
        edit_paint(st, e);
        return 0;
    }
    if (e != NULL) {
        /* An editor window: the document takes it. */
        unsigned ch = (unsigned char)event->text[0];

        if (event->type == TIKU_EVENT_POINTER_DOWN) {
            tiku_rect_t save_b = TB_SAVE;
            tiku_rect_t run_b = TB_RUN;

            if (event->x >= save_b.x && event->x < save_b.x + save_b.w &&
                event->y >= save_b.y && event->y < save_b.y + save_b.h) {
                (void)edit_save(e);
                edit_paint(st, e);
                edit_publish(st, e);
                project_publish(st);
                return 0;
            }
            if (e->lang == TIKU_SYNTAX_BASIC &&
                event->x >= run_b.x && event->x < run_b.x + run_b.w &&
                event->y >= run_b.y && event->y < run_b.y + run_b.h) {
                /* What is run is what is on disk, so the button saves
                 * first -- the same rule the menu's Run keeps.  Painted
                 * AFTER the run starts, so the stop square is already
                 * red when the eye comes looking for it. */
                (void)edit_save(e);
                run_file(st, e->path);
                edit_paint(st, e);
                edit_publish(st, e);
                project_publish(st);
                return 0;
            }
            {
                /* The new tools: undo, redo, and find. */
                tiku_rect_t u = TB_UNDO, r = TB_REDO, fb = TB_FIND;
                int hit_u = event->x >= u.x && event->x < u.x + u.w &&
                            event->y >= u.y && event->y < u.y + u.h;
                int hit_r = event->x >= r.x && event->x < r.x + r.w &&
                            event->y >= r.y && event->y < r.y + r.h;
                int hit_f = event->x >= fb.x && event->x < fb.x + fb.w &&
                            event->y >= fb.y && event->y < fb.y + fb.h;

                if (hit_u || hit_r) {
                    if (edit_clip(st, e, hit_u ? CMD_UNDO : CMD_REDO)) {
                        (void)tiku_textview_reveal(&e->tv, edit_rows(e));
                        edit_paint(st, e);
                        edit_publish(st, e);
                        project_publish(st);
                    }
                    return 0;
                }
                if (hit_f) {
                    e->finding = !e->finding;
                    if (e->finding) {
                        tiku_field_start(&e->find, "");
                    } else {
                        tiku_field_stop(&e->find);
                    }
                    edit_paint(st, e);
                    return 0;
                }
            }
            {
                int tool = tb_tool_at(e, event->x, event->y);

                if (tool == 2 && e->lang == TIKU_SYNTAX_BASIC) {
                    /* Stop: the person's hand on a run that will not
                     * end on its own. */
                    run_stop(st, "? the run was stopped by hand");
                    edit_paint(st, e);
                    return 0;
                }
                if (tool == 1 && e->lang == TIKU_SYNTAX_C) {
                    /* Build, the C page's run: saved first, the same
                     * rule Run keeps. */
                    (void)edit_save(e);
                    edit_paint(st, e);
                    edit_publish(st, e);
                    build_project(st);
                    project_publish(st);
                    return 0;
                }
                if (tool == 6 && e->lang == TIKU_SYNTAX_BASIC) {
                    renumber_basic(e);
                    edit_paint(st, e);
                    edit_publish(st, e);
                    project_publish(st);
                    return 0;
                }
            }
            /* A press on the scrollbar's track PAGES the view toward
             * the hand -- the same gesture the panels and folders
             * answer -- and moves the page WITHOUT the caret, because a
             * person looking elsewhere has not moved where they type. */
            if (event->x >= e->w - PAGE_SB &&
                event->y >= edit_page_top(e) && event->y < e->h - STRIP_H) {
                int rows = edit_rows(e);
                int top = tiku_textview_top(&e->tv);
                int ptop = edit_page_top(e);
                int mid = ptop + (e->h - STRIP_H - ptop) / 2;
                int step = (rows > 1) ? rows - 1 : 1;

                (void)tiku_textview_scroll_to(&e->tv,
                    top + ((event->y < mid) ? -step : step), rows);
                edit_paint(st, e);
                return 0;
            }
            /* Not a button or the bar: the page itself, so the caret
             * goes where the pointer is.  The press ANCHORS a selection
             * there -- a drag from here grows it, and a click that does
             * not move leaves anchor and caret together, which is none. */
            if (event->x < e->w - PAGE_SB &&
                event->y >= edit_page_top(e) && event->y < e->h - STRIP_H) {
                edit_place_from_xy(e, event->x, event->y);
                tiku_textview_select_none(&e->tv);
                tiku_textview_select_begin(&e->tv);
                e->drag_select = 1;
                (void)tiku_textview_reveal(&e->tv, edit_rows(e));
                edit_paint(st, e);
            }
            return 0;
        }
        if (event->type == TIKU_EVENT_POINTER_MOVE) {
            /* Over the toolbar the pointer NAMES the tool it rests on:
             * the tip appears when it arrives and leaves when it does. */
            int tool = (event->y < TOOLBAR_H)
                           ? tb_tool_at(e, event->x, event->y) : -1;

            if (tool != e->tip) {
                e->tip = tool;
                edit_paint(st, e);
            }
            /* A drag from a page press extends the selection to the
             * pointer; a move with no such press is not ours. */
            if (e->drag_select &&
                event->x < e->w - PAGE_SB &&
                event->y >= edit_page_top(e) && event->y < e->h - STRIP_H) {
                edit_place_from_xy(e, event->x, event->y);
                (void)tiku_textview_reveal(&e->tv, edit_rows(e));
                edit_paint(st, e);
            }
            return 0;
        }
        if (event->type == TIKU_EVENT_POINTER_UP) {
            e->drag_select = 0;
            return 0;
        }
        if (ch == 0u) {
            ch = event->key;
        }
        if (e->finding) {
            /* The find bar has the keys while it is open: Escape puts it
             * away, Return walks the hits, the rest edit the word. */
            if (event->key == TIKU_KEY_ESCAPE) {
                e->finding = 0;
                tiku_field_stop(&e->find);
                edit_paint(st, e);
                return 0;
            }
            if (event->key == TIKU_KEY_RETURN) {
                edit_find_run(e);
                edit_paint(st, e);
                return 0;
            }
            if (event->key == TIKU_KEY_BACKSPACE) {
                (void)tiku_field_key(&e->find, TIKU_FIELD_BACKSPACE, 0, '\0');
            } else if (event->key == TIKU_KEY_LEFT) {
                (void)tiku_field_key(&e->find, TIKU_FIELD_LEFT, 0, '\0');
            } else if (event->key == TIKU_KEY_RIGHT) {
                (void)tiku_field_key(&e->find, TIKU_FIELD_RIGHT, 0, '\0');
            } else if (ch >= 32u && ch < 127u &&
                       !(event->modifiers & TIKU_MOD_CMD)) {
                (void)tiku_field_key(&e->find, TIKU_FIELD_CHAR, 0, (char)ch);
            }
            edit_paint(st, e);
            return 0;
        }
        {
            int rows = edit_rows(e);
            int edited = 0;
            int shift = (event->modifiers & TIKU_MOD_SHIFT) != 0;
            int is_move =
                event->key == TIKU_KEY_LEFT ||
                event->key == TIKU_KEY_RIGHT ||
                event->key == TIKU_KEY_UP ||
                event->key == TIKU_KEY_DOWN ||
                event->key == TIKU_KEY_HOME ||
                event->key == TIKU_KEY_END ||
                event->key == TIKU_KEY_PAGE_UP ||
                event->key == TIKU_KEY_PAGE_DOWN;

            /* Cmd-F opens the find bar, which then has the keys. */
            if ((event->modifiers & TIKU_MOD_CMD) && event->key == 'f') {
                e->finding = 1;
                tiku_field_start(&e->find, "");
                edit_paint(st, e);
                return 0;
            }
            /* The file keys: Cmd-S saves, Cmd-W closes THIS window,
             * Cmd-R runs the page's own program, Cmd-. stops a run --
             * each the key its menu row says. */
            if ((event->modifiers & TIKU_MOD_CMD) && event->key == 's') {
                (void)edit_save(e);
                edit_paint(st, e);
                edit_publish(st, e);
                project_publish(st);
                return 0;
            }
            if ((event->modifiers & TIKU_MOD_CMD) && event->key == 'w') {
                edit_drop(st, e);
                return 0;
            }
            if ((event->modifiers & TIKU_MOD_CMD) && event->key == 'r' &&
                e->lang == TIKU_SYNTAX_BASIC) {
                (void)edit_save(e);
                run_file(st, e->path);
                edit_paint(st, e);
                edit_publish(st, e);
                project_publish(st);
                return 0;
            }
            if ((event->modifiers & TIKU_MOD_CMD) && event->key == '.' &&
                e->lang == TIKU_SYNTAX_BASIC) {
                run_stop(st, "? the run was stopped by hand");
                edit_paint(st, e);
                return 0;
            }
            /* The desk's editing keys -- Cmd X/C/V/A, and Cmd-Z /
             * Cmd-Shift-Z for undo and redo -- caught before the
             * switch, so their letters are never typed. */
            if ((event->modifiers & TIKU_MOD_CMD) &&
                (event->key == 'x' || event->key == 'c' ||
                 event->key == 'v' || event->key == 'a' ||
                 event->key == 'z')) {
                int cmd = (event->key == 'x') ? CMD_CUT
                        : (event->key == 'c') ? CMD_COPY
                        : (event->key == 'v') ? CMD_PASTE
                        : (event->key == 'a') ? CMD_SELALL
                        : ((event->modifiers & TIKU_MOD_SHIFT)
                               ? CMD_REDO : CMD_UNDO);
                int did = edit_clip(st, e, cmd);

                if (did && cmd != CMD_COPY && cmd != CMD_SELALL) {
                    (void)tiku_textview_reveal(&e->tv, rows);
                    edit_publish(st, e);
                    project_publish(st);
                }
                edit_paint(st, e);
                return 0;
            }
            /* A move with Shift GROWS the selection from where it was
             * anchored; a move without lets any selection go.  The
             * editing keys need no such care -- the view removes a
             * selection before it inserts, breaks or rubs out. */
            if (is_move) {
                if (shift) {
                    tiku_textview_select_begin(&e->tv);
                } else {
                    tiku_textview_select_none(&e->tv);
                }
            }

            switch (event->key) {
            case TIKU_KEY_LEFT:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_LEFT);
                break;
            case TIKU_KEY_RIGHT:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_RIGHT);
                break;
            case TIKU_KEY_UP:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_UP);
                break;
            case TIKU_KEY_DOWN:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_DOWN);
                break;
            case TIKU_KEY_HOME:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_HOME);
                break;
            case TIKU_KEY_END:
                tiku_textview_move(&e->tv, TIKU_TEXTVIEW_END);
                break;
            case TIKU_KEY_PAGE_UP:
            case TIKU_KEY_PAGE_DOWN: {
                /* A screenful, the caret keeping its column where the
                 * line it lands on is long enough to hold it. */
                int cy, cx;

                tiku_textview_caret(&e->tv, &cy, &cx);
                cy += (event->key == TIKU_KEY_PAGE_UP) ? -rows : rows;
                tiku_textview_place(&e->tv, cy, cx);
                break;
            }
            case TIKU_KEY_DELETE:
                tiku_textview_delete(&e->tv);
                edited = 1;
                break;
            case TIKU_KEY_BACKSPACE:
                tiku_textview_backspace(&e->tv);
                edited = 1;
                break;
            case TIKU_KEY_RETURN: {
                int cy, cx, n;

                tiku_textview_caret(&e->tv, &cy, &cx);
                edit_normalize_word(e);   /* the return ends a word */
                tiku_textview_newline(&e->tv);
                /* The next line's number, already typed: the numbers
                 * are the language's bookkeeping, and bookkeeping is
                 * what a machine is FOR.  A line wearing none offers
                 * none, so prose in a .bas file is not fought over. */
                if (e->lang == TIKU_SYNTAX_BASIC) {
                    n = next_number(&e->tv, cy);
                    if (n > 0) {
                        type_number(&e->tv, n);
                    }
                }
                edited = 1;
                break;
            }
            default:
                if (ch >= 32u && ch < 127u) {
                    /* A non-word character ENDS the word before it, and
                     * a keyword there shouts before the character lands. */
                    if (!norm_is_word((int)ch)) {
                        edit_normalize_word(e);
                    }
                    tiku_textview_insert(&e->tv, (char)ch);
                    edited = 1;
                } else {
                    return 0;   /* not the editor's key */
                }
                break;
            }
            /* The caret is followed after EVERY move, so typing at the
             * bottom of a long file never runs off the page edge. */
            (void)tiku_textview_reveal(&e->tv, rows);
            edit_paint(st, e);
            if (edited) {
                edit_publish(st, e);
                project_publish(st);   /* the mark follows the document */
            }
            return 0;
        }
    }
    if (window == st->msg_id && st->msg_surface != NULL) {
        tiku_rect_t body = { MARGIN, MARGIN, MSG_W - 2 * MARGIN,
                                  MSG_H - STRIP_H - 2 * MARGIN };

        if (event->type == TIKU_EVENT_POINTER_DOWN) {
            tiku_list_click(&st->msg_list,
                            tiku_list_at(&st->msg_list, body, event->x,
                                         event->y),
                            event->modifiers);
            messages_show(st);
            return 0;
        }
        if (event->key == TIKU_KEY_RETURN) {
            /* What FOLLOWING a message means is the application's, the
             * same way opening a row is. */
            message_follow(st);
            return 0;
        }
        if (tiku_list_key(&st->msg_list, event->key, event->modifiers,
                          body)) {
            messages_show(st);
        }
        return 0;
    }
    if (window != st->proj_id) {
        return 0;
    }
    if (event->type == TIKU_EVENT_POINTER_DOWN) {
        int row = tiku_list_at(&st->tv_list, project_body(), event->x,
                               event->y);

        /* The second click of two on the same row OPENS it, exactly as
         * Return does: a person with a mouse is owed the same door. */
        if (row >= 0 && row == st->click_row &&
            event->time_us - st->click_at < 400000 &&
            tiku_list_chosen(&st->tv_list) == row) {
            st->click_row = -1;
            open_chosen(st);
            return 0;
        }
        st->click_row = row;
        st->click_at = event->time_us;
        tiku_list_click(&st->tv_list, row, event->modifiers);
        project_paint(st);
        project_publish(st);    /* Run follows the row it would run */
        return 0;
    }
    if (event->key == TIKU_KEY_RETURN) {
        /* What opening a row MEANS is the application's, which is why
         * the list hands Return back rather than taking it. */
        open_chosen(st);
        return 0;
    }
    if (tiku_list_key(&st->tv_list, event->key, event->modifiers,
                      project_body())) {
        project_paint(st);
        project_publish(st);    /* Run follows the row it would run */
    }
    return 0;
}

static int
ide_pick(void *state, uint32_t window, int command)
{
    ide_t *st = state;
    edit_t *e = edit_of_id(st, window);

    if (command >= CMD_WINDOW) {
        int i = command - CMD_WINDOW;

        if (i >= 0 && i < IDE_EDITS && st->edit[i].surface != NULL &&
            st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             st->edit[i].id);
        }
        return 0;
    }
    if (e != NULL && command == CMD_RUN &&
        e->lang == TIKU_SYNTAX_BASIC) {
        /* Run, asked ON a page: THIS file, not whichever row the
         * project window happens to hold selected. */
        (void)edit_save(e);
        run_file(st, e->path);
        edit_paint(st, e);
        edit_publish(st, e);
        project_publish(st);
        return 0;
    }
    if (e != NULL && command == CMD_STOP) {
        run_stop(st, "? the run was stopped by hand");
        edit_paint(st, e);
        edit_publish(st, e);
        return 0;
    }
    if (e != NULL && command == CMD_FIND) {
        e->finding = 1;
        tiku_field_start(&e->find, "");
        edit_paint(st, e);
        return 0;
    }
    if (e != NULL && command == CMD_RENUM) {
        renumber_basic(e);
        edit_paint(st, e);
        edit_publish(st, e);
        project_publish(st);
        return 0;
    }
    if (e != NULL && (command == CMD_CUT || command == CMD_COPY ||
                      command == CMD_PASTE || command == CMD_SELALL ||
                      command == CMD_UNDO || command == CMD_REDO)) {
        if (edit_clip(st, e, command)) {
            (void)tiku_textview_reveal(&e->tv, edit_rows(e));
            edit_paint(st, e);
            if (command != CMD_COPY && command != CMD_SELALL) {
                edit_publish(st, e);
                project_publish(st);
            }
        }
        return 0;
    }
    switch (command) {
    case CMD_OPEN:
        if (st->services->pick != NULL) {
            st->asked = ASK_OPEN;
            /* Only a project DESCRIPTION answers this question, and
             * the panel says so: every other file shows greyed. */
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_OPEN, NULL,
                                     "project.tiku");
        }
        break;
    case CMD_ABOUT:
        about_open(st);
        break;
    case CMD_NEWPROJ:
        if (st->services->pick != NULL) {
            st->asked = ASK_NEWPROJ;
            /* The shell's own Save panel names the place and the file;
             * the offered name is the one the opener looks for. */
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_SAVE, NULL,
                                     "untitled");
        }
        break;
    case CMD_NEWFILE:
        if (st->services->pick != NULL && st->proj_file[0] != '\0') {
            st->asked = ASK_FILE;
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_SAVE, st->proj.dir,
                                     APP_NEW_FILE);
        }
        break;
    case CMD_OPENFILE:
        if (st->services->pick != NULL) {
            st->asked = ASK_EDIT;
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_OPEN,
                                     (st->proj_file[0] != '\0')
                                         ? st->proj.dir : NULL, NULL);
        }
        break;
    case CMD_ADDFILE:
        if (st->services->pick != NULL && st->proj_file[0] != '\0') {
            st->asked = ASK_FILE;
            (void)st->services->pick(st->services->ctx, st->proj_id,
                                     TIKU_APP_PICK_OPEN, st->proj.dir,
                                     NULL);
        }
        break;
    case CMD_SAVE:
        if (e != NULL) {
            (void)edit_save(e);
            edit_paint(st, e);
            edit_publish(st, e);
            project_publish(st);
        }
        break;
    case CMD_QUIT:
        if (e != NULL) {
            edit_drop(st, e);
        }
        break;
    case CMD_REMOVEFILE:
        project_remove_file(st);
        break;
    case CMD_RUN: {
        int row = tiku_list_chosen(&st->tv_list);

        if (row >= 0 && row < st->proj.nrow &&
            !st->proj.row[row].heading) {
            edit_t *open_now = edit_of_path(st, st->proj.row[row].path);

            /* What is RUN is what is on disk, so a window holding
             * changes is saved first -- running the last saved copy
             * while showing another is how a person debugs a program
             * they are not looking at. */
            if (open_now != NULL &&
                tiku_textview_modified(&open_now->tv)) {
                (void)edit_save(open_now);
                edit_paint(st, open_now);
                edit_publish(st, open_now);
            }
            run_file(st, st->proj.row[row].path);
            project_publish(st);
        }
        break;
    }
    case CMD_BUILD:
        build_project(st);
        project_publish(st);
        break;
    case CMD_FOLLOW:
        message_follow(st);
        break;
    case CMD_SHUT: {
        char names[192];

        if (unsaved(st, names, sizeof names) == 0) {
            return 1;           /* nothing at stake: closing is closing */
        }
        {
            char says[320];

            snprintf(says, sizeof says,
                     "Close the project? %s %s changes nothing has "
                     "saved.", names,
                     (strchr(names, ',') != NULL) ? "hold" : "holds");
            tiku_alert_open(&st->ask, TIKU_ALERT_WARN, 0, says,
                            "Cancel|Discard");
        }
        if (st->services->raise_window != NULL) {
            (void)st->services->raise_window(st->services->ctx,
                                             st->proj_id);
        }
        project_paint(st);
        break;
    }
    default:
        break;
    }
    return 0;
}

static void
ide_picked(void *state, uint32_t window, const char *path)
{
    ide_t *st = state;

    (void)window;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    switch (st->asked) {
    case ASK_NEWPROJ:
        project_create(st, path);
        break;
    case ASK_FILE:
        project_add_file(st, path);
        break;
    case ASK_EDIT: {
        /* An editor on the file, wherever it lives: opening is not
         * adding, and a file already open comes forward. */
        prow_t want;
        const char *slash = strrchr(path, '/');

        memset(&want, 0, sizeof want);
        snprintf(want.path, sizeof want.path, "%s", path);
        snprintf(want.name, sizeof want.name, "%s",
                 (slash != NULL) ? slash + 1 : path);
        edit_open(st, &want);
        break;
    }
    default:
        project_take(st, path);
        break;
    }
}

/** @brief The runtime took a window; the project keeps the rest. */
static void
ide_closed(void *state, uint32_t window)
{
    ide_t *st = state;
    edit_t *e = edit_of_id(st, window);

    if (e != NULL) {
        tiku_surface_free(e->surface);
        e->surface = NULL;
        project_paint(st);
        project_publish(st);
        return;
    }
    if (window == st->msg_id && st->msg_surface != NULL) {
        tiku_surface_free(st->msg_surface);
        st->msg_surface = NULL;
        st->msg_id = 0u;
        return;
    }
    if (window == st->splash_id && st->splash_surface != NULL) {
        tiku_surface_free(st->splash_surface);
        st->splash_surface = NULL;
        return;
    }
    if (window == st->about_id && st->about_surface != NULL) {
        tiku_surface_free(st->about_surface);
        st->about_surface = NULL;
    }
}

const tiku_app_descriptor_t tiku_ide_app = {
    .id = APP_ID,
    .name = APP_NAME,
    .start = ide_start,
    .stop = ide_stop,
    .pick = ide_pick,
    .picked = ide_picked,
    .tick = ide_tick,
    .event_in = ide_event,
    .closed = ide_closed
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_ide_app
};
#endif

#ifndef TIKU_APP_EMBED
int
main(void)
{
    return tiku_client_run(&tiku_ide_app);
}
#endif
