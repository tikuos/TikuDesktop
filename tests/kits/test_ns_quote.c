/* The namespace client's quoting: a value reaches the board's shell as one
 * argument, or is refused.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tiku_ns.h"

/** @brief The board's own tokeniser, from shell/tiku_shell_parser.c. */
static int tokens(char *line, const char *argv[], int max)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            argv[argc++] = p;
            while (*p && *p != q) p++;
            if (*p) *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = '\0';
        }
    }
    return argc;
}

/** @brief "write /p <value>" parses back to exactly @p value. */
static void round_trip(const char *value)
{
    char quoted[128], line[160];
    const char *argv[8];
    assert(tiku_ns_quote(value, quoted, sizeof quoted) == 0);
    snprintf(line, sizeof line, "write /p %s", quoted);
    assert(tokens(line, argv, 8) == 3);
    assert(!strcmp(argv[2], value));
}

int main(void)
{
    char out[64];
    round_trip("plainboard");
    round_trip("Lab sensor");                    /* was cut to "Lab" */
    round_trip("nvm.tier=16384 op=7 identity=00ff expect=3:5 method=erase");
    round_trip("");                              /* was "Usage:" */
    round_trip("say \"hi\" now");
    round_trip("it's here");
    round_trip("\"quoted");                      /* lost its quote */
    round_trip("'single");
    round_trip("a\"b");                          /* inner quote, no space */
    assert(tiku_ns_quote("plainboard", out, sizeof out) == 0 &&
           !strcmp(out, "plainboard"));          /* unchanged when safe */
    assert(tiku_ns_quote("both \" and '", out, sizeof out) == -1);
    assert(tiku_ns_quote("x\nrm /data/f", out, sizeof out) == -1);
    assert(tiku_ns_quote("x\r", out, sizeof out) == -1);
    assert(tiku_ns_quote("a b", out, 5) == -1);  /* "\"a b\"" needs 6 */
    assert(tiku_ns_quote("a b", out, 6) == 0);
    puts("PASS: values reach the board's shell as one argument, or not at all");
    return 0;
}
