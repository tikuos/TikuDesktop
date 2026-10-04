/* tiku_sdr_link.c -- TikuSDR's link: the board borrowed from the desktop,
 * its console spoken a line at a time from the tick (never a blocking
 * read, so closing the window cannot strand a thread), and given back.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "tiku_sdr.h"

#define RELEASE_US  10000000
#define RECLAIM_US  15000000
#define OPEN_US      4000000
#define START_US    20000000
#define ANSWER_US    5000000
#define STOP_US      1500000
#define DRAIN_US      100000
#define READ_MAX       32768

void sdr_targets(sdr_t *st)
{
    char names[sizeof st->names] = "";
    const tiku_app_services_t *sv = st->services;

    if (sv->board_names) sv->board_names(sv->ctx, names, sizeof names);
    if (strcmp(names, st->names)) {
        snprintf(st->names, sizeof st->names, "%s", names);
        st->dirty = 1;
    }
    if (!*st->board && sv->current_device)
        sv->current_device(sv->ctx, st->board, sizeof st->board);
    if (!*st->board) {
        /* One board mounted, and only one: it is the one meant. */
        size_t len = strcspn(names, "\n");
        if (len && len < sizeof st->board &&
            !names[len + strspn(names + len, "\n")])
            snprintf(st->board, sizeof st->board, "%.*s", (int)len, names);
    }
}

/** @brief Write all of @p text and a return; the line is short. */
static void send_line(sdr_t *st, const char *text)
{
    char line[96];
    int n = snprintf(line, sizeof line, "%s\r", text), off = 0;

    for (int tries = 0; off < n && tries < 50; tries++) {
        ssize_t w = write(st->fd, line + off, (size_t)(n - off));
        if (w > 0) off += (int)w;
        else if (w < 0 && errno != EAGAIN && errno != EINTR) break;
    }
    st->pending = 1;
    st->sent_at = st->now;
}

/** @brief The board goes back to the desktop, the port closed first and
 *         a recording with it: what it became waits beside the note, which
 *         keeps saying why the board went. */
static void reclaim(sdr_t *st)
{
    const tiku_app_services_t *sv = st->services;
    char why[sizeof st->note];

    if (st->rec.file) {
        snprintf(why, sizeof why, "%s", st->note);
        sdr_rec_stop(st);
        snprintf(st->note, sizeof st->note, "%s", why);
    }
    if (st->fd >= 0) {
        close(st->fd);
        st->fd = -1;
    }
    st->pending = 0;
    st->state = SDR_RECLAIMING;
    st->deadline = st->now + RECLAIM_US;
    if (!sv->board_reclaim || !sv->board_reclaim(sv->ctx, st->board)) {
        st->state = SDR_IDLE;
        snprintf(st->note, sizeof st->note,
                 "%s could not be returned. Reconnect it through Devices.",
                 st->board);
    }
    st->dirty = 1;
}

/** @brief The board's port, opened raw at 115200 now the desktop has let
 *         go of it; a return asks for a prompt. */
