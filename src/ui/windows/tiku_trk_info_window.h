/*
 * TikuTracker -- the file manager for TikuOS.
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

#include "tiku_window.h"
#include "tiku_tabs.h"
#include "tiku_trk_alert.h"
#include "tiku_field.h"
#include "tiku_fs.h"
#include "tiku_trk_info.h"
#include "tiku_model.h"
#include "tiku_state.h"

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
    tiku_window_t            *window[TIKU_TRK_INFO_WINDOW_MAX];
    tiku_tabs_t                    tabs[TIKU_TRK_INFO_WINDOW_MAX];
    tiku_trk_info_window_context_t context[TIKU_TRK_INFO_WINDOW_MAX];
    tiku_workspace_t         *workspace;
    tiku_backend_t            *backend;
    tiku_store_t              *store;
    tiku_fs_t                 *fs;
    tiku_trk_alert_t              *alert;
    tiku_field_t                name;
    int                            renaming;
    /* The slot whose icon an in-flight drag is over, or -1: the icon
     * answers a hover the way a pose does, by its selected variant
     * (IV-058). */
    int                            drop_hot;
    /* The field the pointer rests on, for the truncated-value pop-up
     * (AW-073): -1 when nowhere. */
    int                            hover_slot;
    int                            hover_field;
    int                           *dirty;
};

void tiku_trk_info_windows_init(tiku_trk_info_windows_t *controller,
    tiku_workspace_t *workspace, tiku_backend_t *backend,
    tiku_store_t *store, tiku_fs_t *fs, tiku_trk_alert_t *alert,
    int *dirty);
int tiku_trk_info_windows_open(tiku_trk_info_windows_t *controller,
                               const char *path);
void tiku_trk_info_windows_close(tiku_trk_info_windows_t *controller,
                                 int slot);
int tiku_trk_info_windows_slot(const tiku_trk_info_windows_t *controller,
                               const tiku_window_t *window);
tiku_trk_info_t *tiku_trk_info_windows_model(
    tiku_trk_info_windows_t *controller, int slot);
tiku_window_t *tiku_trk_info_windows_handle(
    tiku_trk_info_windows_t *controller, int slot);

tiku_trk_info_hit_t tiku_trk_info_windows_hit(
    tiku_trk_info_windows_t *controller, int slot, int x, int y, int *which);

/**
 * @brief Make tab @p which current in @p slot, re-reading what it shows.
 *
 * The tab strip owns which is current; this is where a click on it lands,
 * and where the Attributes tab gets a fresh read on the way in.
 */
void tiku_trk_info_windows_select_tab(tiku_trk_info_windows_t *controller,
                                      int slot, int which);
/** @brief The screen rectangle of field row @p i in @p slot (AW-073). */
int tiku_trk_info_windows_field_rect(tiku_trk_info_windows_t *controller,
                                     int slot, int i,
                                     int *x, int *y, int *w, int *h);

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
