/* TikuArchive: ZIP, tar and gzip archives browsed and extracted through
 * the archive kit.  SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_ARCVIEW_H_
#define TIKU_ARCVIEW_H_

#include "tiku_window.h"

typedef struct tiku_arcview tiku_arcview_t;

/** @brief What the window asks of the application around it. */
typedef struct {
    /* A file to open (@p save 0), or a new folder's place and name to
     * extract into (@p save 1), put to the person; the answer comes back
     * through tiku_arcview_file() or tiku_arcview_extract_to(). */
    void (*pick)(void *ctx, int save, const char *start, const char *name);
    /* @p path opened as a double-click in Tracker would: a folder as a
     * window, a document in its program.  @return nonzero when it was. */
    int (*open_path)(void *ctx, const char *path);
    void *ctx;
} tiku_arcview_host_t;

/** @brief An empty viewer; asking the person for things is @p host's. */
tiku_arcview_t *tiku_arcview_new(tiku_workspace_t *workspace, int *dirty,
                                 const tiku_arcview_host_t *host);
/** @brief Open or raise the window without loading anything. */
void tiku_arcview_open(tiku_arcview_t *c);
/** @brief Close the window and let the archive go. */
void tiku_arcview_close(tiku_arcview_t *c);
/** @brief Detach from the application; workers finish on their own, and
 *         the files opened from inside archives are taken away. */
void tiku_arcview_destroy(tiku_arcview_t *c);
/** @brief The window, or NULL while it is closed. */
tiku_window_t *tiku_arcview_handle(tiku_arcview_t *c);
/** @brief Open and list @p path on a worker; the archive shown stays
 *         until the new one has opened, and stays if it cannot be. */
int tiku_arcview_file(tiku_arcview_t *c, const char *path);
/** @brief Extract into a new folder at @p path, the answer to Extract
 *         To...: the rows chosen, or everything when none is. */
int tiku_arcview_extract_to(tiku_arcview_t *c, const char *path);
/** @brief Each turn: progress drawn as an extraction goes. */
void tiku_arcview_tick(tiku_arcview_t *c);
/** @brief The shown archive's path, "" when there is none. */
const char *tiku_arcview_path(tiku_arcview_t *c);

#endif
