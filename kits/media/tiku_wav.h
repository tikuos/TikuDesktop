/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_wav.h - a WAV file read into the one shape the sound kit plays.
 *
 * PCM only, 8 or 16 bits, one channel or two, at any rate: the channels
 * are averaged and the rate is stepped to the one asked for, so a file
 * from anywhere becomes a clip at TIKU_SOUND_RATE.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_WAV_H_
#define TIKU_WAV_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Read @p path as mono 16-bit frames at @p rate.
 *
 * At most @p max_s seconds are kept.  @return a malloc'd buffer the
 * caller frees, with the count in @p *frames, or NULL for a file that
 * is not PCM WAV or cannot be read.
 */
int16_t *tiku_wav_read(const char *path, unsigned rate, unsigned max_s,
                       size_t *frames);

#endif /* TIKU_WAV_H_ */
