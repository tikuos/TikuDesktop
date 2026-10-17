/* tiku_sdr_link.c -- TikuSDR's link: the board borrowed from the desktop,
 * its console written and read a line at a time from the tick, with no
 * blocking read, and the board given back.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include <ctype.h>
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

/** @brief Return the supported ESP32 receiver part (5 or 61), or zero. */
static int receiver_part(const char *text)
{
    const char *want = "esp32c";
    int part;

    for (; *text && *want; text++) {
        if (*text == '-' || *text == '_') continue;
        if (tolower((unsigned char)*text) != *want++) return 0;
    }
    if (*want) return 0;
    if (*text == '5') { part = 5; text++; }
    else if (text[0] == '6' && text[1] == '1') { part = 61; text += 2; }
    else return 0;
    return !*text || *text == '-' || *text == '_' || *text == ' ' ? part : 0;
}

/** @brief Whether @p board is one line of @p names. */
static int listed(const char *names, const char *board)
{
    size_t n = strlen(board);

    for (const char *p = names; n && *p; p += strcspn(p, "\n") + 1) {
        if (!strncmp(p, board, n) && (p[n] == '\n' || !p[n])) return 1;
        if (!strchr(p, '\n')) break;
    }
    return 0;
}

int sdr_targets(sdr_t *st)
{
    char names[sizeof st->names] = "", fit[sizeof st->names] = "";
    char *save = NULL;
    const tiku_app_services_t *sv = st->services;
    int known = 1;
    size_t n = 0;

    if (sv->board_names)
        known = sv->board_names(sv->ctx, names, sizeof names) >= 0;
    for (char *p = strtok_r(names, "\n", &save); p;
         p = strtok_r(NULL, "\n", &save)) {
        char mcu[48] = "";

        if (sv->board_mcu) sv->board_mcu(sv->ctx, p, mcu, sizeof mcu);
        /* The MCU the board reported decides; a board that reported
         * none is judged by its name. */
        if (!receiver_part(*mcu ? mcu : p) || n + strlen(p) + 2 > sizeof fit)
            continue;
        n += (size_t)snprintf(fit + n, sizeof fit - n, "%s\n", p);
    }
    if (strcmp(fit, st->names)) {
        snprintf(st->names, sizeof st->names, "%s", fit);
        st->dirty = 1;
    }
    if (!*st->board && sv->current_device) {
        char current[sizeof st->board] = "";
        sv->current_device(sv->ctx, current, sizeof current);
        if (listed(fit, current))
            snprintf(st->board, sizeof st->board, "%s", current);
    }
    if (!*st->board && *fit)
        snprintf(st->board, sizeof st->board, "%.*s",
                 (int)strcspn(fit, "\n"), fit);
    return known;
}

/** @brief Write @p text and a return to the board, in at most 50 tries;
 *         the command is then pending.
 *  @note @p text is at most 94 bytes: the line is built in a 96-byte
 *        buffer. */
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

void sdr_link_command(sdr_t *st, const char *line) { send_line(st, line); }

/** @brief Give the board back to the desktop: a recording is stopped
 *         (st->saved says what it became; the note keeps saying why the
 *         board went), the port is closed, and the desktop is asked to
 *         reclaim the board. */
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
    st->tx.phase = st->tx.queued = st->tx.known = st->tx.cancel = 0;
    st->gain_queued = st->gain_inflight = 0;
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

/** @brief Open the board's port raw at 115200, once the desktop has let go
 *         of it, and send a return to ask for a prompt; a port that will
 *         not open is given back. */
