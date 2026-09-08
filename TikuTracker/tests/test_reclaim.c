/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_reclaim.c - letting a board go, and finding it again.
 *
 * Something else on this machine may need the board's port -- a bench
 * run that reflashes it, above all.  The shell can let go, but only if
 * it can get back: a reflashed board comes up on whatever node the host
 * gives it next, so the way back is what the board IS.  Needs a board:
 * -p names its port.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_model.h"

static int passes, fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) {
        passes++;
    } else {
        fails++;
    }
}

/** @brief Whether this process can open @p port for itself. */
static int
port_is_free(const char *port)
{
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (fd < 0) {
        return 0;
    }
    (void)close(fd);
    return 1;
}

int
main(int argc, char **argv)
{
    const char *port = NULL;
    int baud = 115200, i;
    tiku_backend_t *b;
    const tiku_identity_t *id;
    char uid[40], said[64], again[64];
    char buf[128];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            baud = atoi(argv[++i]);
        }
    }
    printf("letting a board go and finding it again\n");
    if (port == NULL) {
        printf("  [SKIP] no -p port given\n");
        return 0;
    }
    b = tiku_backend_tiku_open(port, baud);
    if (b == NULL) {
        printf("  [SKIP] no device answered\n");
        return 0;
    }
    /* Where it is, from the board itself rather than from what was
     * typed: a mount made by scanning was never told a port. */
    check(tiku_backend_tiku_port(b, said, sizeof said) > 0 &&
          strcmp(said, port) == 0,
          "a board says which port it is on");
    id = tiku_backend_tiku_identity(b);
    snprintf(uid, sizeof uid, "%s", (id != NULL) ? id->uid : "");
    check(uid[0] != '\0', "and what it is, by its die");

    tiku_backend_close(b);
    check(port_is_free(port),
          "letting it go frees the port for somebody else");

    b = tiku_backend_tiku_open_uid(uid, baud);
    check(b != NULL, "and it is found again by what it IS, not where");
    if (b == NULL) {
        printf("%d passed, %d failed\n", passes, fails);
        return 1;
    }
    id = tiku_backend_tiku_identity(b);
    check(id != NULL && strcmp(id->uid, uid) == 0,
          "the same board, not merely a board");
    (void)tiku_backend_tiku_port(b, again, sizeof again);
    printf("         (it was on %s, and is on %s)\n", said, again);
    check(b->ops->read(b, "/sys/uptime", buf, sizeof buf) > 0,
          "and its namespace answers as before");
    tiku_backend_close(b);

    /* A uid nothing carries must not hand back the first board that
     * answered: a wrong board is worse than none. */
    b = tiku_backend_tiku_open_uid("no-board-has-this-uid", baud);
    check(b == NULL, "a uid no board carries finds nothing at all");
    tiku_backend_close(b);

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
