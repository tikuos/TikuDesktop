/* Native controllers across a process boundary: no desktop-private objects.
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_window_app.h"
#include "tiku_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned opened, closed, frames, configured, destroyed, menus;
static int width, height;
static char title[160];

static uint32_t open_window(void *ctx, const char *name, int w, int h)
{
    (void)ctx; snprintf(title, sizeof title, "%s", name);
    width = w; height = h; return ++opened;
}

static int frame(void *ctx, uint32_t id, const uint32_t *pixels, int w, int h)
{
    (void)ctx; assert(id && pixels && w > 0 && h > 0);
    assert(pixels[0] == 0x123456); frames++; width = w; height = h; return 1;
}

static int configure(void *ctx, uint32_t id, const char *name,
                      int w, int h, int resize, int zoom)
{
    (void)ctx; (void)id; assert(resize && zoom);
    snprintf(title, sizeof title, "%s", name);
    if (w) { width = w; height = h; }
    configured++; return 1;
}

static void close_window(void *ctx, uint32_t id)
{ (void)ctx; assert(id); closed++; }

static int set_menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{ (void)ctx; assert(id && set->nmenu == 1); menus++; return 1; }

static void draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t c, void *ctx)
{
    (void)w; (void)ctx;
    assert(c.x == 0 && c.y == 0 && c.w == s->w && c.h == s->h);
    tiku_fill(s, c, 0x123456);
}

static void destroy(tiku_window_t *w, void *ctx)
{ (void)w; (void)ctx; destroyed++; }

int main(void)
{
    tiku_app_services_t services = {.open = open_window, .frame = frame,
        .configure = configure, .close = close_window, .menus = set_menus};
    tiku_window_app_t *a = tiku_window_app_new(&services); assert(a);
    tiku_workspace_t *workspace = tiku_window_app_workspace(a);
    tiku_window_t *w = tiku_workspace_open(workspace, "First",
        (tiku_rect_t){50, 50, 410, 331}, draw, NULL, NULL);
    tiku_window_set_handlers(w, NULL, NULL, destroy);
    tiku_menuset_t menu = {.nmenu = 1}; strcpy(menu.menu[0].title, "File");
    tiku_window_publish_menus(w, &menu, NULL, NULL);
    assert(tiku_window_app_sync(a, 1));
    assert(opened == 1 && frames == 1 && configured == 1 && menus == 1);
    unsigned id = tiku_window_app_id(a, w); assert(id);
    tiku_event_t focus = {.type = TIKU_EVENT_DEACTIVATED};
    tiku_window_app_event(a, id, &focus);
    assert(!tiku_workspace_focused(workspace));
    focus.type = TIKU_EVENT_ACTIVATED;
    tiku_window_app_event(a, id, &focus);
    assert(tiku_workspace_focused(workspace) == w);
    tiku_event_t resize = {.type = TIKU_EVENT_RESIZE, .width = 1280, .height = 800};
    tiku_window_app_event(a, id, &resize);
    assert(tiku_window_app_sync(a, 1));
    assert(width == 1280 && height == 800 && configured == 1);
    strcpy(w->title, "Renamed"); assert(tiku_window_app_sync(a, 0));
    assert(!strcmp(title, "Renamed") && configured == 2);
    /* Reuse the local slot before synchronization: the remote id must change. */
    tiku_workspace_close(workspace, w);
    w = tiku_workspace_open(workspace, "Second",
        (tiku_rect_t){50, 50, 410, 331}, draw, NULL, NULL);
    tiku_window_set_handlers(w, NULL, NULL, destroy);
    assert(tiku_window_app_sync(a, 1));
    assert(opened == 2 && closed == 1 && destroyed == 1);
    assert(tiku_window_app_id(a, w) != id);
    id = tiku_window_app_id(a, w);
    tiku_event_t key = {.type = TIKU_EVENT_KEY_DOWN, .key = 'w',
                         .modifiers = TIKU_MOD_CMD};
    tiku_window_app_event(a, id, &key);
    assert(!tiku_window_app_sync(a, 1));
    assert(closed == 2 && destroyed == 2);
    tiku_window_app_free(a);
    puts("PASS: isolated native windows, menus, title, resize, reused slots, standard close");
    return 0;
}
