/* Device Monitor and VFS Inspector access devices through the desktop broker.
 * App workers use RPC proxies; the desktop retains physical backend ownership.
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_window_app.h"
#include "tiku_vfs_rpc.h"
#include "tiku_attr.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include "tiku_icons_tiku.h"
#include "tiku_iconpaint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

struct device_utility;
typedef struct {
    tiku_app_services_t services;
    tiku_window_app_t *host;
    tiku_workspace_t *desk;
    tiku_surface_t *surf;
    struct device_utility *utility[2];
    char current_device[64];
    int dirty;
} utility_app_t;

static int64_t utility_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static int utility_board_names(utility_app_t *a, char *out, size_t max)
{
    return a->services.board_names ?
        a->services.board_names(a->services.ctx, out, max) : 0;
}

void device_utility_close(utility_app_t *app, tiku_window_t *w);

#include "tiku_dialog.h"
#include "tiku_clipboard.h"
#include "tiku_field_event.h"
#include "tiku_list_paint.h"
#include "tiku_sensor_paint.h"
#include "tiku_text_layout_paint.h"
#include "tiku_work.h"
#include <errno.h>
#include <ctype.h>
#include <stdatomic.h>

enum { U_REFRESH = 1, U_DEFAULT, U_NEXT, U_PAUSE, U_UP, U_OPEN,
       U_REVIEW, U_SAVE, U_CANCEL, U_COPY, U_CLOSE, U_PATH, U_VALUE,
       U_LIST, U_TREE, U_RECLAIM_CANCEL, U_RECLAIM_RETRY };
enum { U_MONITOR, U_INSPECTOR };
#define U_ROWS 128
#define U_TREE_HEIGHT 364
#define U_HISTORY 120
typedef struct utility_job utility_job_t;
static int utility_jobs;
typedef struct {
    char name[128];
    int directory, readable, writable, autoread;
    int sampled;
    char data[128];
} utility_entry_t;
typedef struct utility_snapshot {
    struct utility_snapshot *next;
    char path[256];
    int count, truncated, ok;
    utility_entry_t entries[U_ROWS];
} utility_snapshot_t;
typedef struct {
    char path[256];
    int parent, expanded, loaded;
} utility_branch_t;
typedef struct {
    int available, active, fault, can_cancel, can_retry;
    char token[32], title[96], detail[192], basic[128];
} utility_reclaim_t;
typedef struct {
    double uptime, hz, ram, stack, processes, store_size, store_used, sram;
    unsigned valid;
    char process[8][192];
    int nprocess;
    utility_reclaim_t reclaim;
} utility_metrics_t;
struct device_utility {
    utility_app_t *app;
    tiku_backend_t *backend;
    char proxy_target[64];
    tiku_window_t *window;
    int mode, refs, busy, paused, armed, readable, writable, directory;
    int count, truncated;
    int64_t next, sampled;
    char target[64], path[256], value[2048], status[256];
    utility_entry_t entries[U_ROWS];
    utility_metrics_t metrics;
    tiku_dialog_t dialog;
    tiku_list_t list;
    tiku_list_t tree_list;
    utility_branch_t tree[U_ROWS];
    int tree_rows[U_ROWS], tree_count, tree_selected, tree_full, collapse;
    char folder[256];
    tiku_field_t address, edit;
    tiku_field_input_t address_input, edit_input;
    tiku_history_t history[2];
    tiku_sample_t samples[2][U_HISTORY];
    utility_job_t *job;
    utility_snapshot_t *cache;
    int preload;
    int reclaim_confirm;
    char reclaim_confirm_token[32];
};
struct utility_job {
    struct device_utility *owner;
    tiku_backend_t *backend;
    int mode, write, completed, ok, count, truncated, collapse;
    int readable, writable, directory, found;
    char path[256], input[256], before[2048], value[2048], status[256];
    utility_entry_t entries[U_ROWS];
    utility_metrics_t metrics;
    atomic_int cancelled;
    int posted;
    int preload, limited, reads, reclaim_only;
    utility_snapshot_t *cache;
};
static void utility_request(struct device_utility *p, int write);
#include "tiku_device_utilities_model.inl"
#include "tiku_device_utilities_reclaim.inl"
#include "tiku_device_utilities_cache.inl"
#include "tiku_device_utilities_tree.inl"
#include "tiku_device_utilities_io.inl"
#include "tiku_device_utilities_ui.inl"

/** Identify app-owned utility windows, retaining proxy ownership. */
int device_utility_owns(utility_app_t *app, tiku_window_t *w)
{
    for (int i = 0; i < 2; i++)
        if (app->utility[i] && w && app->utility[i]->window == w) return 1;
    return 0;
}

