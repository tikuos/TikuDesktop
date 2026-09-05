/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sound.h - system sounds: the moments a shell may sound, and how.
 *
 * A table of named events in the R5 manner -- the startup chime, the
 * beep, a window opening -- each with a clip, an on/off and a share of
 * one volume.  A shell raises an event at the moment it already knows
 * about and never learns what plays: the kit hands PCM to a backend a
 * chunk per turn, so the loop never waits on the card.  With no backend
 * every event is silent and still counted, which is what a scripted run
 * proves against.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SOUND_H_
#define TIKU_SOUND_H_

#include <stddef.h>
#include <stdint.h>

/** @brief The moments a shell may sound.  Their order is the table's. */
typedef enum {
    TIKU_SOUND_STARTUP,         /* the boot screen's first stage */
    TIKU_SOUND_BEEP,            /* an alert opened */
    TIKU_SOUND_WINDOW_OPEN,
    TIKU_SOUND_WINDOW_CLOSE,
    TIKU_SOUND_WINDOW_MINIMIZE,
    TIKU_SOUND_WINDOW_ZOOM,
    TIKU_SOUND_WINDOW_ACTIVATE,
    TIKU_SOUND_SHUTDOWN,        /* the closing screen began */
    TIKU_SOUND_EVENTS
} tiku_sound_event_t;

/** @brief The rate every clip is rendered at and every backend opened at. */
#define TIKU_SOUND_RATE 44100u

/**
 * @brief Where the PCM goes: mono, signed 16-bit, at TIKU_SOUND_RATE.
 *
 * open() is called on the first play and close() once nothing has played
 * for a while, so an idle shell holds no stream.  room() says how many
 * frames write() would take without blocking, or -1 when the backend
 * cannot tell, in which case the kit writes small chunks.
 */
typedef struct tiku_sound_backend {
    const char *name;
    int  (*open)(void *ctx, unsigned rate);
    int  (*room)(void *ctx);
    int  (*write)(void *ctx, const int16_t *pcm, size_t frames);
    void (*close)(void *ctx);
    void *ctx;
} tiku_sound_backend_t;

/**
 * @brief Start with @p backend, or silent when it is NULL.
 *
 * Every event begins enabled with no clip and the volume at 100; a play
 * before a clip is set is counted and nothing else.
 */
void tiku_sound_init(const tiku_sound_backend_t *backend);

/** @brief Give @p event its PCM: @p frames mono samples, borrowed. */
void tiku_sound_set_clip(tiku_sound_event_t event, const int16_t *pcm,
                         size_t frames);

/** @brief Whether @p event makes a sound at all. */
void tiku_sound_set_enabled(tiku_sound_event_t event, int on);

/** @brief One volume for every event, 0 to 100, linear in amplitude. */
void tiku_sound_set_volume(int volume);

/**
 * @brief Raise @p event.
 *
 * Counted whether or not it sounds.  When it sounds, its clip replaces
 * whatever was playing; a shell that beeps twice hears the second.
 */
void tiku_sound_play(tiku_sound_event_t event);

/**
 * @brief Hand the backend the next chunk, from the shell's turn.
 *
 * Never blocks for longer than the backend's own write of one chunk,
 * which is under fifty milliseconds of audio.  Closes an idle backend.
 */
void tiku_sound_pump(void);

/** @brief Whether a clip is still being handed out. */
int tiku_sound_playing(void);

/** @brief Whether @p event is enabled. */
int tiku_sound_enabled(tiku_sound_event_t event);

/** @brief The volume in force, 0 to 100. */
int tiku_sound_volume(void);

/** @brief How many times @p event has been raised since init. */
unsigned tiku_sound_count(tiku_sound_event_t event);

/** @brief The event's name, for a log or a preferences row. */
const char *tiku_sound_name(tiku_sound_event_t event);

/** @brief Close the backend and forget the clips. */
void tiku_sound_free(void);

/**
 * @brief Render every baked score and give each event its clip.
 * @return how many of the events now have a clip.
 */
int tiku_sound_load_scores(void);

/** @brief The score text baked for @p event, or NULL. */
const char *tiku_sound_score(tiku_sound_event_t event);

/** @brief Give @p event its baked score again.  @return 1 when it has one. */
int tiku_sound_load_score(tiku_sound_event_t event);

/**
 * @brief Give @p event the sound in @p path instead: a .score is rendered,
 *        a .wav is read (tiku_wav.h).  @return 1 when the clip changed;
 *        0 leaves whatever it had.
 */
int tiku_sound_load_file(tiku_sound_event_t event, const char *path);

/** @brief How many frames @p event's clip has, 0 for none. */
size_t tiku_sound_frames(tiku_sound_event_t event);

/**
 * @brief The host backend: the PulseAudio simple API, opened at run time.
 *
 * Found through dlopen("libpulse-simple.so.0"), which PipeWire serves as
 * well as PulseAudio.  @return the backend, or NULL when the library is
 * absent, in which case the shell runs silent and says so once.
 */
const tiku_sound_backend_t *tiku_sound_pulse_backend(void);

#endif /* TIKU_SOUND_H_ */
