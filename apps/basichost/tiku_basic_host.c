/* tiku_basic_host.c -- tiku-basic, the board's BASIC interpreter run on
 * the host: tiku_basic.c compiled unchanged and given the kernel it
 * expects from libc, with a sandbox folder as its VFS.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include <shell/tiku_shell.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/vfs/tiku_vfs.h>
#include "tiku_basic.h"

/*---------------------------------------------------------------------------*/
/* The kernel, as libc                                                       */
/*---------------------------------------------------------------------------*/

/* Nonzero once the prompt has come back after RUN: the program ended. */
static int host_prompted;

/* Nonzero once the program has printed something; a bare newline before
 * that comes from feeding the program in, and is dropped. */
static int host_spoke;
/* --interactive: stdin streams to the REPL; set once stdin has ended. */
static int host_stream, host_eof;
/* A batch run: nonzero while INPUT's "? " is the last thing printed and
 * the clock has not been read since, so INPUT is the one polling. */
static int host_asked;

void
tiku_basic_host_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (host_stream) { fputs(buf, stdout); fflush(stdout); return; }
    host_asked = (strcmp(buf, "? ") == 0);
    /* A batch run prints only what the program says: the REPL's prompt
     * and banner are dropped, and the prompt's return marks the end of
     * the program. */
    if (strcmp(buf, "ok> ") == 0) {
        host_prompted = 1;
        return;
    }
    if (strstr(buf, "ready.") != NULL) {
        return;
    }
    if (!host_spoke && strcmp(buf, "\n") == 0) {
        return;
    }
    host_spoke = 1;
    fputs(buf, stdout);
}

/* tiku_clock.h declares this too; it is defined below. */
unsigned long
tiku_clock_seconds(void);

