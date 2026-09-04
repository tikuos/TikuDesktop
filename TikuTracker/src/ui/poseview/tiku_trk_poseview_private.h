/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_private.h - what the tiku_trk_poseview files share: the
 * includes, the types, the small accessors that read them, and the functions
 * one file defines for another.  Nothing outside this directory includes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_POSEVIEW_PRIVATE_H_
#define TIKU_TRK_POSEVIEW_PRIVATE_H_

/* split: public TikuTracker/src/ui/tiku_trk_poseview.h */

/* split: tag pv */

#include "tiku_trk_poseview.h"
#include "tiku_strings.h"
#include "tiku_trk_frame.h"
#include "tiku_state.h"
#include "tiku_trk_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define POSES_MAX 4096

/** @brief How many on-screen rows a value sweep will ever look at. */
#define REFRESH_WINDOW 40

struct tiku_trk_poseview {
    tiku_backend_t *backend;
    char                path[TIKU_PATH_MAX];

    tiku_trk_pose_t    *poses;          /* fPoseList                        */
    int                 count;
    int                *filtered;       /* fFilteredPoseList: indices       */
    int                 filtered_count;
    int                 filtering;
    uint64_t            menu_generation;
    int64_t             broken_repair_due;
    tiku_trk_repaint_t  repaint;

    int                 pivot;          /* fSelectionPivotPose (-1 = none)  */
    int                 last_added;     /* for shift+arrow walk-back        */
    int                 active;         /* fActivePose                      */

    char                typed[TIKU_TRK_TYPEAHEAD_MAX];
    int                 typed_len;
    int64_t             typed_at;

    /* Filtering keeps its OWN strings, because it is a different mechanism
     * from jump-to-name: the terms accumulate, they are shown, and they are
     * only cleared deliberately, where a jump string lapses on its own. */
    char                filter[TIKU_TRK_FILTER_TERMS][TIKU_TRK_TYPEAHEAD_MAX];
    int                 filter_count;
    char                filter_shown[TIKU_TRK_FILTER_TERMS *
                                     TIKU_TRK_TYPEAHEAD_MAX];

    int                 sort_field;
    int                 sort2_field;    /* the tie-break, or -1             */
    int                 sort_reverse;
    int                 scroll;
    /* Where the value sweep got to.  It survives the tick, because a
     * tick may not finish the screen: see refresh_until(). */
    int                 refresh_at;

    /* Selection MEMBERSHIP is the pose flag; whether it is PAINTED is this.
     * Tracker keeps two structures and clears the flag on deactivation; one
     * flag plus this bit says the same thing without a second list that can
     * drift out of step with the first. */
    int                 selection_visible;
    int                 show_sel_inactive;
    int                 real_pivot;     /* the pivot, parked while hidden   */
    int                 band;           /* a rubber band is being swept     */
    tiku_trk_repaint_rect_t band_rect;  /* where it was when last swept     */
    int                 band_seen;      /* whether band_rect means anything */
    unsigned char      *band_was;       /* selection when the band began    */

    /* The armed inline rename: which pose, and when its click landed. */
    int                 edit_pose;
    int64_t             edit_clicked_at;
    int                 allow_editing;

    tiku_trk_measure_fn measure;
    void               *measure_ctx;
    int                 drop_x, drop_y; /* where the next new entry lands   */
    int                 drop_valid;
    /* Where NAMED entries about to arrive should land.  A drop is decided
     * before the entries exist here, and an arrangement that is applied
     * after they appear is one the user watches assemble itself. */
    struct {
        char            name[TIKU_NAME_MAX];
        int             x, y;
    }                   land[TIKU_TRK_LAND_MAX];
    int                 land_count;

