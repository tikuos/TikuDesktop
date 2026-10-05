/* tiku_cpu.h -- the host's stand-in for hal/tiku_cpu.h, for the BASIC
 * interpreter compiled on the host.  The host has no low-power idle: the
 * types keep the interpreter's SLEEP path compiling unchanged.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_HOST_CPU_STUB_H_
#define TIKU_HOST_CPU_STUB_H_
/** @brief What the idle hook returns: a call that enters an idle mode. */
typedef void (*tiku_cpu_idle_enter_t)(void);
/** @brief An idle mode, as the interpreter asks for one. */
typedef int tiku_cpu_idle_mode_t;
/** @brief The deep idle mode SLEEP asks for. */
#define TIKU_CPU_IDLE_DEEP 1
/** @brief No idle call for any mode: NULL, which SLEEP takes as nothing
 *         to call between wakes. */
static inline tiku_cpu_idle_enter_t tiku_cpu_idle_hook(tiku_cpu_idle_mode_t m)
{
    (void)m;
    return (tiku_cpu_idle_enter_t)0;
}
/** @brief Nothing to do on the host. */
static inline void tiku_cpu_icache_invalidate(void) {}
#endif
