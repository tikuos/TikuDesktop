/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_frame.h - where a window was, and which windows were open.
 *
 * A spatial file manager's promise is that a folder opens where you left it,
 * at the size you left it.  That needs three things this port had none of: a
 * frame remembered per FOLDER, something for a folder that has never been
 * opened to inherit, and a record of what was open when the session ended.
 *
 * The frame is kept beside the folder's arrangement rather than in one
 * shared slot: one frame for every folder is the same as no frame at all,
 * because the last window closed would decide where the next one opens.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FRAME_H_
#define TIKU_TRK_FRAME_H_

#include "tiku_trk_model.h"
#include "tiku_trk_state.h"

/** @brief A window's rectangle, as it is stored. */
typedef struct {
    int32_t x, y, w, h;
} tiku_trk_frame_t;

/** @brief How many windows a session record holds. */
#define TIKU_TRK_SESSION_MAX 16

/** @brief How much of a window must stay reachable when it is clamped. */
#define TIKU_TRK_FRAME_TAB  24
#define TIKU_TRK_FRAME_GRAB 60

/** @brief The smallest window that is still usable. */
#define TIKU_TRK_FRAME_MIN_W 180
#define TIKU_TRK_FRAME_MIN_H 120

/**
 * @brief Read the frame @p path's window had.
 *
 * @return 1 when one was found; @p out is untouched otherwise, so a caller
 *         can seed it with a cascade position and let this overwrite it.
 */
int tiku_trk_frame_load(tiku_trk_store_t *s, const char *path,
                        tiku_trk_frame_t *out);

/**
 * @brief Write it back, but only if it MOVED (CW-002).
 *
 * A window that opened where it was told and was never touched has nothing
 * to say, and writing it anyway is a store write per window per session on
 * a device where writes are the expensive thing.
 *
 * @param was The frame the window opened with, or NULL when it had none.
 * @return 1 when something was written.
 */
int tiku_trk_frame_save(tiku_trk_store_t *s, const char *path,
                        const tiku_trk_frame_t *now,
                        const tiku_trk_frame_t *was);

/**
 * @brief Clamp a frame onto a screen of @p sw by @p sh.
 *
 * A frame from a bigger screen, or one saved when the window was dragged
 * mostly off, must still be reachable: a window nobody can grab is the same
 * as no window.
 */
void tiku_trk_frame_clamp(tiku_trk_frame_t *f, int sw, int sh);

/**
 * @brief The frame a folder opened for the FIRST time should get (CW-005).
 *
 * Its parent's, offset so it does not land exactly on top of it; failing
 * that the saved default; failing that nothing, and the caller cascades.
 * The ROOT is deliberately excluded from inheriting: it has no parent, and
 * a default applied to it would move the one window that should not move
 * (CW-069).
 *
 * @return 1 when a frame was found.
 */
int tiku_trk_frame_inherit(tiku_trk_store_t *s, const char *path,
                           tiku_trk_frame_t *out);

/** @brief Read and write the fallback every unvisited folder starts from. */
int tiku_trk_frame_default(tiku_trk_store_t *s, tiku_trk_frame_t *out);
void tiku_trk_frame_set_default(tiku_trk_store_t *s,
                                const tiku_trk_frame_t *f);

/*---------------------------------------------------------------------------*/
/* The session (AW-004)                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write the set of open windows.
 *
 * Order matters: it is the order they will be reopened in, which decides
 * which ends up in front.
 */
int tiku_trk_session_save(tiku_trk_store_t *s,
                          const char paths[][TIKU_TRK_PATH_MAX], int n);

/**
 * @brief Read it back.
 *
 * @return how many paths were written to @p out.
 */
int tiku_trk_session_load(tiku_trk_store_t *s,
                          char out[][TIKU_TRK_PATH_MAX], int max);

/** @brief Forget it, so the next start opens the default instead. */
void tiku_trk_session_clear(tiku_trk_store_t *s);

#endif /* TIKU_TRK_FRAME_H_ */
