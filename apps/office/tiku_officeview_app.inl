/* tiku_officeview_app.inl -- the descriptor TikuDoc and TikuSlides share:
 * the office viewer's window, run in its own process by tiku-run.  Each
 * application's file defines OV_APP_ID, OV_APP_NAME, OV_APP_SLIDES and
 * OV_APP_HELP, then includes this.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app.h"
#include "tiku_window_app.h"
#include "tiku_work.h"
#include <stdlib.h>
#include <string.h>

/** @brief The running application: the desktop's calls and the viewer. */
typedef struct {
    tiku_app_services_t services;
    tiku_window_app_t *host;
    tiku_officeview_t *viewer;
    int dirty;
    int asking;                     /* 1: a file to open; 2: a PDF's place */
} ov_app_t;

/** @brief Open... asks for a file; Export as PDF... for a place and a
 *         name, through the Save panel. */
static void ov_app_ask(void *ctx, int save, const char *start,
                       const char *name)
{
    ov_app_t *a = ctx;

    if (!a->services.pick) return;
    a->asking = save ? 2 : 1;
    a->services.pick(a->services.ctx, tiku_window_app_id(a->host,
        tiku_officeview_handle(a->viewer)), save ? TIKU_APP_PICK_SAVE
                                                 : TIKU_APP_PICK_LOCAL_OPEN,
        start ? start : getenv("HOME"), name);
}

/** @brief The window, opened on the file it was started with. */
static int ov_app_start(void **state, const tiku_app_services_t *services)
{
    static const tiku_officeview_kind_t kind = { OV_APP_NAME, OV_APP_SLIDES };
    ov_app_t *a = calloc(1, sizeof *a);
    tiku_officeview_host_t host = { ov_app_ask, NULL };

    if (!a) return -1;
    a->services = *services;
    host.ctx = a;
    a->host = tiku_window_app_new(services);
    if (a->host) a->viewer = tiku_officeview_new(
        tiku_window_app_workspace(a->host), &a->dirty, &kind, &host);
    if (!a->viewer) { tiku_window_app_free(a->host); free(a); return -1; }
    tiku_officeview_open(a->viewer);
    if (services->startup_document)
        (void)tiku_officeview_file(a->viewer, services->startup_document);
    if (services->help_page)
        services->help_page(services->ctx, OV_APP_HELP);
    *state = a;
    if (tiku_window_app_sync(a->host, 1)) return 0;
    tiku_officeview_destroy(a->viewer);
    tiku_window_app_free(a->host);
    free(a);
    *state = NULL;
    return -1;
}

/** @brief Let the viewer go; its workers finish on their own. */
static void ov_app_stop(void *state)
{
    ov_app_t *a = state;

    tiku_officeview_destroy(a->viewer);
    (void)tiku_work_collect();
    tiku_window_app_free(a->host);
    free(a);
}

/** @brief Each turn: finished jobs land, and the window is published
 *         when something changed. */
static void ov_app_tick(void *state, int64_t now)
{
    ov_app_t *a = state;

    (void)now;
    (void)tiku_work_collect();
    tiku_window_app_sync(a->host, a->dirty);
    a->dirty = 0;
}

/** @brief Input for the window; the app ends when the window has gone. */
static int ov_app_event(void *state, uint32_t id, const tiku_event_t *ev)
{
    ov_app_t *a = state;

    tiku_window_app_event(a->host, id, ev);
    a->dirty = 1;
    return !tiku_officeview_handle(a->viewer);
}

/** @brief A menu pick; the app ends when the window has gone. */
static int ov_app_pick(void *state, uint32_t id, int command)
{
    ov_app_t *a = state;

    tiku_window_app_pick(a->host, id, command);
    a->dirty = 1;
    return !tiku_officeview_handle(a->viewer);
}

/** @brief The file Open... chose, or where Export as PDF... writes. */
static void ov_app_picked(void *state, uint32_t id, const char *path)
{
    ov_app_t *a = state;

    (void)id;
    if (path && a->asking == 2)
        (void)tiku_officeview_export_to(a->viewer, path);
    else if (path)
        (void)tiku_officeview_file(a->viewer, path);
    a->asking = 0;
    a->dirty = 1;
}

/** @brief The desktop closed the window. */
static void ov_app_closed(void *state, uint32_t id)
{
    tiku_window_app_closed(((ov_app_t *)state)->host, id);
}

static const tiku_app_descriptor_t descriptor = {
    .id = OV_APP_ID, .name = OV_APP_NAME, .start = ov_app_start,
    .stop = ov_app_stop, .event_in = ov_app_event, .tick = ov_app_tick,
    .pick = ov_app_pick, .picked = ov_app_picked, .closed = ov_app_closed
};
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor
};
