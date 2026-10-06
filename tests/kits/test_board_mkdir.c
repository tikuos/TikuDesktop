/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_board_mkdir.c - a folder made on a board through its shell.
 *
 * A fake board answers the shell over a socket pair: it echoes each line,
 * answers `read` of the manifest and `ls /data`, and runs `mkdir`, or
 * answers "Unknown command" as a board built without its file commands
 * does.  Each reply ends with the prompt.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_model.h"
#include "tiku_tx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int fails;

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/*---------------------------------------------------------------------------*/
/* THE FAKE BOARD                                                            */
/*---------------------------------------------------------------------------*/

static char   made[128];            /* the last folder mkdir made         */
static int    has_mkdir = 1;        /* the shell has its file commands    */
static char   line[1024];
static size_t line_len;
static int    board_fd = -1;

static void
say(const char *text)
{
    size_t n = strlen(text);

    if (write(board_fd, text, n) != (ssize_t)n) {
        printf("  [FAIL] the board end could not write\n");
        fails++;
    }
}

/** @brief Run one typed line and print its reply and the prompt. */
static void
board_line(char *cmd)
{
    char path[128];

    say(cmd);                       /* the echo */
    say("\r\n");
    if (strncmp(cmd, "read /sys/vfs/manifest 0 ", 25) == 0) {
        say("# path\tkind\tperm\tmeta\tcap\tid\r\n");
        say("/data\td\t--\t-\t-\t1\r\n");
    } else if (strncmp(cmd, "mkdir", 5) == 0 && !has_mkdir) {
        say("Unknown command: mkdir\r\n");
    } else if (sscanf(cmd, "mkdir %127s", path) == 1) {
        snprintf(made, sizeof made, "%s", path);
    }
    say("tikuOS:/> ");
}

/** @brief The transport's write: what the desktop typed at the board. */
static int
board_hears(const void *buf, size_t len, void *ctx)
{
    const char *p = buf;
    size_t i;

    (void)ctx;
    for (i = 0; i < len; i++) {
        if (p[i] == '\r') {
            continue;
        }
        if (p[i] == '\n') {
            line[line_len] = '\0';
            board_line(line);
            line_len = 0;
            continue;
        }
        if (line_len + 1 < sizeof line) {
            line[line_len++] = p[i];
        }
    }
    return (int)len;
}

int
main(void)
{
    int sv[2];
    tiku_tx_t *tx;
    tiku_backend_t *b;
    char err[160];
    int rc;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        printf("  [FAIL] no socket pair\n");
        return 1;
    }
    board_fd = sv[1];
    tx = tiku_tx_open_custom(sv[0], board_hears, NULL, NULL, "fake board");
    b = (tx != NULL) ? tiku_backend_tiku_open_tx(tx) : NULL;
    check(b != NULL, "the fake board is mounted");
    if (b == NULL) {
        return 1;
    }

    printf("a folder made on a board (D-029)\n");
    err[0] = '\0';
    rc = tiku_backend_mkdir(b, "/data/logs", err, sizeof err);
    check(rc == 0 && strcmp(made, "/data/logs") == 0,
          "the board's shell is asked to make /data/logs");

    made[0] = '\0';
    err[0] = '\0';
    rc = tiku_backend_mkdir(b, "/sys/logs", err, sizeof err);
    printf("  (outside /data: %d, \"%s\")\n", rc, err);
    check(rc < 0 && err[0] != '\0' && made[0] == '\0',
          "a folder outside /data is refused, the board not asked");

    has_mkdir = 0;
    err[0] = '\0';
    rc = tiku_backend_mkdir(b, "/data/more", err, sizeof err);
    printf("  (a shell with no mkdir: %d, \"%s\")\n", rc, err);
    check(rc < 0 && strstr(err, "no mkdir") != NULL,
          "a board whose shell has no mkdir refuses, saying so");

    tiku_backend_close(b);
    close(sv[0]);
    close(sv[1]);
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
