/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sys_linux.c - the machine underneath, as Linux describes it.
 *
 * The twin of tiku_sys_darwin.c; the Makefile compiles one of the two
 * and the rest of the tree never learns which.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE 1           /* REG_RIP and the rest live behind it */

#include "tiku_sys.h"

#include <stddef.h>
#include <stdint.h>
#include <ucontext.h>

void *
tiku_sys_fault_pc(const void *context)
{
    const ucontext_t *uc = context;

    if (uc == NULL) {
        return NULL;
    }
#if defined(__x86_64__)
    return (void *)(uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__i386__)
    return (void *)(uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
#elif defined(__aarch64__)
    return (void *)(uintptr_t)uc->uc_mcontext.pc;
#elif defined(__arm__)
    return (void *)(uintptr_t)uc->uc_mcontext.arm_pc;
#else
    /* An architecture nobody has run this on yet: the frame walk still
     * writes what it can, and one line of the report is missing rather
     * than the report. */
    return NULL;
#endif
}
