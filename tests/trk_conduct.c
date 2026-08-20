/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * trk_conduct.c - the scripted driver, as a process of its own.
 *
 * The other driver (src/shell/tiku_trk_script.c) is compiled INTO the
 * shells and reads them from the inside; this one stands outside and
 * speaks the conductor channel, which offers exactly two things: inject an
 * event, and ask about a pixel or a named string.  Every expectation below
 * is composed out of those, here, in a binary that never ships.
 *
 * That boundary is the point.  A driver on the far end of a stream can
 * drive a shell it was not linked against -- including, once the channel
 * rides the device transport, a shell running on real hardware, where no
 * in-process driver could ever have gone.
 *
 * The grammar is a subset of the in-process one, spelled identically, so a
 * script can be read by either:
 *
 *   wait <turns>                 note <text>
 *   click <x> <y>                press/move/release <x> <y>
 *   key <name> [ctrl|shift|alt]
 *   expect-pixel <x> <y> <rrggbb>
 *   expect-title <substring>     expect-no-title <substring>
 *   expect-menu <substring>      expect-status <substring>
 *   expect-count <substring>     quit
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tiku_desk_conduct.h"
#include "tiku_desk_event.h"

#define LINE_MAX_CHARS 512
#define WORDS_MAX      16

static int passes;
static int fails;

static void
verdict(int ok, const char *what, const char *saw)
{
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
           (!ok && saw != NULL) ? " -- saw: " : "",
           (!ok && saw != NULL) ? saw : "");
    if (ok) {
        passes++;
    } else {
        fails++;
    }
}

/** @brief A turn of the shell's loop, as the in-process driver counts it. */
static void
turn(int n)
{
    struct timespec ts;

    ts.tv_sec = 0;
    ts.tv_nsec = 20 * 1000 * 1000;
    while (n-- > 0) {
        (void)nanosleep(&ts, NULL);
    }
}

static unsigned
mods_of(char **w, int n, int from)
{
    unsigned m = 0u;
    int i;

    for (i = from; i < n; i++) {
        if (strcmp(w[i], "ctrl") == 0)       { m |= TIKU_DESK_MOD_CMD; }
        else if (strcmp(w[i], "shift") == 0) { m |= TIKU_DESK_MOD_SHIFT; }
        else if (strcmp(w[i], "alt") == 0)   { m |= TIKU_DESK_MOD_OPTION; }
    }
    return m;
}

/* Spelled exactly as the in-process driver spells them: one grammar, two
 * readers, and a script that moves between them unchanged. */
