/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_board_values.c - a board's /data files are read and written whole
 * or refused.
 *
 * A fake board answers the shell over a socket pair: it echoes each line,
 * answers `read` of the manifest, `cat`, `write` and `ls`, and ends each
 * reply with its prompt.  Like a real board's shell it keeps at most 255
 * characters of a line when its line limit is on.  A value the desktop
 * cannot carry whole must fail with an error, never arrive cut.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include "tiku_model.h"
#include "tiku_tx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int fails;

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/*---------------------------------------------------------------------------*/
/* THE FAKE BOARD                                                            */
/*---------------------------------------------------------------------------*/

#define FILES     8
#define VALUE_MAX 9000

/* The board's /data files, its typed line, and the end its replies go to. */
static char   file_name[FILES][64];
static char   file_value[FILES][VALUE_MAX];
static int    file_used[FILES];
static char   line[16384];
static size_t line_len;
static int    line_limit;           /* 255 like a board's shell, or 0 */
static int    board_fd = -1;

static void
say(const char *text)
{
    size_t n = strlen(text);

    if (write(board_fd, text, n) != (ssize_t)n) {
        printf("  [FAIL] the board end could not write\n");
        fails++;
    }
}

static int
file_find(const char *path)
{
    int i;

    for (i = 0; i < FILES; i++) {
        if (file_used[i] && strcmp(file_name[i], path) == 0) {
            return i;
        }
    }
    return -1;
}

static void
file_put(const char *path, const char *value, size_t len)
{
    int i = file_find(path);

    for (int k = 0; i < 0 && k < FILES; k++) {
        if (!file_used[k]) {
            i = k;
        }
    }
    if (i < 0 || len >= VALUE_MAX) {
        return;
    }
    file_used[i] = 1;
    snprintf(file_name[i], sizeof file_name[i], "%s", path);
    memcpy(file_value[i], value, len);
    file_value[i][len] = '\0';
}

/** @brief Run one typed line and print its reply and the prompt. */
static void
board_line(char *cmd)
{
    char path[128];

    say(cmd);                       /* the echo */
    say("\r\n");
    if (strncmp(cmd, "read /sys/vfs/manifest 0 ", 25) == 0) {
        say("# path\tkind\tperm\tmeta\tcap\tid\r\n");
        say("/data\td\t--\t-\t-\t1\r\n");
        for (int i = 0; i < FILES; i++) {
            if (file_used[i]) {
                char row[160];

                snprintf(row, sizeof row, "%s\tf\trw\t-\t-\t%x\r\n",
                         file_name[i], i + 2);
                say(row);
            }
        }
    } else if (strncmp(cmd, "ls /data", 8) == 0) {
        for (int i = 0; i < FILES; i++) {
            if (file_used[i]) {
                say("  rw ");
                say(file_name[i] + 6);
                say("\r\n");
            }
        }
    } else if (sscanf(cmd, "cat %127s", path) == 1) {
        int i = file_find(path);

        if (i < 0) {
            say("read: cannot read '");
            say(path);
            say("' (ENOENT)\r\n");
        } else {
            say(file_value[i]);
            say("\r\n");
        }
    } else if (strncmp(cmd, "write ", 6) == 0) {
        char *v = strchr(cmd + 6, ' ');
        size_t n;

        if (v == NULL) {
            say("Usage: write <path> <value>\r\n");
        } else {
            *v++ = '\0';
            n = strlen(v);
            if (n > 0 && (v[0] == '"' || v[0] == '\'')) {
                if (n > 1 && v[n - 1] == v[0]) {
                    n--;
                }
                v++;
                n--;
            }
            file_put(cmd + 6, v, n);
        }
    }
    say("tikuOS:/> ");
}

/** @brief The transport's write: what the desktop typed at the board. */
static int
board_hears(const void *buf, size_t len, void *ctx)
{
    const char *p = buf;
    size_t i;

    (void)ctx;
    for (i = 0; i < len; i++) {
        if (p[i] == '\r') {
            continue;
        }
        if (p[i] == '\n') {
            line[line_len] = '\0';
            board_line(line);
            line_len = 0;
            continue;
        }
        if ((line_limit == 0 || line_len < (size_t)line_limit) &&
            line_len + 1 < sizeof line) {
            line[line_len++] = p[i];
        }
    }
    return (int)len;
}