static void serial_open(sdr_t *st)
{
    struct termios t;
    int fd = open(st->port, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

    if (fd >= 0 && tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        t.c_cflag |= CLOCAL | CREAD;
        t.c_cflag &= (tcflag_t)~CRTSCTS;
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        cfsetispeed(&t, B115200);
        cfsetospeed(&t, B115200);
        if (!tcsetattr(fd, TCSANOW, &t)) {
            st->fd = fd;
            st->rx_n = 0;
            st->state = SDR_OPENING;
            st->deadline = st->now + OPEN_US;
            send_line(st, "");
            snprintf(st->note, sizeof st->note,
                     "Starting the receiver on %s...", st->board);
            return;
        }
    }
    if (fd >= 0) close(fd);
    snprintf(st->note, sizeof st->note, "Cannot open %s: %s.", st->port,
             strerror(errno));
    reclaim(st);
}

void sdr_connect(sdr_t *st)
{
    const tiku_app_services_t *sv = st->services;

    if (st->state != SDR_IDLE) return;
    sdr_targets(st);
    if (!*st->board) {
        snprintf(st->note, sizeof st->note, "No board is mounted.");
        st->dirty = 1;
        return;
    }
    st->port[0] = st->mcu[0] = '\0';
    if (sv->board_port)
        sv->board_port(sv->ctx, st->board, st->port, sizeof st->port);
    if (sv->board_mcu)
        sv->board_mcu(sv->ctx, st->board, st->mcu, sizeof st->mcu);
    if (strncmp(st->port, "/dev/", 5) || !sv->board_release ||
        !sv->board_reclaim) {
        snprintf(st->note, sizeof st->note, "%s is not a serial board.",
                 st->board);
        st->dirty = 1;
        return;
    }
    st->closing = 0;
    st->shown[0] = '\0';                /* the board's, from now on */
    st->state = SDR_RELEASING;
    st->deadline = st->now + RELEASE_US;
    snprintf(st->note, sizeof st->note,
             "Waiting for the desktop to let go of %s...", st->board);
    if (!sv->board_release(sv->ctx, st->board)) {
        st->state = SDR_IDLE;
        snprintf(st->note, sizeof st->note,
                 "%s is busy. Stop other device work and retry.", st->board);
    }
    st->dirty = 1;
}

void sdr_disconnect(sdr_t *st)
{
    if (st->state == SDR_RELEASING) {
        st->closing = 1;               /* given back as soon as it is let go */
        return;
    }
    snprintf(st->note, sizeof st->note, "Returning %s to the desktop...",
             st->board);
    if (st->state == SDR_RUNNING) {
        /* The board gives its capture bank back, then its prompt; the port
         * closes on that prompt, or when it does not come. */
        send_line(st, "sdr stop");
        st->state = SDR_STOPPING;
        st->stopped = 0;
        st->deadline = st->now + STOP_US;
    } else if (st->state == SDR_OPENING || st->state == SDR_STARTING) {
        reclaim(st);
    }
    st->dirty = 1;
}

void sdr_disconnect_now(sdr_t *st)
{
    int queued = 0;

    if (st->fd >= 0 && st->state == SDR_RUNNING) {
        /* The window is going: tell the board, wait a little for the line
         * to leave (never for ever), and give the board back. */
        send_line(st, "sdr stop");
        for (int64_t t = 0; t < DRAIN_US; t += 5000) {
            if (ioctl(st->fd, TIOCOUTQ, &queued) != 0 || queued == 0) break;
            usleep(5000);
        }
    }
    if (st->state == SDR_RELEASING) st->closing = 1;
    else if (st->fd >= 0) reclaim(st);
}

void sdr_board_said(void *state, int what, const char *text)
{
    sdr_t *st = state;

    if (what == TIKU_APP_BOARD_CURRENT) {
        if (st->state == SDR_IDLE && text && *text)
            snprintf(st->board, sizeof st->board, "%s", text);
    } else if ((what == TIKU_APP_BOARD_RELEASED ||
                what == TIKU_APP_BOARD_RECLAIMED) &&
               (!text || strcmp(text, st->board))) {
        return;
    } else if (what == TIKU_APP_BOARD_RELEASED &&
               st->state == SDR_RELEASING) {
        if (st->closing) reclaim(st); else serial_open(st);
    } else if (what == TIKU_APP_BOARD_RECLAIMED &&
               st->state == SDR_RECLAIMING) {
        st->state = SDR_IDLE;
        snprintf(st->note, sizeof st->note, "%s returned to the desktop.",
                 st->board);
    } else if (what == TIKU_APP_BOARD_FAILED &&
               (st->state == SDR_RELEASING || st->state == SDR_RECLAIMING)) {
        st->state = SDR_IDLE;
        snprintf(st->note, sizeof st->note, "The device handoff failed.");
    }
    st->dirty = 1;
}

/** @brief One whole line from the board. */
static void take_line(sdr_t *st, const char *line)
{
    if (st->state == SDR_STARTING) {
        if (!strncmp(line, "SDR ready", 9)) {
            st->state = SDR_RUNNING;
            st->model.spectra = 0;
            st->rate_at = st->now;
            st->rate_count = 0;
            snprintf(st->note, sizeof st->note, "Receiving on %s.", st->board);
        } else if (strstr(line, "Unknown command")) {
            snprintf(st->note, sizeof st->note, "%s's firmware has no receiver "
                     "(build it with TIKU_DRV_SDR_ESP_ENABLE=1).", st->board);
            reclaim(st);
        } else if (!strncmp(line, "ERR ", 4)) {
            snprintf(st->note, sizeof st->note, "%s could not start its "
                     "receiver (%.40s).", st->board, line + 4);
            reclaim(st);
        }
    } else if (st->state == SDR_STOPPING) {
        if (!strncmp(line, "SDR stopped", 11)) st->stopped = 1;
    } else if (st->state == SDR_RUNNING) {
        int took;

        st->model.clock = (double)st->now / 1e6;
        took = sdr_model_line(&st->model, line);

        if (took >= 0) sdr_rec_line(st, line, took == 1);
        if (took == 1) {
            st->rate_count++;
            st->dirty = 1;
        } else if (!strncmp(line, "ERR ", 4)) {
            snprintf(st->note, sizeof st->note, "The board said: %.60s", line);
            st->dirty = 1;
        }
    }
}

/** @brief A prompt: the command before it has finished. */
static void take_prompt(sdr_t *st)
{
    st->pending = 0;
    if (st->state == SDR_STOPPING && st->stopped) {
        snprintf(st->note, sizeof st->note, "Returning %s to the desktop...",
                 st->board);
        reclaim(st);
    } else if (st->state == SDR_OPENING) {
        st->state = SDR_STARTING;
        st->deadline = st->now + START_US;
        send_line(st, "sdr start");
    }
}

/** @brief Whether @p hay's first @p n bytes hold "tikuOS" (memmem is GNU). */
static int has_prompt_word(const char *hay, size_t n)
{
    for (size_t i = 0; i + 6 <= n; i++)
        if (!memcmp(hay + i, "tikuOS", 6)) return 1;
    return 0;
}

/** @brief Split what arrived into lines; a trailing prompt ends a command. */
static void take_bytes(sdr_t *st, const char *bytes, size_t n)
{
    size_t start = 0;

    if (n > sizeof st->rx - st->rx_n) st->rx_n = 0;   /* a runaway line */
    memcpy(st->rx + st->rx_n, bytes, n);
    st->rx_n += n;
    for (size_t i = 0; i < st->rx_n && st->fd >= 0; i++) {
        if (st->rx[i] != '\n') continue;
        st->rx[i] = '\0';
        if (i > start && st->rx[i - 1] == '\r') st->rx[i - 1] = '\0';
        take_line(st, st->rx + start);
        start = i + 1;
    }
    memmove(st->rx, st->rx + start, st->rx_n - start);
    st->rx_n -= start;
    if (st->rx_n >= 2 && st->rx_n < 128 && st->rx[st->rx_n - 2] == '>' &&
        st->rx[st->rx_n - 1] == ' ' && has_prompt_word(st->rx, st->rx_n)) {
        st->rx_n = 0;
        take_prompt(st);
    }
}

/** @brief Deadlines for each waiting state. */
static void deadlines(sdr_t *st)
{
    if ((st->state == SDR_RELEASING || st->state == SDR_RECLAIMING) &&
        st->now > st->deadline) {
        st->state = SDR_IDLE;
        snprintf(st->note, sizeof st->note,
                 "The desktop did not answer about %s.", st->board);
        st->dirty = 1;
    } else if ((st->state == SDR_OPENING || st->state == SDR_STARTING ||
                st->state == SDR_STOPPING) && st->now > st->deadline) {
        snprintf(st->note, sizeof st->note, "%s did not answer.", st->board);
        reclaim(st);
    } else if (st->state == SDR_RUNNING && st->pending &&
               st->now - st->sent_at > ANSWER_US) {
        snprintf(st->note, sizeof st->note, "%s stopped answering.", st->board);
        reclaim(st);
    }
}

void sdr_link_tick(sdr_t *st)
{
    char buf[4096];
    size_t got = 0;

    deadlines(st);
    while (st->fd >= 0 && got < READ_MAX) {
        struct pollfd p = {st->fd, POLLIN, 0};
        ssize_t r;

        if (poll(&p, 1, 0) <= 0 ||
            (p.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                snprintf(st->note, sizeof st->note, "The link to %s was lost.",
                         st->board);
                reclaim(st);
            }
            break;
        }
        r = read(st->fd, buf, sizeof buf);
        if (r <= 0) break;
        take_bytes(st, buf, (size_t)r);
        got += (size_t)r;
    }
    if (st->state == SDR_RUNNING && !st->pending && !st->paused &&
        st->fd >= 0) {
        char cmd[64];
        send_line(st, sdr_command(&st->model, cmd, sizeof cmd));
    }
    if (st->now - st->rate_at >= 1000000) {
        st->rate_hz = st->rate_count * 1e6f / (float)(st->now - st->rate_at);
        st->rate_at = st->now;
        st->rate_count = 0;
    }
}
