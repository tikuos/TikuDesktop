/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_alert.h - what Tracker's alerts MEAN, and the progress a
 * running job reports.
 *
 * The QUESTION itself is the Interface kit's now (tiku_alert.h): state,
 * geometry, keys and the look, one object any application can raise over
 * its own content.  What stays here is Tracker's half -- which alert a
 * collision or a failed item builds, and what the chosen button means to
 * the engine -- because an answer's meaning belongs to whoever asked.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_ALERT_H_
#define TIKU_TRK_ALERT_H_

#include "tiku_fs.h"
#include "tiku_alert.h"

/** @brief What an alert's answer belongs to. */
#define TIKU_TRK_ALERT_TAG_NONE     0
#define TIKU_TRK_ALERT_TAG_CONFLICT 1
#define TIKU_TRK_ALERT_TAG_ERROR    2
#define TIKU_TRK_ALERT_TAG_REFUSAL  3
#define TIKU_TRK_ALERT_TAG_DELETE   4
#define TIKU_TRK_ALERT_TAG_BROKEN_OPEN 5
#define TIKU_TRK_ALERT_TAG_OPEN_ANYWAY 6
#define TIKU_TRK_ALERT_TAG_SHUTDOWN 7
#define TIKU_TRK_ALERT_TAG_SAVE_REPLACE 8   /* a save would overwrite */
#define TIKU_TRK_ALERT_TAG_EMPTY_TRASH 9    /* asks before emptying   */

/**
 * @brief Build the alert a collision asks, from the progress record.
 *
 * A lone file-versus-file collision gets two buttons; a folder collision, or
 * any collision while more than one is pending, gets the four-way form --
 * the shape the engine already worked out (FS-018, FS-019).
 */
void tiku_trk_alert_for_conflict(tiku_alert_t *a,
                                 const tiku_progress_t *pr);

/** @brief Build the alert a failed item asks (FS-035). */
void tiku_trk_alert_for_error(tiku_alert_t *a,
                              const tiku_progress_t *pr);

/**
 * @brief Answer the collision alert; maps the chosen button to the engine.
 *
 * @return the conflict answer to hand to tiku_fs_resolve().
 */
tiku_conflict_t tiku_trk_alert_conflict_answer(const tiku_alert_t *a);

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
    char  current[TIKU_PATH_MAX];
    char  dest[TIKU_PATH_MAX];
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
 * @brief The window's jobs as one line of text: "idle" when nothing
 *        runs, else each row as caption, current leaf and count --
 *        the operation's own lifecycle as a fact a driver can ask for
 *        (and watch: ~operation is "the copy finished" as a push).
 */
size_t tiku_trk_progress_say(const tiku_trk_progress_win_t *p,
                             char *out, size_t max);

/**
 * @brief Whether a job of @p op over @p items should show a window at all.
 */
int tiku_trk_progress_wanted(tiku_op_t op, int items);

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
const char *tiku_trk_progress_caption(tiku_op_t op);

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
                              const tiku_progress_t *pr, int busy,
                              const char *dest);

#endif /* TIKU_TRK_ALERT_H_ */
