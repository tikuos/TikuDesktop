/* Editable defaults over the shared application registry.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TRK_ASSOCIATIONS_H_
#define TIKU_TRK_ASSOCIATIONS_H_
#include "tiku_trk_launch.h"
#include "tiku_trk_types.h"
#include "tiku_state.h"
#define TIKU_TRK_ASSOC_MAX 128
typedef struct {
    char type[TIKU_TYPE_MAX];
    char label[64];
    char saved[TIKU_TRK_APP_NAME_MAX];
    char chosen[TIKU_TRK_APP_NAME_MAX]; /* empty means automatic */
} tiku_trk_association_t;
typedef struct {
    tiku_trk_association_t row[TIKU_TRK_ASSOC_MAX];
    int count;
} tiku_trk_associations_t;
/** @brief Gather document types from the type and application registries. */
void tiku_trk_associations_load(tiku_trk_associations_t *model,
    const tiku_trk_apps_t *apps, const tiku_trk_types_t *types,
    tiku_store_t *store);
/** @brief Save edited rows only, rolling back on write/flush failure. */
int tiku_trk_associations_save(tiku_trk_associations_t *model,
    tiku_store_t *store);
/** @brief Whether any draft differs from its saved preference. */
int tiku_trk_associations_changed(const tiku_trk_associations_t *model);
#endif
