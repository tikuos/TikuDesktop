/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_view_defs.h - shared view and column vocabulary.
 *
 * These declarations describe saved state and commands, not the PoseView
 * implementation.  Menus and type records may use them without importing
 * the complete pose engine API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_VIEW_DEFS_H_
#define TIKU_TRK_VIEW_DEFS_H_

#define TIKU_TRK_COLUMNS_MAX 6

#define TIKU_TRK_MODE_LIST 0
#define TIKU_TRK_MODE_ICON 1
#define TIKU_TRK_MODE_MINI 2

#define TIKU_TRK_MINI_ICON 16
#define TIKU_TRK_PLACE_GAP  3

#define TIKU_TRK_COL_LEFT_MARGIN   11
#define TIKU_TRK_COL_RIGHT_MARGIN   5
#define TIKU_TRK_COL_MARGIN        (TIKU_TRK_COL_LEFT_MARGIN + \
                                    TIKU_TRK_COL_RIGHT_MARGIN)
#define TIKU_TRK_COL_MIN_WIDTH     20
/** @brief How many M's wide a pose label may be in an icon mode (IV-047). */
#define TIKU_TRK_LABEL_CHARS       30
#define TIKU_TRK_COL_ROOM_FOR_LINE  2

#define TIKU_TRK_ALIGN_LEFT   0
#define TIKU_TRK_ALIGN_RIGHT  1
#define TIKU_TRK_ALIGN_CENTER 2

typedef struct {
    const char *title;
    int         width;
    int         field;
    int         offset;
    int         alignment;
} tiku_trk_column_t;

#define TIKU_TRK_COL_NAME  0
#define TIKU_TRK_COL_KIND  1
#define TIKU_TRK_COL_VALUE 2
#define TIKU_TRK_COL_SIZE  3
#define TIKU_TRK_COL_CAP   4
#define TIKU_TRK_COL_DATE  5
#define TIKU_TRK_COL_PATH  6
#define TIKU_TRK_COL_PERM  7
#define TIKU_TRK_COL_ORIGINAL 8
#define TIKU_TRK_COL_KINDS 9

#define TIKU_TRK_COL_ATTR_BASE 16
#define TIKU_TRK_COL_ATTR_MAX   8
#define TIKU_TRK_ATTR_NAME_MAX 32

#endif /* TIKU_TRK_VIEW_DEFS_H_ */
