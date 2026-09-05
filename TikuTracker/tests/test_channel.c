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
 * as before.  A channel then opens its link and carries what the desktop
 * wrote to its socket out as link frames: an overtaken pointer move is
 * dropped, a message behind a full window waits in the socket.  The
 * board end then answers or withholds acknowledgements to prove the
 * greeting is said again, twice, while it stays silent, and afresh with
 * the backlog discarded when a window is lost.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_channel.h"
#include "tiku_link_serial.h"
#include "tiku_session.h"
#include "tiku_tx.h"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DATA    TIKU_LINK_SERIAL_DATA
#define ACK     TIKU_LINK_SERIAL_ACK
#define SYN     TIKU_LINK_SERIAL_SYN
#define RETRIES TIKU_LINK_SERIAL_RETRIES

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

/** @brief Milliseconds on the monotonic clock. */
static long
now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* The messages the desktop writes to its end: a pointer move (type 16,
 * event type 8 at offset 4 of the payload) and two keys, one carrying an
 * END byte; and what the board says, unpaced. */
static const unsigned char move[8 + 12] = {
    16, 0, 0, 0, 12, 0, 0, 0, 1, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0
};
static const unsigned char key[8 + 12] = {
    16, 0, 0, 0, 12, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 0xC0, 0, 0, 0
};
static const unsigned char key2[8 + 12] = {
    16, 0, 0, 0, 12, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 'k', 0, 0, 0
};
static const unsigned char hello[8] = { 'h', 'e', 'l', 'l', 'o', 1, 2, 3 };

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

/** @brief CRC-16/CCITT-FALSE of @p len bytes, the test's own. */
static unsigned
crc16(const unsigned char *p, size_t len)
{
    unsigned crc = 0xFFFFu;
    size_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= (unsigned)p[i] << 8;
        for (b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
        }
        crc &= 0xFFFFu;
    }
    return crc;
}

/** @brief One frame the link wrote, taken apart. */
struct parsed {
    unsigned char        kind;
    unsigned char        seq;
    int                  want_ack;
    const unsigned char *payload;
    size_t               len;       /* of the payload */
};

/** @brief Take @p n bytes of a link frame apart into @p p.
 *         @return 0, or -1 for one too short or with a CRC that disagrees. */
static int
link_parse(const unsigned char *f, size_t n, struct parsed *p)
{
    unsigned crc;

    if (n < TIKU_LINK_SERIAL_OVERHEAD) {
        return -1;
    }
    crc = crc16(f, n - 2u);
    if (f[n - 2u] != (unsigned char)(crc >> 8) ||
        f[n - 1u] != (unsigned char)crc) {
        return -1;
    }
    p->kind = f[0] & TIKU_LINK_SERIAL_KIND;
    p->want_ack = (f[0] & TIKU_LINK_SERIAL_WANT_ACK) != 0u;
    p->seq = f[1];
    p->payload = f + 2;
    p->len = n - TIKU_LINK_SERIAL_OVERHEAD;
    return 0;
}

/** @brief Whether @p p is the channel's 16-byte WELCOME. */
static int
is_welcome(const struct parsed *p)
{
    return p->kind == DATA && p->len == 16u && p->payload[0] == 20u;
}

/** @brief @p n bytes of a link frame onto the wire, framed and escaped
 *         as the board's console would. */
static void
board_put(int master, const unsigned char *f, size_t n)
{
    unsigned char wire[2u * 64u + 3u];
    size_t i, w = 0;

    wire[w++] = 0xC0;
    wire[w++] = 0xF1;
    for (i = 0; i < n; i++) {
        if (f[i] == 0xC0) {
            wire[w++] = 0xDB;
            wire[w++] = 0xDC;
        } else if (f[i] == 0xDB) {
            wire[w++] = 0xDB;
            wire[w++] = 0xDD;
        } else if (f[i] == 0) {
            wire[w++] = 0xDB;
            wire[w++] = 0xDE;
        } else {
            wire[w++] = f[i];
        }
    }
    wire[w++] = 0xC0;
    put(master, wire, w);
}

/** @brief A frame from the board: @p ctl, @p seq, @p len bytes of
 *         @p payload, the CRC high byte first, onto the wire. */
static void
board_send(int master, unsigned char ctl, unsigned char seq,
           const unsigned char *payload, size_t len)
{
    unsigned char f[64];
    unsigned crc;

    f[0] = ctl;
    f[1] = seq;
    if (len > 0u) {
        memcpy(f + 2, payload, len);
    }
    crc = crc16(f, len + 2u);
    f[len + 2u] = (unsigned char)(crc >> 8);
    f[len + 3u] = (unsigned char)crc;
    board_put(master, f, len + TIKU_LINK_SERIAL_OVERHEAD);
}

