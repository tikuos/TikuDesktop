/* Device access is a desktop service, not an application's private port.
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_trk_desktop_private.h"
#include "tiku_vfs_rpc.h"
#include "tiku_work.h"

struct desktop_vfs_job {
    struct desktop_vfs_job *next;
    desktop_t *app;
    tiku_backend_t *backend;
    tiku_msg_t *request, *reply;
    int session;
    uint64_t generation;
    char key[TIKU_WORK_KEY];
};

/** @brief Device operations execute under the same key as mount traffic. */
static void vfs_run(void *ctx)
{
    struct desktop_vfs_job *j = ctx;
    if (!tiku_work_cancelled(j))
        j->reply = tiku_vfs_serve(j->backend, j->request);
}

/** @brief A reused session slot must never receive another app's response. */
static void vfs_done(void *ctx)
{
    struct desktop_vfs_job *j = ctx;
    if (!j->app || !j->reply) return;
    tiku_remote_session_t *s = &j->app->remote.session[j->session];
    if (s->used && s->generation == j->generation)
        tiku_remote_tell(&j->app->remote, j->session, j->reply);
}

/** @brief Remove only this request, including work cancelled by unmount. */
static void vfs_free(void *ctx)
{
    struct desktop_vfs_job *j = ctx;
    if (j->app) {
        struct desktop_vfs_job **at = &j->app->vfs_jobs;
        while (*at && *at != j) at = &(*at)->next;
        if (*at) *at = j->next;
    }
    tiku_msg_free(j->request); tiku_msg_free(j->reply); free(j);
}

/** @brief Unavailable devices never fall back to host files. */
static void vfs_refuse(desktop_t *app, int session, const tiku_msg_t *request)
{
    tiku_msg_t *reply = tiku_vfs_serve(NULL, request);
    if (reply) tiku_remote_tell(&app->remote, session, reply);
    tiku_msg_free(reply);
}

/** @brief Admit bounded requests only for an already connected device. */
void desktop_vfs_request(desktop_t *app, int session, const tiku_msg_t *request)
{
    const char *board = tiku_msg_find_string(request, "board", 0);
    char full[TIKU_PATH_MAX], local[TIKU_PATH_MAX];
    tiku_backend_t *b = NULL;
    int count = 0;
    uint64_t generation = app->remote.session[session].generation;
    for (struct desktop_vfs_job *j = app->vfs_jobs; j; j = j->next) {
        count++;
        if (j->session == session && j->generation == generation) return;
    }
    if (board && *board && strlen(board) < 64 && !strchr(board, '/') &&
        strcmp(board, ".") && strcmp(board, "..") && count < 32 &&
        tiku_msg_flat_size(request) <= 8192) {
        snprintf(full, sizeof full, "/devices/%s/sys", board);
        b = tiku_backend_serving(app->backend, full, local, sizeof local);
    }
    if (!b || !tiku_backend_tiku_identity(b)) {
        vfs_refuse(app, session, request); return;
    }
    struct desktop_vfs_job *j = calloc(1, sizeof *j);
    if (!j) { vfs_refuse(app, session, request); return; }
    j->request = tiku_msg_copy(request);
    if (!j->request) { free(j); vfs_refuse(app, session, request); return; }
    j->app = app; j->backend = b; j->session = session;
    j->generation = generation;
    snprintf(j->key, sizeof j->key, "board:/devices/%.32s", board);
    tiku_work_job_t work = {.run = vfs_run, .done = vfs_done,
                            .free = vfs_free, .ctx = j};
    snprintf(work.key, sizeof work.key, "%s", j->key);
    j->next = app->vfs_jobs; app->vfs_jobs = j;
    if (tiku_work_post(&work)) {
        vfs_free(j); vfs_refuse(app, session, request);
    }
}

/** @brief Drain before closing backends; a stuck worker retains ownership. */
int desktop_vfs_destroy(desktop_t *app)
{
    for (struct desktop_vfs_job *j = app->vfs_jobs; j; j = j->next)
        tiku_work_cancel(j->key);
    int64_t until = tiku_trk_now() + 5000000;
    while (app->vfs_jobs && tiku_trk_now() < until) {
        tiku_work_collect();
        if (app->vfs_jobs) usleep(1000);
    }
    int drained = app->vfs_jobs == NULL;
    while (app->vfs_jobs) {
        struct desktop_vfs_job *j = app->vfs_jobs;
        app->vfs_jobs = j->next; j->app = NULL;
    }
    return drained;
}
