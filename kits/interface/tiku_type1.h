/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_type1.h - the oldest outline font: PostScript Type 1.
 *
 * What LaTeX embeds in every paper, and what came before CFF: a clear
 * text head with the encoding, then an eexec-encrypted body holding
 * each glyph as a Type 1 charstring, a smaller machine than CFF's with
 * flex and accents done through "other subroutines".  Glyphs are named,
 * never numbered, so a caller asks for one by its name.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TYPE1_H_
#define TIKU_TYPE1_H_

#include <stddef.h>

#include "tiku_glyphpath.h"

typedef struct tiku_t1 tiku_t1_t;

/**
 * @brief Read a Type 1 program from @p data (PFA, PFB, or a PDF's
 *        FontFile with its three lengths run together).
 *
 * The bytes are copied as far as they are needed.
 * @return NULL for anything that is not one.
 */
tiku_t1_t *tiku_t1_open(const unsigned char *data, size_t len);
void tiku_t1_close(tiku_t1_t *t1);

/** @brief Whether the font has a glyph called @p name. */
int tiku_t1_has(const tiku_t1_t *t1, const char *name);

/** @brief The glyph the font's own encoding gives @p code, or NULL. */
const char *tiku_t1_encoded(const tiku_t1_t *t1, unsigned code);

/** @brief Units per em from the FontMatrix; 1000 for nearly all. */
float tiku_t1_upem(const tiku_t1_t *t1);

/**
 * @brief Glyph @p name's outline into @p path through @p hint, and its
 *        advance in font units into @p adv (may be NULL).
 * @return 1 when it was drawn, an empty glyph included.
 */
int tiku_t1_outline(const tiku_t1_t *t1, const char *name,
                    const tiku_hint_t *hint, tiku_path_t *path, float *adv);

#endif /* TIKU_TYPE1_H_ */