/** @brief An ACK(@p seq) from the board. */
static void
board_ack(int master, unsigned char seq)
{
    board_send(master, ACK, seq, NULL, 0u);
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

/** @brief The board end again: a frame one byte larger than the session
 *         holds, then a small one behind it. */
static void *
board_big(void *arg)
{
    int master = *(int *)arg;
    static unsigned char big[TIKU_SESSION_FRAME_MAX + 1u + 3u];
    static const unsigned char small[6] = { 0xC0, 0xF1, 'o', 'k', '!', 0xC0 };

    memset(big, 'a', sizeof big);
    big[0] = 0xC0;
    big[1] = 0xF1;
    big[sizeof big - 1u] = 0xC0;
    put(master, big, sizeof big);
    put(master, small, sizeof small);
    return NULL;
}

/* What the link wrote to the master and the test has not taken yet. */
static unsigned char rx[4096];
static size_t rx_len;

/**
 * @brief The next whole frame the link wrote, out of the master: its
 *        wire bytes into @p wire, unescaped into @p plain, waiting up to
 *        @p timeout_ms.  @return the plain length, or 0 when none came.
 */
static size_t
take_frame(int master, int timeout_ms, unsigned char *plain,
           unsigned char *wire, size_t *wire_len)
{
    long deadline = now_ms() + timeout_ms;

    for (;;) {
        struct pollfd p = { master, POLLIN, 0 };
        size_t i = 0, j, len;
        ssize_t n;
        long left;

        while (i + 1 < rx_len && !(rx[i] == 0xC0 && rx[i + 1] == 0xF1)) {
            i++;
        }
        for (j = i + 2; j < rx_len && rx[j] != 0xC0; j++) {
            ;
        }
        if (i + 1 < rx_len && j < rx_len) {
            len = unframe(rx + i, j - i + 1, plain);
            *wire_len = j - i + 1;
            memcpy(wire, rx + i, *wire_len);
            rx_len -= j + 1;
            memmove(rx, rx + j + 1, rx_len);
            return len;
        }
        left = deadline - now_ms();
        if (poll(&p, 1, left > 0 ? (int)left : 0) <= 0) {
            return 0;
        }
        n = read(master, rx + rx_len, sizeof rx - rx_len);
        if (n <= 0) {
            return 0;
        }
        rx_len += (size_t)n;
    }
}

/** @brief The next frame the link wrote, within @p ms, taken apart into
 *         @p p with its bytes in @p plain.  @return 0, or -1 for none. */
static int
next_frame(int master, int ms, struct parsed *p, unsigned char *plain)
{
    unsigned char wire[600];
    size_t wn, n = take_frame(master, ms, plain, wire, &wn);

    return (n > 0) ? link_parse(plain, n, p) : -1;
}

/** @brief Up to @p max bytes out of the desktop's end of @p ch, waiting
 *         @p ms for the first.  @return how many, or 0. */
static size_t
read_desk(tiku_channel_t *ch, unsigned char *buf, size_t max, int ms)
{
    struct pollfd pf = { tiku_channel_fd(ch), POLLIN, 0 };
    ssize_t n;

    if (poll(&pf, 1, ms) <= 0) {
        return 0;
    }
    n = read(tiku_channel_fd(ch), buf, max);
    return (n > 0) ? (size_t)n : 0u;
}

/** @brief What the board end saw while it played along for a while. */
struct watch {
    int           ack;            /* answer every DATA and SYN */
    int           until_lost;     /* stop once the link gives a window up */
    long          t0;             /* ms, what welcome_at counts from */
    long          welcome_at[8];  /* ms from t0, per WELCOME seen */
    int           welcomes;
    unsigned char seqs[32];       /* the seq of every DATA seen, in order */
    int           datas;
};

/**
 * @brief Pump the channel and the session for @p ms as the board end
 *        would see it: every frame the link wrote is taken apart and,
 *        when @p w says so, answered; WELCOMEs and DATA seqs are noted.
 *        The stop is looked at before the frames of the last pump are
 *        taken, so what a pump that gave a window up wrote stays on the
 *        master for the caller.
 */
static void
watch(tiku_channel_t *ch, tiku_session_t *s, int master, long ms,
      struct watch *w)
{
    const tiku_link_serial_stats_t *st;
    long start = now_ms();
    unsigned char plain[300];
    struct parsed p;

    st = tiku_link_serial_stats(tiku_channel_link(ch));
    for (;;) {
        if (now_ms() - start >= ms || (w->until_lost && st->lost > 0)) {
            return;
        }
        while (next_frame(master, 0, &p, plain) == 0) {
            if (p.kind == DATA && w->datas < 32) {
                w->seqs[w->datas++] = p.seq;
            }
            if (is_welcome(&p) && w->welcomes < 8) {
                w->welcome_at[w->welcomes++] = now_ms() - w->t0;
            }
            if (w->ack && p.kind != ACK) {
                board_ack(master, p.seq);
            }
        }
        (void)tiku_channel_pump(ch);
        (void)tiku_session_poll(s, 10);
    }
}

/** @brief The channel's link opens with a SYN and paces what the desktop
 *         writes to its socket: an overtaken move is dropped, a message
 *         behind a full window waits there until the board answers. */
static void
the_channel(tiku_channel_t *ch, tiku_session_t *s, int master)
{
    const tiku_link_serial_stats_t *st;
    unsigned char plain[64], wire[128];
    struct parsed p;
    size_t n, wn;

    printf("the channel\n");
    st = tiku_link_serial_stats(tiku_channel_link(ch));
    check(tiku_channel_fd(ch) >= 0 && st != NULL,
          "a channel has a socket, and a serial link with counters");
    n = take_frame(master, 1000, plain, wire, &wn);
    check(link_parse(plain, n, &p) == 0 && p.kind == SYN && p.seq == 0,
          "the channel's link opens with a SYN(0)");

    put(tiku_channel_fd(ch), move, sizeof move);
    put(tiku_channel_fd(ch), key, sizeof key);
    check(tiku_channel_pump(ch) == 1,
          "of two queued, the overtaken move is dropped and one goes");
    n = take_frame(master, 500, plain, wire, &wn);
    check(wn > 3 && wire[0] == 0xC0 && wire[1] == 0xF1 &&
              wire[wn - 1] == 0xC0,
          "the frame is delimited and marked");
    check(link_parse(plain, n, &p) == 0 && p.kind == DATA && p.want_ack &&
              p.seq == 1 && p.len == sizeof key &&
              memcmp(p.payload, key, sizeof key) == 0,
          "and carries the key event's message, byte for byte, as DATA(1) "
          "wanting an ack");
    check(memmem(wire, wn, "\xDB\xDC", 2) != NULL,
          "with the payload's END byte escaped on the wire");

    put(tiku_channel_fd(ch), key2, sizeof key2);
    check(tiku_channel_pump(ch) == 0 &&
              take_frame(master, 200, plain, wire, &wn) == 0,
          "with the SYN unanswered the window is full: a second key stays "
          "in the socket, pump sends nothing");
    board_ack(master, 0);
    (void)tiku_session_poll(s, 100);
    check(st->acked == 1 && tiku_link_ready(tiku_channel_link(ch)) == 1,
          "the board's ACK of the SYN takes it: acked 1, the link ready");
    check(tiku_channel_pump(ch) == 1 &&
              next_frame(master, 500, &p, plain) == 0 && p.seq == 2 &&
              p.len == sizeof key2 &&
              memcmp(p.payload, key2, sizeof key2) == 0,
          "and the key that waited goes out as DATA(2)");
    board_ack(master, 2);
    (void)tiku_session_poll(s, 100);
    check(st->acked == 3 && tiku_link_ready(tiku_channel_link(ch)) == 1,
          "ACK(2) takes both: acked 3, the window empty");
}

/** @brief The greeting: a WELCOME the board answers; with no message
 *         from the board after it, said again after two seconds and once
 *         more two seconds later, then no more. */
static void
the_greeting(tiku_channel_t *ch, tiku_session_t *s, int master)
{
    unsigned char plain[64], got[16];
    struct parsed p;
    struct watch w;

    printf("the greeting\n");
    board_send(master, DATA, 0, hello, sizeof hello);
    (void)tiku_session_poll(s, 100);
    check(read_desk(ch, got, sizeof got, 500) == sizeof hello &&
              memcmp(got, hello, sizeof hello) == 0,
          "a message from the board comes out of the desktop's end whole");
    /* A few milliseconds, so the greeting is later on the clock than
     * the board's message: one it has yet to answer. */
    (void)tiku_session_poll(s, 5);

    memset(&w, 0, sizeof w);
    w.ack = 1;
    w.t0 = now_ms();
    tiku_channel_greet(ch);
    check(next_frame(master, 500, &p, plain) == 0 && is_welcome(&p),
          "a greeting is a WELCOME the board takes");
    board_ack(master, p.seq);
    watch(ch, s, master, 7600 - (now_ms() - w.t0), &w);
    check(w.welcomes >= 2 && w.welcome_at[0] >= 2000 &&
              w.welcome_at[0] <= 2300 && w.welcome_at[1] >= 4000 &&
              w.welcome_at[1] <= 4600,
          "with no message from the board it is said again after two "
          "seconds, and once more two seconds after that");
    check(w.welcomes == 2, "and not again in the three seconds after");
}

/** @brief A window the board never answers: both frames written again
 *         RETRIES times, then given up, the backlog discarded, and the
 *         board greeted afresh behind a new SYN. */
static void
the_loss(tiku_channel_t *ch, tiku_session_t *s, int master)
{
    const tiku_link_serial_stats_t *st;
    unsigned char plain[64];
    struct parsed p, syn;
    struct watch w;
    int a, i, in_turn = 1;

    printf("the loss\n");
    st = tiku_link_serial_stats(tiku_channel_link(ch));
    board_send(master, DATA, 1, hello, sizeof hello);
    (void)tiku_session_poll(s, 100);
    check(read_desk(ch, plain, sizeof plain, 500) == sizeof hello,
          "the board speaks again, so the spare greetings start over");
    put(tiku_channel_fd(ch), key2, sizeof key2);
    check(tiku_channel_pump(ch) == 1 &&
              next_frame(master, 500, &p, plain) == 0 && p.kind == DATA,
          "one message goes out");
    a = p.seq;
    put(tiku_channel_fd(ch), key2, sizeof key2);
    check(tiku_channel_pump(ch) == 1 &&
              next_frame(master, 500, &p, plain) == 0 &&
              p.seq == (unsigned char)(a + 1),
          "a second beside it: the window is full");
    put(tiku_channel_fd(ch), key, sizeof key);
    check(tiku_channel_pump(ch) == 0, "a third stays in the socket");

    memset(&w, 0, sizeof w);
    w.until_lost = 1;
    watch(ch, s, master, 4000, &w);
    for (i = 0; i < w.datas; i++) {
        if (w.seqs[i] != (unsigned char)(a + (i & 1))) {
            in_turn = 0;
        }
    }
    check(st->lost == 2 && st->retransmit == 2 * RETRIES &&
              w.datas == 2 * RETRIES && in_turn,
          "with no ACK both are written again in order, RETRIES times, "
          "then given up: lost 2");
    check(next_frame(master, 500, &syn, plain) == 0 && syn.kind == SYN &&
              next_frame(master, 500, &p, plain) == 0 && is_welcome(&p) &&
              p.seq == (unsigned char)(syn.seq + 1),
          "then a fresh SYN and a WELCOME behind it: the board is greeted "
          "afresh");
    board_ack(master, syn.seq);
    board_ack(master, p.seq);
    (void)tiku_session_poll(s, 100);
    check(st->acked == 8 && tiku_link_ready(tiku_channel_link(ch)) == 1,
          "which the board takes: acked 8, the window empty");
    check(tiku_channel_pump(ch) == 0 &&
              next_frame(master, 300, &p, plain) != 0,
          "and the message that sat in the socket during the loss was "
          "discarded: nothing else goes out");
}

/** @brief The line, then the channel on it: text and frames apart, then
 *         the window, the greeting and the loss in turn. */
int
main(void)
{
    int master, slave;
    char path[64];
    tiku_session_t *s;
    tiku_channel_t *ch;
    pthread_t th;
    char out[128];
    long deadline;
    int rc;

    printf("frames in the text\n");
    if (openpty(&master, &slave, path, NULL, NULL) != 0) {
        printf("  [FAIL] no pty\n");
        return 1;
    }
    close(slave);
    check(tiku_session_new(NULL) == NULL, "no session without a transport");
    s = tiku_session_new(tiku_tx_open_serial(path, 115200));
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
    heard_count = 0;
    pthread_create(&th, NULL, board_big, &master);
    deadline = now_ms() + 3000;
    while (heard_count < 1 && now_ms() < deadline) {
        (void)tiku_session_poll(s, 20);
    }
    pthread_join(th, NULL);
    check(heard_count == 1 && heard_len == 3 && memcmp(heard, "ok!", 3) == 0,
          "a frame one byte larger than the session holds is dropped whole, "
          "and the one behind it arrives");

    ch = tiku_channel_open(s);
    check(ch != NULL && tiku_channel_link(ch) != NULL,
          "a channel opens on the session, with a link");
    the_channel(ch, s, master);
    the_greeting(ch, s, master);
    the_loss(ch, s, master);

    tiku_channel_close(ch);
    tiku_session_free(s);
    close(master);
    printf("%s\n", fails ? "FAILURES" : "one line carries both");
    return fails ? 1 : 0;
}
