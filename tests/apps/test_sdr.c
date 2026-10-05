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
#include "../../apps/sdr/tiku_sdr_marker.c"
#include "../../apps/sdr/tiku_sdr_model.c"
#include "../../apps/sdr/tiku_sdr_tape.c"
#include "../../apps/sdr/tiku_sdr_view.c"

#define PROMPT "\r\ntikuOS:/> "

static char slave_path[128], lent[64];      /* the board last let go */
static int frames, released, reclaimed, menus_seen;
static const uint32_t *last_px;
static int asked_mode = -1;                 /* the file panel last asked */
static tiku_menuset_t last_menus;           /* the menus last published  */
static char asked_start[512], asked_name[160];

/** @brief Whether @p m shows the 2.4 GHz band, the wide view it opens with. */
static int is_band(const sdr_model_t *m)
{
    return m->start == SDR_BAND_START && m->stop == SDR_BAND_STOP;
}

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

/** @brief Whether the menus last published mark @p command as on: 1 or 0,
 *         or -1 when no menu holds it. */
static int marked(int command)
{
    for (int i = 0; i < last_menus.nmenu; i++)
        for (int j = 0; j < last_menus.menu[i].nitem; j++)
            if (last_menus.menu[i].item[j].command == command)
                return last_menus.menu[i].item[j].marked;
    return -1;
}

/** @brief One board mounted, "c61". */
static int names(void *ctx, char *out, size_t max)
{
    (void)ctx;
    snprintf(out, max, "c61\n");
    return 1;
}

static const char *told;                    /* NULL: the desktop has not
                                               said its boards yet      */
static const char *current = "";

/** @brief The boards as the desktop last said them, -1 before it has, as
 *         an application out of process hears them. */
static int names_told(void *ctx, char *out, size_t max)
{
    (void)ctx;
    snprintf(out, max, "%s", told ? told : "");
    return told ? (int)strlen(out) : -1;
}

/** @brief Each board's MCU, as its name says: nrf* an nRF54L15, esp* an
 *         ESP32-C61, anything else a board that never said. */
static int mcu_named(void *ctx, const char *board, char *out, size_t max)
{
    (void)ctx;
    snprintf(out, max, "%s", !strncmp(board, "nrf", 3) ? "nRF54L15" :
             !strncmp(board, "esp", 3) ? "ESP32-C61" : "");
    return (int)strlen(out);
}

/** @brief The desktop's current device. */
static int current_named(void *ctx, char *out, size_t max)
{
    (void)ctx;
    snprintf(out, max, "%s", current);
    return (int)strlen(out);
}

/** @brief A board's port: the pseudo-terminal's far end, which only the
 *         board the desktop says it let go of ever opens. */
