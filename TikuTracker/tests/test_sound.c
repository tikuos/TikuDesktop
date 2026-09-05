/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_sound.c - the sound kit against a backend that only counts.
 *
 * What the table promises: an event is counted whether or not it sounds,
 * a disabled event or a silent kit hands the backend nothing, the volume
 * scales the samples and nothing else, and a clip is handed out in
 * chunks that never exceed what the backend has room for.  With --play
 * the same clip goes to the host backend, which is the one check that
 * needs a sound server and a pair of ears.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sound.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CLIP_FRAMES 22050          /* half a second */

static int16_t clip[CLIP_FRAMES];
static int fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/* A backend that keeps what it was handed. */
static struct {
    int     opened, closed, room;
    size_t  frames;
    int16_t peak;
    size_t  largest;
} fake;

static int
fake_open(void *ctx, unsigned rate)
{
    (void)ctx;
    fake.opened += (rate == TIKU_SOUND_RATE);
    return 0;
}

static int
fake_room(void *ctx)
{
    (void)ctx;
    return fake.room;
}

static int
fake_write(void *ctx, const int16_t *pcm, size_t frames)
{
    size_t i;

    (void)ctx;
    for (i = 0; i < frames; i++) {
        int16_t v = (int16_t)(pcm[i] < 0 ? -pcm[i] : pcm[i]);

        if (v > fake.peak) {
            fake.peak = v;
        }
    }
    fake.frames += frames;
    if (frames > fake.largest) {
        fake.largest = frames;
    }
    return (int)frames;
}

static void
fake_close(void *ctx)
{
    (void)ctx;
    fake.closed++;
}

static const tiku_sound_backend_t FAKE = {
    "fake", fake_open, fake_room, fake_write, fake_close, NULL
};

static void
drain(void)
{
    int guard = 1000;

    while (tiku_sound_playing() && guard-- > 0) {
        tiku_sound_pump();
    }
}

int
main(int argc, char **argv)
{
    size_t i;

    for (i = 0; i < CLIP_FRAMES; i++) {
        clip[i] = (int16_t)(20000.0 * sin(2.0 * 3.14159265 * 440.0 * i /
                                          (double)TIKU_SOUND_RATE));
    }

    printf("the silent kit\n");
    tiku_sound_init(NULL);
    tiku_sound_set_clip(TIKU_SOUND_BEEP, clip, CLIP_FRAMES);
    tiku_sound_play(TIKU_SOUND_BEEP);
    tiku_sound_pump();
    check(tiku_sound_count(TIKU_SOUND_BEEP) == 1u,
          "an event is counted with no backend at all");
    check(!tiku_sound_playing(), "and nothing is in flight");
    check(strcmp(tiku_sound_name(TIKU_SOUND_WINDOW_OPEN), "window-open") == 0,
          "events have the names the preferences will show");

    printf("the table over a counting backend\n");
    memset(&fake, 0, sizeof fake);
    fake.room = -1;
    tiku_sound_init(&FAKE);
    tiku_sound_set_clip(TIKU_SOUND_BEEP, clip, CLIP_FRAMES);
    tiku_sound_play(TIKU_SOUND_STARTUP);
    tiku_sound_pump();
    check(tiku_sound_count(TIKU_SOUND_STARTUP) == 1u && fake.frames == 0u,
          "an event with no clip is counted and hands the backend nothing");
    tiku_sound_play(TIKU_SOUND_BEEP);
    check(tiku_sound_playing(), "a beep with a clip is in flight");
    drain();
    check(fake.opened == 1, "the backend was opened once, at the rate");
    check(fake.frames == CLIP_FRAMES, "and handed every frame of the clip");
    check(fake.largest <= 2205u, "in chunks of at most fifty milliseconds");
    /* A 440 Hz sine sampled at 44.1 kHz peaks a hair under its amplitude. */
    check(fake.peak >= 19990 && fake.peak <= 20000,
          "at full volume the samples are the clip's");

    memset(&fake, 0, sizeof fake);
    fake.room = -1;
    tiku_sound_init(&FAKE);
    tiku_sound_set_clip(TIKU_SOUND_BEEP, clip, CLIP_FRAMES);
    tiku_sound_set_volume(50);
    tiku_sound_play(TIKU_SOUND_BEEP);
    drain();
    check(fake.peak >= 9995 && fake.peak <= 10000,
          "at half volume the samples are halved");
    tiku_sound_set_volume(0);
    fake.frames = 0;
    tiku_sound_play(TIKU_SOUND_BEEP);
    drain();
    check(fake.frames == 0u, "at volume 0 nothing is handed over");
    tiku_sound_set_volume(100);
    tiku_sound_set_enabled(TIKU_SOUND_BEEP, 0);
    tiku_sound_play(TIKU_SOUND_BEEP);
    drain();
    check(fake.frames == 0u && tiku_sound_count(TIKU_SOUND_BEEP) == 3u,
          "a disabled event is counted and silent");

    memset(&fake, 0, sizeof fake);
    fake.room = 100;
    tiku_sound_init(&FAKE);
    tiku_sound_set_clip(TIKU_SOUND_BEEP, clip, CLIP_FRAMES);
    tiku_sound_play(TIKU_SOUND_BEEP);
    drain();
    check(fake.largest == 100u && fake.frames == CLIP_FRAMES,
          "a backend with room for 100 frames gets 100 at a time, all of them");
    fake.room = 0;
    tiku_sound_play(TIKU_SOUND_BEEP);
    tiku_sound_pump();
    check(tiku_sound_playing() && fake.frames == CLIP_FRAMES,
          "a backend with no room is handed nothing and the clip waits");
    tiku_sound_free();
    check(fake.closed == 1, "free closes the backend once");

    if (argc > 1 && strcmp(argv[1], "--play") == 0) {
        const tiku_sound_backend_t *host = tiku_sound_pulse_backend();

        printf("the host backend\n");
        check(host != NULL, "libpulse-simple was found");
        if (host != NULL) {
            tiku_sound_init(host);
            tiku_sound_set_clip(TIKU_SOUND_BEEP, clip, CLIP_FRAMES);
            tiku_sound_play(TIKU_SOUND_BEEP);
            while (tiku_sound_playing()) {
                tiku_sound_pump();
                usleep(10000);
            }
            check(tiku_sound_count(TIKU_SOUND_BEEP) == 1u,
                  "a half-second A was handed to the server");
            tiku_sound_free();
        }
    }
    printf("%s\n", fails ? "FAILURES" : "the sound kit keeps its table");
    return fails ? 1 : 0;
}
