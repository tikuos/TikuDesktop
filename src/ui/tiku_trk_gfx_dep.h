/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_gfx_dep.h - the drawing floor this app sits on.
 *
 * The surface, font and R5 control set live in applications/desktop; they are
 * the floor, not the product.  Pulling them in through one header keeps the
 * dependency visible in every file that draws.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_GFX_DEP_H_
#define TIKU_TRK_GFX_DEP_H_

#include "tiku_gfx.h"
#include "tiku_font.h"
#include "tiku_ui.h"

#endif /* TIKU_TRK_GFX_DEP_H_ */