static void serial_open(sdr_t *st)
{
    struct termios t;
    int fd = open(st->port, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

    if (fd >= 0 && tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        t.c_cflag |= CLOCAL | CREAD;
        t.c_cflag &= (tcflag_t)~CRTSCTS;
        if (receiver_part(*st->mcu ? st->mcu : st->board) == 5)
            t.c_cflag &= (tcflag_t)~HUPCL;
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        cfsetispeed(&t, B115200);
        cfsetospeed(&t, B115200);
        int controls = TIOCM_DTR | TIOCM_RTS;
        if (!tcsetattr(fd, TCSANOW, &t) &&
            (receiver_part(*st->mcu ? st->mcu : st->board) != 5 ||
             !ioctl(fd, TIOCMBIS, &controls))) {
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

int sdr_connect(sdr_t *st)
{
    const tiku_app_services_t *sv = st->services;

    if (st->state != SDR_IDLE) return 0;
    sdr_targets(st);
    if (!listed(st->names, st->board)) {
        st->board[0] = '\0';            /* gone since it was chosen */
        sdr_targets(st);
    }
    if (!*st->board) return -1;
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
        return 0;
    }
    sdr_play_stop(st);
    st->gain_known = st->gain_queued = st->gain_inflight = 0;
    st->closing = 0;
    st->shown[0] = '\0';                /* the board's spectra from now on */
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
    return 0;
}

void sdr_disconnect(sdr_t *st)
{
    if (st->state == SDR_RUNNING && sdr_tx_busy(st)) {
        st->closing = 1;
        sdr_tx_cancel(st);
        return;
    }
    st->gain_queued = st->gain_inflight = 0;
    if (st->state == SDR_RELEASING) {
        st->closing = 1;               /* given back as soon as it is let go */
        return;
    }
    snprintf(st->note, sizeof st->note, "Returning %s to the desktop...",
             st->board);
    if (st->state == SDR_RUNNING) {
        /* The board answers "SDR stopped" and then its prompt; the port
         * closes on that prompt, or when STOP_US passes without it. */
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
        /* Tell the board to stop and wait up to DRAIN_US for the line to
         * leave; the board is given back below. */
        send_line(st, sdr_tx_busy(st) ? "sdr tx clear\rsdr stop" : "sdr stop");
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
        /* The new current device may be a board just mounted. */
        sdr_targets(st);
        if (st->state == SDR_IDLE && text && listed(st->names, text))
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

int sdr_gain_request(sdr_t *st, int gain)
{
    if (st->state != SDR_RUNNING || gain < -2 || gain > 255 ||
        st->gain_queued || st->gain_inflight ||
        (gain != -2 && st->gain_known != 1)) return 0;
    st->gain_request = gain;
    st->gain_queued = 1;
    st->dirty = 1;
    return 1;
}

/** @brief Read a startup, gain, capture, or shutdown reply from the board. */
static void take_line(sdr_t *st, const char *line)
{
    if (st->state == SDR_RUNNING && st->tx.phase) {
        sdr_tx_line(st, line);
        return;
    }
    if (st->state == SDR_RUNNING && st->caps_inflight) {
        if (st->caps_inflight == 2) (void)sdr_bands_parse(st, line);
        else (void)sdr_caps_parse(st, line);
        return;
    }
    if (st->state == SDR_RUNNING && st->gain_inflight) {
        int gain, used = 0;
        if (!strcmp(line, "SDR gain auto")) gain = -1;
        else if (sscanf(line, "SDR gain %d%n", &gain, &used) != 1 ||
                 line[used] || gain < 0 || gain > 255) return;
        st->gain_value = gain;
        st->gain_known = st->gain_answered = 1;
        snprintf(st->note, sizeof st->note, gain < 0 ?
                 "Receiver gain is automatic." : "Receiver gain index: %d.", gain);
        st->dirty = 1;
        return;
    }
    if (st->state == SDR_STARTING) {
        if (!strncmp(line, "SDR ready", 9)) {
            st->state = SDR_RUNNING;
            st->caps_known = 0;
            st->caps_queued = 1;
            st->caps_inflight = 0;
            st->last_frame = 0;
            st->model.rejected = st->model.incomplete = st->rx_overflows = 0;
            st->model.fft_override = st->model.rate_override = 0;
            st->model.rbw_bin_khz = 0;
            sdr_occupancy_reset(st);
            /* Fit the view again with one_max at SDR_LIVE_SPAN: one set while
             * a version 1 recording played can be one spectrum 40 MHz wide. */
            sdr_model_range(&st->model, st->model.start, st->model.stop);
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

        if (took >= 0) sdr_trigger_line(st, line, took == 1);
        if (took == 1) {
            st->last_frame = st->now;
            sdr_observe(st);
            st->rate_count++;
            st->dirty = 1;
        } else if (!strncmp(line, "ERR ", 4)) {
            snprintf(st->note, sizeof st->note, "The board said: %.60s", line);
            st->dirty = 1;
        }
    }
}

/** @brief A prompt ends the command before it: after SDR stopped the
 *         board is given back; while opening, sdr start is sent. */
static void take_prompt(sdr_t *st)
{
    st->pending = 0;
    if (st->state == SDR_RUNNING && st->tx.phase) {
        sdr_tx_prompt(st);
        return;
    }
    if (st->caps_inflight == 1 && st->caps_known == 1 &&
        receiver_part(*st->mcu ? st->mcu : st->board) == 5) {
        st->caps_inflight = 2;
        send_line(st, "sdr bands");
        return;
    }
    if (st->caps_inflight) {
        st->caps_inflight = 0;
        if (!st->caps_known) {
            st->caps_known = -1;
            if (st->model.fft_override || st->model.rate_override) {
                st->model.fft_override = st->model.rate_override = 0;
                st->model.rbw_bin_khz = 0;
                sdr_model_range(&st->model, st->model.start, st->model.stop);
                sdr_rec_view(st);
            }
            snprintf(st->note, sizeof st->note,
                     "Firmware did not report capabilities; using legacy automatic acquisition.");
        } else snprintf(st->note, sizeof st->note,
                        "Receiver capabilities read. FFT and sample-rate controls are available.");
        st->dirty = 1;
    }
    if (st->gain_inflight) {
        if (!st->gain_answered) {
            st->gain_known = -1;
            snprintf(st->note, sizeof st->note,
                     "Receiver did not confirm gain; controls disabled. Use Read receiver gain to retry.");
        } else if (st->gain_request != -2) {
            sdr_model_range(&st->model, st->model.start, st->model.stop);
            sdr_rec_view(st);
        }
        st->gain_inflight = 0;
        st->dirty = 1;
    }
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

/** @brief Whether @p hay's first @p n bytes hold "tikuOS". */
static int has_prompt_word(const char *hay, size_t n)
{
    for (size_t i = 0; i + 6 <= n; i++)
        if (!memcmp(hay + i, "tikuOS", 6)) return 1;
    return 0;
}

/** @brief Add what arrived to the buffer and act on each whole line; a
 *         prompt ("tikuOS ... > ") left at the end ends a command.  Bytes
 *         that would overflow the buffer drop what it held. */
static void take_bytes(sdr_t *st, const char *bytes, size_t n)
{
    size_t start = 0;

    if (n > sizeof st->rx - st->rx_n) {
        st->rx_n = 0;
        st->rx_overflows++;
    }
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

/** @brief Give up on a state waited in past its deadline: the desktop's
 *         handoff goes idle; opening, starting or stopping, or a command
 *         unanswered for ANSWER_US while running, give the board back. */
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
        if (sdr_tx_busy(st)) { send_line(st, "\rsdr tx clear\rsdr stop"); }
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
    if (sdr_tx_tick(st)) { return; }
    if (st->state == SDR_RUNNING && !st->pending && st->caps_queued && st->fd >= 0) {
        st->caps_queued = 0;
        st->caps_inflight = 1;
        st->caps_known = 0;
        send_line(st, "sdr info");
    }
    if (st->state == SDR_RUNNING && !st->pending && st->gain_queued &&
        st->fd >= 0) {
        char cmd[64];
        if (st->gain_request == -2) snprintf(cmd, sizeof cmd, "sdr gain");
        else if (st->gain_request == -1) snprintf(cmd, sizeof cmd, "sdr gain auto");
        else snprintf(cmd, sizeof cmd, "sdr gain %d", st->gain_request);
        st->gain_queued = 0;
        st->gain_inflight = 1;
        st->gain_answered = 0;
        send_line(st, cmd);
    }
    if (st->state == SDR_RUNNING && !st->pending && !st->paused &&
        st->fd >= 0) {
        char cmd[64];
        if (sdr_acquisition_valid(st))
            send_line(st, sdr_command(&st->model, cmd, sizeof cmd));
        else {
            snprintf(st->note, sizeof st->note,
                     "Range or acquisition is unsupported. Choose a supported rate / FFT or a narrower range.");
            st->dirty = 1;
        }
    }
    if (st->now - st->rate_at >= 1000000) {
        st->rate_hz = st->rate_count * 1e6f / (float)(st->now - st->rate_at);
        st->rate_at = st->now;
        st->rate_count = 0;
    }
}
