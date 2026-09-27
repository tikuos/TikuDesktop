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

#endif /* TIKU_INFLATE_H_ */
