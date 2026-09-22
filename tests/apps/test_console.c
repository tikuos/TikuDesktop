/* Native Console regressions: PTY only, never a real device or host route. */
#define _GNU_SOURCE
#include <assert.h>
#include <sys/stat.h>
#include "../../apps/console/tiku_console_private.h"

static int master, released, releases, reclaims, files, frames;
static char slave_path[256], clipboard[400000];
static uint32_t opened(void *ctx, const char *name, int w, int h)
{ (void)ctx; (void)name; assert(w == CW && h == CH); return 1; }
static void closed(void *ctx, uint32_t id) { (void)ctx; (void)id; }
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; frames++; return 1; }
static const char *board_list = "lab-board\n";
static int names(void *ctx, char *out, size_t cap)
{ (void)ctx; return snprintf(out, cap, "%s", released ? "" : board_list); }
static int current(void *ctx, char *out, size_t cap)
{ (void)ctx; return snprintf(out, cap, "lab-board"); }
static int port(void *ctx, const char *board, char *out, size_t cap)
{ (void)ctx; assert(!strcmp(board, "lab-board")); return snprintf(out, cap, "%s", released ? "" : slave_path); }
static int mcu(void *ctx, const char *board, char *out, size_t cap)
{ (void)ctx; (void)board; return snprintf(out, cap, "%s", released ? "" : "RP2350"); }
static int stats(void *ctx, const char *board, char *out, size_t cap)
{ (void)ctx; (void)board; return snprintf(out, cap, "Device\tLab board\nCPU clock\t150 MHz\nSRAM\t520 KiB\nStore\tUser data and files\n"); }
static int release(void *ctx, const char *board)
{ (void)ctx; assert(!strcmp(board, "lab-board")); assert(!released); releases++; released = 1; return 1; }
static int returned(void *ctx, const char *board)
{ (void)ctx; assert(!strcmp(board, "lab-board")); reclaims++; released = 0; return 1; }
static int open_path(void *ctx, const char *path)
{ (void)ctx; assert(!released); assert(!strcmp(path, "/devices/lab-board/data")); files++; return 1; }
static int clip_set(void *ctx, const char *value)
{ (void)ctx; return snprintf(clipboard, sizeof clipboard, "%s", value); }
static int clip_get(void *ctx, char *out, size_t cap)
{ (void)ctx; return snprintf(out, cap, "%s", clipboard); }
static tiku_app_services_t svc = {.open = opened, .close = closed, .frame = frame,
    .board_names = names, .current_device = current, .board_port = port, .board_mcu = mcu,
    .device_stats = stats, .board_release = release, .board_reclaim = returned,
    .open_path = open_path, .clip_set = clip_set, .clip_get = clip_get};

static console_t *start(void)
{
    void *state = NULL;
    assert(!tiku_console_start(&state, &svc));
    console_t *st = state;
    assert(st->state == IDLE && st->fd == -1 && !strcmp(st->board, "lab-board"));
    assert(st->targets.selected == 0); return st;
}
/* The board this console is on goes away while another is here: the list
 * offers both, and NOTHING is chosen on the user's behalf. */
static void vanished_target_test(console_t *st)
{
    board_list = "other-board\nspare-board\n";
    st->names[0] = '\0';
    tiku_console_targets_refresh(st);
    assert(st->targets.count == 2);
    assert(st->targets.selected == -1);   /* neither is the board in hand */
    assert(!strcmp(st->board, "lab-board"));
    board_list = "lab-board\n";
    st->names[0] = '\0';
    tiku_console_targets_refresh(st);
    assert(st->targets.count == 1 && st->targets.selected == 0);
}

static void bytes(console_t *st, const char *value)
{ for (const unsigned char *p = (const unsigned char *)value; *p; p++) tiku_console_text_byte(st, *p); }

