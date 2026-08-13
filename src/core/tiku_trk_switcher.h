/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_switcher.h - the application switcher's state machine.
 *
 * Almost all of what the switcher does is decided before anything is drawn:
 * whether a Ctrl+Tab is a switch or an invitation to browse, what order the
 * applications are in and when that order is allowed to change, and what
 * each key does while it is up.  Those are what is here; the strip's
 * animation is drawing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SWITCHER_H_
#define TIKU_TRK_SWITCHER_H_

#include <stddef.h>
#include <stdint.h>

#define TIKU_TRK_SW_APPS 32
#define TIKU_TRK_SW_WINS 16
#define TIKU_TRK_SW_NAME 64

/** @brief How long Ctrl may be held before the switcher stays up. */
#define TIKU_TRK_SW_HAIR_US 180000

/** @brief Idle time after which the order may be re-sorted. */
#define TIKU_TRK_SW_RESORT_US 400000

/** @brief The visible slots; more than this and the strip scrolls. */
#define TIKU_TRK_SW_SLOTS 7

/** @brief The two icon sizes the strip draws: the selected one is large. */
#define TIKU_TRK_SW_SMALL 24
#define TIKU_TRK_SW_LARGE 48

/**
 * @brief One inset step of the shrink/grow, in microseconds.
 *
 * The original does two steps each way at 20 ms.  Kept because the feel is
 * the row: fast enough not to be a wait, slow enough to show WHICH icon the
 * selection left.
 */
#define TIKU_TRK_SW_STEP_US 20000
#define TIKU_TRK_SW_STEPS   2

/** @brief One application in the strip. */
typedef struct {
    char    name[TIKU_TRK_SW_NAME];
    char    win[TIKU_TRK_SW_WINS][TIKU_TRK_SW_NAME];
    int     nwin;
    int64_t used;                   /* when it was last switched to      */
    int     is_self;                /* the Tracker itself                */
} tiku_trk_sw_app_t;

/** @brief What a key did. */
typedef enum {
    TIKU_TRK_SW_NOTHING = 0,
    TIKU_TRK_SW_MOVED,
    TIKU_TRK_SW_COMMIT,
    TIKU_TRK_SW_CANCEL,
    TIKU_TRK_SW_QUIT_APP,
    TIKU_TRK_SW_HIDE_APP
} tiku_trk_sw_act_t;

/** @brief The switcher. */
typedef struct {
    tiku_trk_sw_app_t app[TIKU_TRK_SW_APPS];
    int               n;
    int               shown;
    int               sel;          /* selected application              */
    int               selwin;       /* selected window within it         */
    int               scroll;       /* first visible slot                */
    int64_t           opened;
    int64_t           last_switch;
    /* The selection the strip is animating AWAY from, and when it left.
     * Held so the size of every slot is a function of the clock rather
     * than of a frame counter somebody has to remember to advance. */
    int               prev;
    int64_t           moved;
} tiku_trk_switcher_t;

void tiku_trk_switcher_init(tiku_trk_switcher_t *s);

/** @brief Add or refresh an application.  @return its index. */
int tiku_trk_switcher_app(tiku_trk_switcher_t *s, const char *name,
                          int is_self);

/** @brief Give application @p i a window. */
void tiku_trk_switcher_window(tiku_trk_switcher_t *s, int i,
                              const char *title);

/**
 * @brief Ctrl+Tab arrived.
 *
 * Sorts most-recently-used first, but only if it has been idle long
 * enough: repeated fast switching must keep a stable order, or the second
 * press would land somewhere the first press moved (DB-055).
 */
void tiku_trk_switcher_open(tiku_trk_switcher_t *s, int64_t now, int back);

/**
 * @brief Ctrl was released at @p now.
 *
 * @return 1 when this was a QUICK switch -- released inside the hair
 *         trigger, which means "the previous application" rather than
 *         "show me the list" (DB-054).
 */
int tiku_trk_switcher_release(tiku_trk_switcher_t *s, int64_t now);

/** @brief A key while it is up (DB-056). */
tiku_trk_sw_act_t tiku_trk_switcher_key(tiku_trk_switcher_t *s,
                                        const char *key, int shift);

/** @brief A click on slot @p slot, or -1 for outside (DB-059). */
tiku_trk_sw_act_t tiku_trk_switcher_click(tiku_trk_switcher_t *s, int slot);

/**
 * @brief The size icon @p i should draw at, at @p now (DB-058).
 *
 * The outgoing icon shrinks and the incoming one grows over the same two
 * steps, so at any instant the pair says which way the selection went.
 */
int tiku_trk_switcher_icon_size(const tiku_trk_switcher_t *s, int i,
                                int64_t now);

/** @brief Whether the strip is still animating at @p now. */
int tiku_trk_switcher_animating(const tiku_trk_switcher_t *s, int64_t now);

/**
 * @brief Whether a scroller arrow belongs at an end (DB-058).
 *
 * @param right Non-zero for the right-hand one.
 */
int tiku_trk_switcher_scroller(const tiku_trk_switcher_t *s, int right);

/** @brief Where the panel sits on a screen of @p sw by @p sh (DB-060). */
void tiku_trk_switcher_place(int sw, int sh, int w, int h, int *x, int *y);

/** @brief The title a row shows: "App: Window", or just the app. */
void tiku_trk_switcher_title(const tiku_trk_switcher_t *s, int i, int w,
                             char *out, size_t max);

#endif /* TIKU_TRK_SWITCHER_H_ */
