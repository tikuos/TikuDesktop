/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_link.c - the serial link: whole messages, checked and paced.
 *
 * A pty stands in for the board's console and the test plays the board
 * on its master: it reads the frames the link writes, answers or withholds
 * acknowledgements, and writes frames of its own -- unpaced and numbered,
 * one with a byte flipped, one that skips a number, one too short, paced
 * ones wanting an answer, a SYN of the board's own.  What the link's
 * receiver hears and what its counters say prove the rules on both sides
 * of the frame; a second link, on a pty nothing answers, proves a dead
 * one refuses to send.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_link_serial.h"
#include "tiku_session.h"
#include "tiku_tx.h"

#include <fcntl.h>
#include <poll.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DATA    TIKU_LINK_SERIAL_DATA
#define ACK     TIKU_LINK_SERIAL_ACK
#define SYN     TIKU_LINK_SERIAL_SYN
#define OVER    TIKU_LINK_SERIAL_OVERHEAD
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

/* What the link's receiver last heard, and how many messages in all. */
static unsigned char heard[8192];
static size_t heard_len;
static int heard_count;
static int in_order;        /* messages whose first byte was the next index */

/** @brief The link's message sink: keeps the last message, counts them,
 *         and counts those whose first byte is their index in the run. */
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

/** @brief A link frame into @p out: ctl, seq, the payload, the CRC high
 *         byte first.  @return its length. */
