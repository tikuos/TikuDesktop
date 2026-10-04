/* TikuSDR without a radio: the model's lines and band, then the whole app
 * against a fake board on a pseudo-terminal -- handoff, start, a spectrum,
 * the waterfall, a recording made and played back, spectra exported as
 * CSV and the window as a picture, a recording opened from Tracker, and
 * the board given back.
 * SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE       /* posix_openpt and ptsname_r, on glibc too */
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "../../apps/sdr/tiku_sdr_app.c"
#include "../../apps/sdr/tiku_sdr_link.c"
#include "../../apps/sdr/tiku_sdr_model.c"
#include "../../apps/sdr/tiku_sdr_tape.c"
#include "../../apps/sdr/tiku_sdr_view.c"

#define PROMPT "\r\ntikuOS:/> "

static char slave_path[128];
static int frames, released, reclaimed, menus_seen;
static const uint32_t *last_px;
static int asked_mode = -1;                 /* the file panel last asked */
static tiku_menuset_t last_menus;           /* the menus last published  */
static char asked_start[512], asked_name[160];

/** @brief The desktop's open: the window TikuSDR asks for. */
static uint32_t opened(void *ctx, const char *title, int w, int h)
{
    (void)ctx;
    assert(!strcmp(title, "TikuSDR") && w == SDR_W && h == SDR_H);
    return 7;
}

/** @brief A frame shown, its pixels kept. */
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    (void)ctx;
    assert(id == 7 && w > 0 && h > 0);
    last_px = px;
    frames++;
    return 1;
}

/** @brief The menus, the first of them SDR, kept. */
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *m)
{
    (void)ctx;
    (void)id;
    assert(m->nmenu >= 2 && !strcmp(m->menu[0].title, "SDR"));
    last_menus = *m;
    menus_seen++;
    return 1;
}

/** @brief Whether the menus last published offer @p command for use:
 *         1 or 0, or -1 when no menu holds it. */
static int offered(int command)
{
    for (int i = 0; i < last_menus.nmenu; i++)
        for (int j = 0; j < last_menus.menu[i].nitem; j++)
            if (last_menus.menu[i].item[j].command == command)
                return last_menus.menu[i].item[j].enabled;
    return -1;
}

/** @brief One board mounted, "c61". */
static int names(void *ctx, char *out, size_t max)
{
    (void)ctx;
    snprintf(out, max, "c61\n");
    return 1;
}

/** @brief The board's port: the pseudo-terminal's far end. */
static int port(void *ctx, const char *board, char *out, size_t max)
{
    (void)ctx;
    assert(!strcmp(board, "c61"));
    snprintf(out, max, "%s", slave_path);
    return 1;
}

/** @brief The board's MCU. */
static int mcu(void *ctx, const char *board, char *out, size_t max)
{
    (void)ctx;
    (void)board;
    snprintf(out, max, "esp32c61");
    return 1;
}

/** @brief The board let go, counted. */
static int release(void *ctx, const char *board)
{
    (void)ctx;
    assert(!strcmp(board, "c61"));
    released++;
    return 1;
}

/** @brief The board taken back, counted. */
static int reclaim_board(void *ctx, const char *board)
{
    (void)ctx;
    assert(!strcmp(board, "c61"));
    reclaimed++;
    return 1;
}

/** @brief The desktop's file panel: the question kept, the test answers. */
static int panel(void *ctx, uint32_t id, int mode, const char *start,
                 const char *name)
{
    (void)ctx;
    assert(id == 7);
    asked_mode = mode;
    snprintf(asked_start, sizeof asked_start, "%s", start ? start : "");
    snprintf(asked_name, sizeof asked_name, "%s", name ? name : "");
    return 1;
}

/** @brief The lines in the file at @p path. */
static long file_lines(const char *path)
{
    FILE *f = fopen(path, "r");
    long n = 0;
    int c;

    if (!f) return -1;
    while ((c = fgetc(f)) != EOF) n += c == '\n';
    fclose(f);
    return n;
}

/** @brief Whether the file at @p path holds @p needle. */
static int file_has(const char *path, const char *needle)
{
    static char text[65536];
    FILE *f = fopen(path, "r");
    size_t n;

    if (!f) return 0;
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = '\0';
    return strstr(text, needle) != NULL;
}

/** @brief The fake board says @p text, all of it. */
static void say(int master, const char *text)
{
    size_t n = strlen(text);

    assert(write(master, text, n) == (ssize_t)n);
}

