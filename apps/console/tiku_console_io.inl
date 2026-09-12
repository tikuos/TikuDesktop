/* Exclusive serial I/O. No shell subprocess, port scanning or root privileges. */
static speed_t baud_speed(const char *text)
{
    const int rates[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
    const speed_t speeds[] = {B9600, B19200, B38400, B57600, B115200, B230400, B460800, B921600};
    char *end; long rate = strtol(text, &end, 10);
    if (!*text || *end) return 0;
    for (int i = 0; i < 8; i++) if (rate == rates[i]) return speeds[i];
    return 0;
}

/** Enqueue atomically; never transmit the first half of a rejected paste. */
static int queue_bytes(console_t *st, const void *bytes, size_t n)
{
    if (st->state != ACTIVE || !n) return 0;
    if (n > sizeof st->output - (st->out_n - st->out_at)) {
        snprintf(st->note, sizeof st->note, "Output queue full; nothing from this input was sent.");
        st->dirty = 1; return 0;
    }
    memmove(st->output, st->output + st->out_at, st->out_n - st->out_at);
    st->out_n -= st->out_at; st->out_at = 0;
    memcpy(st->output + st->out_n, bytes, n); st->out_n += n;
    st->follow = 1; st->dirty = 1; return 1;
}

static void files_open(console_t *st)
{
    char path[160]; snprintf(path, sizeof path, "/devices/%s/data", st->board);
    st->files_pending = 0;
    if (!st->services->open_path || !st->services->open_path(st->services->ctx, path)) {
        if (st->services->clip_set) st->services->clip_set(st->services->ctx, path);
        snprintf(st->note, sizeof st->note, "Open %s in the desktop file manager (path copied).", path);
    } else snprintf(st->note, sizeof st->note, "File browser requested. Open console again when finished with files.");
}

static void reclaim(console_t *st)
{
    if (st->fd >= 0) { close(st->fd); st->fd = -1; }
    st->out_n = st->out_at = 0; st->ping_active = 0;
    st->state = RECLAIMING; st->deadline = st->now + 15000000;
    snprintf(st->note, sizeof st->note, "Returning %s to TikuDesktop...", st->board);
    if (!st->services->board_reclaim || !st->services->board_reclaim(st->services->ctx, st->board)) {
        st->state = IDLE; st->files_pending = 0;
        snprintf(st->note, sizeof st->note, "Device could not be returned. Reconnect through Devices.");
    }
    st->dirty = 1;
}

static void serial_open(console_t *st)
{
    struct termios t;
    int fd = open(st->port, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0 && tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t); t.c_cflag |= CLOCAL | CREAD; t.c_cflag &= (tcflag_t)~CRTSCTS;
        t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
        cfsetispeed(&t, baud_speed(st->field[2].text)); cfsetospeed(&t, baud_speed(st->field[2].text));
        if (!tcsetattr(fd, TCSANOW, &t)) {
            st->fd = fd; st->state = ACTIVE; st->received = st->sent = 0;
            st->slip_state = st->gui_frame = st->scan = st->utf_need = 0;
            char msg[192]; snprintf(msg, sizeof msg, "--- Console: %s at %.12s baud ---", st->board, st->field[2].text);
            message(st, msg);
            snprintf(st->note, sizeof st->note, "Console owns %s. Other device apps resume after Return to desktop.", st->board);
            return;
        }
    }
    int error = errno;
    if (fd >= 0) close(fd);
    reclaim(st);
    snprintf(st->note, sizeof st->note, "Cannot open console: %s. Returning device to desktop.", strerror(error));
}

