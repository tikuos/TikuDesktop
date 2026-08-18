/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_alert.h - the question the shell puts to the user, and the
 * progress a running job reports.
 *
 * The engine has always been able to ask -- a collision, an error, a
 * refusal -- and the shells have always answered for it, because there was
 * nowhere to put the question.  This is that place: state and hit-testing
 * only, so the same alert can be raised by either front end and driven by a
 * script.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_ALERT_H_
#define TIKU_TRK_ALERT_H_

#include "tiku_trk_fs.h"

/** @brief How many buttons an alert can carry. */
#define TIKU_TRK_ALERT_BUTTONS 4

/** @brief What an alert is for; it decides the icon and the wording. */
typedef enum {
    TIKU_TRK_ALERT_INFO = 0,
    TIKU_TRK_ALERT_WARN,
    TIKU_TRK_ALERT_STOP
} tiku_trk_alert_kind_t;

/** @brief One alert, open or not. */
typedef struct {
    int                   open;
    tiku_trk_alert_kind_t kind;
    char                  text[512];
    char                  button[TIKU_TRK_ALERT_BUTTONS][40];
    int                   nbutton;
    int                   cancel;   /* which button Escape presses       */
    int                   chosen;   /* -1 until one is pressed           */
    /* What the answer is FOR.  An alert raised by a collision has to hand
     * its answer back to the engine, and the shell must not have to
     * remember which question it asked. */
    int                   tag;
} tiku_trk_alert_t;

/** @brief What an alert's answer belongs to. */
#define TIKU_TRK_ALERT_TAG_NONE     0
#define TIKU_TRK_ALERT_TAG_CONFLICT 1
#define TIKU_TRK_ALERT_TAG_ERROR    2
#define TIKU_TRK_ALERT_TAG_REFUSAL  3
#define TIKU_TRK_ALERT_TAG_DELETE   4
#define TIKU_TRK_ALERT_TAG_BROKEN_OPEN 5
#define TIKU_TRK_ALERT_TAG_OPEN_ANYWAY 6
#define TIKU_TRK_ALERT_TAG_SHUTDOWN 7

void tiku_trk_alert_reset(tiku_trk_alert_t *a);

/**
 * @brief Raise an alert.  @p buttons is a "|"-separated list, right to left
 * as the original lays them out: the default is last.
 */
void tiku_trk_alert_open(tiku_trk_alert_t *a, tiku_trk_alert_kind_t kind,
                         int tag, const char *text, const char *buttons);

/**
 * @brief Build the alert a collision asks, from the progress record.
 *
 * A lone file-versus-file collision gets two buttons; a folder collision, or
 * any collision while more than one is pending, gets the four-way form --
 * the shape the engine already worked out (FS-018, FS-019).
 */
void tiku_trk_alert_for_conflict(tiku_trk_alert_t *a,
                                 const tiku_trk_progress_t *pr);

/** @brief Build the alert a failed item asks (FS-035). */
void tiku_trk_alert_for_error(tiku_trk_alert_t *a,
                              const tiku_trk_progress_t *pr);

/** @brief Which button a point is over, or -1. */
int tiku_trk_alert_button_at(const tiku_trk_alert_t *a, int w, int h,
                             int x, int y);

/** @brief The rectangle of button @p i inside a @p w by @p h alert. */
void tiku_trk_alert_button_rect(const tiku_trk_alert_t *a, int i, int w,
                                int h, int *bx, int *by, int *bw, int *bh);

/**
 * @brief Answer the collision alert; maps the chosen button to the engine.
 *
 * @return the conflict answer to hand to tiku_trk_fs_resolve().
 */
tiku_trk_conflict_t tiku_trk_alert_conflict_answer(const tiku_trk_alert_t *a);

/*---------------------------------------------------------------------------*/
/* The progress window                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief A link job shows no window unless it is bigger than this.
 *
 * Making a couple of links is instantaneous; a window that appeared and
 * vanished for it would be a flash the user cannot read (FS-070).
 */