/** @brief A SPEC line: @p n bins of @p base, @p peak at bin @p at. */
static void spec_line(char *out, size_t max, int centre, int n, int base,
                      int at, int peak)
{
    int k = snprintf(out, max, "SPEC %d 40000000 76 %d ", centre, n);

    for (int j = 0; j < n; j++)
        k += snprintf(out + k, max - (size_t)k, "%02x", j == at ? peak : base);
}

/** @brief Lines, the model's way: parsing, stale tunings, the band. */
static void model_tests(void)
{
    sdr_model_t m;
    char line[1200], ch[96], cmd[64];

    sdr_model_init(&m);
    assert(m.mode == SDR_LIVE && m.centre == 2437 && m.n == 256);
    spec_line(line, sizeof line, 2437, 256, 100, 160, 180);
    assert(sdr_model_line(&m, line) == 1);
    assert(m.db[160] == 90.0f && m.db[0] == 50.0f && m.gain == 76);
    assert(m.spectra == 1 && m.wf_count == 1 && m.fresh);
    assert(fabs(sdr_model_freq(&m, 128) - 2437.0) < 1e-9);
    assert(sdr_model_bin(&m, 2437.0) == 128 && sdr_model_bin(&m, 2300) == -1);
    spec_line(line, sizeof line, 2442, 256, 100, 0, 100);
    assert(sdr_model_line(&m, line) == 0);            /* an older tuning */
    assert(sdr_model_line(&m, "SPEC 2437 40000000 76 256 zz") == -1);
    assert(sdr_model_line(&m, "hello") == -1);
    assert(!strcmp(sdr_command(&m, cmd, sizeof cmd), "sdr spec 2437 1 256"));

    sdr_model_mode(&m, SDR_BAND, 2437);
    assert(m.n == SDR_BAND_N && m.wf_count == 0);
    /* The tail of a sweep begun before Band hears part of the band only:
     * no frame of it, or the rest would read as 0 dB. */
    spec_line(line, sizeof line, 2484, 128, 80, -1, 0);
    assert(sdr_model_line(&m, line) == 0);
    assert(sdr_model_line(&m, "SWEEP 6") == 0 && m.wf_count == 0 && !m.fresh);
    for (int c = SDR_BAND_FIRST; c <= SDR_BAND_LAST; c += SDR_BAND_STEP) {
        /* Slice 2420 carries a narrow peak at +6 MHz: BLE 38, 2426 MHz. */
        spec_line(line, sizeof line, c, 128, 80 + (c - 2404) / 16, c == 2420 ?
                  64 + 6 * 128 / 40 : -1, 200);
        assert(sdr_model_line(&m, line) == 0);
    }
    assert(sdr_model_line(&m, "SWEEP 6") == 1 && m.wf_count == 1);
    /* Each slice's floor levelled to 40 dB; the peak keeps its height
     * over its own floor (200 over 81, in half-dB). */
    assert(m.db[sdr_model_bin(&m, 2426.0)] == 99.5f);
    assert(m.db[sdr_model_bin(&m, 2404.0)] == 40.0f);
    assert(m.db[sdr_model_bin(&m, 2484.0)] == 40.0f);
    for (int j = 0; j < m.n; j++) assert(m.db[j] >= 40.0f);  /* no gaps */
    assert(!m.seen[0]);
    assert(!strcmp(sdr_command(&m, cmd, sizeof cmd),
                   "sdr sweep 2404 2484 16 1 128"));

    sdr_channels(2437.0, ch, sizeof ch);
    assert(strstr(ch, "Wi-Fi 4-8"));
    sdr_channels(2426.0, ch, sizeof ch);
    assert(strstr(ch, "BLE 38 (advertising)"));
    sdr_channels(2415.0, ch, sizeof ch);
    assert(strstr(ch, "802.15.4 13"));
    sdr_channels(2404.0, ch, sizeof ch);
    assert(strstr(ch, "BLE 0") && !strstr(ch, "advertising"));
    assert(sdr_palette(-1.0f) == TIKU_RGB(11, 15, 26));
    assert(sdr_palette(2.0f) == TIKU_RGB(255, 245, 214));
}

/** @brief What the fake board read, up to a return. */
static void board_read(int master, char *out, size_t max)
{
    size_t n = 0;

    for (int i = 0; i < 400 && n + 1 < max; i++) {
        ssize_t r = read(master, out + n, max - 1 - n);
        if (r > 0) n += (size_t)r;
        out[n] = '\0';
        if (n && out[n - 1] == '\r') return;
        usleep(1000);
    }
}

