/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script.c - script reader, event source and check tally.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_script.h"

#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SC_MAX_STEPS 512
#define SC_TEXT_MAX  256

/** @brief What one line was asking for. */
typedef enum {
    OP_KEY = 0, OP_PRESS, OP_MOVE, OP_RELEASE, OP_CLICK, OP_WAIT, OP_SHOT,
    OP_EXPECT_STATUS, OP_EXPECT_TITLE, OP_EXPECT_ALERT, OP_EXPECT_SCROLL, OP_EXPECT_ROWS, OP_EXPECT_FILE, OP_EXPECT_NO_FILE,
    OP_NOTE, OP_MENU_PICK, OP_QUIT
} sc_op_t;

typedef struct {
    sc_op_t  op;
    KeySym   ks;
    unsigned state;
    int      x, y;
    unsigned button;
    int      count;                     /* WAIT turns                    */
    char     text[SC_TEXT_MAX];         /* path, substring or note       */
    int      line;
} sc_step_t;

struct tiku_trk_script {
    sc_step_t step[SC_MAX_STEPS];
    int       count;
    int       cursor;
    int       waiting;                  /* turns still owed to a WAIT    */
    int       pending_release;          /* a CLICK's second half         */
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
        if (strcmp(w[i], "ctrl") == 0)       { m |= ControlMask; }
        else if (strcmp(w[i], "shift") == 0) { m |= ShiftMask; }
        else if (strcmp(w[i], "alt") == 0)   { m |= Mod1Mask; }
    }
    return m;
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
            st->ks = XStringToKeysym(w[1]);
            if (st->ks == NoSymbol) {
                fprintf(stderr, "script:%d: unknown key '%s'\n", lineno,
                        w[1]);
                continue;
            }
            st->state = mods_of(w, n, 2);
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
            st->state = mods_of(w, n, 4);
        } else if (strcmp(w[0], "wait") == 0 && n >= 2) {
            st->op = OP_WAIT;
            st->count = atoi(w[1]);
        } else if (strcmp(w[0], "menu-pick") == 0 && n >= 2) {
            st->op = OP_MENU_PICK;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "shot") == 0 && n >= 2) {
            st->op = OP_SHOT;
            snprintf(st->text, sizeof st->text, "%s", w[1]);
        } else if (strcmp(w[0], "expect-status") == 0 && n >= 2) {
            st->op = OP_EXPECT_STATUS;
            rest(w, n, 1, st->text, sizeof st->text);
        } else if (strcmp(w[0], "expect-title") == 0 && n >= 2) {
            st->op = OP_EXPECT_TITLE;
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
    if (s->pending_release) {
        cur = &s->step[s->cursor - 1];
        s->pending_release = 0;
        out->what = TIKU_TRK_SC_RELEASE;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->state = cur->state;
        return 1;
    }
    if (s->cursor >= s->count) {
        return 0;
    }
    cur = &s->step[s->cursor++];
    switch (cur->op) {
    case OP_KEY:
        out->what = TIKU_TRK_SC_KEY;
        out->ks = cur->ks;
        out->state = cur->state;
        return 1;
    case OP_PRESS:
    case OP_CLICK:
        out->what = TIKU_TRK_SC_PRESS;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->state = cur->state;
        s->pending_release = (cur->op == OP_CLICK);
        return 1;
    case OP_MOVE:
        out->what = TIKU_TRK_SC_MOVE;
        out->x = cur->x;
        out->y = cur->y;
        out->state = cur->state;
        out->button = cur->button;
        return 1;
    case OP_RELEASE:
        out->what = TIKU_TRK_SC_RELEASE;
        out->x = cur->x;
        out->y = cur->y;
        out->button = cur->button;
        out->state = cur->state;
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
        verdict(s, st != NULL && st->title != NULL &&
                   strstr(st->title, cur->text) != NULL,
                "title says", cur->text);
        return 1;
    case OP_EXPECT_SCROLL: {
        char n[32];

        snprintf(n, sizeof n, "%d", cur->count);
        if (cur->x != 0) {
            /* The "at most" form.  A >= test against 0 is always true and
             * asserts nothing, which is how three mutations survived the
             * first version of this check. */
            verdict(s, st != NULL && st->scroll <= cur->count,
                    "scrolled at most", n);
        } else {
            verdict(s, st != NULL && st->scroll >= cur->count,
                    "scrolled at least", n);
        }
        return 1;
    }
    case OP_EXPECT_ROWS: {
        char n[32];

        snprintf(n, sizeof n, "%d", cur->count);
        verdict(s, st != NULL && st->rows == cur->count, "rows:", n);
        return 1;
    }
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
