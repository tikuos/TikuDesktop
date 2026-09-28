/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fax.h - CCITT fax decoding: Group 3, one- and two-dimensional
 * (ITU-T T.4), and Group 4 (T.6).
 *
 * How a black-and-white scan is kept small, and so what the pictures of
 * older datasheets and scanned papers are.  Rows of bits come out, one
 * bit a pixel, as PDF's CCITTFaxDecode gives them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FAX_H_
#define TIKU_FAX_H_

/** @brief How the bits were written: PDF's CCITTFaxDecode parameters. */
typedef struct {
    int k;                  /* < 0 Group 4, 0 Group 3 1-D, > 0 mixed  */
    int columns;            /* pixels a row                           */
    int rows;               /* rows, or 0 to read to the data's end   */
    int byte_align;         /* each row starts on a byte              */
    int black_is_1;         /* black is a 1 bit rather than a 0       */
} tiku_fax_t;

/**
 * @brief Decode @p n bytes into rows of (columns + 7) / 8 bytes each.
 *
 * A row the data damages ends the picture there; rows asked for and not
 * read come out white.  EOLs, fill and the end-of-block codes are found
 * whether or not the writer said it wrote them.
 * @param out  Receives a malloc'd buffer the caller frees.
 * @return the bytes made, or -1 when nothing reads or @p omax is short.
 */
long tiku_fax_decode(const unsigned char *src, long n, const tiku_fax_t *p,
                     unsigned char **out, long omax);
/** @brief The same, saying in @p used how many bytes the rows took (an
 *         end of block after them included), for data that goes on. */
long tiku_fax_decode_used(const unsigned char *src, long n,
                          const tiku_fax_t *p, unsigned char **out,
                          long omax, long *used);

#endif /* TIKU_FAX_H_ */
