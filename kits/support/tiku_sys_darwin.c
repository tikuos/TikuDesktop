/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sys_darwin.c - the machine underneath, as Darwin describes it.
 *
 * The twin of tiku_sys_linux.c; the Makefile compiles one of the two
 * and the rest of the tree never learns which.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_sys.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/ucontext.h>

void *
tiku_sys_fault_pc(const void *context)
{
    const ucontext_t *uc = context;

    if (uc == NULL || uc->uc_mcontext == NULL) {
        return NULL;
    }
#if defined(__arm64__) || defined(__aarch64__)
    /* Through the accessor, not the field: an arm64e pointer carries an
     * authentication code in its top bits and printing it raw would name
     * an address the binary does not contain. */
    return (void *)(uintptr_t)
        __darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
#elif defined(__x86_64__)
    return (void *)(uintptr_t)uc->uc_mcontext->__ss.__rip;
#else
    /* An architecture nobody has run this on yet: the frame walk still
     * writes what it can, and one line of the report is missing rather
     * than the report. */
    return NULL;
#endif
}