/** Capture BEFORE release; UART settings never change on the desktop's fd. */
static void begin(console_t *st)
{
    if (st->state != IDLE || st->targets.selected < 0 || !*st->board) return;
    target_capture(st);
    if (strncmp(st->port, "/dev/", 5) || !baud_speed(st->field[2].text) ||
        !st->services->board_release || !st->services->board_reclaim) {
        snprintf(st->note, sizeof st->note, "A mounted serial device and a supported baud rate are required."); return;
    }
    st->cancelled = 0; st->state = RELEASING; st->deadline = st->now + 10000000;
    snprintf(st->note, sizeof st->note, "Waiting for TikuDesktop to release %s...", st->board);
    if (!st->services->board_release(st->services->ctx, st->board)) {
        st->state = IDLE;
        snprintf(st->note, sizeof st->note, "Device is busy or unavailable. Stop other device work and retry.");
    }
}

static void console_board_said(void *state, int what, const char *value)
{
    console_t *st = state;
    if ((what == TIKU_APP_BOARD_RELEASED || what == TIKU_APP_BOARD_RECLAIMED) &&
        (!value || strcmp(value, st->board))) return;
    if (what == TIKU_APP_BOARD_RELEASED && st->state == RELEASING) {
        if (st->cancelled) reclaim(st); else serial_open(st);
    } else if (what == TIKU_APP_BOARD_RECLAIMED && st->state == RECLAIMING) {
        st->state = IDLE;
        snprintf(st->note, sizeof st->note, "%s returned to TikuDesktop.", st->board);
        if (st->files_pending && !st->closing) files_open(st);
        targets_refresh(st);
    } else if (what == TIKU_APP_BOARD_FAILED &&
               (st->state == RELEASING || st->state == RECLAIMING)) {
        st->state = IDLE; st->files_pending = 0;
        snprintf(st->note, sizeof st->note, "Device handoff failed. Check Devices before retrying.");
    }
    st->dirty = 1;
}

/** At most 32 KiB per tick, partial writes retained, unplug reported explicitly. */
static void io_poll(console_t *st)
{
    if (st->state != ACTIVE || st->fd < 0) return;
    /* A frame nobody closed is abandoned, so one stray END before an
     * ordinary letter cannot swallow everything the board says after it. */
    if ((st->gui_frame || st->slip_state) &&
        st->now - st->frame_at > FRAME_TTL_US) {
        st->gui_frame = 0;
        st->slip_state = st->packet_n = st->slip_escape = st->slip_bad = 0;
    }
    struct pollfd p = {st->fd, POLLIN, 0};
    int rc = poll(&p, 1, 0);
    if (rc < 0 && errno == EINTR) return;
    if (rc < 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        message(st, "[Device link lost]"); reclaim(st); return;
    }
    if (st->out_at < st->out_n) {
        ssize_t n = write(st->fd, st->output + st->out_at, st->out_n - st->out_at);
        if (n > 0) { st->out_at += (size_t)n; st->sent += (unsigned long long)n; st->dirty = 1; }
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            message(st, "[Serial write failed]"); reclaim(st); return;
        }
    }
    for (int budget = 0; budget < 32768; budget += 4096) {
        unsigned char buf[4096]; ssize_t n = read(st->fd, buf, sizeof buf);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            message(st, "[Serial read failed]"); reclaim(st); return;
        }
        if (n <= 0) break;
        st->received += (unsigned long long)n;
        if (st->log && fwrite(buf, 1, (size_t)n, st->log) != (size_t)n) {
            fclose(st->log); st->log = NULL;
            snprintf(st->note, sizeof st->note, "Recording stopped: could not write the log.");
        }
        for (ssize_t i = 0; i < n; i++) wire_byte(st, buf[i]);
        st->dirty = 1;
    }
    if (st->log) fflush(st->log);
}

static void record_toggle(console_t *st)
{
    if (st->log) { fclose(st->log); st->log = NULL; return; }
    char path[] = "/tmp/tikuconsole-XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) {
        fcntl(fd, F_SETFD, FD_CLOEXEC); st->log = fdopen(fd, "wb");
        if (!st->log) close(fd);
    }
    if (!st->log) { snprintf(st->note, sizeof st->note, "Cannot create a private recording."); return; }
    snprintf(st->log_path, sizeof st->log_path, "%s", path);
    snprintf(st->note, sizeof st->note, "Recording received bytes to %s. Echoed secrets may be included.", path);
}
