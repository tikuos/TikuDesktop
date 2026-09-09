/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault.c - an application that fails, so the shell can be asked
 * what it does about one.
 *
 * Built only for the tests.  It faults where an application is at its
 * most awkward: inside start(), before it has a window, so there is
 * nothing on screen to go and the only word of it is what the shell
 * says.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdlib.h>

#include "tiku_app.h"

#define APP_ID      "tiku.fault"
#define APP_NAME    "Fault"

/* Zero, but not a constant the compiler can fold away with the store. */
static volatile intptr_t nowhere;

/** @brief Fail, in the way a program fails when a pointer is wrong. */
void __attribute__((noinline))
fault_where_it_starts(void)
{
    *(volatile int *)nowhere = 1;
}

static int
fault_start(void **state, const tiku_app_services_t *services)
{
    (void)state;
    (void)services;
    fault_where_it_starts();
    return -1;                  /* not reached */
}

static void
fault_stop(void *state)
{
    (void)state;
}

const tiku_app_descriptor_t tiku_fault_app = {
    .id = APP_ID,
    .name = APP_NAME,
    .start = fault_start,
    .stop = fault_stop
};

#ifdef TIKU_APP_SO
/* The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_fault_app
};
#endif
