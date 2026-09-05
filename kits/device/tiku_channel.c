/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_channel.c - the socket pair between a link and the desktop's listener.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_channel.h"
#include "tiku_link_serial.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MSG_HEAD 8u
#define OUT_MAX  (MSG_HEAD + TIKU_SESSION_FRAME_MAX)

/* The session's own vocabulary, only as far as coalescing needs it. */
#define RMSG_EVENT        16u
#define EVENT_POINTER_MOVE 8u

/* A greeting the board has not answered is said again after SILENCE_MS,
 * and after a loss; REGREETS of those go out at once since the board
 * last spoke, and past that only one every HEARTBEAT_MS, so a board with
 * a link and no windows costs two spare greetings and then a slow retry
 * that heals one that comes back. */
#define SILENCE_MS        2000L
#define REGREETS          2
#define HEARTBEAT_MS      10000L

struct tiku_channel {
    tiku_link_t    *link;           /* the board, over its medium */
    int             desk_fd;        /* the listener's end */
    int             mine_fd;        /* fed by messages, drained to messages */
    unsigned char   out[OUT_MAX];   /* a message being gathered */
    size_t          got;
    size_t          skip;           /* bytes of one too big still to pass */
    int             greeted;        /* a greeting has gone out */
    long            greeted_at;     /* ms, the last one */
    long            last_rx;        /* ms, the board's last message */
    int             regreets;       /* greetings said again since last_rx */
};

/** @brief Milliseconds on the monotonic clock. */
static long
now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/** @brief A message from the board: handed to the desktop as a stream. */
static void
on_message(const unsigned char *bytes, size_t len, void *ctx)
{
    tiku_channel_t *ch = ctx;
    size_t off = 0;

    ch->last_rx = now_ms();
    ch->regreets = 0;
    while (off < len) {
        ssize_t n = write(ch->mine_fd, bytes + off, len - off);

        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
                continue;
            }
            return;
        }
        off += (size_t)n;
    }
}

tiku_channel_t *
tiku_channel_open(tiku_session_t *s)
{
    tiku_channel_t *ch;
    tiku_link_t *link;
    int sv[2];

    link = tiku_link_serial_open(s);
    if (link == NULL || socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        tiku_link_close(link);
        return NULL;
    }
    ch = calloc(1, sizeof *ch);
    if (ch == NULL) {
        tiku_link_close(link);
        close(sv[0]);
        close(sv[1]);
        return NULL;
    }
    ch->link = link;
    ch->desk_fd = sv[0];
    ch->mine_fd = sv[1];
    (void)fcntl(ch->mine_fd, F_SETFL, O_NONBLOCK);
    tiku_link_on_message(link, on_message, ch);
    return ch;
}

void
tiku_channel_greet(tiku_channel_t *ch)
{
    /* type 20 (WELCOME), 8 bytes: version 1, features: command streams
     * and the invitation bit, which the listener's own reply to a HELLO
     * never carries -- so a board answers the invitation with its hello
     * and lets the reply pass, and the two do not go round for ever. */
    static const unsigned char welcome[16] = { 20, 0, 0, 0, 8, 0, 0, 0,
                                               1, 0, 0, 0, 2, 0, 0, 0x80 };

    if (ch != NULL) {
        (void)tiku_link_send(ch->link, welcome, sizeof welcome);
        ch->greeted = 1;
        ch->greeted_at = now_ms();
    }
}

int
tiku_channel_fd(const tiku_channel_t *ch)
{
    return (ch != NULL) ? ch->desk_fd : -1;
}

tiku_link_t *
tiku_channel_link(const tiku_channel_t *ch)
{
    return (ch != NULL) ? ch->link : NULL;
}

/** @brief The length field of the message at the head of @p ch->out. */
static size_t
head_len(const tiku_channel_t *ch)
{
    return (size_t)ch->out[4] | ((size_t)ch->out[5] << 8) |
           ((size_t)ch->out[6] << 16) | ((size_t)ch->out[7] << 24);
}

/** @brief Whether the message in @p ch->out is a pointer move. */
static int
is_move(const tiku_channel_t *ch)
{
    return ch->out[0] == RMSG_EVENT && ch->out[1] == 0u && head_len(ch) >= 12u
           && ch->out[MSG_HEAD + 4] == EVENT_POINTER_MOVE;
}

/** @brief Greet again: at once while fewer than REGREETS have gone
 *         since the board last spoke, else only HEARTBEAT_MS after the
 *         last one. */
