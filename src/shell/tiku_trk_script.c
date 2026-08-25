/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script.c - script reader, event source and check tally.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_script.h"

#include "tiku_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <sys/stat.h>

#define SC_MAX_STEPS 512
#define SC_TEXT_MAX  256

/** @brief What one line was asking for. */
typedef enum {
    OP_KEY = 0, OP_PRESS, OP_MOVE, OP_RELEASE, OP_CLICK, OP_CLICK_ROW, OP_CLICK_DESK, OP_PRESS_DESK, OP_MOVE_DESK, OP_RELEASE_DESK, OP_DRAG_WINDOW,
    OP_PRESS_ROW, OP_MOVE_ROW, OP_RELEASE_ROW, OP_CLICK_WINDOW, OP_CLICK_CELL,
    OP_WAIT, OP_SHOT, OP_EXPECT_LOCATION, OP_EXPECT_NAV_BACK,
    OP_EXPECT_NAV_FORWARD, OP_EXPECT_NAV_UP, OP_EXPECT_NAV_VISIBLE,
    OP_EXPECT_VIEWPORT_ROWS,
    OP_EXPECT_STATUS, OP_EXPECT_TITLE, OP_EXPECT_ALERT, OP_EXPECT_SCROLL, OP_EXPECT_ROWS, OP_REMOVE, OP_EXPECT_PERM, OP_EXPECT_WINDOWS, OP_EXPECT_SEL, OP_EXPECT_SELNAME, OP_EXPECT_RENAMING, OP_EXPECT_FINDTEXT, OP_EXPECT_FINDTYPE, OP_EXPECT_FOLDERS, OP_EXPECT_WINAT, OP_EXPECT_COLS, OP_EXPECT_BARAT, OP_EXPECT_FILE, OP_EXPECT_NO_FILE, OP_EXPECT_ICONAT, OP_EXPECT_ICONSCREEN, OP_EXPECT_PIXEL, OP_EXPECT_INK, OP_EXPECT_FONT,
    OP_EXPECT_NO_TITLE, OP_DRAW_RESET, OP_EXPECT_DRAWN, OP_EXPECT_MENU, OP_EXPECT_NO_MENU, OP_EXPECT_NARRATE, OP_EXPECT_TOLD, OP_EXPECT_MOUNTS, OP_EXPECT_CONTROL, OP_EXPECT_READ, OP_EXPECT_OPERATION, OP_EXPECT_COUNT, OP_EXPECT_LINK, OP_EXPECT_FILEHAS, OP_EXPECT_FILELACKS, OP_EXPECT_DESKSEL, OP_EXPECT_DESKICON, OP_EXPECT_NO_DESKICON, OP_EXPECT_DESKSELNAME,
    OP_NOTE, OP_MENU_PICK, OP_WRITE, OP_SAME_AS_REDRAW, OP_QUIT
} sc_op_t;

typedef struct {
    sc_op_t  op;
    unsigned key;
    unsigned modifiers;
    int      button_x, button_y;        /* a resolved tab point          */
    int      x, y;
    unsigned button;
    int      count;                     /* WAIT turns                    */
    char     text[SC_TEXT_MAX];         /* path, substring or note       */
    /* A second string, where one is needed.  Long enough for a PATH: it
     * was a word, and a needle too long to hold was silently truncated to
     * nothing -- which made every check that used it pass. */
    char     text2[512];
    int      line;
} sc_step_t;

struct tiku_trk_script {
    sc_step_t step[SC_MAX_STEPS];
    int       count;
    int       cursor;
    int       waiting;                  /* turns still owed to a WAIT    */
    int       pending_release;
    int       dragging_window;          /* a tab drag, mid-play          */
    int       drag_to_x, drag_to_y;          /* a CLICK's second half         */
    char      note[SC_TEXT_MAX];        /* what the next check is for    */
    int       passes, fails;
};

/** @brief Whether @p path exists at all. */
static int
there(const char *path)
{
    struct stat st;

    return (path != NULL && path[0] != '\0' && stat(path, &st) == 0);
}

/** @brief Split @p line into words in place.  @return how many. */
static int
words(char *line, char **out, int max)
{
    int n = 0;
    char *p = line;

    while (*p != '\0' && n < max) {
        /* The line's own newline is a separator like any other: without
         * that, the last word on every line carries it and compares equal
         * to nothing. */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        out[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' &&
               *p != '\r') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return n;
}

/** @brief Everything from word @p from onward, with single spaces. */
static void
rest(char **w, int n, int from, char *out, size_t max)
{
    int i;
    size_t k = 0;

    out[0] = '\0';
    for (i = from; i < n; i++) {
        size_t l = strlen(w[i]);

        if (k + l + 2u >= max) {
            break;
        }
        if (k > 0u) {
            out[k++] = ' ';
        }
        memcpy(out + k, w[i], l);
        k += l;
        out[k] = '\0';
    }
}

/** @brief Read the trailing modifier words of a key line. */
static unsigned
mods_of(char **w, int n, int from)
{
    unsigned m = 0;
    int i;

    for (i = from; i < n; i++) {
        if (strcmp(w[i], "ctrl") == 0) {
            m |= TIKU_MOD_CMD;
        } else if (strcmp(w[i], "shift") == 0) {
            m |= TIKU_MOD_SHIFT;
        } else if (strcmp(w[i], "alt") == 0) {
            m |= TIKU_MOD_OPTION;
        }
    }
    return m;
}

/** @brief Resolve the portable key names accepted by the script grammar. */
static unsigned
key_of(const char *name)
{
    static const struct {
        const char *name;
        unsigned key;
    } named[] = {
        { "Up", TIKU_KEY_UP },
        { "Down", TIKU_KEY_DOWN },
        { "Left", TIKU_KEY_LEFT },
        { "Right", TIKU_KEY_RIGHT },
        { "Home", TIKU_KEY_HOME },
        { "End", TIKU_KEY_END },
        { "Prior", TIKU_KEY_PAGE_UP },
        { "Next", TIKU_KEY_PAGE_DOWN },
        { "BackSpace", TIKU_KEY_BACKSPACE },
        { "Delete", TIKU_KEY_DELETE },
        { "Escape", TIKU_KEY_ESCAPE },
        { "Return", TIKU_KEY_RETURN },
        { "KP_Enter", TIKU_KEY_RETURN },
        { "Tab", TIKU_KEY_TAB },
        { "Menu", TIKU_KEY_MENU },
        { "F2", TIKU_KEY_F2 },
        { "space", ' ' },
        { "comma", ',' },
        { "plus", '+' },
        { "equal", '=' },
        { "minus", '-' }
    };
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return 0u;
    }
    if (name[1] == '\0') {
        return (unsigned char)name[0];
    }
    for (i = 0; i < sizeof named / sizeof named[0]; i++) {
        if (strcmp(name, named[i].name) == 0) {
            return named[i].key;
        }
    }
    return 0u;
}

