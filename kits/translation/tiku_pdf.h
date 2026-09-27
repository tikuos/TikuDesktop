/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf.h - reading a PDF: its pages, and what they say.
 *
 * Written rather than linked, like the pictures beside it: a PDF is
 * objects in a file with a table saying where each is, and the kinds
 * of document read here -- datasheets, manuals, papers -- use a small
 * part of what the format allows (docs/plans/PLAN-pdf.md).  Nothing
 * here draws yet; the text of a page is what this stage gives.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PDF_H_
#define TIKU_PDF_H_

#include <stddef.h>

typedef struct tiku_pdf tiku_pdf_t;

/** @brief Why a file did not open. */
typedef enum {
    TIKU_PDF_OK = 0,
    TIKU_PDF_NOT_PDF,           /* no %PDF header                       */
    TIKU_PDF_UNSUPPORTED,       /* encrypted, or a version this refuses */
    TIKU_PDF_BAD                /* recognised, and beyond repair        */
} tiku_pdf_err_t;

/** @brief Whether @p path holds a PDF, by its first bytes. */
int tiku_pdf_is_pdf(const char *path);

/**
 * @brief Open @p path.
 *
 * A cross-reference table that lies is rebuilt by reading the whole
 * file, so a damaged document opens as far as its objects allow.
 *
 * @return TIKU_PDF_OK with @p out set; @p out is untouched otherwise.
 */
tiku_pdf_err_t tiku_pdf_open(const char *path, tiku_pdf_t **out);

/** @brief Open from bytes, which are copied. */
tiku_pdf_err_t tiku_pdf_decode(const void *bytes, size_t len,
                               tiku_pdf_t **out);

void tiku_pdf_close(tiku_pdf_t *doc);

/** @brief How many pages the document has. */
int tiku_pdf_pages(const tiku_pdf_t *doc);

/** @brief Whether the cross-reference table had to be rebuilt. */
int tiku_pdf_repaired(const tiku_pdf_t *doc);

/**
 * @brief The size of @p page (from 0) in points, and its rotation.
 *
 * The MediaBox, inherited from the page tree when the page has none;
 * a page without one is Letter.  @p rotate is 0, 90, 180 or 270.
 * @return 0, or -1 when there is no such page.
 */
int tiku_pdf_page_size(tiku_pdf_t *doc, int page, double *w, double *h,
                       int *rotate);

/**
 * @brief The words on @p page, as UTF-8 lines, in reading order as far
 *        as the page's own order goes.
 *
 * Text is placed where the content stream puts it: a jump down the
 * page ends a line, a gap along it becomes a space.  Fonts without a
 * text mapping give what their encoding says; a symbol font with
 * neither gives nothing for those glyphs.
 * @return a string the caller frees, "" for a page without text, or
 *         NULL when the page could not be read.
 */
char *tiku_pdf_page_text(tiku_pdf_t *doc, int page);

/** @brief The document's title from its Info dictionary, or NULL. */
char *tiku_pdf_title(tiku_pdf_t *doc);

#endif /* TIKU_PDF_H_ */