static int port(void *ctx, const char *board, char *out, size_t max)
{
    (void)ctx;
    (void)board;
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

/** @brief A board let go, counted and named. */
static int release(void *ctx, const char *board)
{
    (void)ctx;
    snprintf(lent, sizeof lent, "%s", board);
    released++;
    return 1;
}

/** @brief The board taken back, the one that was lent, counted. */
static int reclaim_board(void *ctx, const char *board)
{
    (void)ctx;
    assert(!strcmp(board, lent));
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

/** @brief A SPEC line at @p hz: @p n bins of @p base, @p peak at bin @p at. */
static void spec_at(char *out, size_t max, int centre, long hz, int n,
                    int base, int at, int peak)
{
    int k = snprintf(out, max, "SPEC %d %ld 76 %d ", centre, hz, n);

    for (int j = 0; j < n; j++)
        k += snprintf(out + k, max - (size_t)k, "%02x", j == at ? peak : base);
}

/** @brief The same at 40 MS/s, Live's rate and a sweep's. */
static void spec_line(char *out, size_t max, int centre, int n, int base,
                      int at, int peak)
{
    spec_at(out, max, centre, 40000000, n, base, at, peak);
}

/** @brief Lines, the model's way: parsing, stale tunings, the band. */
static void model_tests(void)
{
    sdr_model_t m;
    char line[1200], ch[96], cmd[64];

    sdr_model_init(&m);
    /* Live: 24 MHz about 2437, the flat middle of a 40 MS/s capture. */
    assert(!m.sweep && m.start == 2425 && m.stop == 2449 && m.tune == 2437);
    assert(m.rate == 1 && m.k0 == 52 && m.n == 153 && sdr_model_is_live(&m));
    spec_line(line, sizeof line, 2437, 256, 100, 160, 180);
    assert(sdr_model_line(&m, line) == 1);
    assert(m.db[160 - 52] == 90.0f && m.db[0] == 50.0f && m.gain == 76);
    assert(m.spectra == 1 && m.wf_count == 1 && m.fresh);
    assert(fabs(sdr_model_freq(&m, 76) - 2437.0) < 1e-9);
    assert(sdr_model_bin(&m, 2437.0) == 76 && sdr_model_bin(&m, 2300) == -1);
    spec_line(line, sizeof line, 2442, 256, 100, 0, 100);
    assert(sdr_model_line(&m, line) == 0);            /* an older tuning */
    assert(sdr_model_line(&m, "SPEC 2437 40000000 76 256 zz") == -1);
    assert(sdr_model_line(&m, "hello") == -1);
    assert(!strcmp(sdr_command(&m, cmd, sizeof cmd), "sdr spec 2437 1 256"));

    sdr_model_range(&m, SDR_BAND_START, SDR_BAND_STOP);
    assert(m.sweep && is_band(&m) && !sdr_model_is_live(&m));
    assert(m.first == 2404 && m.last == 2484 && m.n == 307 && m.wf_count == 0);
    /* The tail of a sweep begun before Band hears part of the band only:
     * no frame of it, or the rest would read as 0 dB. */
    spec_line(line, sizeof line, 2484, 128, 80, -1, 0);
    assert(sdr_model_line(&m, line) == 0);
    assert(sdr_model_line(&m, "SWEEP 6") == 0 && m.wf_count == 0 && !m.fresh);
    for (int c = m.first; c <= m.last; c += SDR_SLICE_STEP) {
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
    assert(sdr_palette(-1.0f) == TIKU_RGB(0, 0, 96));
    assert(sdr_palette(2.0f) == TIKU_RGB(230, 20, 20));
}

/** @brief Ranges: fitted to what the board gives; one spectrum at the
 *         narrowest rate that holds the view, its own bins shown; sweeps
 *         past 40 MHz, short ones no frame, moved down whole at the
 *         board's top; frequencies as text, both ways. */
static void range_tests(void)
{
    sdr_model_t m;
    double a, b, v;
    char line[1200], cmd[64], t[32];

    a = 2100, b = 2150, sdr_range_fit(&a, &b);
    assert(a == 2200 && b == 2250);
    a = 2800, b = 2600, sdr_range_fit(&a, &b);
    assert(a == 2500 && b == 2700);
    a = 1000, b = 9000, sdr_range_fit(&a, &b);
    assert(a == 2200 && b == 2700);
    a = 2437, b = 2437.2, sdr_range_fit(&a, &b);
    assert(fabs(a - 2436.6) < 1e-9 && fabs(b - 2437.6) < 1e-9);
    a = 2437.0004, b = 2440, sdr_range_fit(&a, &b);
    assert(a == 2437.0);

    sdr_model_init(&m);
    /* 2 MHz about BLE 38: 4 MS/s in 15.625 kHz bins, 64 to 192 shown. */
    sdr_model_range(&m, 2425, 2427);
    assert(!m.sweep && m.tune == 2426 && m.rate == 5);
    assert(m.k0 == 64 && m.n == 129 && m.lo_mhz == 2425 &&
           m.bin_mhz == 0.015625);
    assert(!strcmp(sdr_command(&m, cmd, sizeof cmd), "sdr spec 2426 5 256"));
    spec_at(line, sizeof line, 2426, 4000000, 256, 100, 160, 180);
    assert(sdr_model_line(&m, line) == 1);
    assert(m.db[sdr_model_bin(&m, 2426.5)] == 90.0f && m.db[0] == 50.0f);
    spec_line(line, sizeof line, 2426, 256, 100, 0, 100);   /* 40 MS/s */
    assert(sdr_model_line(&m, line) == 0 && m.spectra == 1);
    sdr_model_field(&m, SDR_F_CENTER, t, sizeof t);
    assert(!strcmp(t, "2426"));
    sdr_model_field(&m, SDR_F_SPAN, t, sizeof t);
    assert(!strcmp(t, "2"));
    sdr_model_field(&m, SDR_F_START, t, sizeof t);
    assert(!strcmp(t, "2425"));
    sdr_model_field(&m, SDR_F_STOP, t, sizeof t);
    assert(!strcmp(t, "2427"));
    /* Off the whole MHz by a quarter: 6.5 MHz wants 7 of the 8 MS/s. */
    sdr_model_range(&m, 2430, 2436.5);
    assert(!m.sweep && m.tune == 2433 && m.rate == 4);
    sdr_model_range(&m, 2430, 2440);
    assert(m.rate == 3 && m.k0 == 0 && m.n == 256);
    sdr_model_range(&m, 2420, 2440);         /* one capture: live too */
    assert(m.rate == 2 && sdr_model_is_live(&m));
    sdr_model_range(&m, 2425, 2449);
    assert(!m.sweep && m.rate == 1 && sdr_model_is_live(&m));
    /* Past 24 MHz a 40 MS/s capture is no longer flat: swept instead. */
    sdr_model_range(&m, 2425, 2450);
    assert(m.sweep && m.first == 2433 && m.last == 2449);
    sdr_model_range(&m, 2400, 2440);
    assert(m.sweep && m.first == 2408 && m.last == 2440 && m.n == 128);
    /* A version 1 recording held 40 MHz in one spectrum. */
    m.one_max = SDR_ONE_V1;
    sdr_model_range(&m, 2417, 2457);
    assert(!m.sweep && m.rate == 1 && m.k0 == 0 && m.n == 256);
    m.one_max = SDR_LIVE_SPAN;

    /* Wider, swept from the first slice that reaches the start. */
    sdr_model_range(&m, 2417, 2458);
    assert(m.sweep && m.first == 2425 && m.last == 2457 && m.n == 131);
    assert(!strcmp(sdr_command(&m, cmd, sizeof cmd),
                   "sdr sweep 2425 2457 16 1 128"));
    spec_line(line, sizeof line, 2425, 128, 80, -1, 0);
    assert(sdr_model_line(&m, line) == 0);
    spec_line(line, sizeof line, 2457, 128, 80, -1, 0);
    assert(sdr_model_line(&m, line) == 0);
    assert(sdr_model_line(&m, "SWEEP 2") == 0 && !m.fresh);  /* one short */
    for (int c = m.first; c <= m.last; c += SDR_SLICE_STEP) {
        spec_line(line, sizeof line, c, 128, 80, -1, 0);
        assert(sdr_model_line(&m, line) == 0);
    }
    assert(sdr_model_line(&m, "SWEEP 3") == 1 && m.fresh);
    for (int j = 0; j < m.n; j++) assert(m.db[j] == 40.0f);  /* no gaps */
    /* All 500 MHz: 32 slices, the last moved down onto the board's top. */
    sdr_model_range(&m, 2200, 2700);
    assert(m.sweep && m.first == 2204 && m.last == 2700 && m.n == SDR_BINS);
    for (int c = m.first; c <= m.last; c += SDR_SLICE_STEP) {
        spec_line(line, sizeof line, c, 128, 80, -1, 0);
        assert(sdr_model_line(&m, line) == 0);
    }
    assert(sdr_model_line(&m, "SWEEP 32") == 1);
    for (int j = 0; j < m.n; j++) assert(m.db[j] == 40.0f);  /* no gaps */

    sdr_mhz_text(2437, t, sizeof t);
    assert(!strcmp(t, "2437"));
    sdr_mhz_text(2437.5, t, sizeof t);
    assert(!strcmp(t, "2437.5"));
    sdr_mhz_text(2402.125, t, sizeof t);
    assert(!strcmp(t, "2402.125"));
    sdr_mhz_text(0.5, t, sizeof t);
    assert(!strcmp(t, "0.5"));
    sdr_mhz_text(2437.0004, t, sizeof t);
    assert(!strcmp(t, "2437"));
    assert(sdr_mhz_parse("2437", &v) && v == 2437);
    assert(sdr_mhz_parse(" 2437.5 ", &v) && v == 2437.5);
    assert(sdr_mhz_parse("2.4375 GHz", &v) && v == 2437.5);
    assert(sdr_mhz_parse("500k", &v) && v == 0.5);
    assert(sdr_mhz_parse("40 MHz", &v) && v == 40);
    assert(sdr_mhz_parse("2426.5mhz", &v) && v == 2426.5);
    assert(!sdr_mhz_parse("", &v) && !sdr_mhz_parse("abc", &v));
    assert(!sdr_mhz_parse("2437,5", &v) && !sdr_mhz_parse(".", &v));
    assert(!sdr_mhz_parse("12x", &v) && !sdr_mhz_parse("1.2.3", &v));
    assert(!sdr_mhz_parse("-5", &v) && !sdr_mhz_parse("GHz", &v));
}

/** @brief One spectrum of the view's tuning, 40 MS/s in 256 bins, of the
 *         half-decibel values @p v, taken as the board's. */
static void frame_of(sdr_model_t *m, const unsigned char *v)
{
    char line[1200];
    int k = snprintf(line, sizeof line, "SPEC %d 40000000 76 256 ", m->tune);

    for (int j = 0; j < 256; j++)
        k += snprintf(line + k, sizeof line - (size_t)k, "%02x", v[j]);
    assert(sdr_model_line(m, line) == 1);
}

/** @brief Traces and markers, the model's way: an average over its count,
 *         the holds; peaks counted by how far they rise, a flat top once at
 *         its middle; markers on the highest, moved down and across them,
 *         out on the free peaks, read out against M1; a drifting peak
 *         followed unless held or no longer asked; a marker let go on a
 *         peak, kept on a flat floor; a new view; the trace each reads. */
static void marker_tests(void)
{
    sdr_model_t m;
    unsigned char v[256];
    int bins[16];
    char t[96];

    sdr_model_init(&m);
    assert(m.type[0] == SDR_T_AVG && m.type[1] == SDR_T_OFF &&
           m.type[2] == SDR_T_OFF && m.avg_count == 4);
    assert(!sdr_marks_out(&m) && m.track && m.mark_held == -1);
    m.one_max = SDR_ONE_V1;             /* 40 MHz in one spectrum, so its */
    sdr_model_range(&m, 2417, 2457);    /* bins are the board's own       */
    memset(v, 100, sizeof v);                   /* a 50 dB floor */
    frame_of(&m, v);
    v[10] = 180;                                /* 90 dB, once */
    frame_of(&m, v);
    assert(m.avg[10] == 60.0f && m.peak[10] == 90.0f && m.low[10] == 50.0f);
    assert(sdr_trace(&m, SDR_T_MAX) == m.peak && !sdr_trace(&m, SDR_T_OFF));
    sdr_model_clear(&m);
    assert(m.avg[10] == 90.0f && m.peak[10] == 90.0f && m.low[10] == 90.0f);

    m.type[0] = SDR_T_WRITE;                    /* markers on the latest */
    memset(v, 100, sizeof v);
    v[60] = 200;                                /* 100 dB */
    v[139] = v[140] = v[141] = 180;             /* 90 dB, flat on top */
    v[200] = 160;                               /* 80 dB */
    v[100] = 110;                               /* 5 dB: no peak */
    frame_of(&m, v);
    assert(sdr_peaks(&m, m.db, bins, 16) == 3 && bins[0] == 60 &&
           bins[1] == 140 && bins[2] == 200);
    assert(sdr_peaks(&m, m.db, bins, 2) == 2);

    sdr_mark_on(&m, 0);
    assert(sdr_marks_out(&m) && sdr_mark_bin(&m, 0) == 60);
    assert(sdr_mark_peak(&m, 0, SDR_TO_NEXT) && sdr_mark_bin(&m, 0) == 140);
    assert(sdr_mark_peak(&m, 0, SDR_TO_NEXT) && sdr_mark_bin(&m, 0) == 200);
    assert(!sdr_mark_peak(&m, 0, SDR_TO_NEXT) && sdr_mark_bin(&m, 0) == 200);
    assert(sdr_mark_peak(&m, 0, SDR_TO_LEFT) && sdr_mark_bin(&m, 0) == 140);
    assert(sdr_mark_peak(&m, 0, SDR_TO_LEFT) && sdr_mark_bin(&m, 0) == 60);
    assert(!sdr_mark_peak(&m, 0, SDR_TO_LEFT) && sdr_mark_bin(&m, 0) == 60);
    assert(sdr_mark_peak(&m, 0, SDR_TO_RIGHT) && sdr_mark_bin(&m, 0) == 140);
    assert(sdr_mark_peak(&m, 0, SDR_TO_HIGHEST) && sdr_mark_bin(&m, 0) == 60);
    assert(!sdr_mark_peak(&m, 1, SDR_TO_HIGHEST));      /* not out */
    sdr_mark_text(&m, 0, t, sizeof t);
    assert(!strcmp(t, "M1  2426.375 MHz  100.0 dB"));

    /* More come out on the free peaks, highest first, then the middle. */
    sdr_mark_on(&m, 1);
    sdr_mark_on(&m, 2);
    sdr_mark_on(&m, 3);
    assert(sdr_mark_bin(&m, 1) == 140 && sdr_mark_bin(&m, 2) == 200 &&
           sdr_mark_bin(&m, 3) == 128);
    sdr_mark_text(&m, 1, t, sizeof t);
    assert(!strcmp(t, "M2  2438.875 MHz   90.0 dB   Δ +12.500 MHz  -10.0 dB"));
    sdr_mark_off(&m, 0);
    assert(!m.mark[0].on && sdr_marks_out(&m));
    sdr_mark_text(&m, 1, t, sizeof t);
    assert(!strcmp(t, "M2  2438.875 MHz   90.0 dB"));
    for (int k = 1; k < SDR_MARKERS; k++) sdr_mark_off(&m, k);
    assert(!sdr_marks_out(&m));

    /* Out on a peak that then drifts three bins a frame: followed. */
    memset(v, 100, sizeof v);
    v[60] = 200;
    frame_of(&m, v);
    sdr_mark_on(&m, 0);
    assert(sdr_mark_bin(&m, 0) == 60);
    for (int at = 63; at <= 69; at += 3) {
        memset(v, 100, sizeof v);
        v[at] = 200;
        frame_of(&m, v);
        assert(sdr_mark_bin(&m, 0) == at);
    }
    memset(v, 100, sizeof v);
    v[72] = 200;
    m.mark_held = 0;                            /* the pointer has it */
    frame_of(&m, v);
    assert(sdr_mark_bin(&m, 0) == 69);
    m.mark_held = -1;
    m.track = 0;
    frame_of(&m, v);
    assert(sdr_mark_bin(&m, 0) == 69);
    m.track = 1;
    frame_of(&m, v);
    assert(sdr_mark_bin(&m, 0) == 72);

    /* By hand: where it is let go, on the peak there while following,
     * and kept where it is on a flat floor. */
    sdr_mark_at(&m, 0, sdr_model_freq(&m, 75), 0);
    assert(sdr_mark_bin(&m, 0) == 75);
    sdr_mark_at(&m, 0, sdr_model_freq(&m, 75), 1);
    assert(sdr_mark_bin(&m, 0) == 72);
    sdr_mark_at(&m, 0, sdr_model_freq(&m, 30), 1);
    assert(sdr_mark_bin(&m, 0) == 30);
    m.track = 0;
    sdr_mark_at(&m, 0, sdr_model_freq(&m, 75), 1);
    assert(sdr_mark_bin(&m, 0) == 75);
    m.track = 1;

    /* A new view keeps its frequency; outside it, the readout says so and
     * a peak move starts from the highest. */
    sdr_model_range(&m, 2460, 2500);
    sdr_mark_text(&m, 0, t, sizeof t);
    assert(!strcmp(t, "M1  2428.719 MHz"));
    memset(v, 100, sizeof v);
    v[50] = 190;
    frame_of(&m, v);
    assert(sdr_mark_bin(&m, 0) == -1 && m.mark[0].on && !m.mark[1].on);
    sdr_mark_text(&m, 0, t, sizeof t);
    assert(!strcmp(t, "M1  2428.719 MHz  outside the view"));
    assert(sdr_mark_peak(&m, 0, SDR_TO_NEXT) && sdr_mark_bin(&m, 0) == 50);

    /* Each marker reads its own trace; one that is off, the latest. */
    assert(sdr_mark_trace(&m, 0) == m.db);
    m.mark[0].trace = 1;
    m.type[1] = SDR_T_MAX;
    assert(sdr_mark_trace(&m, 0) == m.peak);
    m.type[1] = SDR_T_OFF;
    assert(sdr_mark_trace(&m, 0) == m.db);
}

/** @brief The graticule's scale: its top on whole tens over the traces,
 *         the narrowest division that holds them, wider at once, narrower
 *         only with a fifth to spare, so it does not flap. */
static void scale_tests(void)
{
    sdr_model_t m;
    unsigned char v[256];

    sdr_model_init(&m);
    m.type[0] = SDR_T_WRITE;
    memset(v, 100, sizeof v);                   /* 50 dB */
    for (int j = 0; j < 256; j += 8) v[j] = 112;    /* 56 dB, here and there */
    frame_of(&m, v);
    sdr_model_scale(&m);
    assert(m.scale_hi == 60.0f && m.div == 2.0f && m.scale_lo == 40.0f);
    v[60] = 190;                                /* 95 dB: wider at once */
    frame_of(&m, v);
    sdr_model_scale(&m);
    assert(m.scale_hi == 100.0f && m.div == 10.0f && m.scale_lo == 0.0f);
    v[60] = 100;                                /* gone: room to spare */
    frame_of(&m, v);
    sdr_model_scale(&m);
    assert(m.scale_hi == 60.0f && m.div == 2.0f);
    m.div = 5.0f;                               /* 19 dB to hold: kept */
    memset(v, 94, sizeof v);
    for (int j = 0; j < 256; j += 8) v[j] = 112;
    frame_of(&m, v);
    sdr_model_scale(&m);
    assert(m.scale_hi == 60.0f && m.div == 5.0f);
    memset(v, 104, sizeof v);                   /* 14 dB: narrower */
    for (int j = 0; j < 256; j += 8) v[j] = 112;
    frame_of(&m, v);
    sdr_model_scale(&m);
    assert(m.div == 2.0f && m.floor_db == 40.0f);  /* 20 dB of colour */
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
    assert(tiku_sdr_app.pick(st, st->id, CMD_LIVE) == 0);
    say(master, PROMPT);                    /* the spec in flight, done */
    ticks(st, now, 2);
    board_read(master, got, sizeof got);
    assert(!strcmp(got, "sdr sweep 2404 2484 16 1 128\r"));
    for (int c = 2404; c <= 2484; c += SDR_SLICE_STEP) {
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
    assert(file_has(path, "# TikuSDR recording 2\n# board: c61 (esp32c61)\n"));
    assert(file_has(path, "\n0.000 VIEW range 2425 2449\n"));
    assert(file_has(path, " SPEC 2437 40000000 76 256 "));
    assert(file_has(path, " VIEW range 2396 2492\n"));
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
    /* IHDR's size: the instrument alone, from the panel's right to the
     * window's, above the bar at its foot. */
    assert(head[18] * 256 + head[19] == SDR_W - 222);
    assert(head[22] * 256 + head[23] == SDR_H - 34);
    unlink(csv);
    unlink(png);
}

/** @brief A key, as the desktop sends one: its code and its text. */
static int key_in(sdr_t *st, unsigned key, const char *text)
{
    tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN, .key = key};

    snprintf(e.text, sizeof e.text, "%s", text ? text : "");
    return tiku_sdr_app.event(st, &e);
}

/** @brief A click at (@p x, @p y). */
static void click_at(sdr_t *st, int x, int y)
{
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                      .x = x, .y = y};

    assert(tiku_sdr_app.event(st, &e) == 0);
    e.type = TIKU_EVENT_POINTER_UP;
    assert(tiku_sdr_app.event(st, &e) == 0);
}

/** @brief A click into field @p i, which selects all of it. */
static void click_field(sdr_t *st, int i)
{
    tiku_rect_t r = sdr_control_rect(st, CMD_FIELD + i);

    click_at(st, r.x + r.w / 2, r.y + r.h / 2);
    assert(editing(st) == i && st->field[i].sel_a == 0 &&
           st->field[i].sel_b == st->field[i].len);
}

/** @brief @p text typed over field @p i, then Return: a click into it,
 *         unless it still has the focus from its last Return, which left
 *         all of it selected too. */
static void type_field(sdr_t *st, int i, const char *text)
{
    if (editing(st) != i) click_field(st, i);
    assert(st->field[i].sel_a == 0 && st->field[i].sel_b == st->field[i].len);
    for (const char *p = text; *p; p++) {
        char c[2] = {*p, '\0'};
        assert(key_in(st, (unsigned char)*p, c) == 0);
    }
    assert(key_in(st, TIKU_KEY_RETURN, NULL) == 0 && editing(st) == i);
}

/** @brief The frequency fields typed in, a board running: Center and Span
 *         keep each other, Start and Stop the other end, or the span when
 *         passed; the board is asked for the view as soon as it is free;
 *         what is no frequency is said; Tab and a press elsewhere apply
 *         what was typed, Escape leaves it, and keys in a field are text. */
static void fields(sdr_t *st, int master, int64_t *now)
{
    sdr_model_t *m = &st->model;
    tiku_rect_t plot, fall;
    char got[256], line[1200];

    type_field(st, SDR_F_CENTER, "2426");           /* Band's 96 MHz */
    assert(m->start == 2378 && m->stop == 2474 && m->sweep);
    assert(!strcmp(st->field[SDR_F_CENTER].text, "2426"));
    type_field(st, SDR_F_SPAN, "2");
    assert(m->start == 2425 && m->stop == 2427 && m->rate == 5);
    say(master, "SWEEP 6" PROMPT);                  /* the one in flight */
    ticks(st, now, 2);
    board_read(master, got, sizeof got);
    assert(!strcmp(got, "sdr spec 2426 5 256\r"));
    spec_at(line, sizeof line, 2426, 4000000, 256, 100, 160, 180);
    strcat(line, PROMPT);
    say(master, line);
    ticks(st, now, 3);
    assert(m->fresh && m->db[sdr_model_bin(m, 2426.5)] == 90.0f);
    board_read(master, got, sizeof got);
    assert(!strcmp(got, "sdr spec 2426 5 256\r"));

    type_field(st, SDR_F_START, "2400");            /* 27 MHz: swept */
    assert(m->start == 2400 && m->stop == 2427 && m->sweep);
    type_field(st, SDR_F_STOP, "2.492G");
    assert(m->start == 2400 && m->stop == 2492 && m->sweep);
    type_field(st, SDR_F_STOP, "2390");             /* below the start */
    assert(m->start == 2298 && m->stop == 2390);
    type_field(st, SDR_F_CENTER, "abc");
    assert(!strcmp(st->note, "abc is not a frequency.") && m->start == 2298);
    assert(!strcmp(st->field[SDR_F_CENTER].text, "2344"));

    click_field(st, SDR_F_SPAN);                    /* 'b' is text here */
    assert(key_in(st, 'b', "b") == 0 && !is_band(m));
    assert(!strcmp(st->field[SDR_F_SPAN].text, "b"));
    assert(key_in(st, TIKU_KEY_ESCAPE, NULL) == 0 && editing(st) < 0);
    assert(m->stop - m->start == 92);

    click_field(st, SDR_F_CENTER);
    assert(key_in(st, '2', "2") == 0 && key_in(st, '4', "4") == 0);
    assert(key_in(st, '4', "4") == 0 && key_in(st, '4', "4") == 0);
    assert(key_in(st, TIKU_KEY_TAB, NULL) == 0 && editing(st) == SDR_F_SPAN);
    assert(m->start == 2398 && m->stop == 2490);
    assert(key_in(st, TIKU_KEY_ESCAPE, NULL) == 0 && editing(st) < 0);
    click_field(st, SDR_F_CENTER);
    assert(key_in(st, '2', "2") == 0 && key_in(st, '4', "4") == 0);
    assert(key_in(st, '0', "0") == 0 && key_in(st, '0', "0") == 0);
    sdr_layout(st, &plot, &fall);
    click_at(st, plot.x + 10, plot.y + 10);         /* a press elsewhere */
    assert(editing(st) < 0 && m->start == 2354 && m->stop == 2446);

    show(st, SDR_BAND_START, SDR_BAND_STOP);
    assert(is_band(m) && !strcmp(sdr_command(m, line, sizeof line),
                                 "sdr sweep 2404 2484 16 1 128"));
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
    assert(st->model.tune == 2437 && sdr_model_is_live(&st->model));
    assert(tiku_sdr_app.pick(st, st->id, CMD_PAUSE) == 0 && !st->paused);
    ticks(st, now, 40);
    assert(!st->play.file && is_band(&st->model));
    assert(st->model.spectra == 2);
    assert(!strcmp(st->note, "End of rec.sdrlog: 0:00, 2 spectra."));
    assert(sdr_enabled(st, CMD_LIVE) && !strcmp(st->shown, path));

    /* Its CSV is the whole recording, converted over ticks: one live
     * spectrum of 153 bins (24 MHz) and one sweep of 307. */
    snprintf(path, sizeof path, "%s/rec.csv", dir);
    assert(tiku_sdr_app.pick(st, st->id, CMD_EXPORT) == 0);
    assert(!strcmp(asked_name, "rec.csv"));
    tiku_sdr_app.picked(st, st->id, path);
    assert(st->csv.in && !strncmp(st->note, "Exporting rec.csv", 17));
    assert(!sdr_enabled(st, CMD_EXPORT));
    ticks(st, now, 3);
    assert(!st->csv.in);
    assert(!strncmp(st->note, "Exported rec.csv: 2 spectra, 460 rows, ", 39));
    assert(file_lines(path) == 461);
    assert(file_has(path, ",2437.00000,") && file_has(path, ",2396.00000,"));
    unlink(path);
    /* Tuned, the screen is no longer the recording's. */
    assert(tiku_sdr_app.pick(st, st->id, CMD_TUNE + 26) == 0);
    assert(!st->shown[0] && !sdr_enabled(st, CMD_EXPORT));
}

/** @brief A recording double-clicked in Tracker: TikuSDR starts with it
 *         and plays it, needing no board and borrowing none connected
 *         meanwhile; the desktop's current device is followed only to an
 *         ESP32-C61. */
static void opened_with(const tiku_app_services_t *services, const char *dir)
{
    tiku_app_services_t with = *services;
    char path[512];
    void *state = NULL;
    sdr_t *st;
    int64_t now = 0;
    int before = released;

    snprintf(path, sizeof path, "%s/rec.sdrlog", dir);
    with.startup_document = path;
    with.board_names = names_told;
    with.board_mcu = mcu_named;
    with.current_device = current_named;
    told = "";                              /* no board at all: none needed */
    assert(tiku_sdr_app.start(&state, &with) == 0);
    st = state;
    assert(st->play.file && !strcmp(st->shown, path));
    assert(!strncmp(st->note, "Playing rec.sdrlog (c61, 20", 27));
    ticks(st, &now, 1);
    assert(!st->looking && !st->alert.open);
    /* No radio: Connect is offered greyed, and asked anyway it says so in
     * the status line, the recording left on screen. */
    assert(offered(CMD_CONNECT) == 0);
    assert(tiku_sdr_app.pick(st, st->id, CMD_CONNECT) == 0);
    assert(!st->alert.open && st->play.file && released == before &&
           !strcmp(st->note, "No software-defined radio is connected."));
    told = "esp32-c61\nesp32-c61-2\nnrf54l15\n";
    tiku_sdr_app.board_said(st, TIKU_APP_BOARD_CURRENT, "nrf54l15");
    assert(!strcmp(st->board, "esp32-c61"));
    tiku_sdr_app.board_said(st, TIKU_APP_BOARD_CURRENT, "esp32-c61-2");
    assert(!strcmp(st->board, "esp32-c61-2"));
    assert(!strcmp(last_menus.menu[5].title, "Device") &&
           last_menus.menu[5].nitem == 2 && last_menus.menu[5].item[1].marked &&
           last_menus.menu[5].item[0].enabled);
    ticks(st, &now, 20);
    assert(st->state == SDR_IDLE && released == before);
    assert(offered(CMD_CONNECT) == 1);      /* there now, for the asking */
    tiku_sdr_app.stop(st);
    told = NULL;
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
    assert(st->model.spectra == 1 && st->model.tune == 2426);
    /* Version 1: Live was 40 MHz in one spectrum, and plays so. */
    assert(!st->model.sweep && st->model.stop - st->model.start == SDR_ONE_V1);
    ticks(st, now, 12);                     /* 99.9 s of silence, cut */
    assert(st->model.spectra == 2 && !st->play.file);
    assert(st->model.one_max == SDR_LIVE_SPAN);     /* the board's again */

    assert(tiku_sdr_app.pick(st, st->id, CMD_REPLAY) == 0);
    tiku_sdr_app.picked(st, st->id, gap);
    assert(st->play.file);
    assert(tiku_sdr_app.pick(st, st->id, CMD_REPLAY) == 0);
    assert(!st->play.file && !strcmp(st->note, "Closed gap.sdrlog."));
    unlink(junk);
    unlink(gap);
}

/** @brief A press on alert button @p button, released @p off pixels away. */
static int click_alert(sdr_t *st, int button, int off)
{
    tiku_rect_t f = sdr_alert_rect(st);
    int bx, by, bw, bh, done;
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1};

    tiku_alert_button_rect(&st->alert, button, f.w, f.h, &bx, &by, &bw, &bh);
    e.x = f.x + bx + bw / 2;
    e.y = f.y + by + bh / 2;
    assert(tiku_sdr_app.event(st, &e) == 0);
    e.type = TIKU_EVENT_POINTER_MOVE;
    e.y += off;
    assert(tiku_sdr_app.event(st, &e) == 0);
    e.type = TIKU_EVENT_POINTER_UP;
    done = tiku_sdr_app.event(st, &e);
    return done;
}

/** @brief No ESP32-C61 among the boards: nothing said until the desktop
 *         has said which boards it has, then an alert with the stop icon
 *         and one button over a toolbar with no Connect; only Quit works
 *         under it, and OK, by hand, Return or Escape, ends TikuSDR, a
 *         press taken off OK answering nothing; a radio connected while it
 *         is up is borrowed, the alert gone. */
static void no_esp32(const tiku_app_services_t *services)
{
    tiku_app_services_t without = *services;
    tiku_event_t ret = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_RETURN};
    tiku_event_t esc = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_ESCAPE};
    void *state = NULL;
    sdr_t *st;
    int64_t now = 0;
    int before = released;

    assert(receiver_part("ESP32-C61") && receiver_part("esp32c61"));
    assert(receiver_part("esp32-c61-ttyUSB1") && receiver_part("ESP32_C61"));
    assert(!receiver_part("ESP32-C6") && !receiver_part("nRF54L15"));
    assert(!receiver_part("") && !receiver_part("ESP32"));
    assert(listed("a\nb\n", "b") && listed("a\nb", "b") && !listed("ab\n", "a"));
    assert(!listed("a\n", "") && !listed("", "a"));

    without.board_names = names_told;
    without.board_mcu = mcu_named;
    without.current_device = current_named;
    told = NULL;                    /* just started, out of process */
    assert(tiku_sdr_app.start(&state, &without) == 0);
    st = state;
    ticks(st, &now, 2);
    assert(st->looking && !st->alert.open && !st->note[0]);
    told = "nrf54l15\n";            /* said: a board, but not one with it */
    ticks(st, &now, 1);
    assert(!st->looking && st->alert.open && st->alert.kind == TIKU_ALERT_STOP);
    assert(!strncmp(st->alert.text, "No software-defined radio detected. ",
                    36));
    assert(strstr(st->alert.text, "supports the ESP32-C61 as its SDR. ") &&
           strstr(st->alert.text, "through the Devices menu."));
    assert(st->alert.nbutton == 1 && !strcmp(st->alert.button[0], "OK") &&
           st->alert.dflt == 0 && st->alert.cancel == 0);
    assert(!st->names[0] && !st->board[0] && released == before);
    assert(offered(CMD_CONNECT) == 0 && offered(CMD_REPLAY) == 0 &&
           offered(CMD_QUIT) == 1 && !sdr_enabled(st, CMD_LIVE));
    assert(!strcmp(last_menus.menu[last_menus.nmenu - 1].title, "Tune"));
    assert(tiku_sdr_app.pick(st, st->id, CMD_LIVE) == 0 &&
           sdr_model_is_live(&st->model) && st->alert.open);
    assert(tiku_sdr_app.pick(st, st->id, CMD_CONNECT) == 0 &&
           released == before && st->alert.open);
    sdr_paint(st);
    assert(tiku_surface_png(st->surface, "build/tests/test_sdr_alert.png") == 0 ||
           tiku_surface_png(st->surface, "test_sdr_alert.png") == 0);
    {
        /* The stop icon on the alert's stripe: red beside its cross. */
        tiku_rect_t f = sdr_alert_rect(st);
        assert(tiku_peek(st->surface, f.x + 3 + 26 - 10, f.y + 36) ==
               TIKU_C_DANGER);
    }
    /* The panel holds no Connect -- the SDR menu's alone -- and under the
     * alert nothing in it works: the bar's Pause first, then the
     * sections, every control inside the panel and above the bar, the
     * lists filled. */
    assert(st->dialog.count > SDR_FIELDS + SDR_CHOICES &&
           st->dialog.control[0].id == CMD_PAUSE &&
           st->dialog.focus == TIKU_DIALOG_NONE);
    for (int i = 0; i < st->dialog.count; i++) {
        const tiku_dialog_control_t *c = &st->dialog.control[i];
        int bar = c->id == CMD_PAUSE || c->id == CMD_RECORD;
        assert(c->id != CMD_CONNECT && !c->enabled);
        assert(c->rect.x >= 8 && c->rect.x + c->rect.w <= 208);
        assert(bar ? c->rect.y > st->h - 30
                   : c->rect.y + c->rect.h < st->h - 30);
    }
    for (int c = 0; c < SDR_CHOICES; c++)
        assert(!st->choice[c].enabled && st->choice[c].count >= 3);

    /* Every answer is OK, and OK ends TikuSDR; a press taken off it does
     * not.  The alert stays up behind each, the client ending the app. */
    assert(tiku_sdr_app.event(st, &ret) == 1 && st->alert.open);
    assert(tiku_sdr_app.event(st, &esc) == 1 && st->alert.open);
    assert(click_alert(st, 0, 60) == 0 && st->alert.open);  /* taken back */
    assert(click_alert(st, 0, 0) == 1);                     /* OK */

    /* One connected through Devices while it is up: borrowed by itself. */
    told = "esp32-c61\nnrf54l15\n";
    ticks(st, &now, 1);
    assert(!st->alert.open && !strcmp(st->names, "esp32-c61\n") &&
           !strcmp(st->board, "esp32-c61"));
    assert(st->state == SDR_RELEASING && released == before + 1 &&
           !strcmp(lent, "esp32-c61"));
    tiku_sdr_app.stop(st);

    /* Two connected while it is up: the desktop's current device is the
     * one borrowed, and the Device menu lists the two, the nRF never. */
    told = "nrf54l15\n";
    assert(tiku_sdr_app.start(&state, &without) == 0);
    st = state;
    ticks(st, &now, 1);
    assert(st->alert.open && released == before + 1);
    told = "esp32-c61\nesp32-c61-2\nnrf54l15\n";
    current = "esp32-c61-2";
    ticks(st, &now, 1);
    assert(!st->alert.open && st->state == SDR_RELEASING &&
           !strcmp(lent, "esp32-c61-2") && released == before + 2);
    assert(!strcmp(last_menus.menu[5].title, "Device") &&
           last_menus.menu[5].nitem == 2 && last_menus.menu[5].item[1].marked);
    tiku_sdr_app.stop(st);
    current = "";
    told = NULL;
}