tiku_trk_script_t *
tiku_trk_script_open(const char *path)
{
    tiku_trk_script_t *s;
    FILE *f;
    char line[SC_TEXT_MAX * 2];
    int lineno = 0;

    if (path == NULL) {
        return NULL;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "script: cannot read '%s'\n", path);
        return NULL;
    }
    s = calloc(1u, sizeof *s);
    if (s == NULL) {
        (void)fclose(f);
        return NULL;
    }
    while (fgets(line, sizeof line, f) != NULL && s->count < SC_MAX_STEPS) {
        char *w[16];
        char *hash;
        int n;
        sc_step_t *st;

        lineno++;
        hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        n = words(line, w, 16);
        if (n == 0) {
            continue;
        }
        st = &s->step[s->count];
        memset(st, 0, sizeof *st);
        st->line = lineno;
        st->button = 1u;

        if (strcmp(w[0], "key") == 0 && n >= 2) {
            st->op = OP_KEY;
            st->key = key_of(w[1]);
            if (st->key == 0u) {
                fprintf(stderr, "script:%d: unknown key '%s'\n", lineno,
                        w[1]);
                continue;
            }
            st->modifiers = mods_of(w, n, 2);
        } else if ((strcmp(w[0], "click") == 0 ||
                    strcmp(w[0], "press") == 0 ||
                    strcmp(w[0], "move") == 0 ||
                    strcmp(w[0], "release") == 0) && n >= 3) {
            st->op = (w[0][0] == 'c') ? OP_CLICK
                   : (w[0][0] == 'p') ? OP_PRESS
                   : (w[0][0] == 'm') ? OP_MOVE : OP_RELEASE;
            st->x = atoi(w[1]);
            st->y = atoi(w[2]);
            if (n >= 4) {
                st->button = (unsigned)atoi(w[3]);
            }
            st->modifiers = mods_of(w, n, 4);
        } else if (strcmp(w[0], "click-window") == 0 && n >= 2) {
            /* A click on the tab, which is how a window is brought to the
             * front by hand -- and by title, so the gesture survives the
             * window moving. */
            st->op = OP_CLICK_WINDOW;
            snprintf(st->text, sizeof st->text, "%s", w[1]);
            st->button = 1u;
        } else if (strcmp(w[0], "drag-window") == 0 && n >= 4) {
            /* By TITLE, not by pixel: where a window's tab sits is the
             * shell's business, and a test that encoded it broke the day
             * the window grew a menu bar. */
            st->op = OP_DRAG_WINDOW;
            snprintf(st->text, sizeof st->text, "%s", w[1]);
            st->x = atoi(w[2]);
            st->y = atoi(w[3]);
            st->button = 1u;
        } else if ((strcmp(w[0], "press-row") == 0 ||
                    strcmp(w[0], "move-row") == 0 ||
                    strcmp(w[0], "release-row") == 0) && n >= 2) {
            /* The halves of a drag, aimed at a ROW rather than a pixel, so
             * a gesture survives the chrome above the listing moving. */
            st->op = (w[0][0] == 'p') ? OP_PRESS_ROW
                   : (w[0][0] == 'm') ? OP_MOVE_ROW : OP_RELEASE_ROW;
            st->count = atoi(w[1]);
            if (n >= 3) {
                st->button = (unsigned)atoi(w[2]);
            }
            st->modifiers = mods_of(w, n, 3);
        } else if (strcmp(w[0], "click-cell") == 0 && n >= 3) {
            st->op = OP_CLICK_CELL;
            st->count = atoi(w[1]);
            rest(w, n, 2, st->text, sizeof st->text);
            st->button = 1u;
        } else if (strcmp(w[0], "click-row") == 0 && n >= 2) {
            st->op = OP_CLICK_ROW;
            st->count = atoi(w[1]);
            if (n >= 3) {
                st->button = (unsigned)atoi(w[2]);
            }
            st->modifiers = mods_of(w, n, 3);
        } else if (strcmp(w[0], "wait") == 0 && n >= 2) {
            st->op = OP_WAIT;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "menu-pick") == 0 && n >= 2) {
            st->op = OP_MENU_PICK;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "shot") == 0 && n >= 2) {
            st->op = OP_SHOT;
            snprintf(st->text, sizeof st->text, "%s", w[1]);
        } else if (strcmp(w[0], "expect-font") == 0 && n >= 2) {
            st->op = OP_EXPECT_FONT;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-status") == 0 && n >= 2) {
            st->op = OP_EXPECT_STATUS;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-title") == 0 && n >= 2) {
            st->op = OP_EXPECT_TITLE;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-no-title") == 0 && n >= 2) {
            st->op = OP_EXPECT_NO_TITLE;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-alert") == 0 && n >= 2) {
            st->op = OP_EXPECT_ALERT;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-scroll") == 0 && n >= 2) {
            st->op = OP_EXPECT_SCROLL;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-scroll-at-most") == 0 && n >= 2) {
            st->op = OP_EXPECT_SCROLL;
            st->count = atoi(w[1]);
            st->x = 1;
        } else if (strcmp(w[0], "expect-rows") == 0 && n >= 2) {
            st->op = OP_EXPECT_ROWS;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-rows-at-least") == 0 && n >= 2) {
            st->op = OP_EXPECT_ROWS;
            st->count = atoi(w[1]);
            st->x = 1;
        } else if (strcmp(w[0], "expect-windows") == 0 && n >= 2) {
            st->op = OP_EXPECT_WINDOWS;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-selected") == 0 && n >= 2) {
            st->op = OP_EXPECT_SEL;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-selected-name") == 0 && n >= 2) {
            st->op = OP_EXPECT_SELNAME;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-renaming") == 0 && n >= 2) {
            st->op = OP_EXPECT_RENAMING;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-find-text") == 0 && n >= 2) {
            st->op = OP_EXPECT_FINDTEXT;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-find-type") == 0 && n >= 2) {
            st->op = OP_EXPECT_FINDTYPE;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-location") == 0 && n >= 2) {
            st->op = OP_EXPECT_LOCATION;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-nav-back") == 0 && n >= 2) {
            st->op = OP_EXPECT_NAV_BACK;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-nav-forward") == 0 && n >= 2) {
            st->op = OP_EXPECT_NAV_FORWARD;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-nav-up") == 0 && n >= 2) {
            st->op = OP_EXPECT_NAV_UP;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-navigator") == 0 && n >= 2) {
            st->op = OP_EXPECT_NAV_VISIBLE;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-visible-rows") == 0 && n >= 2) {
            st->op = OP_EXPECT_VIEWPORT_ROWS;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-type-narrowed") == 0) {
            st->op = OP_EXPECT_FOLDERS;
        } else if (strcmp(w[0], "expect-columns") == 0 && n >= 2) {
            st->op = OP_EXPECT_COLS;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-deskbar-at") == 0 && n >= 3) {
            st->op = OP_EXPECT_BARAT;
            st->x = atoi(w[1]);
            st->y = atoi(w[2]);
        } else if (strcmp(w[0], "expect-window-at") == 0 && n >= 3) {
            st->op = OP_EXPECT_WINAT;
            st->x = atoi(w[1]);
            st->y = atoi(w[2]);
        } else if ((strcmp(w[0], "expect-icon-screen") == 0 ||
                    strcmp(w[0], "expect-icon-at") == 0) && n >= 4) {
            /* The coordinates are the LAST two words, so a name with a
             * space in it -- "New folder" -- can still be named. */
            st->op = (w[0][12] == 's') ? OP_EXPECT_ICONSCREEN
                                       : OP_EXPECT_ICONAT;
            rest(w, n - 2, 1, st->text, sizeof st->text);
            st->x = atoi(w[n - 2]);
            st->y = atoi(w[n - 1]);
        } else if (strcmp(w[0], "draw-reset") == 0) {
            st->op = OP_DRAW_RESET;
        } else if (strcmp(w[0], "expect-drawn") == 0 && n >= 2) {
            /* "at most" by default, "at least <n>" with the word: a check
             * that only ever says "no more than" passes when nothing was
             * painted at all. */
            st->op = OP_EXPECT_DRAWN;
            if (n >= 3 && strcmp(w[1], "at-least") == 0) {
                st->x = 1;
                st->count = atoi(w[2]);
            } else {
                st->count = atoi(w[1]);
            }
        } else if (strcmp(w[0], "expect-ink") == 0 && n >= 3) {
            /* Something is drawn here, whatever shade the font renderer
             * gives its edges: a cell with text in it is the claim, not a
             * particular grey. */
            st->op = OP_EXPECT_INK;
            st->x = atoi(w[1]);
            st->y = atoi(w[2]);
        } else if (strcmp(w[0], "expect-same-as-redraw") == 0) {
            st->op = OP_SAME_AS_REDRAW;
        } else if (strcmp(w[0], "expect-pixel") == 0 && n >= 4) {
            st->op = OP_EXPECT_PIXEL;
            st->x = atoi(w[1]);
            st->y = atoi(w[2]);
            snprintf(st->text, sizeof st->text, "%s", w[3]);
        } else if (strcmp(w[0], "expect-file-has") == 0 && n >= 3) {
            st->op = OP_EXPECT_FILEHAS;
            snprintf(st->text, sizeof st->text, "%s", w[1]);
            rest(w, n, 2, st->text2, sizeof st->text2);
        } else if (strcmp(w[0], "expect-file-lacks") == 0 && n >= 3) {
            st->op = OP_EXPECT_FILELACKS;
            snprintf(st->text, sizeof st->text, "%s", w[1]);
            rest(w, n, 2, st->text2, sizeof st->text2);
        } else if (strcmp(w[0], "expect-link") == 0 && n >= 3) {
            /* The target is the LAST word, so a link whose own name has a
             * space in it -- "a.txt link" -- can still be named. */
            st->op = OP_EXPECT_LINK;
            rest(w, n - 1, 1, st->text, sizeof st->text);
            snprintf(st->text2, sizeof st->text2, "%s", w[n - 1]);
        } else if (strcmp(w[0], "expect-count") == 0 && n >= 2) {
            st->op = OP_EXPECT_COUNT;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-menu") == 0 && n >= 2) {
            st->op = OP_EXPECT_MENU;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-narrate") == 0 && n >= 2) {
            st->op = OP_EXPECT_NARRATE;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-mounts") == 0 && n >= 2) {
            st->op = OP_EXPECT_MOUNTS;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-control") == 0 && n >= 2) {
            st->op = OP_EXPECT_CONTROL;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-read") == 0 && n >= 3) {
            /* The path first, then the substring: a path is one word by
             * construction, a value is not. */
            st->op = OP_EXPECT_READ;
            snprintf(st->text2, sizeof st->text2, "%s", w[1]);
            rest(w, n, 2, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-operation") == 0 && n >= 2) {
            st->op = OP_EXPECT_OPERATION;
            rest(w, n, 1, st->text, sizeof st->text);
            /* Smaller than expect-told's budget on purpose: the engine
             * is asked directly, so there is no notification lag to
             * outwait -- only the job itself, at its stepped pace. */
            st->count = 400;
        } else if (strcmp(w[0], "expect-told") == 0 && n >= 2) {
            st->op = OP_EXPECT_TOLD;
            rest(w, n, 1, st->text, sizeof st->text);
            st->count = 2000;    /* the turn budget: bounded, never hung */
        } else if (strcmp(w[0], "expect-no-menu") == 0) {
            st->op = OP_EXPECT_NO_MENU;
        } else if ((strcmp(w[0], "press-desk") == 0 ||
                    strcmp(w[0], "move-desk") == 0 ||
                    strcmp(w[0], "release-desk") == 0) && n >= 2) {
            /* The pieces of a drag, aimed by NAME like click-desk. */
            st->op = (w[0][0] == 'p') ? OP_PRESS_DESK
                   : (w[0][0] == 'm') ? OP_MOVE_DESK : OP_RELEASE_DESK;
            st->button = 1;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "click-desk") == 0 && n >= 2) {
            /* By NAME, not by pixel: where a Desktop icon lands depends
             * on when its source reported it, which moves under load --
             * the name is the only stable address a script has. */
            st->op = OP_CLICK_DESK;
            st->button = 1;
            if (n >= 3 && atoi(w[n - 1]) > 0) {
                st->button = atoi(w[n - 1]);
                rest(w, n - 1, 1, st->text, sizeof st->text);
            } else {
                rest(w, n, 1, st->text, sizeof st->text);
            }
        } else if (strcmp(w[0], "expect-desk-selected") == 0 && n >= 2) {
            st->op = OP_EXPECT_DESKSEL;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "expect-desk-selected-name") == 0 &&
                   n >= 2) {
            st->op = OP_EXPECT_DESKSELNAME;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-no-desk-icon") == 0 && n >= 2) {
            st->op = OP_EXPECT_NO_DESKICON;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-desk-icon") == 0 && n >= 2) {
            st->op = OP_EXPECT_DESKICON;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-perm") == 0 && n >= 3) {
            st->op = OP_EXPECT_PERM;
            snprintf(st->text, sizeof st->text, "%s", w[1]);
            snprintf(st->text2, sizeof st->text2, "%s", w[2]);
        } else if (strcmp(w[0], "write") == 0 && n >= 3) {
            /* The other half of `remove`: changing a file behind the
             * shell's back, which is what a change notice is about. */
            st->op = OP_WRITE;
            snprintf(st->text, sizeof st->text, "%s", w[1]);
            rest(w, n, 2, st->text2, sizeof st->text2);
        } else if (strcmp(w[0], "remove") == 0 && n >= 2) {
            /* The one way a script can change the world behind the shell's
             * back, which is what liveness means: everything else it does
             * arrives through the shell's own hands. */
            st->op = OP_REMOVE;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-file") == 0 && n >= 2) {
            st->op = OP_EXPECT_FILE;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-no-file") == 0 && n >= 2) {
            st->op = OP_EXPECT_NO_FILE;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "note") == 0 && n >= 2) {
            st->op = OP_NOTE;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "quit") == 0) {
            st->op = OP_QUIT;
        } else {
            fprintf(stderr, "script:%d: cannot parse '%s'\n", lineno, w[0]);
            continue;
        }
        s->count++;
    }
    (void)fclose(f);
    return s;
}

