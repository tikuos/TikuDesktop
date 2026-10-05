/* tiku_clock_hal.h -- the host's stand-in for hal/tiku_clock_hal.h, for
 * the BASIC interpreter compiled on the host: a 32-bit millisecond clock.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_HOST_CLOCK_HAL_STUB_H_
#define TIKU_HOST_CLOCK_HAL_STUB_H_
#include <stdint.h>
#define TIKU_CLOCK_CONF_TIME_T  uint32_t
#define TIKU_CLOCK_ARCH_SECOND  1000u   /* the host clock ticks in ms */
#endif
