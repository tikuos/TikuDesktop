/* Device utilities own their UI and data; the desktop owns device access.
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_device_utilities.c"

#ifdef TIKU_VFS_INSPECTOR
#define APP_MODE 1
#define APP_NAME "VFS Inspector"
#define APP_ID "vfs"
#define APP_HELP "devices.html#inspector"
#else
#define APP_MODE 0
#define APP_NAME "Device Monitor"
#define APP_ID "monitor"
#define APP_HELP "devices.html#monitor"
#endif

static void stop(void *state)
{
    utility_app_t *a = state;
    device_utilities_destroy(a);
    tiku_window_app_free(a->host);
    free(a);
}

static int start(void **state, const tiku_app_services_t *services)
{
    utility_app_t *a = calloc(1, sizeof *a);
    if (!a) return -1;
    a->services = *services;
    a->host = tiku_window_app_new(services);
    if (!a->host) { free(a); return -1; }
    a->desk = tiku_window_app_workspace(a->host);
    if (services->current_device)
        services->current_device(services->ctx, a->current_device,
                                 sizeof a->current_device);
    device_utility_open(a, APP_MODE);
    if (!a->utility[APP_MODE] || !tiku_window_app_sync(a->host, 1)) {
        stop(a); return -1;
    }
    if (services->help_page) services->help_page(services->ctx, APP_HELP);
    *state = a;
    return 0;
}

static void tick(void *state, int64_t now)
{
    utility_app_t *a = state; (void)now;
    struct device_utility *p = a->utility[APP_MODE];
    if (p) tiku_vfs_proxy_pump(p->backend, a->services.send_message, a->services.ctx);
    tiku_work_collect();
    device_utilities_tick(a);
    tiku_window_app_sync(a->host, a->dirty); a->dirty = 0;
}

static int event(void *state, uint32_t id, const tiku_event_t *ev)
{
    utility_app_t *a = state;
    tiku_window_app_event(a->host, id, ev); a->dirty = 1;
    return !a->utility[APP_MODE];
}

static void closed(void *state, uint32_t id)
{
    utility_app_t *a = state;
    struct device_utility *p = a->utility[APP_MODE];
    (void)id;
    if (p) device_utility_close(a, p->window);
}

static void message(void *state, const tiku_msg_t *m)
{
    utility_app_t *a = state;
    struct device_utility *p = a->utility[APP_MODE];
    if (p) tiku_vfs_proxy_reply(p->backend, m);
}

static void board_said(void *state, int what, const char *text)
{
    utility_app_t *a = state;
    if (what != TIKU_APP_BOARD_CURRENT) return;
    snprintf(a->current_device, sizeof a->current_device, "%s", text);
    struct device_utility *p = a->utility[APP_MODE];
    if (p && !p->target[0] && !p->busy && *text) {
        snprintf(p->target, sizeof p->target, "%s", text);
        utility_request(p, 0);
    }
    a->dirty = 1;
}

static const tiku_app_descriptor_t descriptor = {
    .id = APP_ID, .name = APP_NAME, .start = start, .stop = stop,
    .event_in = event, .tick = tick, .closed = closed,
    .board_said = board_said, .message = message
};
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor
};
