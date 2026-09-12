/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_help_window.h - the help pages, read in a window.
 *
 * A page is a tiku_doc; this is what puts one on the screen and what
 * decides where a link goes.  The deciding is here rather than in the
 * kit because it is about files and mounts, which a kit may not know:
 * a name reaches a place in the page, a file name reaches a sibling
 * page, and a device path is handed back to the desktop to open.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_HELP_WINDOW_H_
#define TIKU_TRK_HELP_WINDOW_H_

#include "tiku_model.h"
#include "tiku_doc.h"
#include "tiku_dialog.h"
#include "tiku_scroll.h"
#include "tiku_window.h"

/**
 * @brief The page itself, as a control the keyboard can be put on.
 *
 * Named here because a caller that wants the reading keys rather than
 * the buttons has to say so, and a number copied out of the .c is a
 * number that will go stale.
 */
#define TIKU_TRK_HELP_CONTROL_PAGE 5

/** @brief How far back a reader may step. */
#define TIKU_TRK_HELP_HISTORY 16
/** @brief The largest page this window will read in. */
#define TIKU_TRK_HELP_PAGE_MAX TIKU_DOC_TEXT_MAX

/** @brief Where a link went. */
typedef enum {
    TIKU_TRK_HELP_NOWHERE = 0,  /* refused, and the status says why   */
    TIKU_TRK_HELP_ANCHOR,       /* a place in the page now shown      */
    TIKU_TRK_HELP_PAGE,         /* another page of the help folder    */
    TIKU_TRK_HELP_NAMESPACE     /* a path, handed to the opener       */
} tiku_trk_help_where_t;

/**
 * @brief Open a path that is not a help page.  Set by the desktop.
 *
 * @return nonzero when the place was there and was opened; zero leaves
 *         the window to say that it was not, rather than a link that
 *         looks followed and did nothing.
 */
typedef int (*tiku_trk_help_open_fn)(void *ctx, const char *path);

typedef struct {
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    int              *dirty;
    int               screen_w, screen_h;
    /*
     * The page is held through a pointer: a document is eighty
     * kilobytes of tables, and every other window controller here is
     * embedded in the desktop's own struct by value.
     */
    tiku_doc_t       *doc;
    tiku_scroll_t     bar;
    tiku_dialog_t     dialog;
    int               focus_link;
    int               laid_for;         /* the width the page was laid for */
    int               page_h;           /* and the height it is read in    */
    char              root[TIKU_PATH_MAX];
    char              page[TIKU_PATH_MAX];  /* under the root              */
    char              status[256];
    char              wanted[TIKU_PATH_MAX];/* the last path handed over    */
    char              anchor[128];      /* a place to reach once laid out  */
    int               oversize;        /* the page had more than fitted   */
    struct {
        char page[TIKU_PATH_MAX];
        int  at;
    } back[TIKU_TRK_HELP_HISTORY], forward[TIKU_TRK_HELP_HISTORY];
    int nback, nforward;
    tiku_trk_help_open_fn opener;
    void *opener_ctx;
} tiku_trk_help_window_t;

/** @brief Empty, with @p root the folder the pages are read from. */
void tiku_trk_help_window_init(tiku_trk_help_window_t *c,
                               tiku_workspace_t *workspace,
                               const char *root, int screen_w, int screen_h,
                               int *dirty);

/** @brief Who opens a path that is not a page.  Both may be NULL. */
void tiku_trk_help_window_opener(tiku_trk_help_window_t *c,
                                 tiku_trk_help_open_fn fn, void *ctx);

/** @brief Show the help folder's own first page, or say there is none. */
void tiku_trk_help_window_open(tiku_trk_help_window_t *c);

/**
 * @brief Follow @p href from the page now shown.
 *
 * A name beginning '#' is a place in this page; a plain file name is a
 * page beside it, refused if it climbs out of the help folder; a path
 * beginning '/' or "tiku:" is handed to the opener; anything else,
 * including a web address, is refused and said in the status line.
 */
tiku_trk_help_where_t tiku_trk_help_window_go(tiku_trk_help_window_t *c,
                                              const char *href);

/** @brief Step back, and forward again.  @return 1 when it moved. */
int tiku_trk_help_window_back(tiku_trk_help_window_t *c);
int tiku_trk_help_window_forward(tiku_trk_help_window_t *c);

/** @brief The folder's own first page.  @return 1 when it opened. */
int tiku_trk_help_window_home(tiku_trk_help_window_t *c);

/** @brief What the window last said, and the page it is showing. */
const char *tiku_trk_help_window_status(const tiku_trk_help_window_t *c);
const char *tiku_trk_help_window_page(const tiku_trk_help_window_t *c);
/** @brief The path last handed to the opener. */
const char *tiku_trk_help_window_wanted(const tiku_trk_help_window_t *c);

void tiku_trk_help_window_close(tiku_trk_help_window_t *c);
void tiku_trk_help_window_destroy(tiku_trk_help_window_t *c);
tiku_window_t *tiku_trk_help_window_handle(tiku_trk_help_window_t *c);

#endif /* TIKU_TRK_HELP_WINDOW_H_ */
