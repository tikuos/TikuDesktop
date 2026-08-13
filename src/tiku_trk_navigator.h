/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_navigator.h - bounded browser history.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_NAVIGATOR_H_
#define TIKU_TRK_NAVIGATOR_H_

#include <stddef.h>

#include "tiku_trk_model.h"

#define TIKU_TRK_NAV_HISTORY_MAX 32

typedef struct {
    char back[TIKU_TRK_NAV_HISTORY_MAX][TIKU_TRK_PATH_MAX];
    char forward[TIKU_TRK_NAV_HISTORY_MAX][TIKU_TRK_PATH_MAX];
    int  nback;
    int  nforward;
} tiku_trk_navigator_t;

/** @brief Start a history whose only location is @p path. */
void tiku_trk_navigator_init(tiku_trk_navigator_t *n, const char *path);

/** @brief The location the history is currently showing. */
const char *tiku_trk_navigator_current(const tiku_trk_navigator_t *n);

/** @brief Record a newly visited location and discard the forward branch. */
void tiku_trk_navigator_set(tiku_trk_navigator_t *n, const char *path);

/** @brief Whether another destination exists in either history direction. */
int tiku_trk_navigator_can_back(const tiku_trk_navigator_t *n);
int tiku_trk_navigator_can_forward(const tiku_trk_navigator_t *n);

/** @brief Step through history and copy the destination to @p out. */
int tiku_trk_navigator_back(tiku_trk_navigator_t *n, char *out, size_t max);
int tiku_trk_navigator_forward(tiku_trk_navigator_t *n, char *out,
                               size_t max);

/** @brief Resolve one enclosing folder; the root returns false. */
int tiku_trk_navigator_parent(const char *path, char *out, size_t max);

#endif /* TIKU_TRK_NAVIGATOR_H_ */