/** @brief The clock in milliseconds, from the monotonic wall clock. */
uint32_t
tiku_clock_time(void)
{
    struct timespec ts;

    host_asked = 0;     /* a timed wait, such as DELAY, is not INPUT */
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/** @brief Whole seconds of tiku_clock_time(). */
unsigned long
tiku_clock_seconds(void)
{
    return (unsigned long)(tiku_clock_time() / 1000u);
}

/* The console.  A batch run has no keys: INPUT waits until the run is
 * stopped, sleeping 10 ms per poll rather than spinning.  With
 * --interactive stdin is the keyboard, and its end arrives as Ctrl-C (3),
 * which releases a waiting INPUT. */
#include <poll.h>
#include <unistd.h>
int tiku_shell_io_rx_ready(void)
{
    struct pollfd fd = { STDIN_FILENO, POLLIN, 0 };
    if (!host_stream) {
        if (host_asked) {
            struct timespec nap = { 0, 10000000L };
            (void)nanosleep(&nap, NULL);
        }
        return 0;
    }
    return poll(&fd, 1, 0) > 0;
}
int tiku_shell_io_getc(void)
{
    unsigned char ch;
    if (!host_stream) { return -1; }
    if (read(STDIN_FILENO, &ch, 1) == 1) { return ch; }
    host_eof = 1;
    return 3; /* EOF also releases a blocking INPUT. */
}
int tiku_shell_io_has_echo(void) { return 0; }
int tiku_shell_net_getc(void)    { return -1; }
void tiku_shell_net_pump(void)   {}
void tiku_shell_pump_net(void)   {}
void tiku_shell_pump(void)       {}
int tiku_shell_process(const char *line) { (void)line; return 0; }

/*---------------------------------------------------------------------------*/
/* The memory the arena expects, as malloc                                   */
/*---------------------------------------------------------------------------*/

#include <kernel/memory/tiku_mem.h>

/** @brief Nothing to set up: arenas take their memory from the heap. */
tiku_mem_err_t
tiku_tier_init(void)
{
    return TIKU_MEM_OK;
}

/** @brief An arena of @p size bytes from posix_memalign(), aligned as
 *         @p options asks and at least to a pointer; of the flags only
 *         TIKU_MEM_ALLOW_EXTERNAL is accepted. */
tiku_mem_err_t
tiku_mem_arena_create(tiku_arena_t *arena, tiku_mem_arch_size_t size,
                      const tiku_mem_request_t *options)
{
    void *buf;
    size_t alignment = options ? options->alignment : 0;

    if (arena == NULL || size == 0 ||
        (options && (options->flags & ~TIKU_MEM_ALLOW_EXTERNAL)) ||
        (alignment && (alignment & (alignment - 1)))) {
        return TIKU_MEM_ERR_INVALID;
    }
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (posix_memalign(&buf, alignment, size) != 0) return TIKU_MEM_ERR_NOMEM;
    memset(arena, 0, sizeof *arena);
    arena->buf = buf;
    arena->capacity = size;
    arena->active = 1;
    arena->tier = TIKU_MEM_SRAM;
    return TIKU_MEM_OK;
}

/** @brief @p size bytes from @p arena, rounded up to a pointer's
 *         alignment; NULL, counted as a failure, when they do not fit. */
void *
tiku_arena_alloc(tiku_arena_t *arena, tiku_mem_arch_size_t size)
{
    void *p;
    /* This adapter runs on the host ABI, including 64-bit longs/pointers. */
    const tiku_mem_arch_size_t mask = sizeof(void *) - 1u;
    tiku_mem_arch_size_t aligned;

    if (arena == NULL || !arena->active || size == 0 ||
        size > UINT32_MAX - mask || arena->offset > arena->capacity ||
        (aligned = (size + mask) & ~mask) > arena->capacity - arena->offset) {
        if (arena != NULL) {
            arena->fail++;
        }
        return NULL;
    }
    p = arena->buf + arena->offset;
    arena->offset += aligned;
    if (arena->offset > arena->peak) {
        arena->peak = arena->offset;
    }
    arena->count++;
    return p;
}

/** @brief Empty @p arena; its buffer is kept. */
tiku_mem_err_t
tiku_arena_reset(tiku_arena_t *arena)
{
    if (arena == NULL) {
        return TIKU_MEM_ERR_INVALID;
    }
    arena->offset = 0;
    arena->count = 0;
    return TIKU_MEM_OK;
}

/* The persist store is volatile on the host, as tiku_basic_config.h says
 * of a build without an NVM region: a run keeps nothing for the next. */

/** @brief Nothing to unlock on the host.  @return 0. */
uint16_t tiku_mpu_unlock_nvm(void) { return 0; }
/** @brief Nothing to lock on the host. */
void tiku_mpu_lock_nvm(uint16_t saved) { (void)saved; }

/** @brief Nothing to set up: the store keeps nothing. */
tiku_mem_err_t
tiku_persist_init(tiku_persist_store_t *store)
{
    (void)store;
    return TIKU_MEM_OK;
}

/** @brief Accept @p key, keeping nothing for it. */
tiku_mem_err_t
tiku_persist_register(tiku_persist_store_t *store, const char *key,
                      uint8_t *fram_buf, tiku_mem_arch_size_t capacity)
{
    (void)store;
    (void)key;
    (void)fram_buf;
    (void)capacity;
    return TIKU_MEM_OK;
}

/** @brief Nothing saved under any key, as on a fresh boot.
 *  @return TIKU_MEM_ERR_INVALID, with @p out_len 0. */
tiku_mem_err_t
tiku_persist_read(tiku_persist_store_t *store, const char *key,
                  uint8_t *buf, tiku_mem_arch_size_t buf_size,
                  tiku_mem_arch_size_t *out_len)
{
    (void)store;
    (void)key;
    (void)buf;
    (void)buf_size;
    if (out_len != NULL) {
        *out_len = 0;
    }
    return TIKU_MEM_ERR_INVALID;    /* nothing saved: a fresh boot */
}

/** @brief Accept a write and keep nothing.  @return TIKU_MEM_OK. */
tiku_mem_err_t
tiku_persist_write(tiku_persist_store_t *store, const char *key,
                   const uint8_t *data, tiku_mem_arch_size_t data_len)
{
    (void)store;
    (void)key;
    (void)data;
    (void)data_len;
    return TIKU_MEM_OK;
}

/*---------------------------------------------------------------------------*/
/* The VFS, as a sandbox directory                                           */
/*---------------------------------------------------------------------------*/

/* The sandbox folder VFS paths name files in: argv[2], or ".". */
static char vfs_root[512] = ".";

/** @brief The sandbox file a VFS path names.  Absolute paths keep their
 *         shape under the root, so /led becomes <root>/led. */
static void
vfs_at(const char *path, char *out, size_t max)
{
    snprintf(out, max, "%s/%s", vfs_root,
             (path != NULL && path[0] == '/') ? path + 1 : path);
}

/** @brief "ok" for 0, "no such node" for anything else. */
const char *
tiku_vfs_strerror(int status)
{
    return (status == 0) ? "ok" : "no such node";
}

/** @brief A marker node when @p path names a file in the sandbox, else
 *         NULL.  The host has no node table: the marker has no handlers,
 *         so ON CHANGE polls the file. */
const tiku_vfs_node_t *
tiku_vfs_resolve(const char *path)
{
    static tiku_vfs_node_t here;
    char at[600];
    struct stat sb;

    vfs_at(path, at, sizeof at);
    return (stat(at, &sb) == 0) ? &here : NULL;
}

/** @brief The sandbox file @p path names, read into @p buf and ended
 *         with a NUL.  @return the bytes read, or -1. */
int
tiku_vfs_read(const char *path, char *buf, size_t max)
{
    char at[600];
    FILE *f;
    size_t n;

    vfs_at(path, at, sizeof at);
    f = fopen(at, "rb");
    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, (max > 0u) ? max - 1u : 0u, f);
    (void)fclose(f);
    if (max > 0u) {
        buf[n] = '\0';
    }
    return (int)n;
}

