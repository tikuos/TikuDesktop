/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_link_tcp.c - whole messages over a socket a board dialled.
 *
 * A loopback connection stands in for the board's.  The board end writes
 * lengths and bytes, split and joined every way a stream may split them,
 * and the link delivers each message once and whole; one larger than any
 * board sends is passed over and the next still arrives.  A channel then
 * carries the desktop's greeting out over the same socket.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_channel.h"
#include "tiku_link_tcp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
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

/* What the link delivered, in order. */
static unsigned char heard[8][TIKU_LINK_TCP_MTU];
static size_t heard_len[8];
static int heard_count;

/** @brief The link's receiver: keeps every message. */
static void
on_message(const unsigned char *msg, size_t len, void *ctx)
{
    (void)ctx;
    if (heard_count < 8) {
        heard_len[heard_count] = (len < sizeof heard[0]) ? len
                                                         : sizeof heard[0];
        memcpy(heard[heard_count], msg, heard_len[heard_count]);
        heard_count++;
    }
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

/** @brief A length-prefixed message onto @p fd. */
static void
put_msg(int fd, const void *payload, size_t len)
{
    unsigned char head[TIKU_LINK_TCP_HEADER];

    head[0] = (unsigned char)len;
    head[1] = (unsigned char)(len >> 8);
    head[2] = (unsigned char)(len >> 16);
    head[3] = (unsigned char)(len >> 24);
    put(fd, head, sizeof head);
    if (len > 0u) {
        put(fd, payload, len);
    }
}

/** @brief Pump @p l until it has delivered @p want messages or 2 s pass. */
static void
gather(tiku_link_t *l, int want)
{
    struct timespec nap = { 0, 5 * 1000 * 1000 };
    int spins = 400;

    while (heard_count < want && spins-- > 0) {
        tiku_link_pump(l);
        if (heard_count >= want) {
            return;
        }
        nanosleep(&nap, NULL);
    }
    tiku_link_pump(l);
}

/** @brief Read what the link wrote, waiting up to @p ms.  @return bytes. */
static size_t
take(int fd, unsigned char *buf, size_t max, int ms)
{
    struct pollfd p = { fd, POLLIN, 0 };
    ssize_t n;

    if (poll(&p, 1, ms) <= 0) {
        return 0;
    }
    n = read(fd, buf, max);
    return (n > 0) ? (size_t)n : 0u;
}

/**
 * @brief A loopback pair: the desktop's accepted end and the board's.
 * @return 0, or -1 when the port will not open
 */
static int
pair_up(int port, int *desk_fd, int *board_fd)
{
    struct sockaddr_in a;
    int lfd = tiku_link_tcp_listen(port);

    if (lfd < 0) {
        return -1;
    }
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((unsigned short)port);
    *board_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*board_fd < 0 ||
        connect(*board_fd, (struct sockaddr *)&a, sizeof a) != 0) {
        close(lfd);
        return -1;
    }
    {
        struct pollfd p = { lfd, POLLIN, 0 };

        if (poll(&p, 1, 2000) <= 0) {
            close(lfd);
            return -1;
        }
    }
    *desk_fd = accept(lfd, NULL, NULL);
    close(lfd);
    return (*desk_fd >= 0) ? 0 : -1;
}

/** @brief The stream in every shape: whole, split, joined, and too large. */
static void
the_stream(tiku_link_t *l, int board)
{
    static const unsigned char hello[8] = { 'h', 'e', 'l', 'l', 'o',
                                            1, 2, 3 };
    unsigned char big[TIKU_LINK_TCP_MTU];
    unsigned char head[TIKU_LINK_TCP_HEADER];
    size_t i;

    printf("the stream\n");
    heard_count = 0;
    put_msg(board, hello, sizeof hello);
    gather(l, 1);
    check(heard_count == 1 && heard_len[0] == sizeof hello &&
              memcmp(heard[0], hello, sizeof hello) == 0,
          "a message arrives whole");

    /* Its length in one write, its bytes in two more. */
    heard_count = 0;
    head[0] = (unsigned char)sizeof hello;
    head[1] = head[2] = head[3] = 0u;
    put(board, head, sizeof head);
    tiku_link_pump(l);
    put(board, hello, 3u);
    tiku_link_pump(l);
    check(heard_count == 0, "half a message is not delivered");
    put(board, hello + 3, sizeof hello - 3u);
    gather(l, 1);
    check(heard_count == 1 && heard_len[0] == sizeof hello &&
              memcmp(heard[0], hello, sizeof hello) == 0,
          "and it arrives whole once its last byte does");

    /* Three messages in one write. */
    heard_count = 0;
    {
        unsigned char burst[3u * (TIKU_LINK_TCP_HEADER + 4u)];
        size_t at = 0;

        for (i = 0; i < 3u; i++) {
            burst[at++] = 4u;
            burst[at++] = 0u;
            burst[at++] = 0u;
            burst[at++] = 0u;
            burst[at++] = (unsigned char)i;
            burst[at++] = 'a';
            burst[at++] = 'b';
            burst[at++] = 'c';
        }
        put(board, burst, at);
    }
    gather(l, 3);
    check(heard_count == 3 && heard[0][0] == 0u && heard[1][0] == 1u &&
              heard[2][0] == 2u,
          "three in one write arrive as three, in order");

    /* One as large as a board takes, then one larger than any board
     * sends. */
    heard_count = 0;
    for (i = 0; i < sizeof big; i++) {
        big[i] = (unsigned char)((i * 7u) & 0xFFu);
    }
    put_msg(board, big, sizeof big);
    gather(l, 1);
    check(heard_count == 1 && heard_len[0] == sizeof big &&
              memcmp(heard[0], big, sizeof big) == 0,
          "one the size of the buffer arrives whole");

    heard_count = 0;
    head[0] = (unsigned char)((TIKU_LINK_TCP_RECV_MAX + 1u) & 0xFFu);
    head[1] = (unsigned char)((TIKU_LINK_TCP_RECV_MAX + 1u) >> 8);
    head[2] = head[3] = 0u;
    put(board, head, sizeof head);
    for (i = 0; i < (TIKU_LINK_TCP_RECV_MAX + 1u) / sizeof big; i++) {
        put(board, big, sizeof big);
    }
    put(board, big, (TIKU_LINK_TCP_RECV_MAX + 1u) % sizeof big);
    put_msg(board, hello, sizeof hello);
    gather(l, 1);
    check(heard_count == 1 && heard_len[0] == sizeof hello,
          "one too large is passed over, and the next still arrives");
    check(tiku_link_tcp_stats(l)->oversize == 1u,
          "and it is counted, not delivered short");
}

