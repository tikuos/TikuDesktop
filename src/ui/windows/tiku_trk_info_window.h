/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_info_window.h - Get Info window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_INFO_WINDOW_H_
#define TIKU_TRK_INFO_WINDOW_H_

#include <stdint.h>

#include "tiku_desk_window.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_edit.h"
#include "tiku_trk_fs.h"
#include "tiku_trk_info.h"
#include "tiku_trk_model.h"
#include "tiku_trk_state.h"

#define TIKU_TRK_INFO_WINDOW_MAX 8

typedef enum {
    TIKU_TRK_INFO_HIT_NONE = 0,
    TIKU_TRK_INFO_HIT_TAB,
    TIKU_TRK_INFO_HIT_NAME,
    TIKU_TRK_INFO_HIT_ICON,
    TIKU_TRK_INFO_HIT_FIELD,
    TIKU_TRK_INFO_HIT_PERMISSION
} tiku_trk_info_hit_t;

typedef struct tiku_trk_info_windows tiku_trk_info_windows_t;

typedef struct {
    tiku_trk_info_windows_t *owner;
    int                      slot;
} tiku_trk_info_window_context_t;

struct tiku_trk_info_windows {
    tiku_trk_info_t                model[TIKU_TRK_INFO_WINDOW_MAX];
    tiku_desk_window_t            *window[TIKU_TRK_INFO_WINDOW_MAX];
    tiku_trk_info_window_context_t context[TIKU_TRK_INFO_WINDOW_MAX];
    tiku_desk_workspace_t         *workspace;
    tiku_trk_backend_t            *backend;
    tiku_trk_store_t              *store;
    tiku_trk_fs_t                 *fs;
    tiku_trk_alert_t              *alert;
    tiku_trk_edit_t                name;
    int                            renaming;
    /* The slot whose icon an in-flight drag is over, or -1: the icon
     * answers a hover the way a pose does, by its selected variant
     * (IV-058). */
    int                            drop_hot;
    int                           *dirty;
};

void tiku_trk_info_windows_init(tiku_trk_info_windows_t *controller,
    tiku_desk_workspace_t *workspace, tiku_trk_backend_t *backend,
    tiku_trk_store_t *store, tiku_trk_fs_t *fs, tiku_trk_alert_t *alert,
    int *dirty);
int tiku_trk_info_windows_open(tiku_trk_info_windows_t *controller,
                               const char *path);
void tiku_trk_info_windows_close(tiku_trk_info_windows_t *controller,
                                 int slot);
int tiku_trk_info_windows_slot(const tiku_trk_info_windows_t *controller,
                               const tiku_desk_window_t *window);
tiku_trk_info_t *tiku_trk_info_windows_model(
    tiku_trk_info_windows_t *controller, int slot);
tiku_desk_window_t *tiku_trk_info_windows_handle(
    tiku_trk_info_windows_t *controller, int slot);

tiku_trk_info_hit_t tiku_trk_info_windows_hit(
    tiku_trk_info_windows_t *controller, int slot, int x, int y, int *which);
void tiku_trk_info_windows_begin_rename(
    tiku_trk_info_windows_t *controller, int slot);

/** @return Nonzero if the focused Get Info window consumed the key. */
int tiku_trk_info_windows_key(tiku_trk_info_windows_t *controller,
    int key, unsigned modifiers, char character);

int tiku_trk_info_windows_calc_step(tiku_trk_info_windows_t *controller);
int tiku_trk_info_windows_device_gone(tiku_trk_info_windows_t *controller,
                                      uint64_t device);
int tiku_trk_info_windows_refresh(tiku_trk_info_windows_t *controller,
                                  int64_t now);

#endif /* TIKU_TRK_INFO_WINDOW_H_ */
