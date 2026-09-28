/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_saslprep.h - SASLprep (RFC 4013): a password put in the one form
 * both ends compare, however it was typed -- odd spaces made spaces and
 * invisible marks dropped, Unicode 3.2's NFKC, what may not appear
 * refused, and right-to-left text kept to one direction.  PDF 2.0 asks
 * it of an AES-256 document's password.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SASLPREP_H_
#define TIKU_SASLPREP_H_

#include <stddef.h>

/**
 * @brief SASLprep of UTF-8 @p in into @p out, @p cap bytes with the NUL
 *        that ends it; as a query, so unassigned code points pass.
 * @return the bytes made, or -1 when @p in is not UTF-8 or too long,
 *         holds what the profile prohibits, mixes directions, or does
 *         not fit.
 */
long tiku_saslprep(const char *in, char *out, size_t cap);

#endif /* TIKU_SASLPREP_H_ */