static void model_test(console_t *st)
{
    bytes(st, "Hello \x1b[31"); bytes(st, "mred\x1b[0m \xc3"); bytes(st, "\xa9\n");
    char out[400]; tiku_console_line_string(&st->lines[0], out, sizeof out);
    assert(!strcmp(out, "Hello red é")); assert(st->lines[0].cell[6].ink == 1);
    bytes(st, "abcdef\rXY\x1b[K"); tiku_console_line_string(tiku_console_last_line(st), out, sizeof out);
    assert(!strcmp(out, "XY"));
    bytes(st, "z\b \b!"); tiku_console_line_string(tiku_console_last_line(st), out, sizeof out); assert(!strcmp(out, "XY!"));
    bytes(st, "\x1b[999999999999999999999999999999999999999D"); assert(st->column == 0);
    bytes(st, "\x1b]0;hidden title\x1b\\ok"); assert(!st->scan);
    for (int i = 0; i < ROWS + 100; i++) bytes(st, "line\n");
    assert(st->count == ROWS);
    tiku_field_start(&st->field[1], "line"); tiku_console_rebuild(st); assert(st->visible == ROWS - 1);
    snprintf(clipboard, sizeof clipboard, "keep clipboard");
    tiku_console_copy_history(st, 0); assert(!strcmp(clipboard, "keep clipboard"));
    assert(strstr(st->note, "Too much text"));
    tiku_console_message(st, "distinct line"); tiku_field_start(&st->field[1], "distinct");
    tiku_console_copy_history(st, 0); assert(!strcmp(clipboard, "distinct line\n"));
    tiku_field_start(&st->field[1], ""); tiku_console_action(st, CLEAR);
    tiku_console_history_add(st, "info"); tiku_console_history_add(st, "free"); tiku_console_history_add(st, "free");
    tiku_field_start(&st->field[0], "draft");
    tiku_console_history_move(st, -1); assert(!strcmp(st->field[0].text, "free"));
    tiku_console_history_move(st, -1); assert(!strcmp(st->field[0].text, "info"));
    tiku_console_history_move(st, 1); tiku_console_history_move(st, 1); assert(!strcmp(st->field[0].text, "draft"));
    puts("PASS: incremental ANSI/UTF-8, line editing, bounded history, search and command recall");
}

static void acquire(console_t *st)
{
    int before = releases;
    tiku_console_begin(st); assert(st->state == RELEASING && st->fd < 0 && releases == before + 1);
    tiku_console_board_said(st, TIKU_APP_BOARD_RELEASED, "other-board"); assert(st->fd < 0);
    tiku_console_board_said(st, TIKU_APP_BOARD_RELEASED, "lab-board");
    assert(st->state == ACTIVE && st->fd >= 0 && !strcmp(st->port, slave_path));
}

static void io_test(console_t *st)
{
    acquire(st);
    const char *greeting = "\r\nTikuOS ready\r\n> ";
    assert(write(master, greeting, strlen(greeting)) == (ssize_t)strlen(greeting));
    for (int i = 0; i < 20 && !st->received; i++) { tiku_console_io_poll(st); usleep(1000); }
    assert(st->received == strlen(greeting));
    tiku_field_start(&st->field[0], "info"); tiku_console_action(st, SEND); tiku_console_io_poll(st);
    char got[32] = ""; ssize_t n = read(master, got, sizeof got); assert(n == 5 && !memcmp(got, "info\r", 5));
    assert(!*st->field[0].text);
    unsigned char big[OUT_MAX]; memset(big, 'x', sizeof big);
    assert(tiku_console_queue_bytes(st, big, sizeof big));
    assert(!tiku_console_queue_bytes(st, "z", 1) && st->out_n == sizeof big);
    tiku_console_action(st, BREAK); assert(st->out_n == 1 && st->output[0] == 3); tiku_console_io_poll(st);
    assert(read(master, got, sizeof got) == 1 && got[0] == 3);
    snprintf(clipboard, sizeof clipboard, "info\nrm /data/file");
    st->out_n = st->out_at = 0;
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = 'v', .modifiers = TIKU_MOD_CMD};
    tiku_console_direct_event(st, &ev); assert(st->out_n == 0);
    snprintf(clipboard, sizeof clipboard, "safe text"); tiku_console_direct_event(st, &ev);
    assert(st->out_n == 9); st->out_n = st->out_at = 0;
    tiku_console_record_toggle(st); assert(st->log);
    struct stat sb; assert(!stat(st->log_path, &sb) && (sb.st_mode & 0777) == 0600);
    assert(write(master, "recorded\r\n", 10) == 10);
    for (int i = 0; i < 20; i++) { tiku_console_io_poll(st); usleep(1000); }
    tiku_console_action(st, CLEAR); tiku_console_record_toggle(st);
    FILE *log = fopen(st->log_path, "rb"); assert(log);
    assert(fread(got, 1, 10, log) == 10 && !memcmp(got, "recorded\r\n", 10)); fclose(log);
    unlink(st->log_path);
    tiku_console_action(st, FILES); assert(st->state == RECLAIMING && st->fd == -1 && !files);
    tiku_console_board_said(st, TIKU_APP_BOARD_RECLAIMED, "lab-board"); assert(st->state == IDLE && files == 1);
    puts("PASS: PTY I/O, port handoff, bounded queue, interrupt, safe paste, private logs and Files");
}

