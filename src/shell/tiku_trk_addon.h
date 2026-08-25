/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TRK_ADDON_H_
#define TIKU_TRK_ADDON_H_

#include "tiku_menu.h"
#include "tiku_model.h"

#define TIKU_TRK_ADDON_MAX 32
#define TIKU_TRK_ADDON_COMMAND_BASE 1000

typedef struct {
    char name[48];
    char command[128];
    char type[TIKU_TYPE_MAX];
    char key;
    unsigned mods;
    int generic;
    /* Found in the add-ons directory rather than compiled in: command is
     * the executable's own path, and invoking it is a process of its own
     * (CW-038's port shape -- no shared objects on TikuOS). */
    int external;
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
                               tiku_menu_t *menu, int command_base);
int tiku_trk_addons_invoke(const tiku_trk_addons_t *addons, int command,
                           int command_base, tiku_trk_addon_invoke_fn fn,
                           void *context);
int tiku_trk_addons_shortcut(const tiku_trk_addons_t *addons, char key,
                             unsigned mods, int command_base);

/**
 * @brief Refresh the externally discovered add-ons from @p dir (CW-038).
 *
 * Executable regular files register as external add-ons named after
 * themselves; a sidecar named "<file>.types" declares the type its first
 * line holds, and without one the add-on is generic.  Compiled-in entries
 * are left alone; what an earlier discovery found is replaced, so a
 * removed add-on leaves the menu (AW-023).
 *
 * @return How many were found.
 */
int tiku_trk_addons_discover(tiku_trk_addons_t *addons, const char *dir);

/**
 * @brief Run an external add-on against @p paths, in a process of its own.
 *
 * The separate-thread clause becomes a separate PROCESS here: stronger
 * isolation, and the only kind TikuOS's no-shared-objects rule leaves.
 *
 * @return 0 when the launch started.
 */
int tiku_trk_addons_run_external(const tiku_trk_addon_t *addon,
                                 const char *const *paths, int n);

#endif
