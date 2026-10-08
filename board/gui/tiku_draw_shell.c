/*
 * TikuGUI -- a board's interface on the desktop.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_draw_shell.c - the shell's I/O over a link, beside the cable.
 *
 * Text a desk sends is handed to the console, which serves it to the shell
 * before the wire; a backend in front of the cable's sends a command's
 * output back the way the command came, so a desk on a link and a person
 * on the cable each see their own.  What a link confers is what its
 * commands write with; the cable keeps its own authority.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_draw_shell.h"
#include "tiku_draw.h"

#if TIKU_SHELL_ENABLE

#include <services/console/tiku_console.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include <kernel/vfs/tiku_vfs.h>

#include <string.h>

#define RMSG_SHELL_OUT 22u
#define OUT_CAP  512u                   /* replies waiting for the link */
#define OUT_MSG  200u                   /* the most sent in one message */

static uint8_t  out_buf[OUT_CAP];
static uint16_t out_len;
static uint32_t lost;                   /* bytes the link would not take */
static const tiku_shell_io_t *under;    /* the cable's backend, kept */
static tiku_shell_io_t link_io;
static uint8_t attached;
static uint8_t speaker;                 /* 0 the cable, 1 the link */

/**
 * @brief What the shell wrote goes out, a message at a time, as far as
 *        the link will take it now; the rest waits for the next call.  A
 *        dump wider than the link's outbox loses what does not fit, but
 *        never the prompt that ends it: that waits here and goes when
 *        there is room, so a desk's read of it always ends.
 */
static void
flush_out(void)
{
    while (out_len > 0u) {
        uint16_t n = (out_len < OUT_MSG) ? out_len : OUT_MSG;

        if (tiku_draw_send_msg(RMSG_SHELL_OUT, out_buf, n) != 0) {
            return;                     /* no room now: keep it */
        }
        out_len = (uint16_t)(out_len - n);
        memmove(out_buf, out_buf + n, out_len);
    }
}

/**
 * @brief The shell's authority follows the speaker.  The shell echoes each
 *        byte it takes, so this runs before a line is acted on.
 */
static void
speak(uint8_t who)
{
    uint8_t cap = who ? tiku_draw_cap()
                      : (under != NULL ? under->cap : TIKU_VFS_CAP_NONE);

    if (who != speaker || cap != link_io.cap) {
        if (who != speaker) {
            flush_out();
        }
        speaker = who;
        link_io.cap = cap;
        tiku_shell_io_set_backend(&link_io);    /* re-syncs the caller cap */
    }
}

static void
io_putc(char c)
{
    speak(tiku_console_from_inject());
    if (speaker == 0u) {
        if (under != NULL && under->putc != NULL) {
            under->putc(c);
        }
        return;
    }
    if (out_len >= OUT_CAP) {
        /* Full.  The link offers what it holds, but the shell cannot
         * yield mid-command and the medium must not be pumped from here
         * (its stack is already on the call chain that led here), so what
         * still will not fit is dropped, oldest first: the prompt that
         * ends a reply is the last thing written and so is never what is
         * dropped, and a desk's read of a reply larger than the link
         * carries in one command sees it truncated and can ask again. */
        flush_out();
        if (out_len >= OUT_CAP) {
            uint16_t drop = OUT_MSG;

            lost += drop;
            memmove(out_buf, out_buf + drop, out_len - drop);
            out_len = (uint16_t)(out_len - drop);
        }
    }
    out_buf[out_len++] = (uint8_t)c;
    if (c == '\n') {
        flush_out();
    }
}

static uint8_t
io_rx_ready(void)
{
    return (under != NULL && under->rx_ready != NULL) ? under->rx_ready() : 0u;
}

/**
 * @brief On the shell's poll: a reply that has stood -- a prompt, which
 *        ends without a line end -- goes out now.  A command's output is
 *        written whole within one dispatch, so what is pending here is
 *        finished.
 */
static void
flush_pump(void)
{
    if (speaker != 0u) {
        flush_out();
    }
}

static int
io_getc(void)
{
    return (under != NULL && under->getc != NULL) ? under->getc() : -1;
}

static void
attach(void)
{
    const tiku_shell_io_t *now = tiku_shell_io_get_backend();

    if (now == &link_io) {
        return;
    }
    under = now;
    link_io.putc = io_putc;
    link_io.rx_ready = io_rx_ready;
    link_io.getc = io_getc;
    link_io.flags = (under != NULL) ? under->flags
                                    : (TIKU_SHELL_IO_CRLF | TIKU_SHELL_IO_ECHO);
    link_io.cap = (under != NULL) ? under->cap : TIKU_VFS_CAP_NONE;
    speaker = 0u;
    attached = 1u;
    tiku_shell_io_set_backend(&link_io);
    (void)tiku_shell_add_pump(flush_pump);
}

void
tiku_draw_shell_take(const unsigned char *bytes, size_t len)
{
    if (tiku_draw_link_is_console()) {
        return;                         /* the cable is the shell already */
    }
    if (!attached) {
        attach();
    }
    (void)tiku_console_inject(bytes, len);
}

void
tiku_draw_shell_detach(void)
{
    if (!attached) {
        return;
    }
    flush_out();
    attached = 0u;
    speaker = 0u;
    tiku_shell_remove_pump(flush_pump);
    tiku_shell_io_set_backend(under);
}

#else

void
tiku_draw_shell_take(const unsigned char *bytes, size_t len)
{
    (void)bytes;
    (void)len;                          /* no shell: nothing to reach */
}

void
tiku_draw_shell_detach(void)
{
}

#endif