/** @brief The pointer at (@p x, @p y): @p type, the first button. */
static void pointer_at(sdr_t *st, int type, int x, int y)
{
    tiku_event_t e = {.type = type, .button = 1, .x = x, .y = y};

    assert(tiku_sdr_app.event(st, &e) == 0);
}

/** @brief The plot column over bin @p b of the trace. */
static int column(sdr_t *st, int b)
{
    tiku_rect_t plot, fall;

    sdr_layout(st, &plot, &fall);
    return plot.x + (2 * b + 1) * plot.w / (2 * st->model.n);
}

/** @brief The middle of toolbar control @p id. */
static void click_control(sdr_t *st, int id)
{
    sdr_controls(st);
    for (int i = 0; i < st->dialog.count; i++) {
        tiku_rect_t r = st->dialog.control[i].rect;
        if (st->dialog.control[i].id != id) continue;
        click_at(st, r.x + r.w / 2, r.y + r.h / 2);
        return;
    }
    assert(0);
}

/** @brief List @p c opened by a press and moved @p down rows by key, then
 *         Return: what it says is chosen. */
static void choose(sdr_t *st, int c, int down)
{
    tiku_rect_t r = sdr_control_rect(st, CMD_CHOICE + c);

    click_at(st, r.x + 10, r.y + r.h / 2);
    assert(st->choice[c].open);
    for (int i = 0; i < down; i++) assert(key_in(st, TIKU_KEY_DOWN, NULL) == 0);
    assert(key_in(st, TIKU_KEY_RETURN, NULL) == 0 && !st->choice[c].open);
}