/*---------------------------------------------------------------------------*/
/* THE CHECKS                                                                */
/*---------------------------------------------------------------------------*/

static char text[VALUE_MAX];

/** @brief @p n bytes of a recognisable value, with no space or line break. */
static const char *
value_of(size_t n, char c)
{
    size_t i;

    for (i = 0; i < n; i++) {
        text[i] = (char)(c + (char)(i % 20));
    }
    text[n] = '\0';
    return text;
}

static void
the_reads(tiku_backend_t *b)
{
    static char buf[VALUE_MAX];
    int rc;

    printf("reading a board's /data files\n");
    file_put("/data/fits", value_of(511, 'a'), 511);
    rc = b->ops->read(b, "/data/fits", buf, sizeof buf);
    check(rc == 511 && memcmp(buf, value_of(511, 'a'), 511) == 0,
          "a 511-byte file is read whole");

    file_put("/data/long", value_of(600, 'A'), 600);
    rc = b->ops->read(b, "/data/long", buf, sizeof buf);
    printf("  (a 600-byte file read back as %d bytes)\n", rc);
    check(rc < 0,
          "a file longer than a value holds is refused, not returned cut");
}

static void
the_writes(tiku_backend_t *b)
{
    char err[160];
    int rc, i;

    printf("writing a board's /data files\n");
    line_limit = 0;
    err[0] = '\0';
    rc = b->ops->write(b, "/data/w511", value_of(511, 'k'), 511, err,
                       sizeof err);
    i = file_find("/data/w511");
    check(rc == 0 && i >= 0 && strlen(file_value[i]) == 511,
          "a 511-byte value is written whole");

    err[0] = '\0';
    rc = b->ops->write(b, "/data/w600", value_of(600, 'm'), 600, err,
                       sizeof err);
    i = file_find("/data/w600");
    printf("  (a 600-byte write returned %d; the board holds %d bytes;"
           " \"%s\")\n", rc, (i >= 0) ? (int)strlen(file_value[i]) : -1,
           err);
    check(rc < 0 && err[0] != '\0',
          "a value longer than one write carries fails with an error");
    check(i < 0, "and nothing cut short is left on the board");

    line_limit = 255;
    err[0] = '\0';
    rc = b->ops->write(b, "/data/small", value_of(100, 'p'), 100, err,
                       sizeof err);
    i = file_find("/data/small");
    check(rc == 0 && i >= 0 && strcmp(file_value[i], value_of(100, 'p')) == 0,
          "a value that fits the board's line is written and confirmed");

    err[0] = '\0';
    rc = b->ops->write(b, "/data/small", value_of(120, 'q'), 120, err,
                       sizeof err);
    i = file_find("/data/small");
    check(rc == 0 && i >= 0 && strcmp(file_value[i], value_of(120, 'q')) == 0,
          "and so is a new value for a file already read");

    err[0] = '\0';
    rc = b->ops->write(b, "/data/w300", value_of(300, 'r'), 300, err,
                       sizeof err);
    i = file_find("/data/w300");
    printf("  (a 300-byte write over a 255-character line returned %d; the"
           " board holds %d bytes; \"%s\")\n", rc,
           (i >= 0) ? (int)strlen(file_value[i]) : -1, err);
    check(rc < 0 && err[0] != '\0',
          "a value the board's line cuts short fails with an error");
}

int
main(void)
{
    int sv[2];
    tiku_tx_t *tx;
    tiku_backend_t *b;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        printf("  [FAIL] no socket pair\n");
        return 1;
    }
    board_fd = sv[1];
    tx = tiku_tx_open_custom(sv[0], board_hears, NULL, NULL, "fake board");
    b = (tx != NULL) ? tiku_backend_tiku_open_tx(tx) : NULL;
    check(b != NULL, "the fake board is mounted");
    if (b == NULL) {
        return 1;
    }
    the_reads(b);
    the_writes(b);
    tiku_backend_close(b);
    close(sv[0]);
    close(sv[1]);
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
