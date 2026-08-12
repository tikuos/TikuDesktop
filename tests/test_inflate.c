/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_inflate.c - DEFLATE against a checked-in corpus, and refusals.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_inflate.h"
#include "inflate_corpus.inc"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] IV-028    %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

/** @brief Inflate one corpus case and compare it byte for byte. */
static void
one(const char *name, const unsigned char *z, long zn,
    const unsigned char *want, long wn, const char *what)
{
    unsigned char out[4096];
    long got = tiku_trk_inflate_zlib(z, zn, out, (long)sizeof out);

    (void)name;
    check(got == wn && memcmp(out, want, (size_t)wn) == 0, what);
}

int
main(void)
{
    printf("inflating a corpus\n");
    one("repeat", z_repeat, (long)sizeof z_repeat, d_repeat,
        (long)sizeof d_repeat,
        "long matches and far distances come back byte for byte");
    one("noisy", z_noisy, (long)sizeof z_noisy, d_noisy,
        (long)sizeof d_noisy,
        "dynamic tables of almost all literals do too");
    one("tiny", z_tiny, (long)sizeof z_tiny, d_tiny, (long)sizeof d_tiny,
        "and a fixed-Huffman block, which has no tables of its own");
    one("stored", z_stored, (long)sizeof z_stored, d_stored,
        (long)sizeof d_stored, "as does a stored block");

    printf("\nwhat it must refuse\n");
    {
        unsigned char out[64];
        unsigned char buf[64];

        check(tiku_trk_inflate_zlib((const unsigned char *)"", 0, out,
                                    (long)sizeof out) == -1,
              "an empty stream is refused, not read past");

        memcpy(buf, z_tiny, sizeof z_tiny);
        buf[0] = 0x99;              /* header no longer a multiple of 31 */
        check(tiku_trk_inflate_zlib(buf, (long)sizeof z_tiny, out,
                                    (long)sizeof out) == -1,
              "a zlib header whose check bits do not add up is refused");

        /* Block type 3 is reserved and must never decode. */
        buf[0] = 0x78; buf[1] = 0x01; buf[2] = 0x07;
        check(tiku_trk_inflate_zlib(buf, 8, out, (long)sizeof out) == -1,
              "the reserved block type is refused");

        /* A stored block whose length disagrees with its complement is the
         * one corruption that would otherwise copy whatever follows. */
        buf[0] = 0x78; buf[1] = 0x01; buf[2] = 0x01;
        buf[3] = 0x04; buf[4] = 0x00;   /* LEN  = 4 */
        buf[5] = 0x00; buf[6] = 0x00;   /* ~LEN wrong */
        buf[7] = 'a'; buf[8] = 'b'; buf[9] = 'c'; buf[10] = 'd';
        check(tiku_trk_inflate_zlib(buf, 11, out, (long)sizeof out) == -1,
              "a stored block whose length disagrees with its complement "
              "is refused rather than copied");

        /* Truncation: the same stream, one byte short of its end. */
        check(tiku_trk_inflate_zlib(z_repeat, (long)sizeof z_repeat - 1,
                                    out, (long)sizeof out) == -1,
              "a truncated stream is refused rather than half-inflated");

        /* An output buffer too small must stop, not overrun. */
        check(tiku_trk_inflate_zlib(z_repeat, (long)sizeof z_repeat, out,
                                    16) == -1,
              "and a buffer too small stops the copy instead of running "
              "past its end");
    }

    printf("\n%s\n", fails ? "FAILURES" : "inflate holds");
    return fails ? 1 : 0;
}