/** @brief The window with no board.  Traces: types from the lists or the
 *         menu, the count typed, every level under the pointer.  Markers:
 *         out on the highest peak, Next Peak, a drag let go on a peak,
 *         another from the list, a press near one, a frequency typed,
 *         Delete, All Off, To Center.  Display: the spectrogram off, the
 *         plans on.  In a sweep, markers take a press instead of tuning. */
static void drawn(const tiku_app_services_t *services)
{
    void *state = NULL;
    sdr_t *st;
    sdr_model_t *m;
    tiku_rect_t plot, fall, was, r;
    unsigned char v[256];
    char line[1200];
    int mid;

    assert(tiku_sdr_app.start(&state, services) == 0);
    st = state;
    m = &st->model;
    assert(st->fall_on && !st->plans_on && !sdr_marks_out(m));
    assert(!sdr_enabled(st, CMD_NEXT_PEAK) &&
           !sdr_enabled(st, CMD_FIELD + SDR_F_MARK));
    assert(st->dialog.focus == CMD_PAUSE);  /* never a field by itself */
    memset(v, 100, sizeof v);
    v[m->k0 + 60] = 200;                    /* the view's bins 60 and 140 */
    v[m->k0 + 140] = 180;
    frame_of(m, v);
    sdr_layout(st, &plot, &fall);
    mid = plot.y + plot.h / 2;
    assert(fall.y < plot.y && fall.h > 0);      /* above the spectrum */

    /* Trace 2 a max hold from the lists, trace 3 the latest from the
     * menu; the average over eight; all three said under the pointer. */
    choose(st, SDR_C_TRACE, 1);
    assert(st->trace_sel == 1 && marked(CMD_TRACE_SEL + 1) == 1);
    choose(st, SDR_C_TYPE, 3);
    assert(m->type[1] == SDR_T_MAX && marked(CMD_TYPE + SDR_T_MAX) == 1);
    assert(tiku_sdr_app.pick(st, st->id, CMD_TRACE_SEL + 2) == 0);
    assert(tiku_sdr_app.pick(st, st->id, CMD_TYPE + SDR_T_WRITE) == 0);
    assert(m->type[2] == SDR_T_WRITE && st->trace_sel == 2);
    type_field(st, SDR_F_AVG, "8");
    assert(m->avg_count == 8);
    type_field(st, SDR_F_AVG, "0");
    assert(m->avg_count == 8 && strstr(st->note, "1 to 100"));
    assert(key_in(st, TIKU_KEY_ESCAPE, NULL) == 0 && editing(st) < 0);
    pointer_at(st, TIKU_EVENT_POINTER_MOVE, column(st, 60), mid);
    assert(strstr(st->hover, "1: avg 100.0 dB · 2: max 100.0 dB · 3: 100.0"));

    /* Active puts the marker out on the highest peak; Next Peak moves it
     * down to the next. */
    click_control(st, CMD_MARK_ACTIVE);
    assert(m->mark[0].on && sdr_mark_bin(m, 0) == 60);
    assert(marked(CMD_MARK_ACTIVE) == 1 && marked(CMD_TRACK) == 1);
    assert(offered(CMD_PEAK_SEARCH) == 1 &&
           sdr_enabled(st, CMD_FIELD + SDR_F_MARK));
    click_control(st, CMD_NEXT_PEAK);
    assert(sdr_mark_bin(m, 0) == 140);

    /* Pressed away from it, the selected marker comes to the pointer and
     * is dragged; let go, it climbs onto the peak there. */
    pointer_at(st, TIKU_EVENT_POINTER_DOWN, column(st, 100), mid);
    assert(m->mark_held == 0 && sdr_mark_bin(m, 0) == 100);
    pointer_at(st, TIKU_EVENT_POINTER_MOVE, column(st, 120), mid);
    assert(sdr_mark_bin(m, 0) == 120);
    pointer_at(st, TIKU_EVENT_POINTER_UP, column(st, 137), mid);
    assert(m->mark_held == -1 && sdr_mark_bin(m, 0) == 140);

    /* Marker 2 from the list, out by key on the free peak; a press beside
     * M1 takes M1 up; its frequency typed; Delete takes it in. */
    choose(st, SDR_C_MARKER, 1);
    assert(m->mark_sel == 1 && !sdr_enabled(st, CMD_NEXT_PEAK));
    assert(key_in(st, 'm', "m") == 0 && sdr_mark_bin(m, 1) == 60);
    pointer_at(st, TIKU_EVENT_POINTER_DOWN, column(st, 141), mid);
    pointer_at(st, TIKU_EVENT_POINTER_UP, column(st, 141), mid);
    assert(m->mark_sel == 0 && sdr_mark_bin(m, 0) == 140 &&
           sdr_mark_bin(m, 1) == 60);
    type_field(st, SDR_F_MARK, "2430");
    assert(sdr_mark_bin(m, 0) == 31);
    assert(key_in(st, TIKU_KEY_ESCAPE, NULL) == 0);
    assert(key_in(st, TIKU_KEY_DELETE, NULL) == 0);
    assert(!m->mark[0].on && m->mark[1].on && sdr_marks_out(m));
    click_control(st, CMD_MARK_CLEAR);
    assert(!sdr_marks_out(m) && offered(CMD_MARK_CLEAR) == 0);

    /* The spectrogram off: the spectrum takes its room; the plans on. */
    sdr_layout(st, &was, &fall);
    click_control(st, CMD_WATERFALL);
    sdr_layout(st, &plot, &fall);
    assert(!st->fall_on && fall.h == 0 && plot.y < was.y &&
           plot.h > was.h * 3 / 2 && marked(CMD_WATERFALL) == 0);
    r = sdr_control_rect(st, CMD_WATERFALL);
    sdr_paint(st);
    assert(tiku_peek(st->surface, r.x + r.w - 34 + 8, r.y + 2 + 9) !=
           TIKU_C_SELECT);
    assert(key_in(st, 'w', "w") == 0 && st->fall_on && marked(CMD_WATERFALL));
    sdr_paint(st);
    assert(tiku_peek(st->surface, r.x + r.w - 34 + 8, r.y + 2 + 9) ==
           TIKU_C_SELECT);
    sdr_layout(st, &was, &fall);
    click_control(st, CMD_PLANS);
    sdr_layout(st, &plot, &fall);
    assert(st->plans_on && plot.h < was.h && marked(CMD_PLANS) == 1);

    /* Marker to Center: the view moved under it, its span kept. */
    assert(key_in(st, '1', "1") == 0 && m->mark_sel == 0);
    assert(key_in(st, 'm', "m") == 0 && sdr_mark_bin(m, 0) == 60);
    assert(tiku_sdr_app.pick(st, st->id, CMD_MARK_CENTER) == 0);
    assert(m->start == 2422.5 && m->stop == 2446.5);

    /* In a sweep a press on the spectrum moves a marker, not the view;
     * one on the spectrogram still tunes Live there. */
    assert(tiku_sdr_app.pick(st, st->id, CMD_LIVE) == 0);
    for (int c = m->first; c <= m->last; c += SDR_SLICE_STEP) {
        spec_line(line, sizeof line, c, 128, 80, c == 2420 ? 83 : -1, 200);
        assert(sdr_model_line(m, line) == 0);
    }
    assert(sdr_model_line(m, "SWEEP 6") == 1 && sdr_marks_out(m));
    sdr_layout(st, &plot, &fall);
    mid = plot.y + plot.h / 2;
    pointer_at(st, TIKU_EVENT_POINTER_DOWN, column(st, 20), mid);
    pointer_at(st, TIKU_EVENT_POINTER_UP, column(st, 20), mid);
    assert(is_band(m) && sdr_mark_bin(m, 0) == 20);
    hover(st, column(st, 20), fall.y + 4);
    assert(strstr(st->hover, "click to tune here"));
    hover(st, column(st, 20), mid);
    assert(!strstr(st->hover, "click to tune here"));
    pointer_at(st, TIKU_EVENT_POINTER_DOWN, column(st, 20), fall.y + 4);
    pointer_at(st, TIKU_EVENT_POINTER_UP, column(st, 20), fall.y + 4);
    assert(sdr_model_is_live(m));

    /* Live's light: green over one capture, grey over a sweep; pressed,
     * Live goes to the last view of the other kind, by hand or by key. */
    {
        double a = m->start, b = m->stop;
        int lx, ly;

        r = sdr_control_rect(st, CMD_LIVE);
        lx = r.x + (r.w - 16 - tiku_text_width(tiku_font_plain(), "Live")) /
             2 + 5;
        ly = r.y + r.h / 2;
        sdr_paint(st);
        publish(st);                    /* as the tick does on a change */
        assert(tiku_peek(st->surface, lx, ly) == LIVE_ON && marked(CMD_LIVE));
        click_control(st, CMD_LIVE);
        assert(is_band(m) && !sdr_model_is_live(m) && !marked(CMD_LIVE));
        sdr_paint(st);
        assert(tiku_peek(st->surface, lx, ly) == LIVE_OFF);
        type_field(st, SDR_F_STOP, "2500");       /* another wide view */
        assert(key_in(st, TIKU_KEY_ESCAPE, NULL) == 0 && m->stop == 2500);
        assert(key_in(st, 'l', "l") == 0 && m->start == a && m->stop == b);
        assert(key_in(st, 'l', "l") == 0 && m->start == SDR_BAND_START &&
               m->stop == 2500);
    }
    tiku_sdr_app.stop(st);
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
    range_tests();
    marker_tests();
    scale_tests();
    assert(mkdtemp(dir));
    snprintf(rec, sizeof rec, "%s/rec.sdrlog", dir);
    snprintf(rec2, sizeof rec2, "%s/rec2.sdrlog", dir);
    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(master >= 0 && !grantpt(master) && !unlockpt(master));
    assert(!ptsname_r(master, slave_path, sizeof slave_path));

    no_esp32(&services);
    drawn(&services);
    released = 0;
    assert(tiku_sdr_app.start(&state, &services) == 0 && state);
    st = state;
    assert(frames >= 1 && menus_seen >= 1 && !strcmp(st->board, "c61"));
    assert(st->state == SDR_IDLE && st->note[0] == '\0');    /* nothing said */
    assert(st->looking && !st->alert.open && !released);  /* nothing touched */
    assert(!sdr_enabled(st, CMD_EXPORT) && !sdr_enabled(st, CMD_PICTURE));
    assert(offered(CMD_RECORD) == 0 && offered(CMD_EXPORT) == 0);
    assert(!strcmp(last_menus.menu[1].title, "File"));

    ticks(st, &now, 1);             /* an ESP32-C61 there: borrowed */
    assert(!st->looking && !st->alert.open);
    assert(st->state == SDR_RELEASING && released == 1 && !strcmp(lent, "c61"));
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
    assert(st->model.db[140 - st->model.k0] == 95.0f &&
           st->model.gain == 76);
    assert(offered(CMD_EXPORT) == 1 && offered(CMD_PICTURE) == 1);
    board_read(master, got, sizeof got);                    /* the next ask */
    assert(!strcmp(got, "sdr spec 2437 1 256\r"));

    /* A hover over the peak names its frequency and channels. */
    {
        tiku_rect_t plot, fall;
        sdr_layout(st, &plot, &fall);
        hover(st, plot.x + (140 - st->model.k0) * plot.w / st->model.n + 1,
              plot.y + 4);
        assert(strstr(st->hover, "MHz") && strstr(st->hover, "Wi-Fi"));
        st->hover[0] = '\0';
    }
    sdr_paint(st);
    assert(last_px != NULL);
    assert(tiku_surface_png(st->surface, "build/tests/test_sdr.png") == 0 ||
           tiku_surface_png(st->surface, "test_sdr.png") == 0);
    record(st, master, &now, dir);
    exports(st, dir);
    fields(st, master, &now);
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
    assert(file_has(rec2, "\n0.000 VIEW range 2396 2492\n"));
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