static void network_test(console_t *st)
{
    acquire(st); st->now = 1000000; tiku_console_ping_start(st); assert(st->ping_active);
    st->out_n = st->out_at = 0; st->now = st->ping_next; tiku_console_ping_tick(st);
    assert(st->ping_sent == 1 && st->out_n > 39 && st->output[0] == 0xc0);
    unsigned char reply[39] = {0}; reply[0] = 0x45; tiku_console_put16(reply + 2, sizeof reply);
    reply[8] = 64; reply[9] = 1;
    memcpy(reply + 12, &st->destination, 4); memcpy(reply + 16, &st->source, 4);
    tiku_console_put16(reply + 10, tiku_console_checksum(reply, 20)); tiku_console_put16(reply + 24, st->ping_id);
    memcpy(reply + 28, "tikuconsole", 11); tiku_console_put16(reply + 22, tiku_console_checksum(reply + 20, 19));
    st->now += 12000;
    unsigned char encoded[100]; size_t n = tiku_console_slip_encode(reply, sizeof reply, encoded, sizeof encoded);
    for (size_t i = 0; i < n; i++) tiku_console_wire_byte(st, encoded[i]);
    assert(st->ping_received == 1 && st->rtt_sum == 12.0 && st->rtts.count == 1);
    tiku_console_ip_packet(st, reply, sizeof reply); assert(st->ping_received == 1);
    st->ping_seen[0] = 0;
    reply[10] ^= 1; tiku_console_ip_packet(st, reply, sizeof reply); assert(st->ping_received == 1); reply[10] ^= 1;
    reply[22] ^= 1; tiku_console_ip_packet(st, reply, sizeof reply); assert(st->ping_received == 1); reply[22] ^= 1;
    reply[0] = 0x4f; tiku_console_ip_packet(st, reply, sizeof reply); assert(st->ping_received == 1); reply[0] = 0x45;
    st->ping_seen[0] = 1;
    /* A window frame is END then the 0xf1 marker, the order both encoders
     * write (kits/device/tiku_session.c and the board's console): its
     * payload is never printed as text. */
    int lines = st->count;
    tiku_console_wire_byte(st, 0xc0); tiku_console_wire_byte(st, 0xf1);
    tiku_console_wire_byte(st, '\n'); tiku_console_wire_byte(st, 'X'); tiku_console_wire_byte(st, 0xc0);
    assert(st->count == lines);
    /* A 0xf1 that is NOT the byte after an END is ordinary text. */
    tiku_console_wire_byte(st, 'a'); tiku_console_wire_byte(st, 0xf1); tiku_console_wire_byte(st, '\n');
    assert(st->count == lines + 1);
    /* A frame nobody closes is abandoned, and the text after it arrives. */
    tiku_console_wire_byte(st, 0xc0); tiku_console_wire_byte(st, 0xf1); tiku_console_wire_byte(st, 'Z');
    st->now += 31000000; tiku_console_io_poll(st);
    tiku_console_wire_byte(st, 'h'); tiku_console_wire_byte(st, 'i'); tiku_console_wire_byte(st, '\n');
    assert(st->count == lines + 2);
    while (st->ping_active) {
        st->out_at = st->out_n = 0; st->now = st->ping_next; tiku_console_ping_tick(st);
    }
    assert(st->ping_sent == 4 && st->ping_received == 1 && strstr(st->net_note, "75% loss"));
    tiku_tabs_select(&st->tabs, NETWORK); tiku_console_paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-console-network.png"));
    tiku_console_reclaim(st); tiku_console_board_said(st, TIKU_APP_BOARD_RECLAIMED, "lab-board");
    puts("PASS: rootless SLIP ping, checksum/address/length validation, duplicates, GUI demux and loss");
}