/** @brief The app's tick a few times, the clock moving. */
static void ticks(void *st, int64_t *now, int count)
{
    for (int i = 0; i < count; i++) {
        *now += 60000;
        tiku_sdr_app.tick(st, *now);
    }
}

/** @brief A spectrum and a band sweep recorded, the place from the Save
 *         panel's answer; 'r' stops it, and the file says what it holds. */
static void record(sdr_t *st, int master, int64_t *now, const char *dir)
{
    char path[512], got[256], line[1200];
    const char *home = getenv("HOME");
    tiku_event_t r = {.type = TIKU_EVENT_KEY_DOWN, .key = 'r'};

    snprintf(path, sizeof path, "%s/rec.sdrlog", dir);
    assert(sdr_enabled(st, CMD_RECORD) && !sdr_enabled(st, CMD_REPLAY));
    assert(tiku_sdr_app.pick(st, st->id, CMD_RECORD) == 0);
    assert(asked_mode == TIKU_APP_PICK_SAVE && st->asking == CMD_RECORD);
    assert(!strcmp(asked_start, home ? home : ""));
    assert(!strncmp(asked_name, "TikuSDR c61 20", 14) &&
           strstr(asked_name, ".sdrlog"));
    tiku_sdr_app.picked(st, st->id, path);
    assert(st->rec.file && !strcmp(st->note, "Recording to rec.sdrlog."));
    assert(!strcmp(st->tape_dir, dir) && !strcmp(sdr_label(st, CMD_RECORD),
                                                 "Recording"));

    /* The spectrum already asked for arrives, and is written. */
    spec_line(line, sizeof line, 2437, 256, 112, 150, 200);
    strcat(line, PROMPT);
    say(master, line);
    ticks(st, now, 4);
    assert(st->rec.frames == 1);
    board_read(master, got, sizeof got);
    assert(!strcmp(got, "sdr spec 2437 1 256\r"));

    /* Band: the change is written, then a sweep's six slices. */
    assert(tiku_sdr_app.pick(st, st->id, CMD_BAND) == 0);
    say(master, PROMPT);                    /* the spec in flight, done */
    ticks(st, now, 2);
    board_read(master, got, sizeof got);
    assert(!strcmp(got, "sdr sweep 2404 2484 16 1 128\r"));
    for (int c = SDR_BAND_FIRST; c <= SDR_BAND_LAST; c += SDR_BAND_STEP) {
        spec_line(line, sizeof line, c, 128, 90, c == 2420 ? 83 : -1, 220);
        strcat(line, "\r\n");
        say(master, line);
        ticks(st, now, 1);
    }
    say(master, "SWEEP 6" PROMPT);
    ticks(st, now, 3);
    assert(st->rec.frames == 2 && st->model.spectra == 3);
    board_read(master, got, sizeof got);    /* the next sweep, asked */
    assert(!strcmp(got, "sdr sweep 2404 2484 16 1 128\r"));

    assert(tiku_sdr_app.event(st, &r) == 0 && !st->rec.file);
    assert(strstr(st->saved, "Saved rec.sdrlog: 0:00, 2 spectra, "));
    assert(!strcmp(st->note, st->saved));
    assert(file_has(path, "# TikuSDR recording 1\n# board: c61 (esp32c61)\n"));
    assert(file_has(path, "\n0.000 VIEW live 2437\n"));
    assert(file_has(path, " SPEC 2437 40000000 76 256 "));
    assert(file_has(path, " VIEW band\n"));
    assert(file_has(path, " SPEC 2484 40000000 76 128 "));
    assert(file_has(path, " SWEEP 6\n"));
}

/** @brief Exports while the band shows one sweep: its spectrum as CSV,
 *         the peak recorded at 2426 MHz on its row, and the window below
 *         its toolbar as a PNG of the size it should be. */
