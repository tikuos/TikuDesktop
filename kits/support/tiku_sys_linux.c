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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

/* The UTF-8 locales tried for reading text, in order: C.UTF-8 orders
 * characters by code point, and en_US.UTF-8 stands in where the host has
 * no C.UTF-8. */
static const char *const utf8_locales[] = { "C.UTF-8", "en_US.UTF-8" };

void *
tiku_sys_locale_utf8(void)
{
    locale_t locale = (locale_t)0;
    size_t i;

    for (i = 0; locale == (locale_t)0 &&
                i < sizeof utf8_locales / sizeof utf8_locales[0]; i++) {
        locale = newlocale(LC_CTYPE_MASK | LC_COLLATE_MASK, utf8_locales[i],
                           (locale_t)0);
    }
    return locale;
}

void *
tiku_sys_locale_use(void *locale)
{
    return uselocale((locale_t)locale);
}

void
tiku_sys_locale_free(void *locale)
{
    if (locale != NULL) {
        freelocale((locale_t)locale);
    }
}

int tiku_sys_sync_file(int fd)
{
    return fsync(fd);
}

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

int
tiku_sys_processes(void (*fn)(const tiku_sys_process_t *process, void *ctx),
                   void *ctx)
{
    DIR *directory = opendir("/proc");
    struct dirent *entry;
    uid_t me = getuid();
    int offered = 0;

    if (directory == NULL || fn == NULL) {
        if (directory != NULL) {
            (void)closedir(directory);
        }
        return -1;
    }
    while ((entry = readdir(directory)) != NULL) {
        char path[280], name[64], command[160];
        tiku_sys_process_t process;
        struct stat owner;
        size_t length;
        FILE *file;

        if (entry->d_name[0] < '1' || entry->d_name[0] > '9') {
            continue;
        }
        if (snprintf(path, sizeof path, "/proc/%s", entry->d_name) < 0 ||
            stat(path, &owner) != 0 || owner.st_uid != me) {
            continue;
        }
        /* An empty command line denotes a kernel thread, which has no
         * image or windows for a person to activate. */
        if (snprintf(path, sizeof path, "/proc/%s/cmdline",
                     entry->d_name) < 0) {
            continue;
        }
        file = fopen(path, "r");
        if (file == NULL) {
            continue;
        }
        length = fread(command, 1u, sizeof command - 1u, file);
        (void)fclose(file);
        if (length == 0u) {
            continue;
        }
        /* The IMAGE, not argv[0]: a program started by a relative name
         * is remembered by that name in its command line, and a caller
         * handed it cannot find the file.  /proc/<pid>/exe answers for
         * a process of this user's own, which is the only kind offered
         * here; one that has gone since the directory was read has no
         * row worth offering. */
        if (snprintf(path, sizeof path, "/proc/%s/exe", entry->d_name) < 0) {
            continue;
        }
        {
            ssize_t got = readlink(path, command, sizeof command - 1u);

            if (got <= 0) {
                continue;
            }
            command[got] = '\0';
        }
        if (snprintf(path, sizeof path, "/proc/%s/comm",
                     entry->d_name) < 0) {
            continue;
        }
        file = fopen(path, "r");
        if (file == NULL) {
            continue;
        }
        if (fgets(name, sizeof name, file) == NULL) {
            (void)fclose(file);
            continue;
        }
        (void)fclose(file);
        length = strlen(name);
        while (length > 0u &&
               (name[length - 1u] == '\n' || name[length - 1u] == ' ')) {
            name[--length] = '\0';
        }
        if (name[0] == '\0') {
            continue;
        }
        process.pid = atol(entry->d_name);
        process.name = name;
        process.command = command;
        fn(&process, ctx);
        offered++;
    }
    (void)closedir(directory);
    return offered;
}

void
tiku_sys_memory(long *total_kb, long *avail_kb)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[128];

    *total_kb = 0;
    *avail_kb = 0;
    while (f != NULL && fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            (void)sscanf(line + 9, "%ld", total_kb);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            (void)sscanf(line + 13, "%ld", avail_kb);
        }
    }
    if (f != NULL) {
        (void)fclose(f);
    }
}

long
tiku_sys_uptime(void)
{
    FILE *f = fopen("/proc/uptime", "r");
    double up = 0.0;

    if (f != NULL) {
        if (fscanf(f, "%lf", &up) != 1) {
            up = 0.0;
        }
        (void)fclose(f);
    }
    return (long)up;
}

int
tiku_sys_self_path(char *out, size_t max)
{
    ssize_t n = readlink("/proc/self/exe", out, max - 1u);

    if (n <= 0) {
        out[0] = '\0';
        return -1;
    }
    out[n] = '\0';
    return 0;
}

