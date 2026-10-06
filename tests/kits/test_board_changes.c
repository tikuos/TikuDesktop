/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_board_changes.c - a board's changes that do not fit the caller's
 * buffer are reported incomplete.
 *
 * A fake board answers the shell over a socket pair: it echoes each line,
 * answers `read` of the manifest and `cat /sys/vfs/events`, and ends each
 * reply with its prompt.  Reading the event ring drains it, so a record
 * that does not fit the caller's buffer is not offered again.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_model.h"
#include "tiku_tx.h"

#include <stdio.h>
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

static char   line[1024];
static size_t line_len;
static int    board_fd = -1;
static int    events;               /* records the next ring read gives */

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
board_line(const char *cmd)
{
    char path[128];

    say(cmd);                       /* the echo */
    say("\r\n");
    if (strncmp(cmd, "read /sys/vfs/manifest 0 ", 25) == 0) {
        say("# path\tkind\tperm\tmeta\tcap\tid\r\n");
        say("/data\td\t--\t-\t-\t1\r\n");
    } else if (strcmp(cmd, "cat /sys/vfs/events") == 0) {
        char row[64];
        int i;

        for (i = 0; i < events; i++) {
            snprintf(row, sizeof row, "changed %x %d\r\n", i + 2, i + 1);
            say(row);
        }
        snprintf(row, sizeof row, "# %d drained, 0 dropped\r\n", events);
        say(row);
        events = 0;
    } else if (sscanf(cmd, "cat %127s", path) == 1) {
        say("read: cannot read '");
        say(path);
        say("' (ENOENT)\r\n");
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

/*---------------------------------------------------------------------------*/
/* THE CHECKS                                                                */
/*---------------------------------------------------------------------------*/

static void
the_changes(tiku_backend_t *b)
{
    tiku_model_change_t ch[8];
    int n, complete;

    printf("\na board's changes and the caller's buffer (D-138)\n");
    events = 3;
    complete = -1;
    n = b->ops->changes(b, ch, 8, &complete);
    check(n == 3 && complete == 1,
          "three changes into a buffer of eight are complete");
    events = 5;
    complete = -1;
    n = b->ops->changes(b, ch, 3, &complete);
    check(n == 3 && complete == 0,
          "five changes into a buffer of three: the answer is incomplete");
    complete = -1;
    n = b->ops->changes(b, ch, 8, &complete);
    check(n == 0, "and the two left over are gone from the board");
}

int
main(void)
{
    int sv[2];
    tiku_tx_t *tx;
    tiku_backend_t *b;

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
    the_changes(b);
    tiku_backend_close(b);
    close(sv[0]);
    close(sv[1]);
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
