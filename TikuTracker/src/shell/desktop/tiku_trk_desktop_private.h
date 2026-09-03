/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_private.h - what the desktop shell's files share.
 *
 * The shell is one composition split over the files beside this one:
 * the state they all hold a pointer to, the small accessors that read
 * it, and the functions one file defines for another.  Nothing outside
 * this directory includes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKTOP_PRIVATE_H_
#define TIKU_TRK_DESKTOP_PRIVATE_H_

#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <locale.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "tiku_attr.h"
#include "tiku_logo.h"
#include "tiku_host.h"
#include "tiku_trk_application.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_nodeicon.h"
#include "tiku_trk_frame.h"
#include "tiku_trk_info.h"
#include "tiku_trk_info_window.h"
#include "tiku_trk_open_with_window.h"
#include "tiku_trk_panel.h"
#include "tiku_trk_panel_window.h"
#include "tiku_thumb.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_prefs.h"
#include "tiku_trk_prefs_window.h"
#include "tiku_trk_deskbar_prefs_window.h"
#include "tiku_trk_desktop_prefs_window.h"
#include "tiku_trk_fonts_window.h"
#include "tiku_strings.h"
#include "tiku_volume.h"
#include "tiku_trk_switcher.h"
#include "tiku_trk_switcher_window.h"
#include "tiku_trk_navmenu.h"
#include "tiku_trk_titleview.h"
#include "tiku_conduct.h"
#include "tiku_trk_script.h"
#include "tiku_mount.h"
#include "tiku_tx.h"
#include "tiku_trk_splash.h"
#include "tiku_trk_script_api.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskbar_roster.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_launch.h"
#include "tiku_trk_types.h"
#include "tiku_iconpaint.h"
#include "tiku_icons.h"
#include "tiku_fs.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_model.h"
#include "tiku_trk_poseview.h"
#include "tiku_clipboard.h"
#include "tiku_field.h"
#include "tiku_trk_select.h"
#include "tiku_trk_select_window.h"
#include "tiku_trk_find.h"
#include "tiku_trk_find_window.h"
#include "tiku_trk_about_window.h"
#include "tiku_remote.h"
#include "tiku_msg.h"
#include "tiku_form.h"
#include "tiku_demo_app.h"
#include "tiku_trk_drop_controller.h"
#include "tiku_trk_folder_actions.h"
#include "tiku_trk_folder_file_actions.h"
#include "tiku_trk_folder_edit.h"
#include "tiku_trk_folder_menu.h"
#include "tiku_trk_addon.h"
#include "tiku_trk_folder_lifecycle.h"
#include "tiku_trk_folder_window.h"
#include "tiku_trk_band_controller.h"
#include "tiku_trk_drag_target.h"
#include "tiku_trk_launch_controller.h"
#include "tiku_trk_status_overlay.h"
#include "tiku_state.h"
#include "tiku_query.h"
#include "tiku_trk_task.h"
#include "tiku_trk_session_controller.h"
#include "tiku_trk_script_adapter.h"
#include "tiku_window.h"

/* How many applications this desktop hosts in its own process. */
#define TIKU_TRK_EMBED_MAX 8
/* Windows ONE application may hold at once.  An application is not its
 * window: the id every service already carries is what tells them
 * apart, and a host that kept one window per application was throwing
 * that id away. */
#define TIKU_TRK_EMBED_WINDOWS 8

#define DESK_W 1000
#define DESK_H  680
#define COUNT_H  16

/* How many entries per turn are asked whether they carry a picture of their
 * own.  A bound rather than the whole listing: the ask opens the file. */
#define HINTS_PER_TURN 24

/* Tracker's own list row: ceilf(plain font size * 1.65), which at the 12 px
 * face is 20 -- tall enough that a 16 px mini icon sits in it with room, and
 * the reason rows do not tighten onto the text. */

/** @brief How much of the count view's text a script can read. */
#define TIKU_TRK_COUNT_TEXT_MAX 64

typedef tiku_trk_folder_window_t folder_t;

