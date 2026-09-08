/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_basic.c - run a BASIC program on a board, and watch it.
 *
 * The cues here are the ones TikuBench holds as facts (its `basic-wire`
 * test): the interpreter prompts `ok> `, a program goes in a line at a
 * time, and the FIRST prompt after RUN arrives only once the program has
 * ended -- which is what makes it the end of the run.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_board_basic.h"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_model.h"

#define PROMPT      "ok>"
#define OUT_MAX     4096u
#define LINE_MAX    512u
/* A board that has not reached the interpreter's prompt by now is not
 * going to: something else was mid-sentence on the line, and a run that
 * waits for it waits for ever. */
#define START_MS    12000L

static long
now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

struct tiku_board_basic {
    struct tiku_backend      *b;
    char                     *program;   /* what is left to hand over  */
    const char               *at;        /* where in it                */
    char                      buf[OUT_MAX + 1u];
    size_t                    len;
    char                      line[LINE_MAX];
    size_t                    line_len;
    tiku_board_basic_state_t  state;
    long                      began_ms;  /* when the handover started  */
    int                       started;   /* RUN has gone out           */
    int                       drop_echo; /* the board's echo of RUN     */
    int                       held;      /* the shell is ours          */
};

/** @brief The board's text, as the lease hands it over. */
static void
took_text(const unsigned char *bytes, size_t n, void *ctx)
{
    tiku_board_basic_t *r = ctx;
    size_t i;

    for (i = 0u; i < n; i++) {
        if (r->len < OUT_MAX) {
            r->buf[r->len++] = (char)bytes[i];
            r->buf[r->len] = '\0';
        }
    }
}

/** @brief Whether the interpreter's prompt is in what has arrived, and
 *         where it ends.  The prompt is the only cue that means "ready". */
static int
prompt_at(const tiku_board_basic_t *r, size_t *end)
{
    const char *p = strstr(r->buf, PROMPT);

    if (p == NULL) {
        return 0;
    }
    *end = (size_t)(p - r->buf) + strlen(PROMPT);
    return 1;
}

/** @brief Drop the first @p n bytes of what has arrived. */
static void
consume(tiku_board_basic_t *r, size_t n)
{
    if (n >= r->len) {
        r->len = 0u;
    } else {
        memmove(r->buf, r->buf + n, r->len - n);
        r->len -= n;
    }
    r->buf[r->len] = '\0';
}

/** @brief Hand whole lines of @p text to the caller, keeping a partial
 *         one for the next pump: a program printing slowly is still
 *         printing a line. */
static void
say_lines(tiku_board_basic_t *r, const char *text, size_t n,
          tiku_board_basic_line_fn fn, void *ctx)
{
    size_t i;

    for (i = 0u; i < n; i++) {
        char c = text[i];

        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            r->line[r->line_len] = '\0';
            /* The board echoes what it was told; the echo of RUN is not
             * something the program printed. */
            if (r->drop_echo) {
                r->drop_echo = 0;
                if (strcmp(r->line, "RUN") == 0) {
                    r->line_len = 0u;
                    continue;
                }
            }
            if (fn != NULL) {
                fn(r->line, ctx);
            }
            r->line_len = 0u;
            continue;
        }
        if (r->line_len + 1u < LINE_MAX) {
            r->line[r->line_len++] = c;
        }
    }
}

static int
send_text(tiku_board_basic_t *r, const char *s)
{
    return tiku_backend_tiku_lease_send(r->b, s, strlen(s));
}

tiku_board_basic_t *
tiku_board_basic_start(struct tiku_backend *b, const char *program)
{
    tiku_board_basic_t *r;

    if (b == NULL || program == NULL) {
        return NULL;
    }
    r = calloc(1, sizeof *r);
    if (r == NULL) {
        return NULL;
    }
    r->b = b;
    r->program = strdup(program);
    if (r->program == NULL) {
        free(r);
        return NULL;
    }
    r->at = r->program;
    if (tiku_backend_tiku_lease(b, took_text, r) != 0) {
        free(r->program);
        free(r);
        return NULL;
    }
    r->held = 1;
    /* Anything still arriving belongs to whoever had the line before --
     * the board may be finishing what the last run said, and its trailing
     * prompt would be taken for this one's, ending the run before it has
     * begun.  Let it land, then drop it. */
    {
        int quiet = 0, rounds = 0;

        /* Bounded as well as quiet: this runs inside the shell's own
         * loop, and a board that never stops talking would otherwise
         * stop the shell rather than the drain. */
        while (quiet < 3 && rounds < 20) {
            size_t before = r->len;

            (void)b->ops->pump(b, 30);
            quiet = (r->len == before) ? quiet + 1 : 0;
            rounds++;
        }
    }
    r->len = 0u;
    r->buf[0] = '\0';
    r->state = TIKU_BOARD_BASIC_SENDING;
    r->began_ms = now_ms();
    /* A newline first: whoever had the line may have left a half-typed
     * command on it, and "basic" appended to that is a word the shell
     * does not know.  The board answers a bare newline with a prompt,
     * and the drain above has already taken what came before. */
    (void)send_text(r, "\n");
    /* Entering answers with the banner and the prompt; the pump waits for
     * that prompt before it hands the first line over. */
    if (send_text(r, "basic\n") != 0) {
        r->state = TIKU_BOARD_BASIC_FAILED;
    }
    return r;
}

