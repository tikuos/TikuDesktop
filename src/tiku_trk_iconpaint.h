/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_iconpaint.h - putting a rendered icon on a surface.
 *
 * The icon cache hands back premultiplied ARGB; the surface is opaque RGB.
 * One compositor does that conversion for everyone who draws an icon, so the
 * blend rule exists once rather than once per call site.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_ICONPAINT_H_
#define TIKU_TRK_ICONPAINT_H_

#include "tiku_desk_gfx.h"
#include "tiku_trk_thumb.h"

/**
 * @brief Composite icon @p name at @p size over @p s with its top-left at
 *        (@p x, @p y).
 *
 * Rendering is cached per name and size, so drawing the same icon in every
 * row of a list costs one rasterisation, not one per row.
 *
 * @return 1 when it was drawn, 0 for an unknown name or a failed render --
 *         the caller can then fall back rather than leaving a hole.
 */
int tiku_trk_icon_paint(tiku_desk_surface_t *s, const char *name,
                        int x, int y, int size);

/**
 * @brief Composite it dimmed, for a disabled or minimised item.
 *
 * @param mix 0.0 leaves the icon untouched, 1.0 dissolves it into @p over.
 */
int tiku_trk_icon_paint_dim(tiku_desk_surface_t *s, const char *name,
                            int x, int y, int size, float mix,
                            tiku_desk_rgb_t over);

/**
 * @brief Composite the SELECTED variant of an icon (PVL-082).
 *
 * Selection has to read on the icon and not only on the label, or a row
 * whose label is off the right edge shows no selection at all.  Haiku keeps
 * a second bitmap per icon for this; one wash toward the selection colour
 * gives the same two visual states without a second cache.
 */
int tiku_trk_icon_paint_selected(tiku_desk_surface_t *s, const char *name,
                                 int x, int y, int size);

/**
 * @brief Draw a loaded thumbnail into @p s at @p size, alpha-blended.
 *
 * Scaled down at draw time rather than stored per size: one 128 square
 * answers every icon size the view can ask for.
 */
void tiku_trk_thumb_paint(tiku_desk_surface_t *s,
                          const tiku_trk_image_t *th, int x, int y,
                          int size);

#endif /* TIKU_TRK_ICONPAINT_H_ */