static void exports(sdr_t *st, const char *dir)
{
    char csv[512], png[512];
    unsigned char head[24];
    FILE *f;

    snprintf(csv, sizeof csv, "%s/held.csv", dir);
    snprintf(png, sizeof png, "%s/view.png", dir);
    assert(sdr_enabled(st, CMD_EXPORT) && sdr_enabled(st, CMD_PICTURE));
    assert(tiku_sdr_app.pick(st, st->id, CMD_EXPORT) == 0);
    assert(asked_mode == TIKU_APP_PICK_SAVE && st->asking == CMD_EXPORT);
    assert(!strncmp(asked_name, "TikuSDR c61 20", 14) &&
           !strcmp(asked_name + strlen(asked_name) - 4, ".csv"));
    tiku_sdr_app.picked(st, st->id, csv);
    assert(!strncmp(st->note, "Exported held.csv: 1 spectrum, 307 rows, ", 41));
    assert(file_lines(csv) == 308);
    assert(file_has(csv, "time_s,freq_mhz,db\n0.000,2396.00000,"));
    assert(file_has(csv, "\n0.000,2426.00000,105.0\n"));

    assert(tiku_sdr_app.pick(st, st->id, CMD_PICTURE) == 0);
    assert(!strcmp(asked_name + strlen(asked_name) - 4, ".png"));
    tiku_sdr_app.picked(st, st->id, png);
    assert(!strcmp(st->note, "Saved the picture as view.png."));
    assert((f = fopen(png, "rb")) && fread(head, 1, 24, f) == 24);
    fclose(f);
    assert(!memcmp(head, "\x89PNG", 4));
    assert(head[18] * 256 + head[19] == SDR_W);           /* IHDR's size */
    assert(head[22] * 256 + head[23] == SDR_H - 34);
    unlink(csv);
    unlink(png);
}

/** @brief The recording played back: the Open panel starts where it was
 *         saved, Pause holds the clock, the view is the recording's and
 *         not the Tune menu's, and the end is said. */
static void replay(sdr_t *st, int64_t *now, const char *dir)
{
    char path[512];

    snprintf(path, sizeof path, "%s/rec.sdrlog", dir);
    assert(sdr_enabled(st, CMD_REPLAY) && !sdr_enabled(st, CMD_RECORD));
    assert(tiku_sdr_app.pick(st, st->id, CMD_REPLAY) == 0);
    assert(asked_mode == TIKU_APP_PICK_LOCAL_OPEN &&
           !strcmp(asked_start, dir));
    tiku_sdr_app.picked(st, st->id, path);
    assert(st->play.file && st->play.total > 0.0);
    assert(!strncmp(st->note, "Playing rec.sdrlog (c61, 20", 27));
    assert(!sdr_enabled(st, CMD_LIVE) && sdr_enabled(st, CMD_REPLAY));

    assert(tiku_sdr_app.pick(st, st->id, CMD_PAUSE) == 0 && st->paused);
    ticks(st, now, 10);
    assert(st->play.clock == 0 && st->model.spectra == 0);
    assert(tiku_sdr_app.pick(st, st->id, CMD_TUNE + 26) == 0);
    assert(st->model.centre == 2437);
    assert(tiku_sdr_app.pick(st, st->id, CMD_PAUSE) == 0 && !st->paused);
    ticks(st, now, 40);
    assert(!st->play.file && st->model.mode == SDR_BAND);
    assert(st->model.spectra == 2);
    assert(!strcmp(st->note, "End of rec.sdrlog: 0:00, 2 spectra."));
    assert(sdr_enabled(st, CMD_LIVE) && !strcmp(st->shown, path));

    /* Its CSV is the whole recording, converted over ticks: one live
     * spectrum of 256 bins and one sweep of 307. */
    snprintf(path, sizeof path, "%s/rec.csv", dir);
    assert(tiku_sdr_app.pick(st, st->id, CMD_EXPORT) == 0);
    assert(!strcmp(asked_name, "rec.csv"));
    tiku_sdr_app.picked(st, st->id, path);
    assert(st->csv.in && !strncmp(st->note, "Exporting rec.csv", 17));
    assert(!sdr_enabled(st, CMD_EXPORT));
    ticks(st, now, 3);
    assert(!st->csv.in);
    assert(!strncmp(st->note, "Exported rec.csv: 2 spectra, 563 rows, ", 39));
    assert(file_lines(path) == 564);
    assert(file_has(path, ",2437.00000,") && file_has(path, ",2396.00000,"));
    unlink(path);
    /* Tuned, the screen is no longer the recording's. */
    assert(tiku_sdr_app.pick(st, st->id, CMD_TUNE + 26) == 0);
    assert(!st->shown[0] && !sdr_enabled(st, CMD_EXPORT));
}

/** @brief A recording double-clicked in Tracker: TikuSDR starts with it
 *         and plays it. */
