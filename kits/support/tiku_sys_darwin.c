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

#include <fcntl.h>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>

int tiku_sys_sync_file(int fd)
{
    if (fsync(fd)) return -1;
    return fcntl(fd, F_FULLFSYNC);
}

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

int
tiku_sys_processes(void (*fn)(const tiku_sys_process_t *process, void *ctx),
                   void *ctx)
{
    uid_t me = getuid();
    pid_t *pids;
    int bytes, n, i, offered = 0;

    if (fn == NULL) {
        return -1;
    }
    /* Asked for the room first, and taken at that size: the list is
     * bytes, not entries, which is the one thing easy to get wrong. */
    bytes = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    if (bytes <= 0) {
        return -1;
    }
    pids = malloc((size_t)bytes);
    if (pids == NULL) {
        return -1;
    }
    bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, bytes);
    if (bytes <= 0) {
        free(pids);
        return -1;
    }
    n = bytes / (int)sizeof *pids;
    for (i = 0; i < n; i++) {
        struct proc_bsdshortinfo brief;
        char command[PROC_PIDPATHINFO_MAXSIZE];
        char name[64];
        tiku_sys_process_t process;

        if (pids[i] <= 0) {
            continue;
        }
        /* The short form: the owner and the name in one call, where the
         * long one would read the whole task. */
        if (proc_pidinfo(pids[i], PROC_PIDT_SHORTBSDINFO, 0, &brief,
                         sizeof brief) != (int)sizeof brief ||
            brief.pbsi_uid != me) {
            continue;
        }
        /* No image is this host's kernel thread: nothing to activate,
         * nothing to name. */
        if (proc_pidpath(pids[i], command, sizeof command) <= 0) {
            continue;
        }
        snprintf(name, sizeof name, "%.*s", (int)sizeof brief.pbsi_comm,
                 brief.pbsi_comm);
        if (name[0] == '\0') {
            continue;
        }
        process.pid = (long)pids[i];
        process.name = name;
        process.command = command;
        fn(&process, ctx);
        offered++;
    }
    free(pids);
    return offered;
}

void
tiku_sys_memory(long *total_kb, long *avail_kb)
{
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm;
    uint64_t total = 0;
    size_t room = sizeof total;

    *total_kb = 0;
    *avail_kb = 0;
    if (sysctlbyname("hw.memsize", &total, &room, NULL, 0) == 0) {
        *total_kb = (long)(total / 1024u);
    }
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &count) == KERN_SUCCESS) {
        /* Free, inactive and purgeable: what a program could still be
         * given, which is what MemAvailable means on the other host --
         * not the free count alone, which on a machine that has been up
         * a while is always small and always alarming. */
        uint64_t pages = (uint64_t)vm.free_count +
                         (uint64_t)vm.inactive_count +
                         (uint64_t)vm.purgeable_count;

        *avail_kb = (long)(pages * (uint64_t)vm_kernel_page_size / 1024u);
    }
}

long
tiku_sys_uptime(void)
{
    struct timeval boot;
    size_t room = sizeof boot;
    time_t now;

    if (sysctlbyname("kern.boottime", &boot, &room, NULL, 0) != 0 ||
        boot.tv_sec == 0) {
        return 0;
    }
    now = time(NULL);
    return (now > boot.tv_sec) ? (long)(now - boot.tv_sec) : 0;
}

int
tiku_sys_self_path(char *out, size_t max)
{
    char raw[PROC_PIDPATHINFO_MAXSIZE];
    uint32_t room = (uint32_t)sizeof raw;
    char real[PROC_PIDPATHINFO_MAXSIZE];

    if (_NSGetExecutablePath(raw, &room) != 0) {
        out[0] = '\0';
        return -1;
    }
    /* Resolved, because what comes back is the path as it was invoked and
     * a caller climbing it wants the one on the disk. */
    if (realpath(raw, real) != NULL) {
        snprintf(out, max, "%s", real);
    } else {
        snprintf(out, max, "%s", raw);
    }
    return 0;
}

void
tiku_sys_frame_name(const char *raw, char *out, size_t max)
{
    static char self[PROC_PIDPATHINFO_MAXSIZE];
    char image[64] = "", address[32] = "", symbol[160] = "";
    const char *leaf;
    int index = 0;

    /*
     * What backtrace_symbols_fd() writes here is already the symbol --
     * the number, the image, the address, then the name and its offset --
     * so nothing has to be looked up.  The image is worth keeping only
     * when the frame is not this program's own.
     */
    if (sscanf(raw, "%d %63s %31s %159[^\n]", &index, image, address,
               symbol) != 4) {
        snprintf(out, max, "%.100s", raw);
        return;
    }
    /* Asked once: a report is read a frame at a time and the answer
     * cannot change under a running program. */
    if (self[0] == '\0' && tiku_sys_self_path(self, sizeof self) != 0) {
        self[0] = '\1';        /* asked, and it would not say */
    }
    leaf = strrchr(self, '/');
    leaf = (leaf != NULL) ? leaf + 1 : self;
    if (leaf[0] != '\0' && strcmp(leaf, image) == 0) {
        snprintf(out, max, "%.119s", symbol);
    } else {
        snprintf(out, max, "%s: %.100s", image, symbol);
    }
}
