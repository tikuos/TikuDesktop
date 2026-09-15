/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sound_gallery.c - hear the system sounds, one after another.
 *
 * A sample, not part of the desktop: plays every event's clip through
 * the host backend with a pause between, or one event named on the
 * command line, and prints each score as it goes so what is heard can
 * be read.  The place to decide what the chime should be.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sound.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
play(tiku_sound_event_t e)
{
    printf("%s\n%s\n", tiku_sound_name(e), tiku_sound_score(e));
    tiku_sound_play(e);
    while (tiku_sound_playing()) {
        tiku_sound_pump();
        usleep(10000);
    }
    usleep(400000);
}

int
main(int argc, char **argv)
{
    const tiku_sound_backend_t *host = tiku_sound_host_backend();
    int i, n;

    if (host == NULL) {
        return 1;
    }
    tiku_sound_init(host);
    n = tiku_sound_load_scores();
    printf("%d of %d sounds rendered\n", n, (int)TIKU_SOUND_EVENTS);
    if (argc > 1) {
        for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
            if (strcmp(argv[1], tiku_sound_name((tiku_sound_event_t)i)) == 0) {
                play((tiku_sound_event_t)i);
                tiku_sound_free();
                return 0;
            }
        }
        fprintf(stderr, "no such sound: %s\n", argv[1]);
        return 1;
    }
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        play((tiku_sound_event_t)i);
    }
    tiku_sound_free();
    return 0;
}
