/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_ns_walk.c - the read and write state of nodes found by an ls walk.
 *
 * A fake board answers the shell over a socket pair: its manifest holds
 * /gui with no children, and `ls /gui` lists three files whose perm
 * columns say read and write, write only and read only.  Mounting it walks
 * /gui, and each file must show the read and write state its column says.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_model.h"
#include "tiku_tx.h"
#include "tiku_ns.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int fails;
static char line[1024];
static size_t line_len;
static int board_fd = -1;
static const char *events = "";

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/** @brief Send @p text from the board's end. */
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
    say(cmd);                       /* the echo */
    say("\r\n");
    if (strncmp(cmd, "read /sys/vfs/manifest 0 ", 25) == 0) {
        say("# path\tkind\tperm\tmeta\tcap\tid\r\n");
        say("/gui\td\t--\t-\t-\t1\r\n");
    } else if (strcmp(cmd, "cat /sys/vfs/events") == 0) {
        say(events);
    } else if (strcmp(cmd, "ls /gui") == 0) {
        say("  rw both\r\n");
        say("  -w poke\r\n");
        say("  r- view\r\n");
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

/** @brief The read and write bits @p path shows, or ~0u when it has no
 *         entry. */
static unsigned
perm_of(tiku_backend_t *b, const char *path)
{
    tiku_model_t m;

    if (b->ops->stat(b, path, &m) != 0) {
        return ~0u;
    }
    return m.facts.perm & (TIKU_P_READ | TIKU_P_WRITE);
}

/** @brief Dropped event counts include both device losses and local capacity. */
static void
event_counts(tiku_backend_t *b)
{
    tiku_ns_t *ns = tiku_ns_new(tiku_backend_tiku_session(b));
    tiku_ns_change_t out[3];
    unsigned dropped;
    int cap;

    events = "write 10 1\r\nwrite 20 2\r\nremove 30 3\r\n"
             "# 3 drained, 2 dropped\r\n";
    for (cap = 0; cap <= 3; cap++) {
        check(tiku_ns_events(ns, out, cap, &dropped) == cap &&
              dropped == (unsigned)(5 - cap),
              "D-176: local overflow adds to device-reported losses");
    }
    check(tiku_ns_events(ns, NULL, 3, &dropped) == 0 && dropped == 5,
          "D-176: an absent output buffer counts every drained record");
    check(tiku_ns_events(ns, out, 3, NULL) == 3 &&
          out[0].id == 0x10 && out[2].seq == 3,
          "D-176: dropped is optional and records keep their values");
    events = "write 10 1\r\nwrite 20 2\r\n";
    check(tiku_ns_events(ns, out, 1, &dropped) == 1 && dropped == 1,
          "D-176: local overflow is counted without a device footer");
    tiku_ns_free(ns);
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
    printf("files found by an ls walk\n");
    check(perm_of(b, "/gui/both") == (TIKU_P_READ | TIKU_P_WRITE),
          "a file ls lists as rw shows read and write");
    check(perm_of(b, "/gui/poke") == TIKU_P_WRITE,
          "a file ls lists as -w shows write only");
    check(perm_of(b, "/gui/view") == TIKU_P_READ,
          "a file ls lists as r- shows read only");
    event_counts(b);
    tiku_backend_close(b);
    close(sv[0]);
    close(sv[1]);
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
