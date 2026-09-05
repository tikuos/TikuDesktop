/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_channel.c - frames and shell text share one line.
 *
 * A pty stands in for the board's console.  The board end writes typed
 * text with frames in the middle of it; the session cuts the frames out
 * whole, with their escapes undone, and the text still answers a command
 * as before.  A channel then carries a message the desktop wrote to its
 * socket out as one frame, and drops a pointer move a newer message has
 * overtaken.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_channel.h"
#include "tiku_session.h"
#include "tiku_tx.h"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

static unsigned char heard[256];
static size_t heard_len;
static int heard_count;

/** @brief The session's frame sink: keeps the last frame. */
static void
sink(const unsigned char *bytes, size_t len, void *ctx)
{
    (void)ctx;
    heard_len = (len < sizeof heard) ? len : sizeof heard;
    memcpy(heard, bytes, heard_len);
    heard_count++;
}

/** @brief Write all of @p n bytes, or say so. */
static void
put(int fd, const void *buf, size_t n)
{
    if (write(fd, buf, n) != (ssize_t)n) {
        printf("  [FAIL] short write\n");
        fails++;
    }
}

/** @brief Undo the frame's escapes between its delimiters.  @return len. */
static size_t
unframe(const unsigned char *wire, size_t n, unsigned char *out)
{
    size_t i, len = 0;
    int esc = 0;

    for (i = 2; i + 1 < n; i++) {
        unsigned char b = wire[i];

        if (esc) {
            esc = 0;
            out[len++] = (b == 0xDC) ? 0xC0 : (b == 0xDD) ? 0xDB : 0;
        } else if (b == 0xDB) {
            esc = 1;
        } else {
            out[len++] = b;
        }
    }
    return len;
}

/** @brief The board end: answers "ping" with a frame inside the reply. */
static void *
board(void *arg)
{
    int master = *(int *)arg;
    char line[64];
    size_t n = 0;
    /* A frame whose payload holds the END, ESC and NUL bytes on purpose. */
    static const unsigned char frame[] = {
        0xC0, 0xF1, 'T', 'K', 0xDB, 0xDC, 0xDB, 0xDD, 0xDB, 0xDE, '!',
        0xC0
    };

    while (n < sizeof line - 1) {
        char c;

        if (read(master, &c, 1) != 1) {
            return NULL;
        }
        line[n++] = c;
        if (c == '\n') {
            break;
        }
    }
    put(master, "ping\r\n", 6);
    put(master, frame, sizeof frame);
    put(master, "PONG\r\ntikuOS:/> ", 16);
    return NULL;
}

/** @brief Read the pty's master until @p want bytes or a timeout. */
static size_t
slurp(int master, unsigned char *out, size_t want)
{
    size_t got = 0;

    while (got < want) {
        struct pollfd p = { master, POLLIN, 0 };
        ssize_t n;

        if (poll(&p, 1, 500) <= 0) {
            break;
        }
        n = read(master, out + got, want - got);
        if (n <= 0) {
            break;
        }
        got += (size_t)n;
    }
    return got;
}

int
main(void)
{
    int master, slave;
    char path[64];
    tiku_tx_t *tx;
    tiku_session_t *s;
    tiku_channel_t *ch;
    pthread_t th;
    char out[128];
    unsigned char wire[256];
    int rc;

    printf("frames in the text\n");
    if (openpty(&master, &slave, path, NULL, NULL) != 0) {
        printf("  [FAIL] no pty\n");
        return 1;
    }
    close(slave);
    tx = tiku_session_new(NULL) ? NULL : tiku_tx_open_serial(path, 115200);
    s = tiku_session_new(tx);
    check(s != NULL, "a session on the pty's board end");
    tiku_session_on_frame(s, sink, NULL);
    pthread_create(&th, NULL, board, &master);
    rc = tiku_session_cmd(s, "ping", out, sizeof out, 3000);
    pthread_join(th, NULL);
    check(rc >= 0 && strcmp(out, "PONG") == 0,
          "the command's answer is the text alone");
    check(heard_count == 1 && heard_len == 6 &&
              memcmp(heard, "TK\xC0\xDB\x00!", 6) == 0,
          "and the frame arrived whole, its escapes undone");

    printf("the channel\n");
    ch = tiku_channel_open(s);
    check(ch != NULL && tiku_channel_fd(ch) >= 0, "a channel has a socket");
    {
        /* Two messages the desktop might write: a pointer move (type 16,
         * event type 8 at offset 4 of the payload) and a key.  The move
         * is overtaken, so only the key goes out. */
        unsigned char move[8 + 12] = { 16, 0, 0, 0, 12, 0, 0, 0,
                                       1, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0 };
        unsigned char key[8 + 12]  = { 16, 0, 0, 0, 12, 0, 0, 0,
                                       1, 0, 0, 0, 4, 0, 0, 0, 0xC0, 0, 0, 0 };
        size_t n;

        unsigned char plain[64];

        put(tiku_channel_fd(ch), move, sizeof move);
        put(tiku_channel_fd(ch), key, sizeof key);
        check(tiku_channel_pump(ch) == 1,
              "of two queued, the overtaken move is dropped and one goes");
        n = slurp(master, wire, sizeof wire);
        check(n > 3 && wire[0] == 0xC0 && wire[1] == 0xF1 &&
                  wire[n - 1] == 0xC0,
              "the frame is delimited and marked");
        check(unframe(wire, n, plain) == sizeof key &&
                  memcmp(plain, key, sizeof key) == 0,
              "and carries the key event's message, byte for byte");
        check(memmem(wire, n, "\xDB\xDC", 2) != NULL,
              "with the payload's END byte escaped on the wire");
    }
    tiku_channel_close(ch);
    tiku_session_free(s);
    close(master);
    printf("%s\n", fails ? "FAILURES" : "one line carries both");
    return fails ? 1 : 0;
}
