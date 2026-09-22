/* tiku_desktop_jobs_private.h -- desktop background-job interfaces.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
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
int desktop_jobs_is_board(const tiku_backend_t *b);
void desktop_jobs_path_key(char *out, size_t max, const char *path);
int desktop_jobs_sound_open(void *ctx);

#endif /* TIKU_DESKTOP_JOBS_PRIVATE_H_ */