static void opened_with(const tiku_app_services_t *services, const char *dir)
{
    tiku_app_services_t with = *services;
    char path[512];
    void *state = NULL;
    sdr_t *st;

    snprintf(path, sizeof path, "%s/rec.sdrlog", dir);
    with.startup_document = path;
    assert(tiku_sdr_app.start(&state, &with) == 0);
    st = state;
    assert(st->play.file && !strcmp(st->shown, path));
    assert(!strncmp(st->note, "Playing rec.sdrlog (c61, 20", 27));
    tiku_sdr_app.stop(st);
}

/** @brief What is not a recording, a Save answer come too late, a long
 *         silence cut short, and Close Recording. */
static void odd_files(sdr_t *st, int64_t *now, const char *dir)
{
    char junk[512], gap[512], line[1200];
    FILE *f;

    snprintf(junk, sizeof junk, "%s/notes.txt", dir);
    assert((f = fopen(junk, "w")) && fputs("shopping list\n", f) >= 0);
    fclose(f);
    assert(tiku_sdr_app.pick(st, st->id, CMD_REPLAY) == 0);
    tiku_sdr_app.picked(st, st->id, junk);
    assert(!st->play.file);
    assert(!strcmp(st->note, "notes.txt is not a TikuSDR recording."));
    st->asking = CMD_RECORD;
    tiku_sdr_app.picked(st, st->id, junk);
    assert(!st->rec.file && strstr(st->note, "Nothing was recorded"));
    assert(file_has(junk, "shopping list"));
    /* A CSV asked of what is not a recording is not begun. */
    snprintf(st->shown, sizeof st->shown, "%s", junk);
    snprintf(line, sizeof line, "%s/never.csv", dir);
    st->asking = CMD_EXPORT;
    tiku_sdr_app.picked(st, st->id, line);
    assert(!st->csv.in && access(line, F_OK) != 0);
    assert(!strcmp(st->note, "notes.txt is not a TikuSDR recording."));
    st->shown[0] = '\0';

    snprintf(gap, sizeof gap, "%s/gap.sdrlog", dir);
    assert((f = fopen(gap, "w")));
    fputs("# TikuSDR recording 1\n# board: c61 (esp32c61)\n", f);
    fputs("0.000 VIEW live 2426\n", f);
    spec_line(line, sizeof line, 2426, 256, 100, 128, 180);
    fprintf(f, "0.100 %s\n100.000 %s\n", line, line);
    fclose(f);
    assert(tiku_sdr_app.pick(st, st->id, CMD_REPLAY) == 0);
    tiku_sdr_app.picked(st, st->id, gap);
    assert(st->play.file && st->play.total == 100.0);
    ticks(st, now, 4);                      /* the first spectrum */
    assert(st->model.spectra == 1 && st->model.centre == 2426);
    ticks(st, now, 12);                     /* 99.9 s of silence, cut */
    assert(st->model.spectra == 2 && !st->play.file);

    assert(tiku_sdr_app.pick(st, st->id, CMD_REPLAY) == 0);
    tiku_sdr_app.picked(st, st->id, gap);
    assert(st->play.file);
    assert(tiku_sdr_app.pick(st, st->id, CMD_REPLAY) == 0);
    assert(!st->play.file && !strcmp(st->note, "Closed gap.sdrlog."));
    unlink(junk);
    unlink(gap);
}