    int                 mode;           /* TIKU_TRK_MODE_*                  */
    /* Which ICON mode was last live, kept beside the current mode the way
     * the original keeps fLastIconMode: the stored pose positions are in
     * ITS geometry, and a folder leaving list mode needs to know whose
     * grid it is reading (MA-061). */
    int                 last_icon;
    /* A named state slot overriding the path: the Desktop and the Disks
     * window must never collide with a folder window that happens to show
     * the same directory (MA-064). */
    char                state_key[16];
    int                 icon_size;
    int                 hint_x, hint_y; /* fHintLocation: where to place next */
    int                 state_dirty;
    /* Set only by the column mutators, so a release that merely
     * re-sorted does not rewrite the layout record (TS-036). */
    int                 columns_dirty;    /* fStateNeedsSaving                */
    int                 is_query;       /* results, not a directory         */
    char                query_layout[TIKU_PATH_MAX];
    char                query_type[TIKU_TYPE_MAX];
    tiku_store_t    *layout_store;  /* synthetic query layout records   */
    int                 visible_rows;   /* what the window can show         */
    int                 view_w, view_h; /* the window, in PIXELS            */
    /* The font the labels are drawn in.  Zero until the owner says, and
     * every icon-mode height falls back to the art size until it does. */
    int                 font_h, font_a;
    int                 row_h;      /* what the owner draws, 0 = derive  */
    int                 folders_first;  /* the setting, on by default    */
    int                 no_autoscroll;  /* the Desktop's exemption       */
    int                 hide_dotfiles;
    /* Columns that show a named ATTRIBUTE rather than a fixed fact.  The
     * slot is the field id; the name is what a saved layout carries. */
    struct {
        char name[TIKU_TRK_ATTR_NAME_MAX];
        char title[24];
        int  width;
    }                   attrcol[TIKU_TRK_COL_ATTR_MAX];
    int                 attrcol_count;
    tiku_trk_hit_fn     icon_hit;
    void               *icon_hit_ctx;
    /* The pixel origin of each icon mode, kept apart from the list's row
     * scroll so a mode round-trip returns to where it was (PVL-009). */
    int                 icon_x, icon_y;
    int                 mini_x, mini_y;
    /* The bounding box of everything placed, maintained as poses arrive and
     * move rather than walked on every ask (PVL-050).  Inverted while empty,
     * so the first pose DEFINES it instead of the origin doing so. */
    int                 ext_x0, ext_y0, ext_x1, ext_y1;
    int                 ext_valid;
    /* Notices that arrived before the row they concern, and entries seen but
     * not readable -- neither is dropped, because a change lost is a row
     * that lies and an unreadable entry is not an absent one. */
    tiku_park_t     park;
    tiku_model_t    zombie[16];
    int                 zombie_count;

    tiku_trk_column_t   columns[TIKU_TRK_COLUMNS_MAX];
    int                 column_count;
    int                 hscroll;        /* titles and rows share it        */
    int                 font_px;        /* what widths are scaled against  */
    uint64_t            cell_cache_hits;
    uint64_t            cell_cache_misses;

    /* The batched load.  The read is synchronous; what is staged here is
     * PUBLISHED a batch at a time, so the window is never blank and never
     * blocked (PVL-088, PVN-047). */
    tiku_model_t   *stage;
    int                 stage_count;
    int                 stage_at;
    int                 loading;
    int64_t             next_chunk;
    int64_t             pole_at;        /* when the pole becomes visible   */
    /* Bumped whenever the listing this view shows changes identity.  A
     * batch or a notice carrying an older one belongs to a folder the
     * window has left (PVL-089). */
    int                 load_gen;
    /* Node ids published this load.  The loader and the change notices are
     * two mouths feeding one listing, and without this a node reported by
     * both arrives twice (PVL-090). */
    uint64_t           *inserted;
    int                 inserted_count;
    /* The shared icon cache, or NULL.  Kept in step from the notice
     * handler: see tiku_trk_poseview_set_icons. */
    tiku_trk_nodeicon_t *icons;
    /* Whether several poses may be selected at once.  Stored inverted so a
     * zeroed view is a normal multi-select one, which every existing
     * caller expects. */
    int single_only;
};

/* Process-wide typed layout channel.  Unlike the file clipboard this is a
 * value clipboard: it has no node lifetime to watch and is replaced as one
 * immutable snapshot, so every open view sees the same paste immediately. */



