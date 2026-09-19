/* TikuTracker services: what a view asks the shell for, through one table.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TRK_SERVICES_H_
#define TIKU_TRK_SERVICES_H_

#include "tiku_gfx.h"

struct tiku_trk_app;

/**
 * @brief The facts and acts a view needs from the shell: which installed
 *        application a path is, the art it wears, a line for the status
 *        strip, and a frame for what changed.  Every entry may be NULL in
 *        a shell that has no answer; @p ctx is the shell's own.
 */
typedef struct tiku_trk_services {
    void *ctx;
    /** @brief The registered application @p path is or links to, or NULL. */
    const struct tiku_trk_app *(*app_of)(void *ctx, const char *path);
    /** @brief The art a pose that is an application wears, or NULL. */
    const char *(*art_for)(void *ctx, const char *path);
    /** @brief A line for the status strip. */
    void (*status)(void *ctx, const char *text);
    /** @brief A frame of @p r; a negative width asks for everything. */
    void (*damage)(void *ctx, tiku_rect_t r);
} tiku_trk_services_t;

#endif