static size_t
link_build(unsigned kind, unsigned char seq, int want_ack,
           const unsigned char *payload, size_t len, unsigned char *out)
{
    unsigned crc;

    out[0] = (unsigned char)kind;
    if (want_ack) {
        out[0] |= TIKU_LINK_SERIAL_WANT_ACK;
    }
    out[1] = seq;
    if (len > 0u) {
        memcpy(out + 2, payload, len);
    }
    crc = crc16(out, len + 2u);
    out[len + 2u] = (unsigned char)(crc >> 8);
    out[len + 3u] = (unsigned char)crc;
    return len + OVER;
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

    if (n < OVER) {
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
    p->len = n - OVER;
    return 0;
}

/** @brief SLIP-frame @p len bytes of @p msg on the marked channel, as the
 *         board's console would.  @return the wire length. */
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

/** @brief @p n bytes of a link frame onto the wire, framed, as the board. */
static void
board_put(int master, const unsigned char *f, size_t n)
{
    static unsigned char wire[2u * 4200u + 3u];

    put(master, wire, frame(f, n, wire));
}

/** @brief A whole link frame from the board: built, framed, written. */
static void
board_send(int master, unsigned kind, unsigned char seq, int want_ack,
           const unsigned char *payload, size_t len)
{
    static unsigned char f[4200];

    board_put(master, f, link_build(kind, seq, want_ack, payload, len, f));
}

/* What the link wrote to the master and the test has not taken yet. */
static unsigned char rx[32768];
static size_t rx_len;

/**
 * @brief The next whole frame the link wrote, out of the master and
 *        unescaped into @p plain, waiting up to @p timeout_ms for it.
 *        @return its length, or 0 when none came in time.
 */
static size_t
take_frame(int master, int timeout_ms, unsigned char *plain)
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

/** @brief Pump the session until @p count messages were heard or
 *         @p timeout_ms passed. */
static void
await(tiku_session_t *s, int count, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;

    while (heard_count < count && now_ms() < deadline) {
        if (tiku_session_poll(s, 20) < 0) {
            break;
        }
    }
}

/** @brief The board's stream: unpaced DATA frames, delivered as they
 *         come, each checked, a gap counted once.  After the SYN's own
 *         ACK the board is expected to count from zero. */
static void
board_to_desktop(int master, tiku_session_t *s, tiku_link_t *l)
{
    const tiku_link_serial_stats_t *st = tiku_link_serial_stats(l);
    static unsigned char msg[4096], one[16], f[64];
    static const unsigned char runt[3] = { 0x00, 0xE1, 0xF0 };
    size_t i, big = TIKU_SESSION_FRAME_MAX - OVER, n;
    unsigned char seq = 5;

    printf("board -> desktop\n");
    memset(one, 's', sizeof one);
    board_send(master, DATA, seq++, 0, one, sizeof one);
    await(s, 1, 1000);
    check(heard_count == 1 && st->rx == 1 && st->gap == 1,
          "after the SYN's own ACK the board is expected from zero: a "
          "stream starting at 5 is delivered and counts a gap");
    check(tiku_link_lost(l) == 1 && tiku_link_lost(l) == 0,
          "which is a loss, reported once");

    heard_count = 0;
    for (i = 0; i < 50u; i++) {
        memset(one, (int)i, sizeof one);
        one[0] = (unsigned char)i;
        board_send(master, DATA, seq++, 0, one, sizeof one);
    }
    await(s, 50, 3000);
    check(heard_count == 50 && in_order == 50 && st->rx == 51 &&
              st->gap == 1,
          "fifty unpaced DATA frames arrive whole and in order: rx 51, no "
          "further gap");

    for (i = 0; i < big; i++) {
        msg[i] = (unsigned char)(i * 5u);
    }
    heard_count = 0;
    board_send(master, DATA, seq++, 0, msg, big);
    await(s, 1, 3000);
    check(heard_count == 1 && heard_len == big && memcmp(heard, msg, big) == 0,
          "one unpaced DATA of four kilobytes arrives whole");

    memset(one, 'x', sizeof one);
    n = link_build(DATA, seq, 0, one, sizeof one, f);
    f[5] ^= 0xFFu;              /* a payload byte, flipped after the CRC */
    board_put(master, f, n);
    await(s, 2, 300);
    check(heard_count == 1 && st->bad == 1,
          "a DATA with one payload byte flipped is not delivered: bad 1");
    board_send(master, DATA, seq++, 0, one, sizeof one);
    await(s, 2, 1000);
    check(heard_count == 2 && st->gap == 1,
          "the same frame intact is delivered, and its number is no gap");

    seq++;                                  /* one number never sent */
    board_send(master, DATA, seq++, 0, one, sizeof one);
    await(s, 3, 1000);
    check(heard_count == 3 && st->gap == 2,
          "a DATA whose seq skips one is delivered and counts one gap");
    check(tiku_link_lost(l) == 1 && tiku_link_lost(l) == 0,
          "and the gap is a loss, reported once");

    n = link_build(DATA, seq++, 0, one, sizeof one, f);
    f[n - 1u] ^= 0xFFu;                     /* the CRC itself */
    board_put(master, f, n);
    board_put(master, runt, sizeof runt);   /* CRC of its first byte: agrees */
    await(s, 4, 300);
    check(heard_count == 3 && st->bad == 3 && st->rx == 54,
          "a wrong CRC, and a three-byte frame whose CRC agrees, never "
          "reach the receiver: bad 3");
}

/* What the desktop's DATA(3) and DATA(4) looked like on the wire, for
 * the retransmits to be compared against byte for byte. */
static unsigned char third[4200], fourth[4200];
static size_t third_len, fourth_len;

/** @brief The desktop's stream: DATA wanting acks through a window of
 *         two, the third queued until an ACK frees the window, a fourth
 *         straight out beside it. */
static void
desktop_window(int master, tiku_session_t *s, tiku_link_t *l)
{
    const tiku_link_serial_stats_t *st = tiku_link_serial_stats(l);
    static unsigned char a[200], b[64], plain[4200], again[4200];
    struct parsed p1, p2;
    size_t i, n, m;

    printf("desktop -> board\n");
    for (i = 0; i < sizeof a; i++) {
        a[i] = (unsigned char)(i ^ 0xC0u);  /* END and ESC among them */
    }
    memset(b, 'b', sizeof b);
    check(tiku_link_send(l, a, sizeof a) == 0 &&
              tiku_link_send(l, b, sizeof b) == 0,
          "two sends are accepted");
    n = take_frame(master, 500, plain);
    m = take_frame(master, 500, again);
    check(link_parse(plain, n, &p1) == 0 && p1.kind == DATA && p1.want_ack &&
              p1.seq == 1 && p1.len == sizeof a &&
              memcmp(p1.payload, a, sizeof a) == 0,
          "the first goes straight out as DATA(1) wanting an ack, CRC valid");
    check(link_parse(again, m, &p2) == 0 && p2.kind == DATA && p2.want_ack &&
              p2.seq == 2 && p2.len == sizeof b,
          "the second follows as DATA(2)");
    check(tiku_link_send(l, b, 8u) == 0 && tiku_link_ready(l) == 0 &&
              st->tx == 3 && take_frame(master, 200, plain) == 0,
          "a third is queued: not ready, nothing written, tx counts SYN + 2");

    board_send(master, ACK, 2, 0, NULL, 0u);
    (void)tiku_session_poll(s, 100);
    third_len = take_frame(master, 500, third);
    check(st->acked == 3 && link_parse(third, third_len, &p1) == 0 &&
              p1.kind == DATA && p1.seq == 3 && tiku_link_ready(l) == 1,
          "ACK(2) frees the window and the third goes out as DATA(3)");
    memset(b, 'd', sizeof b);
    check(tiku_link_send(l, b, 12u) == 0, "a fourth is accepted");
    fourth_len = take_frame(master, 500, fourth);
    check(link_parse(fourth, fourth_len, &p2) == 0 && p2.kind == DATA &&
              p2.seq == 4 && p2.len == 12u && st->tx == 5 &&
              tiku_link_ready(l) == 0,
          "and goes straight out beside it as DATA(4): the window is full");
}

/**
 * @brief Watch DATA(3) and DATA(4) come again while no ACK answers them:
 *        pumps the link until it gives the window up.  @p first takes how
 *        long the first repeat took.  @return how many repeats came, each
 *        byte for byte the frame first sent and in the order 3, 4, 3, 4;
 *        or -1 when one differed or came out of turn.
 */
static int
watch_retransmits(int master, tiku_session_t *s, tiku_link_t *l, long *first)
{
    const tiku_link_serial_stats_t *st = tiku_link_serial_stats(l);
    static unsigned char again[4200];
    long start = now_ms();
    size_t m;
    int seen = 0, same = 1;

    *first = -1;
    while (st->lost == 0 && now_ms() - start < 4000) {
        const unsigned char *want = (seen % 2 == 0) ? third : fourth;
        size_t want_len = (seen % 2 == 0) ? third_len : fourth_len;

        tiku_link_pump(l);
        (void)tiku_session_poll(s, 10);
        m = take_frame(master, 0, again);
        if (m > 0) {
            if (m != want_len || memcmp(again, want, m) != 0) {
                same = 0;
            }
            seen++;
            if (*first < 0) {
                *first = now_ms() - start;
            }
        }
    }
    return same ? seen : -1;
}

/** @brief DATA(3) and DATA(4) left unacknowledged: both written again in
 *         order each RETRY_MS, lost after RETRIES rounds, and the next
 *         send opens a fresh stream with a SYN.  A SYN taken by the ACK
 *         of the DATA behind it leaves the board's numbering unknown. */
static void
desktop_retransmit(int master, tiku_session_t *s, tiku_link_t *l)
{
    const tiku_link_serial_stats_t *st = tiku_link_serial_stats(l);
    static unsigned char b[64], plain[4200], again[4200];
    struct parsed p1, p2;
    size_t n, m;
    long first;
    int seen;

    memset(b, 'b', sizeof b);
    seen = watch_retransmits(master, s, l, &first);
    check(seen == 2 * RETRIES && st->retransmit == 2 * RETRIES &&
              first >= TIKU_LINK_SERIAL_RETRY_MS - 20,
          "with no ACK both frames are written again, in order, after "
          "RETRY_MS, RETRIES times: retransmit 8");
    check(st->lost == 2 && tiku_link_lost(l) == 1 && tiku_link_lost(l) == 0,
          "then both count as lost, reported as one fault");

    check(tiku_link_send(l, b, 8u) == 0, "a send after the loss is accepted");
    n = take_frame(master, 500, plain);
    m = take_frame(master, 500, again);
    check(link_parse(plain, n, &p1) == 0 && p1.kind == SYN && p1.want_ack &&
              p1.seq == 5 && link_parse(again, m, &p2) == 0 &&
              p2.kind == DATA && p2.seq == 6,
          "a fresh SYN(5) precedes it, the DATA numbered after the SYN");
    board_send(master, ACK, (unsigned char)(p1.seq + 9u), 0, NULL, 0u);
    (void)tiku_session_poll(s, 100);
    check(st->acked == 3 && tiku_link_ready(l) == 0,
          "an ACK for a seq not in flight is ignored");
    board_send(master, ACK, p2.seq, 0, NULL, 0u);
    (void)tiku_session_poll(s, 100);
    check(st->acked == 5 && tiku_link_ready(l) == 1,
          "an ACK for the DATA takes the SYN with it");

    heard_count = 0;
    board_send(master, DATA, 5, 0, b, 8u);
    await(s, 1, 1000);
    check(heard_count == 1 && st->gap == 2,
          "and leaves the board's numbering unknown: a stream starting at "
          "5 counts no gap");
}

/** @brief An ACK whose CRC disagrees is no ACK: the frame stays in
 *         flight until one intact comes. */
static void
desktop_bad_ack(int master, tiku_session_t *s, tiku_link_t *l)
{
    const tiku_link_serial_stats_t *st = tiku_link_serial_stats(l);
    static unsigned char b[8], plain[64], f[16];
    struct parsed p;
    size_t n;

    memset(b, 'b', sizeof b);
    check(tiku_link_send(l, b, sizeof b) == 0 &&
              link_parse(plain, take_frame(master, 500, plain), &p) == 0,
          "one more DATA goes out");
    n = link_build(ACK, p.seq, 0, NULL, 0u, f);
    f[3] ^= 0xFFu;
    board_put(master, f, n);
    (void)tiku_session_poll(s, 100);
    check(st->acked == 5 && st->bad == 4,
          "an ACK whose CRC disagrees never reaches the sender: bad counts");
    board_send(master, ACK, p.seq, 0, NULL, 0u);
    (void)tiku_session_poll(s, 100);
    check(st->acked == 6, "the same ACK intact takes the frame");
}

/** @brief The board's paced frames: the one expected is delivered and
 *         answered, a repeat answered again and not delivered, a
 *         stranger dropped unanswered; a SYN of the board's own is
 *         answered and restarts this link's numbering at zero. */
static void
desktop_paced_rx(int master, tiku_session_t *s, tiku_link_t *l)
{
    const tiku_link_serial_stats_t *st = tiku_link_serial_stats(l);
    static unsigned char one[16], plain[64];
    struct parsed p;
    size_t n;

    printf("board -> desktop, paced\n");
    memset(one, 'p', sizeof one);
    heard_count = 0;
    board_send(master, DATA, 40, 1, one, sizeof one);
    await(s, 1, 1000);
    n = take_frame(master, 500, plain);
    check(heard_count == 1 && st->answered == 1 && st->rx == 56 &&
              link_parse(plain, n, &p) == 0 && p.kind == ACK && p.seq == 40,
          "a DATA(40) wanting an ack is delivered, then answered ACK(40)");
    board_send(master, DATA, 40, 1, one, sizeof one);
    await(s, 2, 300);
    n = take_frame(master, 500, plain);
    check(heard_count == 1 && st->dup == 1 && st->answered == 2 &&
              link_parse(plain, n, &p) == 0 && p.kind == ACK && p.seq == 40,
          "the same again is a repeat: answered ACK(40), not delivered");
    board_send(master, DATA, 50, 1, one, sizeof one);
    await(s, 2, 300);
    check(heard_count == 1 && st->reject == 1 && st->answered == 2 &&
              take_frame(master, 200, plain) == 0,
          "one out of order is rejected: not delivered, nothing sent");
    board_send(master, SYN, 9, 1, NULL, 0u);
    (void)tiku_session_poll(s, 100);
    n = take_frame(master, 500, plain);
    check(st->answered == 3 && link_parse(plain, n, &p) == 0 &&
              p.kind == ACK && p.seq == 9,
          "a SYN(9) from the board is answered ACK(9)");
    check(tiku_link_send(l, one, 4u) == 0 &&
              link_parse(plain, take_frame(master, 500, plain), &p) == 0 &&
              p.kind == DATA && p.seq == 0,
          "and restarts this link's numbering: the next frame is DATA(0)");
    board_send(master, ACK, 0, 0, NULL, 0u);
    (void)tiku_session_poll(s, 100);
}

/** @brief A message larger than the board takes is refused at once and
 *         nothing is written; one of exactly that size goes. */
static void
desktop_mtu(int master, tiku_session_t *s, tiku_link_t *l)
{
    const tiku_link_serial_stats_t *st = tiku_link_serial_stats(l);
    static unsigned char big[TIKU_LINK_SERIAL_MTU + 1u], plain[4200];
    struct parsed p;

    memset(big, 'm', sizeof big);
    check(tiku_link_mtu(l) == TIKU_LINK_SERIAL_MTU &&
              tiku_link_send(l, big, sizeof big) == -1 && st->refused == 1 &&
              take_frame(master, 200, plain) == 0,
          "a send one byte over the MTU is refused at once: refused 1, "
          "nothing written");
    check(tiku_link_send(l, big, TIKU_LINK_SERIAL_MTU) == 0 &&
              link_parse(plain, take_frame(master, 500, plain), &p) == 0 &&
              p.kind == DATA && p.len == TIKU_LINK_SERIAL_MTU,
          "one of exactly the MTU goes out");
    board_send(master, ACK, p.seq, 0, NULL, 0u);
    (void)tiku_session_poll(s, 100);
}

/** @brief A link on a pty nothing answers: its SYN is written again
 *         RETRIES times, then the link is dead and refuses to send. */
static void
dead_link(void)
{
    int master, slave;
    char path[64];
    tiku_session_t *s;
    tiku_link_t *l;
    const tiku_link_serial_stats_t *st;
    static unsigned char plain[64], b[8];
    long start;

    printf("a link nothing answers\n");
    if (openpty(&master, &slave, path, NULL, NULL) != 0) {
        check(0, "a second pty");
        return;
    }
    close(slave);
    s = tiku_session_new(tiku_tx_open_serial(path, 115200));
    l = tiku_link_serial_open(s);
    st = tiku_link_serial_stats(l);
    rx_len = 0;
    check(st != NULL && take_frame(master, 500, plain) == OVER,
          "a second link opens and its SYN goes out");
    start = now_ms();
    while (st->lost == 0 && now_ms() - start < 4000) {
        tiku_link_pump(l);
        (void)tiku_session_poll(s, 10);
        (void)take_frame(master, 0, plain);
    }
    check(st->lost == 1 && st->retransmit == RETRIES &&
              tiku_link_lost(l) == 1,
          "unanswered, the SYN is written again RETRIES times, then lost");
    memset(b, 'b', sizeof b);
    check(tiku_link_send(l, b, sizeof b) == -1 && st->refused == 1 &&
              tiku_link_ready(l) == 0 && take_frame(master, 300, plain) == 0,
          "and the link is dead: a send is refused, not ready, nothing "
          "written");
    tiku_link_close(l);
    tiku_session_free(s);
    close(master);
}

/** @brief The serial link over a pty: opened, then the rules on each
 *         side of the frame in turn. */
int
main(void)
{
    int master, slave;
    char path[64];
    tiku_session_t *s;
    tiku_link_t *l;
    const tiku_link_serial_stats_t *st;
    static unsigned char plain[64];
    struct parsed p;
    size_t n;

    printf("the serial link opens\n");
    if (openpty(&master, &slave, path, NULL, NULL) != 0) {
        printf("  [FAIL] no pty\n");
        return 1;
    }
    close(slave);
    s = tiku_session_new(tiku_tx_open_serial(path, 115200));
    l = tiku_link_serial_open(s);
    st = tiku_link_serial_stats(l);
    check(s != NULL && l != NULL && st != NULL,
          "a link opens on a session on the pty, with counters");
    check(tiku_link_serial_crc(0xFFFFu, "123456789", 9u) == 0x29B1u,
          "the CRC of \"123456789\" is 0x29B1");
    tiku_link_on_message(l, on_msg, NULL);
    n = take_frame(master, 1000, plain);
    check(n == OVER && link_parse(plain, n, &p) == 0 && p.kind == SYN &&
              p.want_ack && p.seq == 0,
          "the first thing the link writes is SYN(0) wanting an ack, CRC "
          "valid");
    board_send(master, ACK, 0, 0, NULL, 0u);
    (void)tiku_session_poll(s, 100);
    check(st->acked == 1 && tiku_link_ready(l) == 1,
          "the board's ACK takes it: acked 1, the link ready");

    board_to_desktop(master, s, l);
    desktop_window(master, s, l);
    desktop_retransmit(master, s, l);
    desktop_bad_ack(master, s, l);
    desktop_paced_rx(master, s, l);
    desktop_mtu(master, s, l);

    tiku_link_close(l);
    tiku_session_free(s);
    close(master);
    dead_link();
    printf("%s\n", fails ? "FAILURES" : "whole messages, checked and paced");
    return fails ? 1 : 0;
}
