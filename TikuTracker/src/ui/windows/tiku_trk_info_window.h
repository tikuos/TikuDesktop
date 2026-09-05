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
    /* One slot per open window: what it describes, the window, its tabs,
     * and the context the window hands back to name its slot. */
    tiku_trk_info_t                model[TIKU_TRK_INFO_WINDOW_MAX];
    tiku_window_t            *window[TIKU_TRK_INFO_WINDOW_MAX];
    tiku_tabs_t                    tabs[TIKU_TRK_INFO_WINDOW_MAX];
    tiku_trk_info_window_context_t context[TIKU_TRK_INFO_WINDOW_MAX];
    tiku_workspace_t         *workspace;   /* where the windows are opened */
    tiku_backend_t            *backend;    /* where the facts are read from */
    tiku_store_t              *store;      /* the settings, for the frames */
    tiku_fs_t                 *fs;         /* renames go through it */
    tiku_alert_t              *alert;      /* says why a rename was refused */
    tiku_field_t                name;      /* the rename editor */
    int                            renaming;  /* the slot in rename, or -1 */
    /* The slot whose icon an in-flight drag is over, or -1: the icon
     * answers a hover the way a pose does, by its selected variant
     * (IV-058). */
    int                            drop_hot;
    /* The field the pointer rests on, for the truncated-value pop-up
     * (AW-073): -1 when nowhere. */
    int                            hover_slot;   /* which window */
    int                            hover_field;  /* which of its fields */
    int                           *dirty;        /* the shell's redraw flag */
};

/**
 * @brief Bind the set of windows to the workspace, the backend, the store,
 *        the filesystem and the alert.  Opens nothing.
 */
void tiku_trk_info_windows_init(tiku_trk_info_windows_t *controller,
    tiku_workspace_t *workspace, tiku_backend_t *backend,
    tiku_store_t *store, tiku_fs_t *fs, tiku_alert_t *alert,
    int *dirty);
/**
 * @brief Show @p path: the window already on it is activated, else a free
 *        slot opens one.  @return the slot, or -1 when every slot is taken.
 */
int tiku_trk_info_windows_open(tiku_trk_info_windows_t *controller,
                               const char *path);
/** @brief Close the window in @p slot and forget what it showed. */
void tiku_trk_info_windows_close(tiku_trk_info_windows_t *controller,
                                 int slot);
/** @brief The slot whose open window is @p window, or -1. */
int tiku_trk_info_windows_slot(const tiku_trk_info_windows_t *controller,
                               const tiku_window_t *window);
/** @brief The model behind @p slot, or NULL for a slot out of range. */
tiku_trk_info_t *tiku_trk_info_windows_model(
    tiku_trk_info_windows_t *controller, int slot);
/** @brief The window in @p slot, or NULL. */
tiku_window_t *tiku_trk_info_windows_handle(
    tiku_trk_info_windows_t *controller, int slot);

/**
 * @brief What a point in @p slot's window lands on, with @p which set to
 *        the tab, field or permission it is, or -1.
 */
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

/** @brief Start editing the name shown in @p slot in place. */
void tiku_trk_info_windows_begin_rename(
    tiku_trk_info_windows_t *controller, int slot);

/** @return Nonzero if the focused Get Info window consumed the key. */
int tiku_trk_info_windows_key(tiku_trk_info_windows_t *controller,
    int key, unsigned modifiers, char character);

/**
 * @brief One step of a folder-size walk, in the first window with one
 *        under way.  @return 1 when a step was taken.
 */
int tiku_trk_info_windows_calc_step(tiku_trk_info_windows_t *controller);
/** @brief Close every window on @p device.  @return how many closed. */
int tiku_trk_info_windows_device_gone(tiku_trk_info_windows_t *controller,
                                      uint64_t device);
/**
 * @brief Re-read every window's node.  A node that no longer stats is
 *        looked for again by identity and the window retitled when found;
 *        one that cannot be found closes.  @return how many changed.
 */
int tiku_trk_info_windows_refresh(tiku_trk_info_windows_t *controller,
                                  int64_t now);

#endif /* TIKU_TRK_INFO_WINDOW_H_ */