/** Detach on close; a pending worker retains its own reference. */
void device_utility_close(utility_app_t *app, tiku_window_t *w)
{
    for (int i = 0; i < 2; i++) {
        struct device_utility *p = app->utility[i];
        if (!p || p->window != w) continue;
        if (p->job) atomic_store(&p->job->cancelled, 1);
        tiku_vfs_proxy_cancel(p->backend);
        utility_cache_free(p->cache); p->cache = NULL;
        tiku_workspace_close(app->desk, w);
        p->app = NULL; p->window = NULL; app->utility[i] = NULL;
        if (--p->refs == 0) { tiku_backend_close(p->backend); free(p); }
        app->dirty = 1;
    }
}

/** Detach app windows and drain or cancel their outstanding proxy requests. */
void device_utilities_destroy(utility_app_t *app)
{
    for (int i = 0; i < 2; i++)
        if (app->utility[i])
            device_utility_close(app, app->utility[i]->window);
    /* Backend and callbacks stay alive until queued reads retire, and
     * the wait has the same five-second ceiling quitting gives a copy:
     * a board that stopped answering mid-read must not hold the way
     * out. */
    int64_t until = utility_now() + 5000000;
    while (utility_jobs && utility_now() < until) {
        (void)tiku_work_collect();
        if (utility_jobs) usleep(10000);
    }
}

/** Capture the default once; later default changes never retarget it. */
void device_utility_open(utility_app_t *app, int mode)
{
    if (mode < 0 || mode > 1) return;
    if (app->utility[mode]) {
        tiku_workspace_activate(app->desk, app->utility[mode]->window); return;
    }
    struct device_utility *p = calloc(1, sizeof *p);
    if (!p) return;
    p->app = app; p->mode = mode; p->refs = 1;
    snprintf(p->target, sizeof p->target, "%s", app->current_device);
    snprintf(p->path, sizeof p->path, "%s", mode ? "/" : "/sys");
    utility_tree_reset(p);
    tiku_dialog_init(&p->dialog); tiku_list_init(&p->list, 1);
    tiku_field_start(&p->address, p->path); tiku_field_start(&p->edit, "");
    tiku_field_input_init(&p->address_input);
    tiku_field_input_init(&p->edit_input);
    for (int i = 0; i < 2; i++)
        tiku_history_init(&p->history[i], p->samples[i], U_HISTORY);
    int screen_h = app->surf ? app->surf->h : 700;
    int y = screen_h > 720 ? 54 + mode * 24 : 32;
    p->window = tiku_workspace_open(app->desk,
        mode ? "VFS Inspector" : "Device Monitor",
        (tiku_rect_t){70 + mode * 24, y, 860, 650},
        utility_draw, p, NULL);
    if (!p->window) { free(p); return; }
    tiku_window_set_handlers(p->window, utility_event, NULL, NULL);
    tiku_window_set_size_controls(p->window, 0, 0);
    app->utility[mode] = p;
    utility_request(p, 0); app->dirty = 1;
}

/** Poll open, unpaused monitors; one request per window at a time. */
void device_utilities_tick(utility_app_t *app)
{
    int64_t now = utility_now();
    for (int i = 0; i < 2; i++) {
        struct device_utility *p = app->utility[i];
        if (!p || p->busy || now < p->next) continue;
        p->next = now + 5000000;
        if (i || p->paused) continue;
        utility_request(p, 0);
        app->dirty = 1;         /* the sample age moves with the sample */
    }
}