void
tiku_sys_frame_name(const char *raw, char *out, size_t max)
{
    const char *paren = strchr(raw, '(');
    const char *self = strrchr(raw, '/');
    char leaf[64] = "";

    self = (self != NULL) ? self + 1 : raw;
    (void)sscanf(self, "%63[^(]", leaf);
    /* An offset into this build, which only a symboliser can name; a
     * library's own symbol stands as it is. */
    if (paren != NULL && paren[1] == '+' &&
        strcmp(leaf, "TikuDesktop") == 0) {
        char image[512], cmd[700], line[160];
        FILE *p;

        if (tiku_sys_self_path(image, sizeof image) != 0) {
            snprintf(image, sizeof image, "/proc/self/exe");
        }
        snprintf(cmd, sizeof cmd, "addr2line -e %.500s -f -C -p %.*s "
                 "2>/dev/null", image,
                 (int)strcspn(paren + 1, ")"), paren + 1);
        p = popen(cmd, "r");
        if (p != NULL && fgets(line, sizeof line, p) != NULL) {
            char *at = strstr(line, " at ");

            if (at != NULL) {
                *at = '\0';
            }
            line[strcspn(line, "\n")] = '\0';
            if (strncmp(line, "??", 2) == 0) {
                /* An offset this build does not have: the report is an
                 * earlier build's, and only its own binary could say. */
                snprintf(out, max, "an earlier build's %.*s",
                         (int)strcspn(paren + 1, ")"), paren + 1);
            } else {
                snprintf(out, max, "%.119s", line);
            }
            (void)pclose(p);
            return;
        }
        if (p != NULL) {
            (void)pclose(p);
        }
    }
    if (paren != NULL && paren[1] != '+' && paren[1] != ')') {
        snprintf(out, max, "%s: %.*s", leaf, (int)strcspn(paren + 1, "+)"),
                 paren + 1);
    } else if (paren != NULL) {
        snprintf(out, max, "%s %.*s", leaf, (int)strcspn(paren + 1, ")"),
                 paren + 1);
    } else {
        snprintf(out, max, "%.100s", raw);
    }
}

int
tiku_sys_process_pin(int fd, long *pid)
{
#if defined(SO_PEERPIDFD)
    struct ucred cred;
    socklen_t size = sizeof cred;
    int handle = -1;

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &size) != 0) {
        return -1;
    }
    /* Never init, never the caller, never another user's: the dialog
     * this serves can stop a program for good. */
    if (size != sizeof cred || cred.uid != geteuid() ||
        cred.pid <= 1 || cred.pid == getpid()) {
        errno = EPERM;
        return -1;
    }
    size = sizeof handle;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERPIDFD, &handle, &size) != 0) {
        return -1;
    }
    if (fcntl(handle, F_SETFD, FD_CLOEXEC) < 0) {
        int keep = errno;

        (void)close(handle);
        errno = keep;
        return -1;
    }
    if (pid != NULL) {
        *pid = (long)cred.pid;
    }
    return handle;
#else
    /* A kernel without SO_PEERPIDFD cannot hold a process, and holding
     * the NUMBER would be the bug this exists to avoid. */
    (void)fd;
    (void)pid;
    errno = ENOTSUP;
    return -1;
#endif
}

int
tiku_sys_process_kill(int handle)
{
#if defined(SYS_pidfd_send_signal)
    return (int)syscall(SYS_pidfd_send_signal, handle, SIGKILL, NULL, 0);
#else
    (void)handle;
    errno = ENOTSUP;
    return -1;
#endif
}

int
tiku_sys_reserve(int fd, long long bytes)
{
    /* A sparse tmpfs mapping can succeed then SIGBUS on the first write;
     * this is the call that says so now instead. */
    return posix_fallocate(fd, 0, (off_t)bytes);
}

/**
 * @brief Run an open serial line at exactly @p baud.
 *
 * Nothing to do here and nothing that can be done.  Linux names every
 * rate this tree offers as a termios constant, so the ordinary call
 * already set it and this is only reached when a rate was asked for
 * that the line itself refused -- which no second way would change.
 */
int
tiku_sys_serial_speed(int fd, int baud)
{
    (void)fd;
    (void)baud;
    return -1;
}

/**
 * @brief The serial ports a board could be on the other end of.
 *
 * udev writes a directory of stable names, and the name carries the
 * probe: J-Link (Nordic/Ambiq/Renesas kits), CDC (Pico), FTDI (the
 * FR5994 rig), TikuOS (a board whose console is its OWN USB port, the
 * LM20 native-USB build -- the probe is off the console path there, so
 * the port carries the device's name).
 */
int
tiku_sys_serial_ports(char out[][256], int max)
{
    static const char *dir = "/dev/serial/by-id";
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;

    if (d == NULL) {
        return -1;
    }
    while (n < max && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        if (strstr(e->d_name, "J-Link") == NULL &&
            strstr(e->d_name, "CDC")    == NULL &&
            strstr(e->d_name, "FT232")  == NULL &&
            strstr(e->d_name, "TikuOS") == NULL &&
            strstr(e->d_name, "Pico")   == NULL) {
            continue;
        }
        /* by-id names run long; a truncated path would open the wrong
         * device or nothing, so drop the entry instead. */
        if (strlen(dir) + strlen(e->d_name) + 2u > 256u) {
            continue;
        }
        (void)snprintf(out[n], 256, "%s/%s", dir, e->d_name);
        n++;
    }
    (void)closedir(d);
    return n;
}
