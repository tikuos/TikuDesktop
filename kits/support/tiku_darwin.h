/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker both stand on.
 *
 * tiku_darwin.h - what macOS spells differently.
 *
 * Force-included on Darwin by mk/toolchain.mk, never named by a source:
 * the tree is written to POSIX and to glibc where POSIX is silent, and
 * this file is the list of places where Darwin's libc chose another
 * spelling for the same thing.  Nothing here is a policy, only a name.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DARWIN_H
#define TIKU_DARWIN_H

#ifdef __APPLE__

/*
 * <stddef.h> and nothing more: this header is forced in front of every
 * translation unit, and a source here may define a static function called
 * send() or accept().  Pulling <sys/socket.h> in ahead of it would turn
 * that into a redeclaration error a hundred files away from the cause.
 */
#include <stddef.h>

/* Darwin keeps the sub-second stamps under the 4.4BSD names.  The struct
 * is the same three timespecs; only the members are called something
 * else, so an alias is the whole difference. */
#ifndef st_atim
#define st_atim  st_atimespec
#define st_mtim  st_mtimespec
#define st_ctim  st_ctimespec
#endif

/* A GNU extension with no BSD twin.  Four lines rather than a dependency. */
#ifndef TIKU_HAVE_MEMRCHR
static inline void *
tiku_memrchr(const void *block, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)block;

    while (n-- > 0u) {
        if (p[n] == (unsigned char)c) {
            return (void *)(p + n);
        }
    }
    return NULL;
}
#define memrchr tiku_memrchr
#endif

/* Darwin's termios speeds ARE the integers (B115200 is 115200), so the
 * two this tree uses that the header stops short of are the same rule
 * carried two rows further.  Whether the driver honours them is the
 * driver's business, exactly as on Linux. */
#ifndef B460800
#define B460800 460800
#endif
#ifndef B921600
#define B921600 921600
#endif

/* No atomic close-on-exec flag on the socket calls.  Zero keeps the call
 * legal, and both callers in this tree close the pair by hand in the child
 * before they exec, so nothing here depends on the flag; a caller that
 * did would have to set FD_CLOEXEC with fcntl() after the socketpair. */
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

/*
 * send() carries no per-call SIGPIPE suppression here; the socket holds it
 * as an option instead.  Zero on its own would turn a peer that hung up
 * from a short write into a dead process, so every socket this tree sends
 * on must either belong to a program that ignores SIGPIPE -- which
 * tiku_remote and tiku_conduct both do, covering the desktop and every
 * program it loads -- or carry SO_NOSIGPIPE from where it was made, as
 * the TCP link does.
 */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#endif /* __APPLE__ */
#endif /* TIKU_DARWIN_H */