void
tiku_trk_script_free(tiku_trk_script_t *s)
{
    free(s);
}

/** @brief Record one check, naming it from the last note. */
static void
verdict(tiku_trk_script_t *s, int ok, const char *what, const char *arg)
{
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
           (arg != NULL && arg[0] != '\0') ? " " : "",
           (arg != NULL) ? arg : "");
    if (s->note[0] != '\0') {
        printf("         %s\n", s->note);
        s->note[0] = '\0';
    }
    if (ok) {
        s->passes++;
    } else {
        s->fails++;
    }
}

int
tiku_trk_script_pump(tiku_trk_script_t *s, const tiku_trk_script_state_t *st,
                     tiku_trk_script_step_t *out)
{
    sc_step_t *cur;

    if (s == NULL || out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->what = TIKU_TRK_SC_IDLE;

    /* A wait owes the loop turns before anything else happens: the point of
     * it is to let deferred work -- a stepped copy, an armed rename -- get
     * as far as it would on a real clock. */
    if (s->waiting > 0) {
        s->waiting--;
        return 1;
    }
    if (s->dragging_window == 1) {
        /* Halfway: the move that carries it. */
        s->dragging_window = 2;
        out->what = TIKU_TRK_SC_MOVE;
        out->x = s->drag_to_x;
        out->y = s->drag_to_y;
        out->button = 1u;
        return 1;
    }
    if (s->dragging_window == 2) {
        s->dragging_window = 0;
        out->what = TIKU_TRK_SC_RELEASE;
        out->x = s->drag_to_x;
        out->y = s->drag_to_y;
        out->button = 1u;
        return 1;
    }
    if (s->pending_release) {
        cur = &s->step[s->cursor - 1];
        s->pending_release = 0;
        out->what = TIKU_TRK_SC_RELEASE;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        return 1;
    }
    if (s->cursor >= s->count) {
        return 0;
    }
    cur = &s->step[s->cursor++];
    switch (cur->op) {
    case OP_KEY:
        out->what = TIKU_TRK_SC_KEY;
        out->key = cur->key;
        out->modifiers = cur->modifiers;
        return 1;
    case OP_CLICK_DESK:
    case OP_PRESS_DESK:
    case OP_MOVE_DESK:
    case OP_RELEASE_DESK: {
        int px = 0, py = 0;
        int (*at)(void *, const char *, int *, int *) =
            (st != NULL && st->pose_desk_at != NULL) ? st->pose_desk_at
            : (st != NULL) ? st->pose_at : NULL;

        if (at == NULL || !at(st->pose_ctx, cur->text, &px, &py)) {
            verdict(s, 0, "no desktop icon named:", cur->text);
            return 1;
        }
        /* The reported point is the icon's corner; the CENTRE is what a
         * click can always hit. */
        cur->x = px + 16;
        cur->y = py + 16;
        out->what = (cur->op == OP_RELEASE_DESK) ? TIKU_TRK_SC_RELEASE
                  : (cur->op == OP_MOVE_DESK) ? TIKU_TRK_SC_MOVE
                                              : TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        s->pending_release = (cur->op == OP_CLICK_DESK);
        return 1;
    }
    case OP_PRESS:
    case OP_CLICK:
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        s->pending_release = (cur->op == OP_CLICK);
        return 1;
    case OP_CLICK_WINDOW:
        if (st == NULL || st->tab_point == NULL ||
            !st->tab_point(st->pose_ctx, cur->text, &cur->button_x,
                           &cur->button_y)) {
            verdict(s, 0, "no window titled:", cur->text);
            return 1;
        }
        cur->x = cur->button_x;
        cur->y = cur->button_y;
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = 1u;
        s->pending_release = 1;
        return 1;
    case OP_DRAG_WINDOW:
        if (st == NULL || st->tab_point == NULL ||
            !st->tab_point(st->pose_ctx, cur->text, &cur->button_x,
                           &cur->button_y)) {
            verdict(s, 0, "no window titled:", cur->text);
            return 1;
        }
        /* Press on the tab; the move and the release follow on the next
         * two turns, so the shell sees a real gesture. */
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->button_x;
        out->y = cur->button_y;
        out->button = 1u;
        s->drag_to_x = cur->x;
        s->drag_to_y = cur->y;
        s->dragging_window = 1;
        return 1;
    case OP_PRESS_ROW:
    case OP_MOVE_ROW:
    case OP_RELEASE_ROW:
        if (st == NULL || st->row_point == NULL ||
            !st->row_point(st->pose_ctx, cur->count, &cur->x, &cur->y)) {
            char row[32];

            snprintf(row, sizeof row, "%d", cur->count);
            verdict(s, 0, "cannot reach row:", row);
            return 1;
        }
        out->what = (cur->op == OP_PRESS_ROW) ? TIKU_TRK_SC_PRESS
                  : (cur->op == OP_MOVE_ROW) ? TIKU_TRK_SC_MOVE
                                             : TIKU_TRK_SC_RELEASE;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        return 1;
    case OP_CLICK_CELL:
        if (st == NULL || st->cell_point == NULL ||
            !st->cell_point(st->pose_ctx, cur->count, cur->text, &cur->x,
                            &cur->y)) {
            verdict(s, 0, "cannot click cell:", cur->text);
            return 1;
        }
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        s->pending_release = 1;
        return 1;
    case OP_CLICK_ROW:
        if (st == NULL || st->row_point == NULL ||
            !st->row_point(st->pose_ctx, cur->count, &cur->x, &cur->y)) {
            char row[32];

            snprintf(row, sizeof row, "%d", cur->count);
            verdict(s, 0, "cannot click row:", row);
            return 1;
        }
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        s->pending_release = 1;
        return 1;
    case OP_MOVE:
        out->what = TIKU_TRK_SC_MOVE;
        out->x = cur->x;
        out->y = cur->y;
        out->modifiers = cur->modifiers;
        out->button = cur->button;
        return 1;
    case OP_RELEASE:
        out->what = TIKU_TRK_SC_RELEASE;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->modifiers = cur->modifiers;
        return 1;
    case OP_WAIT:
        s->waiting = (cur->count > 0) ? cur->count : 1;
        return 1;
    case OP_MENU_PICK:
        out->what = TIKU_TRK_SC_MENU_PICK;
        out->path = cur->text;
        return 1;
    case OP_SHOT:
        out->what = TIKU_TRK_SC_SHOT;
        out->path = cur->text;
        return 1;
    case OP_NOTE:
        snprintf(s->note, sizeof s->note, "%s", cur->text);
        return 1;
    case OP_EXPECT_STATUS:
        verdict(s, st != NULL && st->status != NULL &&
                   strstr(st->status, cur->text) != NULL,
                "status says", cur->text);
        if (s->fails > 0 && st != NULL && st->status != NULL) {
            /* Showing what it DID say is the difference between a failure
             * you can act on and one you have to reproduce by hand. */
            printf("         (status: \"%s\")\n", st->status);
        }
        return 1;
    case OP_EXPECT_TITLE:
        /* The verdict reads back what the WINDOW says, not what was asked
         * for: a failure that repeats the expectation tells the reader
         * nothing about why it failed. */
        verdict(s, st != NULL && st->title != NULL &&
                   strstr(st->title, cur->text) != NULL,
                "title says",
                (st != NULL && st->title != NULL) ? st->title : "nothing");
        return 1;
    case OP_EXPECT_NO_TITLE:
        verdict(s, st == NULL || st->title == NULL ||
                   strstr(st->title, cur->text) == NULL,
                "title does not contain",
                (st != NULL && st->title != NULL) ? st->title : "nothing");
        return 1;
    case OP_EXPECT_SCROLL: {
        char n[32];

        snprintf(n, sizeof n, "%d", (st != NULL) ? st->scroll : -1);
        if (cur->x != 0) {
            /* The "at most" form.  A >= test against 0 is always true and
             * asserts nothing, which is how three mutations survived the
             * first version of this check. */
            verdict(s, st != NULL && st->scroll <= cur->count,
                    "scrolled at most, and is at", n);
        } else {
            verdict(s, st != NULL && st->scroll >= cur->count,
                    "scrolled at least, and is at", n);
        }
        return 1;
    }
    case OP_EXPECT_ROWS: {
        char n[32];

        snprintf(n, sizeof n, "%d", (st != NULL) ? st->rows : -1);
        verdict(s, st != NULL && (cur->x ? st->rows >= cur->count
                                         : st->rows == cur->count),
                "rows:", n);
        return 1;
    }
    case OP_EXPECT_WINDOWS: {
        char n[32];

        snprintf(n, sizeof n, "%d", (st != NULL) ? st->windows : -1);
        verdict(s, st != NULL && st->windows == cur->count, "windows:", n);
        return 1;
    }
    case OP_EXPECT_SEL: {
        char n[32];

        /* What was COUNTED, not what was asked for: a verdict that reads
         * back the expectation says nothing when it fails. */
        snprintf(n, sizeof n, "%d", (st != NULL) ? st->selected : -1);
        verdict(s, st != NULL && st->selected == cur->count, "selected:", n);
        return 1;
    }
    case OP_EXPECT_SELNAME:
        verdict(s, st != NULL && st->sel_name != NULL &&
                strcmp(st->sel_name, cur->text) == 0, "selected:",
                (st != NULL && st->sel_name != NULL) ? st->sel_name
                                                     : "nothing");
        return 1;
    case OP_EXPECT_RENAMING: {
        char n[16];

        snprintf(n, sizeof n, "%d", (st != NULL) ? st->renaming : -1);
        snprintf(n, sizeof n, "%d", (st != NULL) ? st->renaming : -1);
        verdict(s, st != NULL && st->renaming == cur->count, "renaming:", n);
        return 1;
    }
    case OP_EXPECT_FINDTEXT:
        verdict(s, st != NULL && st->find_text != NULL &&
                strcmp(st->find_text, cur->text) == 0, "find text:",
                cur->text);
        return 1;
    case OP_EXPECT_FINDTYPE:
        verdict(s, st != NULL && st->find_type != NULL &&
                strcmp(st->find_type, cur->text) == 0, "of type:",
                cur->text);
        return 1;
    case OP_EXPECT_LOCATION:
        verdict(s, st != NULL && st->location != NULL &&
                strcmp(st->location, cur->text) == 0, "location:",
                (st != NULL && st->location != NULL) ? st->location
                                                     : "nothing");
        return 1;
    case OP_EXPECT_NAV_BACK:
    case OP_EXPECT_NAV_FORWARD:
    case OP_EXPECT_NAV_UP:
    case OP_EXPECT_NAV_VISIBLE:
    case OP_EXPECT_VIEWPORT_ROWS: {
        int actual = (st == NULL) ? -1
                   : (cur->op == OP_EXPECT_NAV_BACK) ? st->nav_back
                   : (cur->op == OP_EXPECT_NAV_FORWARD) ? st->nav_forward
                   : (cur->op == OP_EXPECT_NAV_UP) ? st->nav_up
                   : (cur->op == OP_EXPECT_NAV_VISIBLE) ? st->nav_visible
                                                       : st->viewport_rows;
        char value[16];

        snprintf(value, sizeof value, "%d", actual);
        verdict(s, actual == cur->count,
                (cur->op == OP_EXPECT_NAV_BACK) ? "nav back:"
                : (cur->op == OP_EXPECT_NAV_FORWARD) ? "nav forward:"
                : (cur->op == OP_EXPECT_NAV_UP) ? "nav up:"
                : (cur->op == OP_EXPECT_NAV_VISIBLE) ? "navigator visible:"
                                                     : "visible rows:", value);
        return 1;
    }
    case OP_EXPECT_FOLDERS:
        verdict(s, st != NULL && (st->kinds & 2) != 0 &&
                (st->kinds & 1) == 0, "every row is of the asked type", "");
        return 1;
    case OP_EXPECT_COLS: {
        char n[32];

        snprintf(n, sizeof n, "%d", cur->count);
        snprintf(n, sizeof n, "%d", (st != NULL) ? st->columns : -1);
        verdict(s, st != NULL && st->columns == cur->count, "columns:", n);
        return 1;
    }
    case OP_EXPECT_BARAT: {
        char at[48];

        snprintf(at, sizeof at, "%d,%d", (st != NULL) ? st->bar_x : -1,
                 (st != NULL) ? st->bar_y : -1);
        verdict(s, st != NULL && st->bar_x == cur->x && st->bar_y == cur->y,
                "deskbar at:", at);
        return 1;
    }
    case OP_EXPECT_WINAT: {
        char at[48];

        /* Report where the window IS: an expectation echoed back tells
         * the reader nothing about what moved it. */
        snprintf(at, sizeof at, "%d,%d", (st != NULL) ? st->win_x : -1,
                 (st != NULL) ? st->win_y : -1);
        verdict(s, st != NULL && st->win_x == cur->x && st->win_y == cur->y,
                "window at:", at);
        return 1;
    }
    case OP_EXPECT_ICONAT:
    case OP_EXPECT_ICONSCREEN: {
        char at[64];
        int px = 0, py = 0, got = 0;

        if (st != NULL && cur->op == OP_EXPECT_ICONSCREEN) {
            got = st->pose_screen != NULL &&
                  st->pose_screen(st->pose_ctx, cur->text, &px, &py);
        } else if (st != NULL && st->pose_at != NULL) {
            got = st->pose_at(st->pose_ctx, cur->text, &px, &py);
        }
        if (got) {
            snprintf(at, sizeof at, "%s at %d,%d", cur->text, px, py);
        } else {
            snprintf(at, sizeof at, "%s is not there", cur->text);
        }
        /* Within a few pixels: the position is stored unscaled and scaled
         * back for the view, and a rounding of one pixel each way is not
         * the arrangement being wrong. */
        verdict(s, got && px >= cur->x - 2 && px <= cur->x + 2 &&
                       py >= cur->y - 2 && py <= cur->y + 2, "icon:", at);
        return 1;
    }
    case OP_DRAW_RESET:
        if (st != NULL && st->rows_drawn != NULL) {
            (void)st->rows_drawn(st->pose_ctx, 1);
        }
        return 1;
    case OP_EXPECT_DRAWN: {
        char n[32];
        int got = (st != NULL && st->rows_drawn != NULL)
                      ? st->rows_drawn(st->pose_ctx, 0) : -1;

        snprintf(n, sizeof n, "%d", got);
        verdict(s, got >= 0 && (cur->x ? got >= cur->count
                                       : got <= cur->count),
                "rows painted:", n);
        return 1;
    }
    case OP_EXPECT_FONT: {
        /* Asked of the TOOLKIT, which is where every label in the
         * interface gets its metrics: what it answers is what the whole
         * desktop is drawn in, not what one window believes. */
        const char *name = tiku_font_current_family();
        char got[64];

        snprintf(got, sizeof got, "%s %d px",
                 (name != NULL) ? name : "nothing",
                 tiku_font_size());
        verdict(s, strstr(got, cur->text) != NULL,
                "the interface is drawn in", got);
        return 1;
    }
    case OP_EXPECT_INK: {
        char got[16];
        unsigned rgb = 0u;

        if (st != NULL && st->pixel_at != NULL) {
            rgb = st->pixel_at(st->pose_ctx, cur->x, cur->y);
        }
        snprintf(got, sizeof got, "%06x", rgb & 0xffffffu);
        verdict(s, (rgb & 0xffffffu) != 0xffffffu, "ink at:", got);
        return 1;
    }
    case OP_SAME_AS_REDRAW: {
        char got[32];
        long differ = -1;

        if (st != NULL && st->redraw_diff != NULL) {
            differ = st->redraw_diff(st->redraw_ctx);
        }
        snprintf(got, sizeof got, "%ld pixels", differ);
        verdict(s, differ == 0, "against a full redraw:", got);
        return 1;
    }
    case OP_EXPECT_PIXEL: {
        char got[16];
        unsigned rgb = 0u;

        if (st != NULL && st->pixel_at != NULL) {
            rgb = st->pixel_at(st->pose_ctx, cur->x, cur->y);
        }
        snprintf(got, sizeof got, "%06x", rgb & 0xffffffu);
        verdict(s, strcasecmp(got, cur->text) == 0, "pixel:", got);
        return 1;
    }
    case OP_EXPECT_FILEHAS:
    case OP_EXPECT_FILELACKS: {
        char path[sizeof cur->text + sizeof cur->text2];
        char needle[sizeof cur->text2];
        FILE *f;
        char got[512] = "";
        int ok = 0;

        /* A path with spaces in it cannot be one word.  The path is the
         * LONGEST prefix of the line that names a real file and the
         * needle is the rest -- longest, so "New Query 2" is not
         * shadowed by a "New Query" beside it; one-word paths behave
         * exactly as they always did. */
        {
            char line[sizeof path];
            size_t k, len, best = 0;
            struct stat pst;

            snprintf(line, sizeof line, "%s%s%s", cur->text,
                     cur->text2[0] != '\0' ? " " : "", cur->text2);
            len = strlen(line);
            for (k = strlen(cur->text); k <= len; k++) {
                if (line[k] != '\0' && line[k] != ' ') {
                    continue;
                }
                snprintf(path, sizeof path, "%.*s", (int)k, line);
                if (stat(path, &pst) == 0) {
                    best = k;
                }
            }
            if (best == 0) {
                best = strlen(cur->text);
            }
            snprintf(path, sizeof path, "%.*s", (int)best, line);
            snprintf(needle, sizeof needle, "%s",
                     (best < len) ? line + best + 1 : "");
        }
        f = fopen(path, "r");
        if (f != NULL) {
            size_t rd = fread(got, 1u, sizeof got - 1u, f);

            got[rd] = '\0';
            (void)fclose(f);
            ok = (needle[0] != '\0' && strstr(got, needle) != NULL);
        }
        /* What it HOLDS, not that it exists: a file written by something
         * that ran with no arguments exists just as readily.  The LACKS
         * form asserts the absence of a line: since the settings file
         * records decisions rather than defaults, "back to the default"
         * is proved by the line being GONE, not by an "off" appearing. */
        if (cur->op == OP_EXPECT_FILELACKS) {
            ok = (needle[0] != '\0' && strstr(got, needle) == NULL);
            verdict(s, ok, "absent from file:", cur->text2);
        } else {
            verdict(s, ok, "in file:", cur->text2);
        }
        return 1;
    }
    case OP_EXPECT_LINK: {
        char target[512];
        ssize_t got = readlink(cur->text, target, sizeof target - 1u);

        /* What the link SAYS, not what it resolves to: a relative link and
         * an absolute one to the same file are indistinguishable once
         * followed, and the difference is the whole point. */
        target[(got > 0) ? (size_t)got : 0u] = '\0';
        verdict(s, got > 0 && strcmp(target, cur->text2) == 0,
                "link says:", (got > 0) ? target : "nothing");
        return 1;
    }
    case OP_EXPECT_COUNT:
        verdict(s, st != NULL && st->count != NULL &&
                strstr(st->count, cur->text) != NULL, "count says:",
                (st != NULL && st->count != NULL) ? st->count : "nothing");
        return 1;
    case OP_EXPECT_MENU:
        verdict(s, st != NULL && st->menu != NULL &&
                strstr(st->menu, cur->text) != NULL, "menu:",
                (st != NULL && st->menu != NULL) ? st->menu : "nothing");
        return 1;
    case OP_EXPECT_NO_MENU:
        verdict(s, st != NULL && st->menu != NULL && st->menu[0] == '\0',
                "menu:", "nothing");
        return 1;
    case OP_EXPECT_MOUNTS:
    case OP_EXPECT_CONTROL: {
        /* Asked through the shell's own noun table -- the same answer
         * the conductor's QUERY gives, so this line means one thing in
         * both drivers. */
        const char *noun =
            (cur->op == OP_EXPECT_MOUNTS) ? "mounts" : "controls";
        char text[2048];

        text[0] = '\0';
        if (st != NULL && st->say != NULL) {
            (void)st->say(st->say_ctx, noun, text, sizeof text);
        }
        verdict(s, strstr(text, cur->text) != NULL, noun, cur->text);
        if (s->fails > 0 && text[0] != '\0') {
            printf("         (%s: \"%s\")\n", noun, text);
        }
        return 1;
    }
    case OP_EXPECT_READ: {
        /* The namespace as a noun table: the path is asked through the
         * same say() the conductor answers with. */
        char text[2048];

        text[0] = '\0';
        if (st != NULL && st->say != NULL) {
            (void)st->say(st->say_ctx, cur->text2, text, sizeof text);
        }
        verdict(s, strstr(text, cur->text) != NULL, cur->text2, cur->text);
        if (s->fails > 0 && text[0] != '\0') {
            printf("         (read: \"%s\")\n", text);
        }
        return 1;
    }
    case OP_EXPECT_OPERATION: {
        /* Eventually, bounded: a job's lifecycle takes turns to reach
         * any given state, and a driver that must catch it mid-flight
         * would be racing the clock this harness refuses to race. */
        char text[2048];

        text[0] = '\0';
        if (st != NULL && st->say != NULL) {
            (void)st->say(st->say_ctx, "operation", text, sizeof text);
        }
        if (strstr(text, cur->text) != NULL) {
            verdict(s, 1, "operation says", cur->text);
            return 1;
        }
        if (cur->count-- > 0) {
            s->cursor--;
            s->waiting = 1;
            return 1;
        }
        verdict(s, 0, "operation says", cur->text);
        if (text[0] != '\0') {
            printf("         (operation: \"%s\")\n", text);
        }
        return 1;
    }
    case OP_EXPECT_TOLD: {
        /* Being told, composed the only way an in-process driver can:
         * ask again each turn, bounded.  The conducted driver spells
         * this same line as a real push from the shell; the CLAIM --
         * the window comes to say it within the bound -- is what the
         * two of them prove twice, two ways. */
        char lines[2048];

        lines[0] = '\0';
        if (st != NULL && st->narrate != NULL) {
            (void)st->narrate(st->pose_ctx, lines, sizeof lines);
        }
        if (strstr(lines, cur->text) != NULL) {
            verdict(s, 1, "the window comes to say", cur->text);
            return 1;
        }
        if (cur->count-- > 0) {
            s->cursor--;        /* the same line again, next turn */
            s->waiting = 1;
            return 1;
        }
        verdict(s, 0, "the window comes to say", cur->text);
        if (lines[0] != '\0') {
            printf("         (narration: \"%s\")\n", lines);
        }
        return 1;
    }
    case OP_EXPECT_NARRATE: {
        /* The focused window as its display list narrates it: the same
         * answer the conductor's "narrate" noun gives, so this line means
         * one thing in both drivers. */
        char lines[2048];

        lines[0] = '\0';
        if (st != NULL && st->narrate != NULL) {
            (void)st->narrate(st->pose_ctx, lines, sizeof lines);
        }
        verdict(s, strstr(lines, cur->text) != NULL,
                "narration says", cur->text);
        if (s->fails > 0 && lines[0] != '\0') {
            printf("         (narration: \"%s\")\n", lines);
        }
        return 1;
    }
    case OP_EXPECT_DESKSEL: {
        char n[32];

        snprintf(n, sizeof n, "%d", (st != NULL) ? st->desk_selected : -1);
        snprintf(n, sizeof n, "%d", (st != NULL) ? st->desk_selected : -1);
        verdict(s, st != NULL && st->desk_selected == cur->count,
                "desktop selected:", n);
        return 1;
    }
    case OP_EXPECT_DESKSELNAME:
        verdict(s, st != NULL && st->desk_sel_name != NULL &&
                strcmp(st->desk_sel_name, cur->text) == 0,
                "desktop selected:",
                (st != NULL && st->desk_sel_name != NULL)
                    ? st->desk_sel_name : "nothing");
        return 1;
    case OP_EXPECT_NO_DESKICON: {
        int px = 0, py = 0, got = 0;

        if (st != NULL && st->pose_at != NULL) {
            got = st->pose_at(st->pose_ctx, cur->text, &px, &py);
        }
        verdict(s, !got, "gone from the Desktop:", cur->text);
        return 1;
    }
    case OP_EXPECT_DESKICON: {
        int px = 0, py = 0, got = 0;
        char at[64];

        if (st != NULL && st->pose_at != NULL) {
            got = st->pose_at(st->pose_ctx, cur->text, &px, &py);
        }
        snprintf(at, sizeof at, "%s %s", cur->text,
                 got ? "is on the Desktop" : "is not there");
        verdict(s, got, "desktop:", at);
        return 1;
    }
    case OP_EXPECT_PERM: {
        struct stat sb;
        char got[4];

        got[0] = got[1] = got[2] = '-';
        got[3] = '\0';
        if (stat(cur->text, &sb) == 0) {
            if (sb.st_mode & S_IRUSR) { got[0] = 'r'; }
            if (sb.st_mode & S_IWUSR) { got[1] = 'w'; }
            if (sb.st_mode & S_IXUSR) { got[2] = 'x'; }
        }
        verdict(s, strcmp(got, cur->text2) == 0, "owner bits:", got);
        return 1;
    }
    case OP_REMOVE:
        if (remove(cur->text) != 0) {
            fprintf(stderr, "script: cannot remove '%s'\n", cur->text);
        }
        return 1;
    case OP_EXPECT_ALERT:
        /* "exists" asks only that one is up; anything else must appear in
         * its words. */
        verdict(s, st != NULL && st->alert != NULL &&
                   (strcmp(cur->text, "exists") == 0 ||
                    strstr(st->alert, cur->text) != NULL),
                "an alert says", cur->text);
        return 1;
    case OP_EXPECT_FILE:
        verdict(s, there(cur->text), "exists:", cur->text);
        return 1;
    case OP_EXPECT_NO_FILE:
        verdict(s, !there(cur->text), "gone:", cur->text);
        return 1;
    case OP_WRITE: {
        FILE *f = fopen(cur->text, "w");

        if (f != NULL) {
            (void)fputs(cur->text2, f);
            (void)fputc('\n', f);
            (void)fclose(f);
        }
        return 1;
    }
    case OP_QUIT:
    default:
        out->what = TIKU_TRK_SC_QUIT;
        return 1;
    }
}

int
tiku_trk_script_failures(const tiku_trk_script_t *s)
{
    return (s != NULL) ? s->fails : 0;
}

int
tiku_trk_script_report(const tiku_trk_script_t *s, const char *who)
{
    if (s == NULL) {
        return 0;
    }
    /* A script that asserted nothing is a script that proved nothing, and
     * saying so is the difference between a green suite and a real one. */
    if (s->passes + s->fails == 0) {
        printf("\n%s: no checks were made\n", (who != NULL) ? who : "script");
        return 1;
    }
    printf("\n%s: %d passed, %d failed\n", (who != NULL) ? who : "script",
           s->passes, s->fails);
    return (s->fails > 0) ? 1 : 0;
}