static void
regreet(tiku_channel_t *ch)
{
    if (ch->regreets < REGREETS) {
        ch->regreets++;
        tiku_channel_greet(ch);
    } else if (now_ms() - ch->greeted_at >= HEARTBEAT_MS) {
        tiku_channel_greet(ch);
    }
}

/** @brief Discard what the desktop wrote and has not gone out: after a
 *         loss those events are stale, and the greeting that follows
 *         has the board say its windows again.  Bytes of a message too
 *         big for the board count against what is still to be passed. */
static void
flush(tiku_channel_t *ch)
{
    unsigned char scratch[256];
    ssize_t n;

    while ((n = read(ch->mine_fd, scratch, sizeof scratch)) > 0) {
        ch->skip = ((size_t)n < ch->skip) ? ch->skip - (size_t)n : 0u;
    }
    ch->got = 0;
}

/**
 * @brief Keep the link alive and the board's picture of the session
 *        fresh: its timers run; a loss it reports empties the backlog
 *        and greets again; so does a greeting left unanswered past
 *        SILENCE_MS.  A repeated greeting is safe, since the desktop's
 *        listener treats a repeated OPEN with the same id as the same
 *        window.
 */
static void
tend(tiku_channel_t *ch)
{
    tiku_link_pump(ch->link);
    if (tiku_link_lost(ch->link) > 0) {
        flush(ch);
        regreet(ch);
    } else if (ch->greeted && ch->last_rx < ch->greeted_at &&
               now_ms() - ch->greeted_at > SILENCE_MS) {
        regreet(ch);
    }
}

/** @brief Pass over the rest of a message too big for the board: its
 *         bytes still in the socket go to a scratch buffer.  @return 0
 *         when none are left, -1 while the socket has yet to yield them. */
static int
pass(tiku_channel_t *ch)
{
    unsigned char scratch[256];

    while (ch->skip > 0u) {
        size_t want = (ch->skip < sizeof scratch) ? ch->skip : sizeof scratch;
        ssize_t n = read(ch->mine_fd, scratch, want);

        if (n <= 0) {
            return -1;
        }
        ch->skip -= (size_t)n;
    }
    return 0;
}

int
tiku_channel_pump(tiku_channel_t *ch)
{
    int sent = 0;

    if (ch == NULL) {
        return 0;
    }
    tend(ch);
    for (;;) {
        ssize_t n;
        size_t len;

        if (pass(ch) != 0) {
            return sent;
        }
        /* The socket is the backlog: a message is pulled from it only
         * when the link would take one now, and a full window leaves it
         * there rather than queued behind the window. */
        if (ch->got < OUT_MAX && tiku_link_ready(ch->link)) {
            n = read(ch->mine_fd, ch->out + ch->got, OUT_MAX - ch->got);
            if (n > 0) {
                ch->got += (size_t)n;
            }
        }
        if (ch->got < MSG_HEAD) {
            return sent;
        }
        len = head_len(ch);
        if (MSG_HEAD + len > OUT_MAX) {
            /* Too big for the board: what is gathered is dropped and the
             * rest, which the length says how far, is passed over before
             * the next header is read, so the stream stays in step. */
            ch->skip = MSG_HEAD + len - ch->got;
            ch->got = 0;
            continue;
        }
        if (ch->got < MSG_HEAD + len) {
            return sent;
        }
        {
            /* A move overtaken by the next message is stale: the pointer
             * has already gone on, and a board reads at line rate. */
            int drop = is_move(ch) && ch->got > MSG_HEAD + len;

            if (!drop && !tiku_link_ready(ch->link)) {
                return sent;
            }
            /* A message the link refuses, one larger than the board
             * takes, is dropped here and counted there as refused. */
            if (!drop &&
                tiku_link_send(ch->link, ch->out, MSG_HEAD + len) == 0) {
                sent++;
            }
            memmove(ch->out, ch->out + MSG_HEAD + len,
                    ch->got - MSG_HEAD - len);
            ch->got -= MSG_HEAD + len;
            if (sent > 0) {
                /* One frame a turn: the board reads at line rate, and the
                 * desktop's turn must not wait on it. */
                return sent;
            }
        }
    }
}

void
tiku_channel_close(tiku_channel_t *ch)
{
    if (ch == NULL) {
        return;
    }
    tiku_link_close(ch->link);
    close(ch->desk_fd);
    close(ch->mine_fd);
    free(ch);
}
