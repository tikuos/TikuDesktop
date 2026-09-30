/* TikuDoc and TikuSlides: a Word, OpenDocument or PowerPoint file shown as
 * the pages it lays out to, and exported as a PDF.  The two applications
 * are this viewer under two names: a document is read a page's width at
 * a time, slides a slide at a time, and presented.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_OFFICEVIEW_H_
#define TIKU_OFFICEVIEW_H_

#include "tiku_window.h"

typedef struct tiku_officeview tiku_officeview_t;

/** @brief Which of the two applications the viewer is. */
typedef struct {
    const char *name;               /* "TikuDoc", "TikuSlides"           */
    int         slides;             /* slides: fitted whole, presented   */
} tiku_officeview_kind_t;

/** @brief What the window asks of the application around it. */
typedef struct {
    /* A file to open (@p save 0), or where to write a PDF (@p save 1,
     * @p name offered), put to the person; the answer comes back through
     * tiku_officeview_file() or tiku_officeview_export_to(). */
    void (*pick)(void *ctx, int save, const char *start, const char *name);
    void *ctx;
} tiku_officeview_host_t;

/** @brief An empty viewer; asking the person for things is @p host's. */
tiku_officeview_t *tiku_officeview_new(tiku_workspace_t *workspace,
                                       int *dirty,
                                       const tiku_officeview_kind_t *kind,
                                       const tiku_officeview_host_t *host);
/** @brief Open or raise the window without loading anything. */
void tiku_officeview_open(tiku_officeview_t *c);
/** @brief Detach from the application; workers finish on their own, and
 *         the viewer goes with the last of them. */
void tiku_officeview_destroy(tiku_officeview_t *c);
/** @brief The window, or NULL while it is closed. */
tiku_window_t *tiku_officeview_handle(tiku_officeview_t *c);
/** @brief Lay out and show @p path on a worker; the file shown stays
 *         until it is ready.  @return 0 when nothing was begun. */
int tiku_officeview_file(tiku_officeview_t *c, const char *path);
/** @brief The file shown written as a PDF at @p pdf, on a worker: the
 *         answer to Export as PDF.  @return 0 when nothing was begun. */
int tiku_officeview_export_to(tiku_officeview_t *c, const char *pdf);
/** @brief The file shown, "" when there is none. */
const char *tiku_officeview_path(tiku_officeview_t *c);

#endif
