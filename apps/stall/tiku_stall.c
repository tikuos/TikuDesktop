/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_stall.c - an application that stalls its own isolated event loop.
 *
 * Built only for tests. A legacy trusted install must still run in a
 * separate process: this eight-second callback cannot freeze the desktop.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "tiku_app.h"

#define APP_ID      "tiku.stall"
#define APP_NAME    "Stall"

/*
 * Not static, so the name is in the table a backtrace reads from; and
 * it does not give up when asked to speak, because a turn that ended at
 * the first ask was never stuck.
 */
void __attribute__((noinline))
the_turn_that_will_not_end(void)
{
    struct timespec began, now;

    (void)clock_gettime(CLOCK_MONOTONIC, &began);
    for (;;) {
        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - began.tv_sec >= 8) {
            return;
        }
        (void)usleep(100000);
    }
}

static int
stall_start(void **state, const tiku_app_services_t *services)
{
    static int once;

    uint32_t window = services->open(services->ctx, APP_NAME, 240, 100);
    if (!window) return -1;
    *state = &once;
    return 0;
}

static void
stall_stop(void *state)
{
    (void)state;
}

static void
stall_tick(void *state, int64_t now_us)
{
    int *once = state;

    (void)now_us;
    if (once == NULL || *once) {
        return;                 /* the once is the point: not a hang */
    }
    *once = 1;
    the_turn_that_will_not_end();
}

const tiku_app_descriptor_t tiku_stall_app = {
    .id = APP_ID,
    .name = APP_NAME,
    .start = stall_start,
    .stop = stall_stop,
    .tick = stall_tick
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_stall_app
};
#endif
