/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_background.h - the picture behind a folder or the desktop.
 *
 * A background is a node's own property: which picture, laid out which way.
 * It is kept in the state store beside the folder's other state, so a
 * namespace node that has no attributes can still carry one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_BACKGROUND_H_
#define TIKU_TRK_BACKGROUND_H_

#include <stddef.h>

#include "tiku_model.h"
#include "tiku_state.h"

/**
 * @brief The node a folder with no background of its own inherits from.
 *
 * The source looks the picture up on the folder and then on a template
 * node; this is that node, named rather than a real directory because the
 * store is keyed by name and needs no file to hang a record on.
 */
#define TIKU_TRK_BG_TEMPLATE_NODE "\x01template"

/** @brief The state record a background is kept under. */
#define TIKU_TRK_ATTR_BACKGROUND "_trk/background"

/** @brief How a picture is laid over the view it backs. */
typedef enum {
    TIKU_TRK_BG_AT_OFFSET = 0,   /* once, at a point of its own          */
    TIKU_TRK_BG_CENTERED,
    TIKU_TRK_BG_SCALED,          /* stretched to the whole view          */
    TIKU_TRK_BG_TILED
} tiku_trk_bg_mode_t;

typedef struct {
    char               path[TIKU_PATH_MAX];
    tiku_trk_bg_mode_t mode;
    int                x, y;     /* AT_OFFSET: where its corner sits     */
} tiku_trk_bg_t;

/**
 * @brief Read a record, "<mode> <x> <y> <path>".
 *
 * @return 1 when the record is one of ours, 0 otherwise.
 */
int tiku_trk_bg_parse(const char *rec, tiku_trk_bg_t *out);

/** @brief Write one.  @return length written. */
int tiku_trk_bg_format(const tiku_trk_bg_t *bg, char *out, size_t max);

/** @brief Whether @p node carries a background at all (AW-016's guard). */
int tiku_trk_bg_have(tiku_store_t *store, const char *node);

/** @brief Read @p node's background.  @return 1 when there is one. */
int tiku_trk_bg_load(tiku_store_t *store, const char *node,
                     tiku_trk_bg_t *out);

/** @brief Give @p node a background.  @return 1 when it was written. */
int tiku_trk_bg_save(tiku_store_t *store, const char *node,
                     const tiku_trk_bg_t *bg);

/**
 * @brief Where blit @p i of this background goes, in view coordinates.
 *
 * One rectangle for every mode but TILED, which walks the grid; the caller
 * stops when this answers 0.  The rectangle may fall outside the view, and
 * the caller clips -- a picture larger than the window is drawn from its
 * corner, not shrunk to fit.
 *
 * @return 1 when @p dst was filled, 0 when there is nothing more to draw.
 */
int tiku_trk_bg_place(const tiku_trk_bg_t *bg, int view_w, int view_h,
                      int img_w, int img_h, int i, int *dx, int *dy,
                      int *dw, int *dh);

/** @brief Where a default desktop picture is looked for (AW-016). */
#define TIKU_TRK_BG_DEFAULT_FILE "/.config/tracker/desktop-background"

/**
 * @brief Install a default desktop picture, once (AW-016).
 *
 * Only when @p node carries no background yet: a desktop the user has set,
 * or deliberately cleared, is not overwritten on the next boot.  The
 * picture goes at a golden-ratio x and nine tenths of the height, as the
 * source's logo does.  Nothing is installed when there is no default
 * picture on disk -- this port ships no wallpaper of its own.
 *
 * @return 1 when one was installed.
 */
int tiku_trk_bg_install_default(tiku_store_t *store, const char *node,
                                int screen_w, int screen_h);

#endif /* TIKU_TRK_BACKGROUND_H_ */
