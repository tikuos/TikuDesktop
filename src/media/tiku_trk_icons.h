/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_icons.h - the embedded HVIF icon set.
 *
 * The standard Tracker artwork, compiled in as HVIF blobs rather than
 * bitmaps: one vector definition serves every size, and the renderer
 * rasterises at the size actually asked for.  Names are the Tracker's own
 * vocabulary ("folder", "trash_full", "folder_queries"), not Haiku's
 * resource ids.
 *
 * Artwork from Haiku (haiku-os.org), MIT licensed; the TrackerIcons.rdef
 * blobs additionally carry the Open Tracker License (Be Incorporated,
 * 1991-2000).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TIKU_TRK_ICONS_H_
#define TIKU_TRK_ICONS_H_

#include <stddef.h>
#include <stdint.h>

/** @brief How many icons the set holds. */
#define TIKU_TRK_ICON_COUNT 34

/** @brief One named HVIF blob. */
typedef struct {
    const char    *name;
    const uint8_t *data;
    size_t         len;
} tiku_trk_icon_t;

/** @brief Number of icons in the set; always TIKU_TRK_ICON_COUNT. */
int tiku_trk_icons_count(void);

/** @brief The @p i th icon in set order, or NULL if @p i is out of range. */
const tiku_trk_icon_t *tiku_trk_icons_at(int i);

/**
 * @brief Look an icon up by short name.
 *
 * @param name One of the set's names, e.g. "folder" or "trash_full".
 * @return The blob, or NULL if nothing carries that name.
 */
const tiku_trk_icon_t *tiku_trk_icons_find(const char *name);

/**
 * @brief Render @p name at @p size x @p size, once per size.
 *
 * The result is premultiplied 0xAARRGGBB owned by the cache, valid until
 * tiku_trk_icons_flush().  Callers blit it; they never free it.
 *
 * @param name Icon name.
 * @param size Edge length in pixels, 1..1024.
 * @return The bitmap, or NULL for an unknown name or a failed render.
 */
const uint32_t *tiku_trk_icons_bitmap(const char *name, int size);

/** @brief Drop every cached bitmap. */
void tiku_trk_icons_flush(void);

#endif /* TIKU_TRK_ICONS_H_ */