static void cancel_test(console_t *st)
{
    int before = reclaims; tiku_console_begin(st); tiku_console_action(st, RETURN);
    assert(st->state == RELEASING && st->cancelled && st->fd < 0);
    tiku_console_board_said(st, TIKU_APP_BOARD_RELEASED, "lab-board");
    assert(st->state == RECLAIMING && st->fd < 0 && reclaims == before + 1);
    tiku_console_board_said(st, TIKU_APP_BOARD_RECLAIMED, "lab-board");
    tiku_console_begin(st); st->now = st->deadline; tiku_console_tick(st, st->now);
    assert(st->state == RECLAIMING && st->fd < 0);
    st->now = st->deadline; tiku_console_tick(st, st->now); assert(st->state == IDLE);
    assert(strstr(st->note, "Reconnect"));
    puts("PASS: cancellation during release and bounded handoff timeouts");
}

static void click(console_t *st, int x, int y)
{
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = x, .y = y};
    tiku_console_event(st, st->id, &ev); ev.type = TIKU_EVENT_POINTER_UP;
    tiku_console_event(st, st->id, &ev);
}

static void ui_test(console_t *st)
{
    acquire(st); tiku_tabs_select(&st->tabs, CONSOLE); tiku_console_paint(st);
    click(st, 200, 560); assert(st->dialog.focus == FIELD);
    tiku_field_start(&st->field[0], "help");
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_RETURN};
    tiku_console_event(st, st->id, &ev); assert(st->out_n - st->out_at == 5);
    assert(!memcmp(st->output + st->out_at, "help\r", 5)); st->out_n = st->out_at = 0;
    click(st, 100, 520); assert(st->direct && st->dialog.focus == LIST);
    ev.key = 'x'; tiku_console_event(st, st->id, &ev); assert(st->out_n == 1 && st->output[0] == 'x');
    st->out_n = st->out_at = 0;
    click(st, 200, 108); assert(st->dialog.focus == FIELD + 1);
    ev.key = 'z'; tiku_console_event(st, st->id, &ev); assert(!st->out_n);
    tiku_field_start(&st->field[1], "");
    click(st, 480, 64); assert(st->tabs.current == NETWORK);
    click(st, 780, 64); assert(st->tabs.current == DEVICE);
    click(st, 180, 64); assert(st->tabs.current == CONSOLE);
    tiku_console_reclaim(st); tiku_console_board_said(st, TIKU_APP_BOARD_RECLAIMED, "lab-board");
    puts("PASS: pointer controls, tab navigation, Enter-to-send and field/direct-input isolation");
}

int main(void)
{
    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(master >= 0 && !grantpt(master) && !unlockpt(master));
    assert(!ptsname_r(master, slave_path, sizeof slave_path));
    console_t *st = start(); assert(!releases);
    model_test(st); io_test(st); network_test(st); ui_test(st); cancel_test(st);
    vanished_target_test(st);
    tiku_tabs_select(&st->tabs, CONSOLE);
    tiku_console_message(st, "TikuOS serial console"); bytes(st, "\x1b[32mReady\x1b[0m\r\n> info\r\nDevice: RP2350\r\n> ");
    tiku_console_paint(st); assert(!tiku_surface_png(st->surface, "/tmp/tiku-console.png"));
    tiku_font_set_size(16); tiku_console_paint(st); assert(!tiku_surface_png(st->surface, "/tmp/tiku-console-large.png"));
    tiku_tabs_select(&st->tabs, DEVICE); tiku_console_paint(st);
    assert(!tiku_surface_png(st->surface, "/tmp/tiku-console-device.png"));
    tiku_font_set_size(12); tiku_console_stop(st); close(master);
    assert(frames > 5); puts("PASS: native Console renders at both font sizes; no hardware accessed");
    return 0;
}
