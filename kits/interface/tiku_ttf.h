/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ttf.h - reading a font somebody dropped in.
 *
 * A TrueType file is a table directory, a character map and a set of
 * quadratic outlines; rasterising one is the same signed-area coverage
 * the icon renderer resolves its paths with.  No font library: the point
 * of the fonts directory is that a device can be handed a face over a
 * wire and draw with it, which rules out asking the host for help.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TTF_H_
#define TIKU_TTF_H_

#include <stddef.h>

#include "tiku_glyphpath.h"

typedef struct tiku_ttf tiku_ttf_t;

/**
 * @brief Open a TrueType file.
 *
 * @return NULL for anything that cannot be drawn with -- a PostScript-outline
 *         .otf included, which says so in its header rather than lying.
 */
tiku_ttf_t *tiku_ttf_open(const char *path);
/** @brief Open a face from bytes, which are copied. */
tiku_ttf_t *tiku_ttf_open_mem(const unsigned char *data, size_t len);
void tiku_ttf_close(tiku_ttf_t *ttf);

/* For a reader that places glyphs itself -- a PDF names them by index
 * and draws them at any size and slant -- the face by its parts. */

/** @brief Units per em. */
int tiku_ttf_upem(const tiku_ttf_t *ttf);
/** @brief How many glyphs the face has. */
int tiku_ttf_glyphs(const tiku_ttf_t *ttf);
/** @brief The glyph the character map gives @p cp, or 0. */
unsigned tiku_ttf_glyph(const tiku_ttf_t *ttf, unsigned cp);
/** @brief Glyph @p gid's advance, in font units. */
int tiku_ttf_advance(const tiku_ttf_t *ttf, unsigned gid);
/**
 * @brief Glyph @p gid's outline into @p path, through @p hint, whose
 *        scale is applied to font units (a CFF face's own units).
 * @return 1 when it was drawn, an empty glyph included.
 */
int tiku_ttf_outline(const tiku_ttf_t *ttf, unsigned gid,
                     const tiku_hint_t *hint, tiku_path_t *path);

/** @brief The family name the file gives itself. */
const char *tiku_ttf_family(const tiku_ttf_t *ttf);

/** @brief Whether the face calls itself bold. */
int tiku_ttf_bold(const tiku_ttf_t *ttf);

/** @brief Whether @p path looks like a face that could be opened, cheaply. */
int tiku_ttf_is_font(const char *path);

/**
 * @brief Ascent and line height in whole pixels at @p px.
 *
 * Both may be NULL.
 */
void tiku_ttf_metrics(const tiku_ttf_t *ttf, int px,
                           int *ascent, int *height);

/** @brief One rasterised glyph.  @c cover is w*h bytes of coverage. */
typedef struct {
    int            adv;
    int            w, h;
    int            ox, oy;      /* from the pen, and from the baseline */
    unsigned char *cover;
} tiku_ttf_glyph_t;

/**
 * @brief Draw @p cp at @p px pixels.
 *
 * A code point the file has no glyph for is not drawn: the caller shows
 * what it shows for anything else it hasn't got.
 *
 * @return 1 when @p out was filled, 0 otherwise.
 */
int tiku_ttf_render(const tiku_ttf_t *ttf, unsigned cp, int px,
                         tiku_ttf_glyph_t *out);

/** @brief Whether the file has a glyph for @p cp at all. */
int tiku_ttf_has(const tiku_ttf_t *ttf, unsigned cp);

void tiku_ttf_free_glyph(tiku_ttf_glyph_t *glyph);

#endif /* TIKU_TTF_H_ */
