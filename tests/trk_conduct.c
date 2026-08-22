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

#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "tiku_conduct.h"
#include "tiku_event.h"

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
        if (strcmp(w[i], "ctrl") == 0)       { m |= TIKU_MOD_CMD; }
        else if (strcmp(w[i], "shift") == 0) { m |= TIKU_MOD_SHIFT; }
        else if (strcmp(w[i], "alt") == 0)   { m |= TIKU_MOD_OPTION; }
    }
    return m;
}

/* Spelled exactly as the in-process driver spells them: one grammar, two
 * readers, and a script that moves between them unchanged. */
static unsigned
key_of(const char *name)
{
    static const struct { const char *name; unsigned key; } named[] = {
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
        { "Tab", TIKU_KEY_TAB },
        { "Menu", TIKU_KEY_MENU },
        { "F2", TIKU_KEY_F2 },
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
pointer(tiku_conduct_client_t *c, int type, int x, int y,
        unsigned button)
{
    tiku_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = (tiku_event_type_t)type;
    ev.x = x;
    ev.y = y;
    ev.button = button;
    (void)tiku_conduct_inject(c, &ev);
}

static void
keypress(tiku_conduct_client_t *c, unsigned key, unsigned mods)
{
    tiku_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_KEY_DOWN;
    ev.key = key;
    ev.modifiers = mods;
    (void)tiku_conduct_inject(c, &ev);
    ev.type = TIKU_EVENT_KEY_UP;
    (void)tiku_conduct_inject(c, &ev);
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
ask_text(tiku_conduct_client_t *c, const char *name, char *out,
         size_t max)
{
    /* Emptied first: a question the shell could not answer must read as
     * nothing, never as whatever this buffer happened to hold. */
    if (max > 0u) {
        out[0] = '\0';
    }
    return tiku_conduct_query(c, TIKU_CQ_TEXT, 0, 0, name, NULL,
                                   out, max);
}

/**
 * @brief Put a line in the state a frame needs: raw, and nothing else.
 *
 * VMIN is left where cfmakeraw puts it -- see the note in
 * tiku_conduct_open_tty.  A terminal set to poll answers a quiet
 * moment with zero bytes, and zero bytes from a socket means the far end
 * hung up; the two must not be made to look alike.
 */
static void
raw_line(int fd)
{
    struct termios tio;

    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);
        (void)tcsetattr(fd, TCSANOW, &tio);
    }
}

/**
 * @brief Start @p cmd on the far end of a fresh line, and take this end.
 *
 * The rehearsal for a real board: the shell is handed a serial device and
 * knows nothing else about who is driving it, and the driver holds the
 * other end of the cable.  Nothing here is pretending -- it IS a tty, with
 * a line discipline and a baud rate; only the copper is missing.
 *
 * @return the master fd, or -1.  @p kid receives the child's pid.
 */
static int
spawn_on_line(char **cmd, pid_t *kid)
{
    char slave_name[128];
    int master, slave;
    pid_t pid;

    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0 ||
        ptsname_r(master, slave_name, sizeof slave_name) != 0) {
        return -1;
    }
    /* Opened here and HELD, never read.  It covers the window before the
     * child has opened the line for itself: a master whose slave nobody
     * holds reads EIO, and the child is still on its way to exec.  The
     * window is short enough that nothing here fails without this on an
     * idle machine, which is exactly why it stays -- a loaded one is not
     * a different contract, only a slower exec. */
    slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave < 0) {
        (void)close(master);
        return -1;
    }
    raw_line(master);
    raw_line(slave);

    pid = fork();
    if (pid < 0) {
        (void)close(master);
        (void)close(slave);
        return -1;
    }
    if (pid == 0) {
        char *args[32];
        int n = 0;

        (void)close(master);
        while (cmd[n] != NULL && n < 28) {
            args[n] = cmd[n];
            n++;
        }
        args[n++] = (char *)"-conduct-tty";
        args[n++] = slave_name;
        args[n] = NULL;
        (void)execv(args[0], args);
        _exit(127);
    }
    *kid = pid;
    return master;
}

