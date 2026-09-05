/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_link.c - the serial link carries whole messages, both ways.
 *
 * A pty stands in for the board's console.  A session on one end, a serial
 * link over it: a message the link sends leaves as one frame the board end
 * reads and unframes whole; a frame the board end writes arrives at the
 * link's receiver as one message, its escapes undone.  Fifty in a burst and
 * one of four kilobytes prove order and size.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_link_serial.h"
#include "tiku_session.h"
#include "tiku_tx.h"

#include <fcntl.h>
#include <poll.h>
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

/* What the link's receiver last heard, and how many messages in all. */
static unsigned char heard[8192];
static size_t heard_len;
static int heard_count;
static int in_order;        /* messages whose first byte was the next index */

static void
on_msg(const unsigned char *msg, size_t len, void *ctx)
{
    (void)ctx;
    if (len > 0u && msg[0] == (unsigned char)heard_count) {
        in_order++;
    }
    heard_len = (len < sizeof heard) ? len : sizeof heard;
    memcpy(heard, msg, heard_len);
    heard_count++;
}

/** @brief Write all @p n bytes to @p fd, or record a failure. */
static void
put(int fd, const void *buf, size_t n)
{
    if (write(fd, buf, n) != (ssize_t)n) {
        printf("  [FAIL] short write\n");
        fails++;
    }
}

/** @brief SLIP-frame @p len bytes of @p msg as the board would. */
static size_t
frame(const unsigned char *msg, size_t len, unsigned char *out)
{
    size_t i, n = 0;

    out[n++] = 0xC0;
    out[n++] = 0xF1;
    for (i = 0; i < len; i++) {
        unsigned char b = msg[i];

        if (b == 0xC0) {
            out[n++] = 0xDB;
            out[n++] = 0xDC;
        } else if (b == 0xDB) {
            out[n++] = 0xDB;
            out[n++] = 0xDD;
        } else if (b == 0) {
            out[n++] = 0xDB;
            out[n++] = 0xDE;
        } else {
            out[n++] = b;
        }
    }
    out[n++] = 0xC0;
    return n;
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

/** @brief Read the pty master until @p want bytes or a timeout. */
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
    tiku_link_t *l;
    unsigned char msg[4096], wire[9000], plain[4096];
    size_t i, n, w;

    printf("messages both ways over the serial link\n");
    if (openpty(&master, &slave, path, NULL, NULL) != 0) {
        printf("  [FAIL] no pty\n");
        return 1;
    }
    close(slave);
    tx = tiku_tx_open_serial(path, 115200);
    s = tiku_session_new(tx);
    l = tiku_link_serial_open(s);
    check(s != NULL && l != NULL, "a link opens on a session on the pty");
    tiku_link_on_message(l, on_msg, NULL);

    /* Board -> desktop: fifty framed messages, first byte the index. */
    for (i = 0; i < 50u; i++) {
        unsigned char one[16];

        memset(one, (int)i, sizeof one);
        one[0] = (unsigned char)i;
        w = frame(one, sizeof one, wire);
        put(master, wire, w);
    }
    /* One session_poll pass reads what is waiting and sorts the frames. */
    while (tiku_session_poll(s, 50) >= 0 && heard_count < 50) {
        /* keep pumping until all fifty are in or the link breaks */
        if (tiku_session_poll(s, 50) < 0) {
            break;
        }
    }
    check(heard_count == 50 && in_order == 50,
          "fifty framed messages arrive whole and in order");

    /* Board -> desktop: one message of four kilobytes, every byte value. */
    heard_count = 0;
    heard_len = 0;
    for (i = 0; i < sizeof msg; i++) {
        msg[i] = (unsigned char)(i * 5u);
    }
    w = frame(msg, sizeof msg, wire);
    put(master, wire, w);
    while (heard_len < sizeof msg && tiku_session_poll(s, 100) >= 0) {
        if (heard_count > 0) {
            break;
        }
    }
    check(heard_count == 1 && heard_len == sizeof msg &&
              memcmp(heard, msg, sizeof msg) == 0,
          "a four-kilobyte message arrives whole");

    /* Desktop -> board: the link sends, the board end unframes it whole. */
    for (i = 0; i < 200u; i++) {
        msg[i] = (unsigned char)(i ^ 0xC0u);    /* END and ESC among them */
    }
    check(tiku_link_send(l, msg, 200u) == 0, "the link sends a message");
    n = slurp(master, wire, sizeof wire);
    check(n > 202 && wire[0] == 0xC0 && wire[1] == 0xF1 && wire[n - 1] == 0xC0,
          "it leaves as one frame on the marked channel");
    check(unframe(wire, n, plain) == 200u && memcmp(plain, msg, 200u) == 0,
          "carrying the message byte for byte, its specials escaped");

    tiku_link_close(l);
    tiku_session_free(s);
    close(master);
    printf("%s\n", fails ? "FAILURES" : "whole messages, both ways");
    return fails ? 1 : 0;
}
