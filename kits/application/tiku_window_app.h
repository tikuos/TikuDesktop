/* Host app-owned native window controllers over the desktop window protocol.
 * Only pixels, menus and events cross the process boundary.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_WINDOW_APP_H
#define TIKU_WINDOW_APP_H
#include "tiku_app.h"

typedef struct tiku_window_app tiku_window_app_t;
/* Owns its private workspace; services and callbacks remain app-owned.
 * Use on the application's UI thread, never a device worker. */
tiku_window_app_t *tiku_window_app_new(const tiku_app_services_t *services);
void tiku_window_app_free(tiku_window_app_t *app);
tiku_workspace_t *tiku_window_app_workspace(tiku_window_app_t *app);
uint32_t tiku_window_app_id(tiku_window_app_t *app, tiku_window_t *window);
/* Synchronize controllers and publish changed content. Returns nonzero
 * while at least one window was successfully published. New surfaces are
 * allocated before the old ones are released; allocation failure returns 0. */
int tiku_window_app_sync(tiku_window_app_t *app, int dirty);
int tiku_window_app_event(tiku_window_app_t *app, uint32_t id,
                          const tiku_event_t *event);
void tiku_window_app_pick(tiku_window_app_t *app, uint32_t id, int command);
void tiku_window_app_closed(tiku_window_app_t *app, uint32_t id);
#endif