int
main(int argc, char **argv)
{
    tiku_conduct_client_t client;
    const char *sock = NULL;
    const char *tty = NULL;
    const char *path = NULL;
    const char *who = "conducted";
    char **spawn = NULL;
    pid_t kid = -1;
    FILE *f;
    char line[LINE_MAX_CHARS];
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)      { sock = argv[++i]; }
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) { tty = argv[++i]; }
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) { who = argv[++i]; }
        else if (strcmp(argv[i], "--") == 0 && i + 1 < argc) {
            spawn = &argv[i + 1];
            break;              /* the rest is the shell's command line */
        }
        else                                                 { path = argv[i]; }
    }
    if ((sock == NULL && tty == NULL && spawn == NULL) || path == NULL) {
        fprintf(stderr, "usage: trk-conduct [-s <socket> | -t <tty>] "
                        "[-n <name>] <script> [-- <shell> <args>...]\n");
        return 2;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "trk-conduct: cannot read %s\n", path);
        return 2;
    }
    if (spawn != NULL) {
        int master = spawn_on_line(spawn, &kid);

        if (master < 0 ||
            tiku_conduct_connect_fd(&client, master) != 0) {
            fprintf(stderr, "trk-conduct: cannot start %s on a line\n",
                    spawn[0]);
            (void)fclose(f);
            return 2;
        }
    } else if (tty != NULL) {
        int line_fd = tiku_conduct_open_tty(tty, 0);

        if (line_fd < 0 ||
            tiku_conduct_connect_fd(&client, line_fd) != 0) {
            fprintf(stderr, "trk-conduct: cannot speak over %s\n", tty);
            (void)fclose(f);
            return 2;
        }
    } else if (tiku_conduct_connect(&client, sock, 8000) != 0) {
        /* The shell may still be starting; the wait is what frees the
         * harness from sequencing two processes by hand. */
        fprintf(stderr, "trk-conduct: no shell answered on %s\n", sock);
        (void)fclose(f);
        return 2;
    }

    while (fgets(line, sizeof line, f) != NULL) {
        char *w[WORDS_MAX];
        char *save = NULL;
        char *tok;
        char text[TIKU_CONDUCT_TEXT];
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
            pointer(&client, TIKU_EVENT_POINTER_DOWN, atoi(w[1]),
                    atoi(w[2]), 1u);
            pointer(&client, TIKU_EVENT_POINTER_UP, atoi(w[1]),
                    atoi(w[2]), 1u);
            turn(2);
        } else if (strcmp(w[0], "press") == 0 && n >= 3) {
            pointer(&client, TIKU_EVENT_POINTER_DOWN, atoi(w[1]),
                    atoi(w[2]), 1u);
        } else if (strcmp(w[0], "move") == 0 && n >= 3) {
            pointer(&client, TIKU_EVENT_POINTER_MOVE, atoi(w[1]),
                    atoi(w[2]), 0u);
        } else if (strcmp(w[0], "release") == 0 && n >= 3) {
            pointer(&client, TIKU_EVENT_POINTER_UP, atoi(w[1]),
                    atoi(w[2]), 1u);
        } else if (strcmp(w[0], "key") == 0 && n >= 2) {
            keypress(&client, key_of(w[1]), mods_of(w, n, 2));
            turn(2);
        } else if (strcmp(w[0], "expect-pixel") == 0 && n >= 4) {
            unsigned want = (unsigned)strtoul(w[3], NULL, 16);
            int got = 0;
            char saw[32];

            if (!tiku_conduct_query(&client, TIKU_CQ_PIXEL,
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
    tiku_conduct_disconnect(&client);
    if (kid > 0) {
        /* A shell we started on a line is ours to end: the line is gone
         * the moment we are, and a shell left holding a dead cable would
         * outlive every run that followed. */
        (void)kill(kid, SIGTERM);
        (void)waitpid(kid, NULL, 0);
    }

    printf("\n%s: %d passed, %d failed\n", who, passes, fails);
    return (fails > 0) ? 1 : 0;
}
