/*
 * TikuOS kits -- archive stream regression tests.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_archive_private.h"
#include "tiku_inflate.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails;

/** @brief Count a failed contract check. */
static void check(int ok, const char *id)
{
    if (!ok) { printf("FAIL %s\n", id); fails++; }
}

/** @brief Open a file stream containing a complete or truncated empty gzip. */
static void trailers(void)
{
    unsigned char gz[] = {31,139,8,0,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,0};

    for (int cut = 0; cut <= 8; cut++) {
        FILE *f = tmpfile();
        unsigned char out[16];
        arc_stream_t *s;
        long n;

        if (!f) { check(0, "setup"); return; }
        fwrite(gz, 1, sizeof gz - cut, f); fflush(f);
        s = arc_gzip_stream(arc_file_stream(fileno(f), 0, sizeof gz - cut,
                                          NULL), NULL, 0);
        n = s ? arc_read(s, out, sizeof out) : -1;
        check(s && (cut ? n < 0 && s->err == TIKU_ARCHIVE_DAMAGED : n == 0),
              "D-083");
        arc_close(s); fclose(f);
    }
}

/** @brief The unspecified-length stream reads through EOF from its offset. */
static void whole_file(void)
{
    FILE *f = tmpfile();
    unsigned char out[16] = {0};
    arc_stream_t *s;

    if (!f) { check(0, "setup"); return; }
    fwrite("012345", 1, 6, f); fflush(f);
    s = arc_file_stream(fileno(f), 2, -1, NULL);
    check(s && arc_read(s, out, sizeof out) == 4 &&
          !memcmp(out, "2345", 4) && arc_read(s, out, sizeof out) == 0,
          "D-105");
    arc_close(s); fclose(f);
}

/** @brief Supply a truncated Deflate64 stream once, then EOF. */
static long truncated(void *ctx, unsigned char *out, long max)
{
    static const unsigned char bytes[] = {115, 28, 5};
    int *sent = ctx;

    if (*sent || max < 3) return 0;
    memcpy(out, bytes, 3); *sent = 1;
    return 3;
}

/** @brief An incomplete sixteen-bit length cannot emit a copied match. */
static void deflate64(void)
{
    int sent = 0;
    unsigned char out;
    tiku_inflater_t *z = tiku_inflater_new(truncated, &sent,
                                          TIKU_INFLATE_DEFLATE64);

    check(z && tiku_inflater_read(z, &out, 1) == 1 && out == 'A' &&
          tiku_inflater_read(z, &out, 1) < 0, "D-084");
    tiku_inflater_free(z);
}

/** @brief Run the archive stream regressions. */
int main(void)
{
    trailers(); whole_file(); deflate64();
    printf("%s: archive audit\n", fails ? "FAIL" : "PASS");
    return !!fails;
}