#define TIKU_TRK_PROGRESS_LINK_MIN 10

/** @brief How long the status line shows speed, then the estimate. */
#define TIKU_TRK_PROGRESS_SPEED_US    8000000
#define TIKU_TRK_PROGRESS_ESTIMATE_US 4000000

/** @brief How many samples the rolling speed is averaged over. */
#define TIKU_TRK_PROGRESS_SLOTS 4

/** @brief How many jobs the shared window can show at once. */
#define TIKU_TRK_PROGRESS_ROWS 4

/** @brief One row of the shared status window. */
typedef struct {
    int   used;
    char  caption[48];              /* "Copying: ", "Moving: " ...        */
    char  current[TIKU_TRK_PATH_MAX];
    char  dest[TIKU_TRK_PATH_MAX];
    int   done, total;
    int   paused;
    /* A rolling mean rather than the last sample: a copy that hits one
     * large file would otherwise report a speed that is true of nothing. */
    int64_t bytes[TIKU_TRK_PROGRESS_SLOTS];
    int64_t at[TIKU_TRK_PROGRESS_SLOTS];
    int     nsample;
    int64_t total_bytes;
} tiku_trk_progress_row_t;

/** @brief The shared status window: it exists only while a job does. */
typedef struct {
    tiku_trk_progress_row_t row[TIKU_TRK_PROGRESS_ROWS];
    int                     open;
} tiku_trk_progress_win_t;

void tiku_trk_progress_reset(tiku_trk_progress_win_t *p);

/**
 * @brief Whether a job of @p op over @p items should show a window at all.
 */
int tiku_trk_progress_wanted(tiku_trk_op_t op, int items);

/** @brief Note that @p bytes have been transferred by row @p i, at @p now. */
void tiku_trk_progress_sample(tiku_trk_progress_win_t *p, int i,
                              int64_t bytes, int64_t now);

/** @brief Bytes per second, averaged over the samples.  0 when unknown. */
int64_t tiku_trk_progress_speed(const tiku_trk_progress_win_t *p, int i);

/**
 * @brief Seconds still to go, or -1 when it cannot be estimated.
 *
 * From the rolling speed and what is left, which is why a job that has
 * only just started has no estimate rather than a wild one.
 */
int64_t tiku_trk_progress_eta(const tiku_trk_progress_win_t *p, int i);

/**
 * @brief Whether the status line should show speed rather than the estimate.
 *
 * Eight seconds of one then four of the other, on a cycle shared by every
 * row: two rows alternating out of step would be harder to read than
 * either alone (FS-071).
 */
int tiku_trk_progress_show_speed(int64_t since_open);

/** @brief The caption a job of kind @p op shows. */
const char *tiku_trk_progress_caption(tiku_trk_op_t op);

/** @brief What a click on a progress row landed on. */
typedef enum {
    TIKU_TRK_PROGRESS_HIT_NONE = 0,
    TIKU_TRK_PROGRESS_HIT_PAUSE,
    TIKU_TRK_PROGRESS_HIT_STOP
} tiku_trk_progress_hit_t;

/** @brief The pause or stop button of row @p i, in window coordinates. */
void tiku_trk_progress_button_rect(int i, int which, int w, int *bx, int *by,
                                   int *bw, int *bh);

/**
 * @brief What a point hits inside a @p w wide progress window.
 *
 * @param row Receives which row was hit.
 */
tiku_trk_progress_hit_t tiku_trk_progress_hit(const tiku_trk_progress_win_t *p,
                                              int w, int x, int y, int *row);

/** @brief The text a row shows; a paused row says how to get going again. */
const char *tiku_trk_progress_row_text(const tiku_trk_progress_win_t *p,
                                       int i);

/**
 * @brief Take the engine's state into row @p i, opening the window if this
 * is the first job and closing it when the last one ends (FS-072).
 */
void tiku_trk_progress_update(tiku_trk_progress_win_t *p, int i,
                              const tiku_trk_progress_t *pr, int busy,
                              const char *dest);

#endif /* TIKU_TRK_ALERT_H_ */