typedef struct desktop {
    tiku_host_t    *host;
    tiku_surface_t *surf;
    tiku_trk_splash_t     splash;
    tiku_workspace_t     *desk;
    tiku_trk_deskbar_t  *deskbar;
    tiku_backend_t  *backend;
    tiku_fs_t       *fs;
    folder_t             folders[TIKU_WINDOW_MAX];
    tiku_trk_folder_render_t folder_render;
    tiku_trk_folder_lifecycle_t folder_lifecycle;
    int                  folder_count;
    int                  row_h;
    int                  team;
    int                  running;
    /* Being taken down: the loop runs on so the screen can go under,
     * but nothing a person does reaches anything any more. */
    int                  closing;
    int                  dirty;
    /* Menus.  One set serves every folder window, rebuilt from whichever is
     * focused each time a menu opens -- the original rebuilds on open too. */
    tiku_trk_cwmenu_t    menus;
    tiku_trk_launch_controller_t launcher;
    tiku_trk_drop_controller_t drop_controller;
    tiku_trk_folder_file_actions_t file_actions;
    tiku_trk_folder_edit_t folder_edit;
    tiku_trk_band_controller_t band_controller;
    tiku_trk_drag_target_t drag_target;
    tiku_trk_session_controller_t session;
    tiku_trk_script_adapter_t script_adapter;
    tiku_trk_folder_menu_t folder_menu;
    tiku_menu_t     *popup;          /* the menu showing, or NULL       */
    tiku_rect_t     popup_frame;
    tiku_menu_t     *popup_sub;      /* its open submenu, or NULL       */
    tiku_rect_t     popup_sub_frame;
    folder_t            *bar_fl;         /* window whose bar opened popup   */
    int                  bar_open;       /* title in that window, or -1     */
    char                 status[200];
    uint32_t             backdrop_color;
    /* Find is a MODE, as the shell's own watch is: there is no second window
     * to put a panel in, and a namespace search wants the same one-line
     * entry the rest of the system uses. */
    int                  finding;
    char                 find_buf[64];
    int                  find_len;
    int                  find_serial;
    tiku_trk_find_window_t find_window;
    tiku_remote_listener_t remote;
    /* The conductor channel: an outside driver's events and questions.
     * Off unless -conduct asked for it.  Injected events queue here and
     * are drained by the same loop that drains the display's, so a
     * driven shell and a used one take exactly the same path. */
    tiku_conduct_t         conduct;
    int                         conduct_on;
    tiku_event_t           cq[64];
    int                         cq_head, cq_tail;
    /* One EMBEDDED descriptor slot: the same application code the client
     * runner takes out of process, hosted here with workspace-backed
     * services -- the both-ways proof. */
    struct desktop_embed {
        struct desktop      *app;       /* the slot's way back      */
        const tiku_app_descriptor_t *descriptor;
        tiku_app_services_t services;
        void                *state;
        /* One of these per window the application holds.  Each is its
         * own workspace TAG, which is what stops a second window from
         * being handed back the first one. */
        struct embed_win {
            struct desktop_embed *slot; /* the window's way back    */
            uint32_t             id;    /* what the application was told */
            tiku_window_t       *window;
            uint32_t            *frame;
            int                  fw, fh;
        } win[TIKU_TRK_EMBED_WINDOWS];
        uint32_t             next_id;
        int                  running;
    } embeds[TIKU_TRK_EMBED_MAX];
    /* The remote window owning the current press, so MOVE and RELEASE
     * follow the DOWN to the same session -- a drag is one conversation. */
    tiku_window_t  *remote_press;
    struct embed_win     *embed_press;  /* the window holding a press */
    /* The window a resize track is reshaping, so the owner can be told
     * the new size when the hand lets go. */
    tiku_window_t  *resize_track;
    /*
     * The application the file panel is up for, if it is up for one.
     * The panel carries only an opaque (target, command) pair, and the
     * rest of an ask lives here beside pending_query and info_relink --
     * which is where every other ask's remainder already lives.
     */
    struct {
        int                   kind;     /* 0 nobody, 1 embedded, 2 remote */
        struct desktop_embed *slot;     /* embedded: which one            */
        int                   session;  /* remote: which session          */
        uint32_t              window;   /* the window that asked          */
    } pending_pick;
    /*
     * A panel a DEVICE described.  Not a window class of its own: the
     * form says what controls there are and which places they show, and
     * everything else here is the shell doing what it already does --
     * reading a path, writing a path, drawing controls.
     */
    struct {
        tiku_form_t      form;
        tiku_window_t   *window;
        char             path[TIKU_PATH_MAX];
        int              unknown;   /* rows this build did not know */
    } device_panel;
    /* Where the runner is, for starting an installed application in a
     * process of its own. */
    char                 runner[TIKU_PATH_MAX];
    /* The query the save panel is asking a destination for. */
    tiku_query_t     pending_query;
    /* Which theme the desktop is wearing; -1 until first applied. */
    int                  theme_mode;
    /* The global menu bar (phase one): one strip at the top of the
     * screen, fed by whatever the focused window PUBLISHED (phase two's
     * protocol) plus the system menu at its left.  The scratch menus are
     * rebuilt per draw from the published data alone -- the strip never
     * reaches into a window's widgets. */
    int                  gbar_on;       /* -1 until the first sync      */
    tiku_menu_t      sysmenu;
    tiku_menu_t      gmenu[TIKU_MENUSET_MENUS];
    tiku_menu_t      gsub[TIKU_MENUSET_MENUS][4];
    tiku_menubar_t   gbar;
    tiku_window_t  *gbar_from;     /* whose menus the strip shows  */
    /* The focused program's About row, hoisted into the system menu:
     * the published command, and the label it was published under. */
    int                  about_hoist;
    char                 about_label[64];
    int                  gbar_open;     /* open strip menu, or -1       */
    /* The Find icon in flight (Q-034): dragged, and where it landed. */
    int                  qdrag_on;
    char                 qdrop_dest[TIKU_PATH_MAX];
    /* The Deskbar's own double-click memory (DB-019). */
    int64_t              db_click_at;
    int                  db_click_x, db_click_y;
    /* The zoom: a frame sliding between the menu item and the window it
     * names, alive for a few frames after a Deskbar window action
     * (DB-030). */
    int64_t              zoom_t0;
    tiku_rect_t     zoom_from, zoom_to;
    /* A CMD press on the bar, still ambiguous: moved it re-docks, held
     * still it is a chord the rows answer (DB-049 vs DB-021). */
    int                  dock_armed;
    int                  dock_x, dock_y;
    unsigned             dock_mods;
    unsigned             dock_button;
    /* The resize strip along the bar's inner edge (DB-050). */
    int                  bar_resize;
    int                  bar_resize_x;
    int                  bar_resize_w;
    /* One cascade origin shared by every window that opens without a
     * remembered position, advanced per window and reset when it runs off
     * the screen -- not a modulo, which would silently stack the sixth
     * window on the first. */
    int                  cascade_x, cascade_y;
    struct tiku_store *store;
    /* The backdrop pose view owns its union source and watcher. */
    tiku_trk_desktop_view_t desktop_view;
    int                  desk_row;      /* pose under the pointer, or -1 */
    /* ONE clipboard for the desktop, as the system's is one: cutting in a
     * window and pasting in another is the whole point of it. */
    tiku_trk_clip_t      clip;
    /* Select by pattern.  ONE of these serves the whole desktop and is
     * hidden rather than destroyed, so its expression outlives the window
     * it was last used on (CW-063, AW-105). */
    tiku_trk_select_window_t select_window;
    tiku_trk_open_with_window_t open_with_window;
    /* Deferred and periodic work, pulsed from this loop's own tick: the
     * cooperative protothread the TIKU note asks for in place of the
     * source's worker thread (AW-083). */
    tiku_trk_loop_t     *loop;
    char                 clock_stamp[64]; /* what the clock last showed  */
    /* Where the last press landed, for the long press and for telling a
     * slow second click from a drag. */
    int64_t              press_at;
    int                  press_x, press_y;
    int                  press_row;
    tiku_trk_drag_t      drag;      /* the gesture between press and drop */
    unsigned             drag_live_mods; /* modifiers as of the last move  */
    /* The column strip's tracker (TS-063): one gesture at a time, owned
     * by whichever folder window the press landed in. */
    tiku_trk_titleview_t tstrip;
    folder_t            *tstrip_fl;
    folder_t            *sbar_fl;   /* whose bar holds the pointer      */
    int                  strip_ptr_x;
    int                  strip_ptr_y;
    tiku_thumb_q_t   thumbs;    /* image previews, made off-tick  */
    /* ONE node-icon cache for the desktop: the same file shown in two
     * windows is one entry, and an icon edited on disk is dropped once
     * rather than per window (IV-038). */
    tiku_trk_nodeicon_t  nicons;
    /* Which applications there are and what each reads.  One registry for
     * the desktop: the Open with menu, a double click and a drop on an
     * application all ask the same question (AW-040). */
    tiku_trk_apps_t      apps;
    tiku_trk_addons_t    addons;
    /* The add-ons directory and the user shortcut file, both watched by
     * modification time so an install or a rebind lands without a restart
     * (AW-022, AW-023). */
    char                 addons_dir[TIKU_PATH_MAX];
    char                 addons_keys[TIKU_PATH_MAX];
    time_t               addons_mtime;
    time_t               keys_mtime;
    int64_t              addons_checked;
    /* What the system knows about a type: its description, its icon, and
     * the columns it is worth showing (AW-013, CW-026). */
    tiku_trk_types_t     types;
    /* What the count view last drew, so a script can read the one line
     * that reports the selection rather than the folder. */
    char                 count_text[TIKU_TRK_COUNT_TEXT_MAX];
    /* The folder the running job is writing into, for the progress
     * window's "To:" line (FS-069). */
    char                 prog_dest[TIKU_PATH_MAX];
    /* The Open/Save panel.  ONE, because it is asking one question at a
     * time and a second would be a second answer to the same ask. */
    /* What is mounted, and what it was last time we looked: the port's
     * only source of mount and unmount notices (PVN-008). */
    tiku_volwatch_t  volwatch;
    /* The record the last search wrote, so Edit query has something to
     * reopen when the window it came from is the one asking. */
    char                 last_query[TIKU_NAME_MAX];
    tiku_trk_panel_window_t file_panel;
    tiku_trk_info_windows_t info_windows;
    /* The broken link an open offered Get Info for (AW-031). */
    char                 broken_open_path[TIKU_PATH_MAX];
    /* Which broken link a picker was opened to re-point, or -1. */
    int                  info_relink;
    tiku_trk_status_overlay_t status_overlay;
    tiku_trk_prefs_window_t prefs_window;
    tiku_trk_deskbar_prefs_window_t deskbar_prefs_window;
    tiku_trk_desktop_prefs_window_t desktop_prefs_window;
    tiku_trk_fonts_window_t         fonts_window;
    int                  applied_hide_dotfiles;
    int                  applied_full_path;
    int                  applied_folders_first;
    tiku_trk_switcher_window_t switcher_window;
    tiku_trk_nav_t       nav;      /* the open navigation walk   */
    int                  nav_on;
    /* The SPRING navigation: the same walk, anchored to a sprung pose
     * instead of a context-menu row (PVS-051). */
    int                  spring_nav_on;
    int                  spring_nav_x;
    int                  spring_nav_y;
    tiku_window_t      *prog_was_active;  /* focus to give back */
    int                  press_down;
    int                  ptr_x, ptr_y;  /* where the pointer last was      */
    /* A context menu raised on one named thing -- the info window's icon --
     * rather than on a window's selection (AW-076). */
    char                 popup_path[TIKU_PATH_MAX];
    /* And which FOLDER: the menu model is one shared thing that every
     * drawing folder rebuilds for itself, so an open popup has to be put
     * back for the folder it was raised on -- and the choice carried out
     * there, not on whatever window has focus. */
    folder_t            *popup_fl;
    tiku_window_t  *press_window; /* deferred content raise (PVS-022) */
    tiku_window_t  *press_focus;  /* who was in front when it began   */
    int                  dock_drag;     /* Ctrl-dragging the Deskbar     */
    folder_t            *press_fl;
    int                  press_menu;    /* the long press already fired    */
    int                  deskbar_press; /* primary hold over the clock     */
} desktop_t;

