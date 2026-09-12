/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_panel_window.h - Open and Save panel window controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_PANEL_WINDOW_H_
#define TIKU_TRK_PANEL_WINDOW_H_

#include <stddef.h>

#include "tiku_window.h"
#include "tiku_trk_alert.h"
#include "tiku_field.h"
#include "tiku_trk_panel.h"
#include "tiku_trk_poseview.h"

/** @brief Apply the answer while keeping the caller's target opaque. */
typedef void (*tiku_trk_panel_window_commit_fn)(void *context, void *target,
    int command, const char *destination);

/** @brief One reusable destination panel and all of its transient UI state. */
typedef struct {
    tiku_trk_panel_t       model;
    tiku_field_t        name;
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    tiku_trk_poseview_t   *view;
    tiku_backend_t    *backend;
    tiku_store_t      *store;
    tiku_trk_nodeicon_t   *icons;
    void                  *target;
    int                    command;
    int                    row_h;
    int                    screen_w;
    int                    screen_h;
    int                   *dirty;
    int64_t                press_at;
    int                    press_row;
    tiku_trk_panel_window_commit_fn commit;
    void                  *commit_context;
    /* The shell's alert, for a save's refusals and its Replace question;
     * the checked path waits here while the question is on screen. */
    tiku_alert_t      *alert;
    char                   pending_save[TIKU_PATH_MAX];
    /* The save name the caller OFFERED, so entering a folder the New
     * folder button just made can put it back in the field. */
    char                   offered[64];
    /* The rows a person is SHOWN: the poses whose names do not begin
     * with a dot, by pose index -- the panel is for choosing, and the
     * machinery folders are not choices.  The scroll is in this space. */
    int                    vis[1024];
    /* Beside each visible row: whether a FOLDER row is a project --
     * it holds a project.tiku -- so the listing can dress it as the
     * developer's folder it is. */
    unsigned char          proj[1024];
    /* When set, the ONE file name this panel may answer with: every
     * other file shows greyed and refuses the pick, while folders stay
     * the roads they are.  Empty means any file. */
    char                   only[64];
    int                    nvis;
    int                    scroll;
    /* The Favorites menu: favourites, then recent folders, dropped up
     * from the bottom-left button (AW-062, CW-058). */
    int                    fav_open;
    int                    fav_count;
    char                   fav_path[12][TIKU_PATH_MAX];
    char                   fav_label[12][48];
    int                    refused;   /* a paste the name would not take */
} tiku_trk_panel_window_t;

/**
 * @brief Bind the panel to its workspace, backend, store and icons, and to
 *        the shell's commit callback.  Opens no window.
 */
/** @brief Whether the last key refused a paste, clearing the mark. */
int tiku_trk_panel_window_refused(tiku_trk_panel_window_t *controller);

void tiku_trk_panel_window_init(tiku_trk_panel_window_t *controller,
    tiku_workspace_t *workspace, tiku_backend_t *backend,
    tiku_store_t *store, tiku_trk_nodeicon_t *icons, int row_h,
    int screen_w, int screen_h, int *dirty,
    tiku_trk_panel_window_commit_fn commit, void *commit_context);

/**
 * @brief Open the panel for @p target and @p command at its remembered
 *        frame, listing @p initial_directory.
 */
void tiku_trk_panel_window_open(tiku_trk_panel_window_t *controller,
    void *target, int command, const char *initial_directory);

/** @brief Raise the panel as a SAVE panel, name field seeded and focused. */
void tiku_trk_panel_window_open_save(tiku_trk_panel_window_t *controller,
    void *target, int command, const char *initial_directory,
    const char *initial_name);

/**
 * @brief Open the panel to choose a FILE rather than a folder.
 *
 * The same panel and the same answer road; only what it may be answered
 * with differs.  A folder is still entered by double-clicking it -- it
 * is simply not an answer.
 */
void tiku_trk_panel_window_open_file(tiku_trk_panel_window_t *controller,
    void *target, int command, const char *initial_directory,
    const char *only);

/** @brief The Replace question said replace (AW-052). */
void tiku_trk_panel_window_confirmed(tiku_trk_panel_window_t *controller);

/**
 * @brief A drop landed on the panel (AW-059).
 *
 * One entry navigates the panel; a file is selected in its parent and, in
 * a save panel, named in the field.  Every drop on the panel is consumed.
 */
void tiku_trk_panel_window_drop(tiku_trk_panel_window_t *controller,
                                const char *path);

/** @brief Give the panel the shell's alert for its save questions. */
void tiku_trk_panel_window_set_alert(tiku_trk_panel_window_t *controller,
                                     tiku_alert_t *alert);
/** @brief Close the panel, remembering its frame. */
void tiku_trk_panel_window_close(tiku_trk_panel_window_t *controller);
/** @brief Close the panel if open; nothing else is held. */
void tiku_trk_panel_window_destroy(tiku_trk_panel_window_t *controller);

/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_panel_window_handle(
    tiku_trk_panel_window_t *controller);
/** @brief The panel's model: its mode, directory and question. */
tiku_trk_panel_t *tiku_trk_panel_window_model(
    tiku_trk_panel_window_t *controller);

/** @return Nonzero when the focused panel consumed the key. */
int tiku_trk_panel_window_key(tiku_trk_panel_window_t *controller,
    unsigned key, unsigned modifiers, char character);

/** @return Nonzero when the panel view changed. */
int tiku_trk_panel_window_poll(tiku_trk_panel_window_t *controller,
    int64_t now, const tiku_model_change_t *changes, int count, int complete);

/** @return Nonzero when the panel moved away from the removed mount. */
int tiku_trk_panel_window_device_gone(tiku_trk_panel_window_t *controller,
                                      const char *mount);

/** @brief The list's scroll position, 0 with no list. */
int tiku_trk_panel_window_scroll(const tiku_trk_panel_window_t *controller);
/** @brief How many rows the list holds, 0 with no list. */
int tiku_trk_panel_window_rows(const tiku_trk_panel_window_t *controller);

/** @brief Insert committed text only; 0 not focused, 1 handled, -1 rejected. */
int tiku_trk_panel_window_text(tiku_trk_panel_window_t *controller,
    const char *text);

#endif /* TIKU_TRK_PANEL_WINDOW_H_ */
