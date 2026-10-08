/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw_basic.c - the interpreter's own stream over a link.
 *
 * A desk that runs a program here does not need the board's shell: the
 * lines, the RUN and the answers to INPUT arrive as messages of their
 * own, and what the program prints goes back the same way.  The console
 * stays the shell's throughout, so a program can run while the desk
 * still reads the namespace -- which is the whole reason this exists
 * beside tiku_draw_shell.c rather than inside it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_draw_basic.h"
#include "tiku_draw.h"

#if TIKU_SHELL_ENABLE && TIKU_SHELL_CMD_BASIC

#include <services/console/tiku_console.h>
#include <basic/tiku_basic.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include <kernel/vfs/tiku_vfs.h>
#include <string.h>

#define RMSG_BASIC_OUT 24u

#define OUT_CAP  512u                   /* what the program said, waiting */
#define OUT_MSG  200u                   /* the most sent in one message   */
#define IN_CAP   256u                   /* what the desk sent, unread     */

static uint8_t  out_buf[OUT_CAP];
static uint16_t out_len;
static uint8_t  in_buf[IN_CAP];
static uint16_t in_head, in_tail;
static uint32_t lost;                   /* bytes the link would not take  */
/* Inside the port service below: a message that arrives THERE is the
 * answer something is already waiting for, so it is only put where that
 * wait will find it -- feeding the interpreter from inside itself is how
 * one line ends up being read twice. */
static uint8_t  serving;
static uint8_t  pumping;                /* the flush hook is installed    */

/** @brief What the program printed goes out, as far as the link takes now. */
static void
flush_out(void)
{
    while (out_len > 0u) {
        uint16_t n = (out_len < OUT_MSG) ? out_len : OUT_MSG;

        if (tiku_draw_send_msg(RMSG_BASIC_OUT, out_buf, n) != 0) {
            return;                     /* no room now: keep it */
        }
        out_len = (uint16_t)(out_len - n);
        memmove(out_buf, out_buf + n, out_len);
    }
}

static void
io_putc(char c)
{
    if (out_len >= OUT_CAP) {
        /* Full.  The medium must not be pumped from here -- its own stack
         * led here -- so what still will not fit goes, oldest first: the
         * prompt that ends a reply is written last and so is never what
         * is dropped. */
        flush_out();
        if (out_len >= OUT_CAP) {
            lost += OUT_MSG;
            memmove(out_buf, out_buf + OUT_MSG, out_len - OUT_MSG);
            out_len = (uint16_t)(out_len - OUT_MSG);
        }
    }
    out_buf[out_len++] = (uint8_t)c;
    if (c == '\n') {
        flush_out();
    }
}

/**
 * @brief Whether the desk has sent a byte the interpreter has not read.
 *
 * The console is pumped while looking, because INPUT waits here in a
 * loop of its own: without it the answer being waited for could never
 * arrive.  A byte of the shell's own text met on the way is put back,
 * since this stream is not the console and must not eat its line.
 */
static uint8_t
io_rx_ready(void)
{
    if (in_head != in_tail) {
        return 1u;
    }
    /* A question leaves before its answer can arrive.  INPUT prints its
     * prompt without a line end and then waits here, so nothing else
     * would ever send it: the desk would wait for a question that had
     * not gone out, and the board for an answer to it. */
    flush_out();
    /* And the console is SERVED here, not merely read: while INPUT
     * waits, nothing else moves the port. */
        serving = 1u;
    tiku_console_pump();
    serving = 0u;
    {
        int ch = tiku_console_getc();   /* dispatches frames on the way */

        if (ch >= 0) {
            unsigned char text = (unsigned char)ch;

            /* A byte of the shell's own line met on the way back: this
             * stream is not the console and must not eat it. */
            (void)tiku_console_inject(&text, 1u);
        }
    }
    return (uint8_t)(in_head != in_tail);
}

static int
io_getc(void)
{
    int c;

    if (in_head == in_tail) {
        return -1;
    }
    c = (int)in_buf[in_tail];
    in_tail = (uint16_t)((in_tail + 1u) % IN_CAP);
    return c;
}

/* What the link confers is what a program run from it writes with. */
static tiku_shell_io_t stream_io = {
    io_putc, io_rx_ready, io_getc,
    TIKU_SHELL_IO_CRLF, TIKU_VFS_CAP_ALL
};

/**
 * @brief On the shell's own beat: a reply that has STOOD goes out now.
 *
 * The interpreter ends a program by printing its prompt, and a prompt
 * has no line end to send it -- so without this it waits here, and a
 * desk waiting for it to know the run is over waits for ever.
 */
static void
flush_pump(void)
{
    flush_out();
}

void
tiku_draw_basic_take(const unsigned char *bytes, size_t len)
{
    size_t i;

    if (!pumping) {
        pumping = 1u;
        (void)tiku_shell_add_pump(flush_pump);
    }

    /* The mode's own state decides, not a flag of this file's: a program
     * that ended with BYE left the interpreter, and the next thing the
     * desk sends has to enter it again rather than be fed to nothing. */
        if (!tiku_basic_mode_active() && !serving) {
        const tiku_shell_io_t *under = tiku_shell_io_get_backend();

        stream_io.cap = tiku_draw_cap();
        tiku_basic_mode_set_stream(&stream_io);
        /* Entered with the stream in front, so the banner and the first
         * prompt go to the desk that asked and not down the board's own
         * line, where they would read as something the shell said. */
        tiku_shell_io_set_backend(&stream_io);
        tiku_basic_mode_enter();
        tiku_shell_io_set_backend(under);
    }
    for (i = 0u; i < len; i++) {
        uint16_t nxt = (uint16_t)((in_head + 1u) % IN_CAP);

        if (nxt == in_tail) {
            lost++;                     /* unread: the oldest keeps its place */
            break;
        }
        in_buf[in_head] = bytes[i];
        in_head = nxt;
    }
        if (serving) {
        return;                 /* whoever is waiting will read it */
    }
    /* Fed here rather than waited for: the mode's own hook takes what has
     * arrived, and a line ends when its end arrives. */
    while (in_head != in_tail && tiku_basic_mode_active()) {
        int ch = io_getc();

        if (ch < 0) {
            break;
        }
        tiku_basic_mode_feed_char(ch);
    }
    flush_out();
    if (!tiku_basic_mode_active()) {
        /* BYE: the interpreter is the console's again, and nothing of
         * this stream should be left in front of it. */
        tiku_draw_basic_detach();
    }
}

void
tiku_draw_basic_detach(void)
{
    flush_out();
    if (pumping) {
        pumping = 0u;
        tiku_shell_remove_pump(flush_pump);
    }
    in_head = 0u;
    in_tail = 0u;
    tiku_basic_mode_set_stream((const tiku_shell_io_t *)0);
}

#else

void
tiku_draw_basic_take(const unsigned char *bytes, size_t len)
{
    (void)bytes;
    (void)len;                          /* no interpreter: nothing to reach */
}

void
tiku_draw_basic_detach(void)
{
}

#endif