static unsigned
key_of(const char *name)
{
    static const struct { const char *name; unsigned key; } named[] = {
        { "Up", TIKU_DESK_KEY_UP },
        { "Down", TIKU_DESK_KEY_DOWN },
        { "Left", TIKU_DESK_KEY_LEFT },
        { "Right", TIKU_DESK_KEY_RIGHT },
        { "Home", TIKU_DESK_KEY_HOME },
        { "End", TIKU_DESK_KEY_END },
        { "Prior", TIKU_DESK_KEY_PAGE_UP },
        { "Next", TIKU_DESK_KEY_PAGE_DOWN },
        { "BackSpace", TIKU_DESK_KEY_BACKSPACE },
        { "Delete", TIKU_DESK_KEY_DELETE },
        { "Escape", TIKU_DESK_KEY_ESCAPE },
        { "Return", TIKU_DESK_KEY_RETURN },
        { "Tab", TIKU_DESK_KEY_TAB },
        { "Menu", TIKU_DESK_KEY_MENU },
        { "F2", TIKU_DESK_KEY_F2 },
        { "space", ' ' }
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

static void
pointer(tiku_desk_conduct_client_t *c, int type, int x, int y,
        unsigned button)
{
    tiku_desk_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = (tiku_desk_event_type_t)type;
    ev.x = x;
    ev.y = y;
    ev.button = button;
    (void)tiku_desk_conduct_inject(c, &ev);
}

static void
keypress(tiku_desk_conduct_client_t *c, unsigned key, unsigned mods)
{
    tiku_desk_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_DESK_EVENT_KEY_DOWN;
    ev.key = key;
    ev.modifiers = mods;
    (void)tiku_desk_conduct_inject(c, &ev);
    ev.type = TIKU_DESK_EVENT_KEY_UP;
    (void)tiku_desk_conduct_inject(c, &ev);
}

/** @brief The rest of the line from word @p from, spaces preserved. */
static void
rest(char **w, int n, int from, char *out, size_t max)
{
    int i;

    out[0] = '\0';
    for (i = from; i < n; i++) {
        size_t used = strlen(out);

        snprintf(out + used, max - used, "%s%s",
                 (i > from) ? " " : "", w[i]);
    }
}

static int
ask_text(tiku_desk_conduct_client_t *c, const char *name, char *out,
         size_t max)
{
    /* Emptied first: a question the shell could not answer must read as
     * nothing, never as whatever this buffer happened to hold. */
    if (max > 0u) {
        out[0] = '\0';
    }
    return tiku_desk_conduct_query(c, TIKU_DESK_CQ_TEXT, 0, 0, name, NULL,
                                   out, max);
}

int
main(int argc, char **argv)
{
    tiku_desk_conduct_client_t client;
    const char *sock = NULL;
    const char *path = NULL;
    const char *who = "conducted";
    FILE *f;
    char line[LINE_MAX_CHARS];
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)      { sock = argv[++i]; }
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) { who = argv[++i]; }
        else                                                 { path = argv[i]; }
    }
    if (sock == NULL || path == NULL) {
        fprintf(stderr, "usage: trk-conduct -s <socket> [-n <name>] "
                        "<script>\n");
        return 2;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "trk-conduct: cannot read %s\n", path);
        return 2;
    }
    /* The shell may still be starting; the wait is what frees the harness
     * from sequencing two processes by hand. */
    if (tiku_desk_conduct_connect(&client, sock, 8000) != 0) {
        fprintf(stderr, "trk-conduct: no shell answered on %s\n", sock);
        (void)fclose(f);
        return 2;
    }

    while (fgets(line, sizeof line, f) != NULL) {
        char *w[WORDS_MAX];
        char *save = NULL;
        char *tok;
        char text[TIKU_DESK_CONDUCT_TEXT];
        char arg[LINE_MAX_CHARS];
        int n = 0;

        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        for (tok = strtok_r(line, " \t\r\n", &save);
             tok != NULL && n < WORDS_MAX;
             tok = strtok_r(NULL, " \t\r\n", &save)) {
            w[n++] = tok;
        }
        if (n == 0) {
            continue;
        }

        if (strcmp(w[0], "note") == 0) {
            rest(w, n, 1, arg, sizeof arg);
            printf("         %s\n", arg);
        } else if (strcmp(w[0], "wait") == 0 && n >= 2) {
            turn(atoi(w[1]));
        } else if (strcmp(w[0], "click") == 0 && n >= 3) {
            pointer(&client, TIKU_DESK_EVENT_POINTER_DOWN, atoi(w[1]),
                    atoi(w[2]), 1u);
            pointer(&client, TIKU_DESK_EVENT_POINTER_UP, atoi(w[1]),
                    atoi(w[2]), 1u);
            turn(2);
        } else if (strcmp(w[0], "press") == 0 && n >= 3) {
            pointer(&client, TIKU_DESK_EVENT_POINTER_DOWN, atoi(w[1]),
                    atoi(w[2]), 1u);
        } else if (strcmp(w[0], "move") == 0 && n >= 3) {
            pointer(&client, TIKU_DESK_EVENT_POINTER_MOVE, atoi(w[1]),
                    atoi(w[2]), 0u);
        } else if (strcmp(w[0], "release") == 0 && n >= 3) {
            pointer(&client, TIKU_DESK_EVENT_POINTER_UP, atoi(w[1]),
                    atoi(w[2]), 1u);
        } else if (strcmp(w[0], "key") == 0 && n >= 2) {
            keypress(&client, key_of(w[1]), mods_of(w, n, 2));
            turn(2);
        } else if (strcmp(w[0], "expect-pixel") == 0 && n >= 4) {
            unsigned want = (unsigned)strtoul(w[3], NULL, 16);
            int got = 0;
            char saw[32];

            if (!tiku_desk_conduct_query(&client, TIKU_DESK_CQ_PIXEL,
                                         atoi(w[1]), atoi(w[2]), "",
                                         &got, NULL, 0u)) {
                verdict(0, "the shell answered no pixel at all", NULL);
                continue;
            }
            snprintf(saw, sizeof saw, "%06x", (unsigned)got & 0xffffffu);
            snprintf(arg, sizeof arg, "pixel %s %s is %s", w[1], w[2],
                     w[3]);
            verdict(((unsigned)got & 0xffffffu) == (want & 0xffffffu),
                    arg, saw);
        } else if (strcmp(w[0], "expect-title") == 0 && n >= 2) {
            rest(w, n, 1, arg, sizeof arg);
            (void)ask_text(&client, "title", text, sizeof text);
            {
                char what[LINE_MAX_CHARS];

                snprintf(what, sizeof what, "title says %s", arg);
                verdict(strstr(text, arg) != NULL, what, text);
            }
        } else if (strcmp(w[0], "expect-no-title") == 0 && n >= 2) {
            rest(w, n, 1, arg, sizeof arg);
            (void)ask_text(&client, "titles", text, sizeof text);
            {
                char what[LINE_MAX_CHARS];

                snprintf(what, sizeof what, "no window titled %s", arg);
                verdict(strstr(text, arg) == NULL, what, text);
            }
        } else if (strcmp(w[0], "expect-menu") == 0 && n >= 2) {
            rest(w, n, 1, arg, sizeof arg);
            (void)ask_text(&client, "menu", text, sizeof text);
            {
                char what[LINE_MAX_CHARS];

                snprintf(what, sizeof what, "menu offers %s", arg);
                verdict(strstr(text, arg) != NULL, what, text);
            }
        } else if (strcmp(w[0], "expect-status") == 0 && n >= 2) {
            rest(w, n, 1, arg, sizeof arg);
            (void)ask_text(&client, "status", text, sizeof text);
            {
                char what[LINE_MAX_CHARS];

                snprintf(what, sizeof what, "status says %s", arg);
                verdict(strstr(text, arg) != NULL, what, text);
            }
        } else if (strcmp(w[0], "expect-count") == 0 && n >= 2) {
            rest(w, n, 1, arg, sizeof arg);
            (void)ask_text(&client, "count", text, sizeof text);
            {
                char what[LINE_MAX_CHARS];

                snprintf(what, sizeof what, "count says %s", arg);
                verdict(strstr(text, arg) != NULL, what, text);
            }
        } else if (strcmp(w[0], "quit") == 0) {
            break;
        } else {
            /* An op this driver does not have is a FAILURE, not a skip: a
             * script quietly half-run is the one result worse than a red
             * one, because it is reported green. */
            snprintf(arg, sizeof arg, "op '%s' is not one this driver has",
                     w[0]);
            verdict(0, arg, NULL);
        }
    }
    (void)fclose(f);
    tiku_desk_conduct_disconnect(&client);

    printf("\n%s: %d passed, %d failed\n", who, passes, fails);
    return (fails > 0) ? 1 : 0;
}
