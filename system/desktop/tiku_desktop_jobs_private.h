/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_jobs_private.h - what the desktop's worker-job files share:
 * the kit headers they use and the helpers one job file defines for
 * another.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESKTOP_JOBS_PRIVATE_H_
#define TIKU_DESKTOP_JOBS_PRIVATE_H_

#include "tiku_trk_desktop_private.h"

#include <unistd.h>

#include "tiku_board_basic.h"
#include "tiku_fs.h"
#include "tiku_thumb.h"
#include "tiku_volume.h"
#include "tiku_trk_poseview.h"
#include "tiku_channel.h"
#include "tiku_sound.h"
#include "tiku_trail.h"
#include "tiku_work.h"

/**
 * @brief Whether backend @p b is a board on a wire: the "tiku" backend,
 *        which can say what changed.
 */
int desktop_jobs_is_board(const tiku_backend_t *b);

/**
 * @brief The job key for @p path, into @p out: "board:/devices/<name>",
 *        the key of that board's jobs, for a path under /devices/<name>;
 *        "board:" and the path itself for any other path.
 */
void desktop_jobs_path_key(char *out, size_t max, const char *path);

/**
 * @brief The sound kit's opener: post the connect to the sound server on
 *        a worker.
 * @return 1 when a worker took it.
 */
int desktop_jobs_sound_open(void *ctx);

#endif /* TIKU_DESKTOP_JOBS_PRIVATE_H_ */