int main(void)
{
    tiku_app_services_t services = {.open = opened, .frame = frame,
        .menus = menus, .board_names = names, .board_port = port,
        .board_mcu = mcu, .board_release = release,
        .board_reclaim = reclaim_board, .pick = panel};
    void *state = NULL;
    sdr_t *st;
    char got[256], line[1200], dir[] = "/tmp/test_sdr_XXXXXX", rec[600];
    char rec2[600];
    int master;
    int64_t now = 0;

    model_tests();
    assert(mkdtemp(dir));
    snprintf(rec, sizeof rec, "%s/rec.sdrlog", dir);
    snprintf(rec2, sizeof rec2, "%s/rec2.sdrlog", dir);
    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(master >= 0 && !grantpt(master) && !unlockpt(master));
    assert(!ptsname_r(master, slave_path, sizeof slave_path));

    assert(tiku_sdr_app.start(&state, &services) == 0 && state);
    st = state;
    assert(frames >= 1 && menus_seen >= 1 && !strcmp(st->board, "c61"));
    assert(st->state == SDR_IDLE && st->note[0] == '\0');    /* nothing said */
    assert(!sdr_enabled(st, CMD_EXPORT) && !sdr_enabled(st, CMD_PICTURE));
    assert(offered(CMD_RECORD) == 0 && offered(CMD_EXPORT) == 0);
    assert(!strcmp(last_menus.menu[1].title, "File"));

    assert(tiku_sdr_app.pick(st, st->id, CMD_CONNECT) == 0);
    assert(st->state == SDR_RELEASING && released == 1);
    sdr_board_said(st, TIKU_APP_BOARD_RELEASED, "another");
    assert(st->state == SDR_RELEASING);
    tiku_sdr_app.board_said(st, TIKU_APP_BOARD_RELEASED, "c61");
    assert(st->state == SDR_OPENING && st->fd >= 0);

    board_read(master, got, sizeof got);                    /* a bare return */
    assert(!strcmp(got, "\r"));
    say(master, "\r\ntikuOS:/> ");
    ticks(st, &now, 3);
    assert(st->state == SDR_STARTING);
    board_read(master, got, sizeof got);
    assert(!strcmp(got, "sdr start\r"));
    say(master, "sdr start\r\nSDR ready\r\ntikuOS:/> ");
    ticks(st, &now, 3);
    assert(st->state == SDR_RUNNING);
    /* The menus follow the board between commands, with nothing picked. */
    assert(offered(CMD_RECORD) == 1 && offered(CMD_EXPORT) == 0);
    board_read(master, got, sizeof got);
    assert(!strcmp(got, "sdr spec 2437 1 256\r"));
    spec_line(line, sizeof line, 2437, 256, 110, 140, 190);
    strcat(line, "\r\ntikuOS:/> ");
    say(master, line);
    ticks(st, &now, 4);
    assert(st->model.spectra == 1 && st->model.wf_count == 1);
    assert(st->model.db[140] == 95.0f && st->model.gain == 76);
    assert(offered(CMD_EXPORT) == 1 && offered(CMD_PICTURE) == 1);
    board_read(master, got, sizeof got);                    /* the next ask */
    assert(!strcmp(got, "sdr spec 2437 1 256\r"));

    /* A hover over the peak names its frequency and channels. */
    {
        tiku_rect_t plot, fall;
        sdr_layout(st, &plot, &fall);
        hover(st, plot.x + 140 * plot.w / 256 + 1, plot.y + 4);
        assert(strstr(st->hover, "MHz") && strstr(st->hover, "Wi-Fi"));
        st->hover[0] = '\0';
    }
    sdr_paint(st);
    assert(last_px != NULL);
    assert(tiku_surface_png(st->surface, "build/tests/test_sdr.png") == 0 ||
           tiku_surface_png(st->surface, "test_sdr.png") == 0);
    record(st, master, &now, dir);
    exports(st, dir);
    /* A second recording, still going when the board is given back. */
    assert(tiku_sdr_app.pick(st, st->id, CMD_RECORD) == 0);
    tiku_sdr_app.picked(st, st->id, rec2);
    assert(st->rec.file && !st->saved[0]);

    assert(tiku_sdr_app.pick(st, st->id, CMD_CONNECT) == 0);   /* disconnect */
    assert(st->state == SDR_STOPPING && st->fd >= 0 && !reclaimed);
    board_read(master, got, sizeof got);
    assert(strstr(got, "sdr stop\r"));
    {
        /* The spec still in flight finishes first: its prompt is not the
         * stop's, so the port stays open until "SDR stopped". */
        say(master, "\r\ntikuOS:/> ");
        ticks(st, &now, 2);
        assert(st->state == SDR_STOPPING && st->fd >= 0);
        say(master, "sdr stop\r\nSDR stopped\r\ntikuOS:/> ");
        ticks(st, &now, 2);
    }
    assert(st->state == SDR_RECLAIMING && reclaimed == 1 && st->fd < 0);
    /* Closed with the port; the note still says where the board went. */
    assert(!st->rec.file && strstr(st->saved, "Saved rec2.sdrlog: "));
    assert(!strcmp(st->note, "Returning c61 to the desktop..."));
    assert(file_has(rec2, "\n0.000 VIEW band\n"));
    tiku_sdr_app.board_said(st, TIKU_APP_BOARD_RECLAIMED, "c61");
    assert(st->state == SDR_IDLE && st->saved[0]);
    replay(st, &now, dir);
    opened_with(&services, dir);
    odd_files(st, &now, dir);
    tiku_sdr_app.stop(st);
    close(master);
    unlink(rec);
    unlink(rec2);
    assert(rmdir(dir) == 0);
    puts("test_sdr: ok");
    return 0;
}