static folder_t *folder_of(desktop_t *app, tiku_window_t *w);
static folder_t *desktop_folder(desktop_t *app);


/* The models behind the windows, read where they are used. */

/** @brief Preferences remain shared data; their window owns the lifetime. */
static inline tiku_trk_prefs_t *
prefs_model(desktop_t *app)
{
    return tiku_trk_prefs_window_model(&app->prefs_window);
}

/** @brief Select state is retained by its reusable window controller. */
static inline tiku_trk_select_t *
select_model(desktop_t *app)
{
    return tiku_trk_select_window_model(&app->select_window);
}

/** @brief Find state is retained by its reusable window controller. */
static inline tiku_trk_find_t *
find_model(desktop_t *app)
{
    return tiku_trk_find_window_model(&app->find_window);
}

/** @brief Modal question state is presented by the shared overlay. */
static inline tiku_alert_t *
alert_model(desktop_t *app)
{
    return tiku_trk_status_overlay_alert(&app->status_overlay);
}

/** @brief File-job state is presented by the shared overlay. */
static inline tiku_trk_progress_win_t *
progress_model(desktop_t *app)
{
    return tiku_trk_status_overlay_progress(&app->status_overlay);
}

/** @brief What the action table needs to know about the focused window. */
static inline tiku_trk_sc_ctx_t
menu_ctx(desktop_t *app, folder_t *fl, unsigned mods)
{
    return tiku_trk_folder_menu_context(&app->folder_menu, fl, mods);
}

/** @brief The folder behind a window, or NULL. */
static inline folder_t *
folder_of(desktop_t *app, tiku_window_t *w)
{
    return tiku_trk_folder_lifecycle_find(&app->folder_lifecycle, w);
}

/** @brief The folder interface shared by Desktop and folder interactions. */
static inline folder_t *
desktop_folder(desktop_t *app)
{
    return tiku_trk_desktop_view_folder(&app->desktop_view);
}

/* Shared across the desktop's files: defined in one, used in another. */

/* tiku_trk_desktop_embed.c */
int embed_deliver(struct embed_win *ew, const tiku_event_t *ev);
int embed_open_or_raise(desktop_t *app,
                        const tiku_app_descriptor_t *descriptor);
int embed_start(desktop_t *app, const tiku_app_descriptor_t *descriptor);
int tick_embeds(void *ctx);
struct embed_win *embed_win_of_window(desktop_t *app, const tiku_window_t *w);
void embed_drop_window(struct embed_win *ew, int tell);
void embed_stop(struct desktop_embed *slot);

/* End of the shared functions. */

#endif /* TIKU_TRK_DESKTOP_PRIVATE_H_ */