/** @brief Replace the sandbox file @p path names with @p len bytes of
 *         @p data.  @return 0, or -1. */
int
tiku_vfs_write(const char *path, const char *data, size_t len)
{
    char at[600];
    FILE *f;

    vfs_at(path, at, sizeof at);
    f = fopen(at, "wb");
    if (f == NULL) {
        return -1;
    }
    if (len > 0u && fwrite(data, 1, len, f) != len) {
        (void)fclose(f);
        return -1;
    }
    return (fclose(f) == 0) ? 0 : -1;
}

/** @brief List nothing: the host's VFS has no directories to walk.
 *  @return 0. */
int
tiku_vfs_list(const char *path, tiku_vfs_list_fn callback, void *ctx)
{
    (void)path;
    (void)callback;
    (void)ctx;
    return 0;
}

/** @brief Refuse: the host keeps no watches.  @return -1. */
int8_t
tiku_vfs_watch(const char *path, struct tiku_process *p)
{
    (void)path;
    (void)p;
    return -1;
}

struct tiku_tfs;
/** @brief No durable /data store on the host.  @return NULL. */
struct tiku_tfs *
tiku_vfs_tree_data_store(void)
{
    return NULL;
}

/*---------------------------------------------------------------------------*/
/* The run                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Run a program, or the REPL.
 *
 * `tiku-basic <program.bas> [sandbox-dir]` feeds the program to the
 * interpreter's REPL a line at a time, types RUN, and pumps the mode until
 * the program ends, printing its output.  `tiku-basic --interactive
 * [sandbox-dir]` streams stdin to the REPL and all its output to stdout.
 * @return 0; 2 on a usage or read error; 3 when the program was stopped
 *         at the tick guard.
 */
int
main(int argc, char **argv)
{
    FILE *f;
    char line[256];
    int guard;

    if (argc < 2) {
        fprintf(stderr, "usage: tiku-basic <program.bas> [sandbox-dir]\n");
        return 2;
    }
    if (argc > 2) {
        snprintf(vfs_root, sizeof vfs_root, "%s", argv[2]);
    }
    if (!strcmp(argv[1], "--interactive")) {
        host_stream = 1;
        tiku_basic_mode_enter();
        while (tiku_basic_mode_active() && !host_eof) {
            int budget = 256;
            struct pollfd input = { STDIN_FILENO, POLLIN, 0 };
            while (budget-- && tiku_shell_io_rx_ready() && !host_eof) {
                tiku_basic_mode_feed_char(tiku_shell_io_getc());
            }
            tiku_basic_mode_tick();
            (void)poll(&input, 1, 1);
        }
        return 0;
    }
    f = fopen(argv[1], "r");
    if (f == NULL) {
        fprintf(stderr, "tiku-basic: cannot read %s\n", argv[1]);
        return 2;
    }
    tiku_basic_mode_enter();
    /* Each line goes through the interpreter's own line input, as typing
     * it would: numbered lines are stored, and nothing here interprets a
     * line itself. */
    while (fgets(line, sizeof line, f) != NULL) {
        char *p = line;

        while (*p != '\0') {
            if (*p != '\r') {
                tiku_basic_mode_feed_char(*p);
            }
            p++;
        }
    }
    (void)fclose(f);
    host_prompted = 0;
    tiku_basic_mode_feed_char('R');
    tiku_basic_mode_feed_char('U');
    tiku_basic_mode_feed_char('N');
    tiku_basic_mode_feed_char('\n');
    /* Pump until the program ends, for at most 3,000,000 ticks: a program
     * that never ends is stopped, and the run exits with status 3. */
    for (guard = 0; guard < 3000000 && tiku_basic_mode_active() &&
                    !host_prompted; guard++) {
        tiku_basic_mode_tick();
    }
    fflush(stdout);
    if (guard >= 3000000) {
        fprintf(stderr, "tiku-basic: still running after the guard -- "
                        "stopped rather than trusted\n");
        return 3;
    }
    return 0;
}
