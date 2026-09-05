/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sound_scores.c - the eight system sounds, rendered once at start.
 *
 * The scores are the text files under assets/sounds, baked into a header
 * at build time the way the strings catalogue is, so the sounds ship in
 * the binary and stay reviewable as text in the tree.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sound.h"
#include "tiku_score.h"

#include <stdlib.h>
#include <string.h>

#include "tiku_scores_default.h"

static int16_t *rendered[TIKU_SOUND_EVENTS];

/** @brief The baked score text for @p name, or NULL. */
static const char *
score_text(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof TIKU_SCORES / sizeof TIKU_SCORES[0]; i++) {
        if (strcmp(TIKU_SCORES[i].name, name) == 0) {
            return TIKU_SCORES[i].text;
        }
    }
    return NULL;
}

int
tiku_sound_load_scores(void)
{
    int i, loaded = 0;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        const char *text = score_text(tiku_sound_name((tiku_sound_event_t)i));
        size_t frames;

        free(rendered[i]);
        rendered[i] = NULL;
        if (text == NULL) {
            continue;
        }
        frames = tiku_score_frames(text, TIKU_SOUND_RATE);
        if (frames == 0) {
            continue;
        }
        rendered[i] = malloc(frames * sizeof(int16_t));
        if (rendered[i] == NULL) {
            continue;
        }
        frames = tiku_score_render(text, TIKU_SOUND_RATE, rendered[i], frames);
        tiku_sound_set_clip((tiku_sound_event_t)i, rendered[i], frames);
        loaded++;
    }
    return loaded;
}

const char *
tiku_sound_score(tiku_sound_event_t event)
{
    return score_text(tiku_sound_name(event));
}
