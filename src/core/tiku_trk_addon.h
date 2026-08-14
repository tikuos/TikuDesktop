/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TRK_ADDON_H_
#define TIKU_TRK_ADDON_H_

#include "tiku_trk_menu.h"
#include "tiku_trk_model.h"

#define TIKU_TRK_ADDON_MAX 32
#define TIKU_TRK_ADDON_COMMAND_BASE 1000

typedef struct {
    char name[48];
    char command[128];
    char type[TIKU_TRK_TYPE_MAX];
    char key;
    unsigned mods;
    int generic;
} tiku_trk_addon_t;

typedef struct {
    tiku_trk_addon_t item[TIKU_TRK_ADDON_MAX];
    int count;
    unsigned generation;
} tiku_trk_addons_t;

typedef int (*tiku_trk_addon_invoke_fn)(const tiku_trk_addon_t *addon,
                                        void *context);

void tiku_trk_addons_init(tiku_trk_addons_t *addons);
int tiku_trk_addons_register(tiku_trk_addons_t *addons, const char *name,
                             const char *command, const char *type, char key);
int tiku_trk_addons_rebind(tiku_trk_addons_t *addons, const char *settings);
int tiku_trk_addons_accepts(const tiku_trk_addon_t *addon,
                            const char *const *types, int ntypes);
int tiku_trk_addons_build_menu(const tiku_trk_addons_t *addons,
                               const char *const *types, int ntypes,
                               tiku_trk_menu_t *menu, int command_base);
int tiku_trk_addons_invoke(const tiku_trk_addons_t *addons, int command,
                           int command_base, tiku_trk_addon_invoke_fn fn,
                           void *context);

#endif
