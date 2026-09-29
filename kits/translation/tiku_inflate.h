/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_inflate.h - DEFLATE decompression (RFC 1951).
 *
 * Written rather than linked because the one thing this port needed from
 * zlib was the decompressor, and a PNG that will not decompress is a file
 * that shows no thumbnail.  Decompression only: nothing here compresses,
 * and the PNG writer's stored blocks stay as they are.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_INFLATE_H_
#define TIKU_INFLATE_H_

#include <stddef.h>

/**
 * @brief Inflate a raw DEFLATE stream.
 *
 * @param src   Compressed bytes, with no zlib or gzip wrapper.
 * @param out   Where to write; the caller must know the size already, which
 *              PNG does (its rows have a fixed width).
 * @return Bytes written, or -1 on a malformed stream or an overrun.
 */
long tiku_inflate(const unsigned char *src, long slen,
                      unsigned char *out, long omax);

/**
 * @brief Inflate a zlib stream (RFC 1950): a two-byte header, then DEFLATE.
 *
 * The trailing Adler-32 is not checked -- a corrupt PNG shows a wrong
 * thumbnail at worst, and a checksum that costs a pass over every byte is
 * not worth that.
 */
long tiku_inflate_zlib(const unsigned char *src, long slen,
                           unsigned char *out, long omax);

/**
 * @brief Inflate into memory this allocates, zlib-wrapped or raw.
 *
 * For a stream whose size nobody stated, as a PDF's are.  A stream cut
 * short or corrupt past some point gives its head, because a page whose
 * last word is missing still reads; nothing at all is -1.
 * @param omax  The most @p out may grow to.
 * @return Bytes written to @p out, which the caller frees; -1 with @p out
 *         untouched when nothing could be inflated.
 */
long tiku_inflate_alloc(const unsigned char *src, long slen,
                        unsigned char **out, long omax, int zlib);

/* ------------------------------------------------------------------ */
/* Streams: decoded as they are read, for data too big to hold whole.  */

/** @brief Where a stream's compressed bytes come from: up to @p max into
 *         @p buf.  @return how many, 0 at the end, -1 for an error. */
typedef long (*tiku_inflate_fill_t)(void *ctx, unsigned char *buf, long max);

/** @brief A DEFLATE stream being decoded; see tiku_inflate_stream.c. */
typedef struct tiku_inflater tiku_inflater_t;

#define TIKU_INFLATE_DEFLATE64 1    /* 64 KB window, ZIP method 9      */

/** @brief A decoder asking @p fill for its input as it goes; @p flags
 *         may name Deflate64.  @return it, or NULL out of memory. */
tiku_inflater_t *tiku_inflater_new(tiku_inflate_fill_t fill, void *ctx,
                                   int flags);

/**
 * @brief Up to @p max decoded bytes into @p out.
 * @return how many; 0 once the final block has ended; -1 for a stream
 *         that is malformed, cut short, or whose @c fill failed (bytes
 *         decoded before that are handed out first).
 */
long tiku_inflater_read(tiku_inflater_t *z, unsigned char *out, long max);

/** @brief Whether the final block ended and everything was handed out. */
int tiku_inflater_done(const tiku_inflater_t *z);

/** @brief Once done, the bytes it read past the stream's end, up to @p max
 *         of them, taken out of it: a gzip trailer, the next member.
 *  @return how many. */
long tiku_inflater_tail(tiku_inflater_t *z, unsigned char *out, long max);

void tiku_inflater_free(tiku_inflater_t *z);

/* ------------------------------------------------------------------ */
/* Writing: what the readers above read.                               */

/** @brief @p n bytes as a zlib stream (RFC 1950 around RFC 1951): LZ77
 *         over a 32 KB window, in one block of the fixed codes.
 *  @return a malloc'd buffer, its length in @p outlen; NULL out of
 *          memory. */
unsigned char *tiku_deflate_zlib(const unsigned char *in, size_t n,
                                 size_t *outlen);

#endif /* TIKU_INFLATE_H_ */