/** @brief Hand over the next line of the program, or start it. */
static void
send_next(tiku_board_basic_t *r)
{
    char line[LINE_MAX];
    const char *nl;
    size_t n;

    while (r->at != NULL && *r->at != '\0') {
        nl = strchr(r->at, '\n');
        n = (nl != NULL) ? (size_t)(nl - r->at) : strlen(r->at);
        if (n >= LINE_MAX) {
            n = LINE_MAX - 1u;
        }
        memcpy(line, r->at, n);
        line[n] = '\0';
        r->at = (nl != NULL) ? nl + 1 : r->at + strlen(r->at);
        /* A blank line at the prompt only reprints it; skipping keeps the
         * handover one line, one prompt. */
        if (line[0] != '\0') {
            size_t k = strlen(line);

            line[k] = '\n';
            line[k + 1u] = '\0';
            (void)send_text(r, line);
            return;
        }
    }
    (void)send_text(r, "RUN\n");
    r->started = 1;
    r->drop_echo = 1;
    r->state = TIKU_BOARD_BASIC_RUNNING;
}

tiku_board_basic_state_t
tiku_board_basic_pump(tiku_board_basic_t *r, int timeout_ms,
                      tiku_board_basic_line_fn fn, void *ctx)
{
    size_t end;

    if (r == NULL) {
        return TIKU_BOARD_BASIC_FAILED;
    }
    if (r->state == TIKU_BOARD_BASIC_ENDED ||
        r->state == TIKU_BOARD_BASIC_STOPPED ||
        r->state == TIKU_BOARD_BASIC_FAILED) {
        return r->state;
    }
    /* Pumping the backend is what moves the board's bytes to took_text. */
    (void)r->b->ops->pump(r->b, timeout_ms);

    if (r->state == TIKU_BOARD_BASIC_SENDING) {
        /* One prompt, one line: anything the board says while taking the
         * program is its own echo and not the program's output. */
        while (r->state == TIKU_BOARD_BASIC_SENDING && prompt_at(r, &end)) {
            consume(r, end);
            send_next(r);
        }
        if (r->state == TIKU_BOARD_BASIC_SENDING &&
            now_ms() - r->began_ms > START_MS) {
            r->state = TIKU_BOARD_BASIC_FAILED;
        }
        return r->state;
    }
    /* Running: everything up to the next prompt is the program talking,
     * and that prompt is the end of the run. */
    if (prompt_at(r, &end)) {
        size_t said = end - strlen(PROMPT);

        say_lines(r, r->buf, said, fn, ctx);
        if (r->line_len > 0u) {          /* a last line with no newline */
            r->line[r->line_len] = '\0';
            if (fn != NULL) {
                fn(r->line, ctx);
            }
            r->line_len = 0u;
        }
        consume(r, end);
        if (r->state == TIKU_BOARD_BASIC_RUNNING) {
            r->state = TIKU_BOARD_BASIC_ENDED;
        }
        return r->state;
    }
    say_lines(r, r->buf, r->len, fn, ctx);
    consume(r, r->len);
    return r->state;
}

int
tiku_board_basic_input(tiku_board_basic_t *r, const char *text)
{
    char line[LINE_MAX];

    if (r == NULL || text == NULL || r->state != TIKU_BOARD_BASIC_RUNNING) {
        return -1;
    }
    (void)snprintf(line, sizeof line, "%s\n", text);
    return send_text(r, line);
}

void
tiku_board_basic_stop(tiku_board_basic_t *r)
{
    if (r == NULL || !r->held) {
        return;
    }
    if (r->started && r->state == TIKU_BOARD_BASIC_RUNNING) {
        (void)tiku_backend_tiku_lease_send(r->b, "\x03", 1u);
        r->state = TIKU_BOARD_BASIC_STOPPED;
    }
}

void
tiku_board_basic_free(tiku_board_basic_t *r)
{
    if (r == NULL) {
        return;
    }
    if (r->held) {
        /* Out of the interpreter before the shell is anybody else's: a
         * board left in BASIC answers no command the namespace asks. */
        (void)tiku_backend_tiku_lease_send(r->b, "\x03", 1u);
        (void)r->b->ops->pump(r->b, 200);
        (void)tiku_backend_tiku_lease_send(r->b, "BYE\n", 4u);
        (void)r->b->ops->pump(r->b, 200);
        tiku_backend_tiku_unlease(r->b);
    }
    free(r->program);
    free(r);
}