/**
 * @brief Pose info as stored: two unscaled coordinates and a flag word.
 *
 * The flags carry the one bit a listing cannot derive -- whether the entry is
 * meant to be shown at all.  A record of any other length is REFUSED: reading
 * a longer one into this struct would silently take its first eight bytes and
 * call the result a position.
 */
typedef struct {
    int32_t  x;
    int32_t  y;
    uint32_t flags;
} pose_info_t;

/** @brief Stored, but not to be shown. */
#define POSE_INFO_INVISIBLE 0x1u

/** @brief The pre-flags record, still readable. */
typedef struct {
    int32_t x;
    int32_t y;
} pose_info_v1_t;

/** @brief Collects a re-listing for the differ. */
struct relist {
    tiku_model_t    *set;
    int                  n;
    int                  max;
    tiku_trk_poseview_t *view;   /* for the zombie hold                   */
};

/**
 * @brief One column, as stored.
 *
 * The key is written in HOST order and is the only thing that says which
 * order the rest of the record is in.  Tracker gets that from the name it
 * stored the attribute under -- two names, one per endianness -- which needs
 * a namespace that can hold both; a stamp inside the record needs nothing,
 * and answers the same question.
 */
typedef struct {
    uint32_t key;
    int32_t  version;
    int32_t  field;
    int32_t  offset;          /* order only; the position is re-derived    */
    int32_t  width;
    int32_t  alignment;
    /* An attribute column is identified by its NAME: the field id is a
     * slot in this session's table, and a slot number means nothing to the
     * next one.  Empty for the fixed columns. */
    char     attr[TIKU_TRK_ATTR_NAME_MAX];
} column_rec_t;

#define COLUMN_REC_KEY     0x54434f4cu      /* "TCOL"                      */
#define COLUMN_REC_VERSION 2

/* Shared across the family: defined in one file, used in another. */

/* tiku_trk_poseview.c */
extern int pv_g_layout_clip_valid;
extern tiku_trk_layout_clip_t pv_g_layout_clip;
const char *pv_cell_text_at(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p,
                            int field, int width, char *buf, size_t max);

/* tiku_trk_poseview_draw.c */
int pv_poses_sorted(tiku_trk_poseview_t *v);
int pv_sort_fallback(const tiku_trk_poseview_t *v);
void pv_column_def_v(const tiku_trk_poseview_t *v, int field,
                     const char **title, int *width);

/* tiku_trk_poseview_extent.c */
int pv_insert_sorted(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p);
int pv_pose_index(const tiku_trk_poseview_t *v, const char *path);
int pv_pose_is_hidden(tiku_trk_poseview_t *v, const char *path);
int pv_sorted_slot(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p);
void pv_extent_add(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p);
void pv_extent_clear(tiku_trk_poseview_t *v);
void pv_remove_pose(tiku_trk_poseview_t *v, int at);

/* tiku_trk_poseview_iconmode.c */
float pv_mode_scale(int mode, int size);
int pv_take_landing(tiku_trk_poseview_t *v, const char *path, int *x, int *y);

/* tiku_trk_poseview_load.c */
extern tiku_trk_poseview_t *pv_g_sorting;
int pv_pose_cmp(const void *pa, const void *pb);
int pv_sane_scroll_origin(int value, int allow_negative);
void pv_apply_visibility(tiku_trk_poseview_t *v);
void pv_note_inserted(tiku_trk_poseview_t *v, uint64_t id);
void pv_resort(tiku_trk_poseview_t *v);

/* tiku_trk_poseview_select.c */
int pv_display_of(const tiku_trk_poseview_t *v, int pi);
void pv_clear_selection(tiku_trk_poseview_t *v);
void pv_set_selected(tiku_trk_pose_t *p, int on);

/* tiku_trk_poseview_spatial.c */
const char *pv_state_node(const tiku_trk_poseview_t *v, char *buf, size_t max);

/* tiku_trk_poseview_typeahead.c */
void pv_filter_changed(tiku_trk_poseview_t *v, int grew);
void pv_rebuild_filter(tiku_trk_poseview_t *v);

/* End of the shared declarations. */

#endif /* TIKU_TRK_POSEVIEW_PRIVATE_H_ */