/** @brief What the link writes: a length, the bytes, and nothing else. */
static void
the_sending(tiku_link_t *l, int board)
{
    static const unsigned char msg[6] = { 'w', 'i', 'n', 'd', 'o', 'w' };
    unsigned char big[TIKU_LINK_TCP_MTU + 1u];
    unsigned char got[64];
    size_t n;

    printf("the sending\n");
    check(tiku_link_send(l, msg, sizeof msg) == 0 && tiku_link_ready(l),
          "a message is taken");
    n = take(board, got, sizeof got, 1000);
    check(n == TIKU_LINK_TCP_HEADER + sizeof msg && got[0] == sizeof msg &&
              got[1] == 0u && got[2] == 0u && got[3] == 0u &&
              memcmp(got + TIKU_LINK_TCP_HEADER, msg, sizeof msg) == 0,
          "and goes out as its length and its bytes");
    memset(big, 'x', sizeof big);
    check(tiku_link_send(l, big, sizeof big) == -1 &&
              tiku_link_tcp_stats(l)->refused == 1u,
          "one larger than the board takes is refused, not truncated");
    check(tiku_link_mtu(l) == TIKU_LINK_TCP_MTU,
          "and the link says how large that is");
}

/** @brief The board hanging up is a fault the desktop is told about. */
static void
the_hangup(tiku_link_t *l, int board)
{
    printf("the hangup\n");
    check(tiku_link_lost(l) == 0, "nothing lost while the board is there");
    close(board);
    gather(l, 99);
    check(tiku_link_lost(l) == 1, "the board hanging up is one fault");
    check(tiku_link_send(l, "x", 1u) == -1 && !tiku_link_ready(l),
          "and the link takes nothing after it");
}

/** @brief A channel on such a link: its greeting reaches the board. */
static void
the_channel(int port)
{
    tiku_channel_t *ch;
    unsigned char got[64];
    int desk = -1, board = -1;
    size_t n;

    printf("the channel\n");
    if (pair_up(port, &desk, &board) != 0) {
        check(0, "a loopback pair for the channel");
        return;
    }
    ch = tiku_channel_adopt(tiku_link_tcp_open(desk));
    check(ch != NULL && tiku_channel_fd(ch) >= 0,
          "a channel adopts the link, whatever medium it is");
    tiku_channel_greet(ch);
    n = take(board, got, sizeof got, 1000);
    check(n == TIKU_LINK_TCP_HEADER + 16u && got[0] == 16u &&
              got[TIKU_LINK_TCP_HEADER] == 20u,
          "and its greeting goes out as one length-framed WELCOME");
    tiku_channel_close(ch);
    close(board);
}

/** @brief The link over a socket, then a channel on one. */
int
main(void)
{
    tiku_link_t *l;
    int desk = -1, board = -1;

    printf("whole messages over a socket\n");
    if (pair_up(47749, &desk, &board) != 0) {
        printf("  [FAIL] no loopback pair\n");
        return 1;
    }
    check(tiku_link_tcp_open(-1) == NULL, "no link on a socket that is not");
    l = tiku_link_tcp_open(desk);
    check(l != NULL && tiku_link_tcp_stats(l) != NULL,
          "a link on the accepted socket, with counters");
    tiku_link_on_message(l, on_message, NULL);
    the_stream(l, board);
    the_sending(l, board);
    the_hangup(l, board);
    tiku_link_close(l);
    the_channel(47750);
    printf("%s\n", fails ? "FAILURES" : "a length in front is enough");
    return fails ? 1 : 0;
}
