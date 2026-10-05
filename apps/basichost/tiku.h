/* tiku.h -- the host's stand-in for the firmware umbrella header.  The
 * BASIC interpreter's source is compiled unchanged with every peripheral
 * gated off; tiku_basic_host.c provides the console, clock and arena.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_HOST_STUB_H_
#define TIKU_HOST_STUB_H_
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
/** @brief Nothing: the host has no watchdog. */
static inline void tiku_watchdog_kick(void) {}
#define TIKU_PRINTF(...) printf(__VA_ARGS__)
#endif
