/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_clip.h - the file clipboard.
 *
 * Tracker's clipboard holds no file content and copies nothing: it holds a
 * reference per item plus a mode saying whether the eventual Paste should
 * move or copy.  The entries are keyed by the node's IDENTITY rather than
 * its path, which is what lets an entry survive being renamed while it sits
 * there -- and what makes an item structurally unable to appear twice.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_CLIP_H_
#define TIKU_TRK_CLIP_H_

#include <stdint.h>

#include "tiku_trk_model.h"

#define TIKU_TRK_CLIP_MAX 256

/** @brief What a Paste of this entry should do. */
typedef enum {
    TIKU_TRK_CLIP_NONE = 0,
    TIKU_TRK_CLIP_COPY,
    TIKU_TRK_CLIP_MOVE           /* Cut: the item leaves where it is      */
} tiku_trk_clip_mode_t;

typedef struct {
    uint64_t             id;     /* the node's identity, and the key      */
    char                 path[TIKU_TRK_PATH_MAX];
    tiku_trk_clip_mode_t mode;
} tiku_trk_clip_entry_t;

typedef struct {
    tiku_trk_clip_entry_t item[TIKU_TRK_CLIP_MAX];
    int                   count;
    /* Bumped whenever the contents change, so a view can tell that what it
     * last drew is stale without comparing the whole list. */
    int                   serial;
} tiku_trk_clip_t;

void tiku_trk_clip_init(tiku_trk_clip_t *c);

/**
 * @brief Put @p models on the clipboard.
 *
 * @param replace Clear it first -- Cut and Copy do, "Cut more" and "Copy
 *                more" do not.
 * @param mode    COPY or MOVE.
 * @return How many entries the clipboard holds afterwards.
 */
int tiku_trk_clip_add(tiku_trk_clip_t *c, const tiku_trk_model_t *models,
                      int n, tiku_trk_clip_mode_t mode, int replace);

/** @brief What a Paste of the node @p id would do, or NONE. */
tiku_trk_clip_mode_t tiku_trk_clip_mode_of(const tiku_trk_clip_t *c,
                                           uint64_t id);

/**
 * @brief Follow an entry whose file was renamed or moved (FS-060).
 *
 * The entry is keyed by identity, so a path change updates the payload and
 * leaves the mode alone: an item cut and then renamed is still cut.
 *
 * @return 1 when an entry was updated.
 */
int tiku_trk_clip_moved(tiku_trk_clip_t *c, uint64_t id, const char *to);

/** @brief Drop the entry for @p id, if any (FS-058).  @return 1 if dropped. */
int tiku_trk_clip_removed(tiku_trk_clip_t *c, uint64_t id);

/** @brief Drop every entry whose file has gone.  @return How many went. */
int tiku_trk_clip_prune(tiku_trk_clip_t *c);

/**
 * @brief Spend the cuts a paste has just carried out (FS-055).
 *
 * A cut entry becomes a copy rather than leaving the clipboard: the file is
 * no longer where the entry said it was, and a second paste copies what it
 * moved.  Emptying instead would take the copied entries with it.
 *
 * @return How many entries changed.
 */
int tiku_trk_clip_spend_cuts(tiku_trk_clip_t *c);

/** @brief Empty it (FS-062). */
void tiku_trk_clip_clear(tiku_trk_clip_t *c);

/** @brief Remove just these entries, leaving the rest (Shift+Escape). */
int tiku_trk_clip_remove(tiku_trk_clip_t *c, const tiku_trk_model_t *models,
                         int n);

int tiku_trk_clip_count(const tiku_trk_clip_t *c);
const tiku_trk_clip_entry_t *tiku_trk_clip_at(const tiku_trk_clip_t *c,
                                              int i);

#endif /* TIKU_TRK_CLIP_H_ */
