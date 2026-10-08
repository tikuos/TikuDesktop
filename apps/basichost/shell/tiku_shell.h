/* tiku_shell.h -- the host's stand-in for shell/tiku_shell.h, for
 * the BASIC interpreter compiled on the host: the console it prints to
 * and reads from, and the pumps it yields to, from tiku_basic_host.c.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_HOST_SHELL_STUB_H_
#define TIKU_HOST_SHELL_STUB_H_
#include <stdio.h>
#include <tiku.h>
/** @brief Print through the harness.  A batch run drops the REPL's prompt
 *         and banner, and takes the prompt's return as the end of the
 *         program; --interactive passes everything through. */
void tiku_basic_host_printf(const char *fmt, ...);
#define SHELL_PRINTF(...) tiku_basic_host_printf(__VA_ARGS__)
/* The colour codes are empty: the output lands in an editor's pane, which
 * does not render escapes. */
#define SH_RED    ""
#define SH_GREEN  ""
#define SH_YELLOW ""
#define SH_CYAN   ""
#define SH_BOLD   ""
#define SH_DIM    ""
#define SH_RST    ""
#define SHELL_POLL_TICKS 1
/** @brief Whether a key is waiting on stdin; always 0 in a batch run,
 *         after a 10 ms sleep when INPUT is the one asking. */
int  tiku_shell_io_rx_ready(void);
/** @brief The next byte of stdin, Ctrl-C (3) once it has ended, or -1 in
 *         a batch run. */
int  tiku_shell_io_getc(void);
/** @brief 0: the console does not echo what is typed. */
int  tiku_shell_io_has_echo(void);
/** @brief -1: the host has no network console. */
int  tiku_shell_net_getc(void);
/** @brief Nothing: the host has no network to pump. */
void tiku_shell_net_pump(void);
/** @brief Nothing: the host has no network to pump. */
void tiku_shell_pump_net(void);
/** @brief Nothing: the host has no shell loop to pump. */
void tiku_shell_pump(void);
/** @brief 0, running nothing: the host has no shell to give @p line to. */
int  tiku_shell_process(const char *line);

/* The process calls the interpreter makes.  On a board the arena is
 * attached to the shell's process; the host has none, so TIKU_THIS() is
 * NULL and the calls do nothing. */
struct tiku_process;
#define TIKU_THIS() ((struct tiku_process *)0)
/** @brief Nothing: the host has no process to attach @p arena to. */
static inline void tiku_process_attach_mem_arena(struct tiku_process *p,
                                                 void *arena) {
    (void)p; (void)arena;
}
/** @brief Nothing: the host has no other processes to queue. */
static inline void tiku_process_queue_dispatchable_except(
    struct tiku_process *p) { (void)p; }
/** @brief Nothing: the host has no other processes to run. */
static inline void tiku_process_run_except(struct tiku_process *p) {
    (void)p;
}
#endif
