/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cff.h - the other kind of outline.
 *
 * An OpenType file with PostScript outlines keeps them in a CFF table:
 * a stack of INDEXes and DICTs, and per glyph a Type 2 charstring, which
 * is a little program that draws.  Sixty of the three hundred faces on a
 * plain Fedora are these, so refusing them refuses a fifth of what a
 * person could drop in the fonts folder.
 *
 * Cubics, where TrueType has quadratics; both end up in the same path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CFF_H_
#define TIKU_CFF_H_

#include <stddef.h>

#include "tiku_glyphpath.h"

typedef struct tiku_cff tiku_cff_t;

/**
 * @brief Read the CFF table at @p data, @p len bytes of it.
 *
 * The bytes are borrowed, not copied: they must outlive the handle.
 *
 * @return NULL for a table nothing here can draw from.
 */
tiku_cff_t *tiku_cff_open(const unsigned char *data, size_t len);
void tiku_cff_close(tiku_cff_t *cff);

/**
 * @brief Run glyph @p gid's charstring into @p path.
 *
 * Coordinates are pushed in device space: multiplied by @p scale, with y
 * downwards, which is what the path and everything below it expect.
 *
 * @return 1 when the glyph was drawn (an empty glyph counts).
 */
int tiku_cff_outline(const tiku_cff_t *cff, unsigned gid,
                          const tiku_hint_t *hint,
                          tiku_path_t *path);

/** @brief Units per em as the CFF's own FontMatrix states it. */
float tiku_cff_upem(const tiku_cff_t *cff);

/* A PDF names a glyph by its name, its code in the font's own encoding
 * or, in a CID-keyed font, its CID; the charset answers all three. */

/** @brief The glyph called @p name, or 0 when the font has none. */
unsigned tiku_cff_glyph_named(const tiku_cff_t *cff, const char *name);
/** @brief The glyph the font's own encoding gives @p code, or 0: the
 *        standard encoding, or a custom one with its supplements. */
unsigned tiku_cff_glyph_coded(const tiku_cff_t *cff, unsigned code);
/** @brief The glyph with CID @p cid in a CID-keyed font, or 0. */
unsigned tiku_cff_glyph_cid(const tiku_cff_t *cff, unsigned cid);
/** @brief Whether the font is CID-keyed. */
int tiku_cff_is_cid(const tiku_cff_t *cff);
/** @brief How many glyphs the font has. */
int tiku_cff_glyphs(const tiku_cff_t *cff);

/** @brief Standard string @p sid of the 391 every CFF shares, or NULL. */
const char *tiku_cff_standard_string(unsigned sid);

#endif /* TIKU_CFF_H_ */
