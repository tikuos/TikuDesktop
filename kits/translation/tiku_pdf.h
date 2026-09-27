/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf.h - reading a PDF: its pages, what they say, how they look.
 *
 * Written rather than linked, like the pictures beside it: a PDF is
 * objects in a file with a table saying where each is, and the kinds
 * of document read here -- datasheets, manuals, papers -- use a small
 * part of what the format allows (docs/plans/PLAN-pdf.md).  A page
 * gives its text as lines, or its look as pixels at any scale.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PDF_H_
#define TIKU_PDF_H_

#include <stddef.h>
#include <stdint.h>

typedef struct tiku_pdf tiku_pdf_t;

/** @brief Why a file did not open. */
typedef enum {
    TIKU_PDF_OK = 0,
    TIKU_PDF_NOT_PDF,           /* no %PDF header                       */
    TIKU_PDF_UNSUPPORTED,       /* encrypted by a handler this lacks    */
    TIKU_PDF_BAD,               /* recognised, and beyond repair        */
    TIKU_PDF_PASSWORD           /* encrypted: the password does not open it */
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

/**
 * @brief The same for an encrypted document, with @p password (UTF-8),
 *        its user's or its owner's.
 *
 * The standard handler: RC4 and AES-128 (revisions 2 to 4) and AES-256
 * (5 and 6).  The plain calls use the empty password, which is what
 * opens most encrypted documents.
 */
tiku_pdf_err_t tiku_pdf_open_password(const char *path, const char *password,
                                      tiku_pdf_t **out);
/** @brief The same from bytes, which are copied. */
tiku_pdf_err_t tiku_pdf_decode_password(const void *bytes, size_t len,
                                        const char *password,
                                        tiku_pdf_t **out);

/** @brief Whether the document's author allows its text to be copied
 *         out; always, for one not encrypted or opened by its owner. */
int tiku_pdf_may_copy(const tiku_pdf_t *doc);

/** @brief Free the document and every object read from it. */
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

/** @brief One character of a page's text, and the box it stands in on
 *         the page as drawn at scale 1: points, rotation applied, y down. */
typedef struct {
    uint32_t cp;                    /* a made-up break has an empty box   */
    float    x0, y0, x1, y1;
} tiku_pdf_char_t;

/**
 * @brief The characters of @p page, in the order and with the spaces and
 *        line breaks tiku_pdf_page_text() gives, each where it is drawn.
 *
 * What a search highlights and a selection copies.
 * @return how many, with @p out to free(); -1 when the page is unreadable.
 */
long tiku_pdf_page_chars(tiku_pdf_t *doc, int page, tiku_pdf_char_t **out);

/** @brief Where a link or an outline entry leads: a page and the point to
 *         bring into view on it (as drawn at scale 1), or an address. */
typedef struct {
    int    page;                    /* from 0; -1 when not in the document */
    double left, top;
    char  *uri;                     /* an address outside it, or NULL     */
} tiku_pdf_dest_t;

/** @brief One entry of the outline (a document's bookmarks). */
typedef struct {
    char           *title;          /* UTF-8                              */
    int             level;          /* 0 at the top                       */
    int             below;          /* how many entries follow under it   */
    int             open;           /* the document shows it unfolded     */
    tiku_pdf_dest_t to;
} tiku_pdf_outline_t;

/**
 * @brief The outline, depth first: an entry, then everything under it.
 * @return how many entries, with @p out to give back through
 *         tiku_pdf_outline_free(); 0 for a document without one.
 */
int tiku_pdf_outline(tiku_pdf_t *doc, tiku_pdf_outline_t **out);
void tiku_pdf_outline_free(tiku_pdf_outline_t *items, int n);

/** @brief A link on a page: its area on the page as drawn at scale 1. */
typedef struct {
    float           x0, y0, x1, y1;
    tiku_pdf_dest_t to;
} tiku_pdf_link_t;

/**
 * @brief The links on @p page that lead somewhere: a place in the
 *        document, by name or directly, or an address outside it.
 * @return how many, with @p out to give back through tiku_pdf_links_free();
 *         -1 when there is no such page.
 */
int tiku_pdf_links(tiku_pdf_t *doc, int page, tiku_pdf_link_t **out);
void tiku_pdf_links_free(tiku_pdf_link_t *links, int n);

/** @brief A page drawn: 0xAARRGGBB pixels, top row first, opaque. */
typedef struct {
    int       w, h;
    uint32_t *px;                   /* w*h, owned; free with _pixmap_free */
} tiku_pdf_pixmap_t;

/**
 * @brief Draw @p page at @p scale pixels per point, rotation applied.
 *
 * What the format allows and this does not draw yet is left out, never
 * guessed at: the page is still readable around it.
 * @return 0 with @p out filled, or -1 when there is no such page or no
 *         room for it.
 */
int tiku_pdf_render(tiku_pdf_t *doc, int page, double scale,
                    tiku_pdf_pixmap_t *out);

/**
 * @brief Draw the window @p x0, @p y0, @p w by @p h pixels of the page as
 *        tiku_pdf_render() would lay it out at @p scale.
 *
 * A band of rows at a time keeps the memory to the band, which is how a
 * small machine would page through a datasheet.
 */
int tiku_pdf_render_rect(tiku_pdf_t *doc, int page, double scale, int x0,
                         int y0, int w, int h, tiku_pdf_pixmap_t *out);

/** @brief The size tiku_pdf_render() would draw @p page at @p scale. */
int tiku_pdf_render_size(tiku_pdf_t *doc, int page, double scale, int *w,
                         int *h);

/** @brief Give a drawn page's pixels back. */
void tiku_pdf_pixmap_free(tiku_pdf_pixmap_t *p);

/**
 * @brief Where to find faces to stand in for fonts the file does not
 *        embed: a folder of TrueType files, as the desktop's fonts folder.
 *
 * Without one, the machine's own faces are looked for in the usual
 * places; without those, such text is left out.
 */
void tiku_pdf_fonts(tiku_pdf_t *doc, const char *dir);

#endif /* TIKU_PDF_H_ */
