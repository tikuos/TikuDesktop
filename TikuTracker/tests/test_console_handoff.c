/* Close ordering for Console and Bench: deterministic worker, fake backend. */
#include <assert.h>
#include "../src/shell/desktop/tiku_trk_desktop_private.h"
#include "tiku_work.h"
#include "tiku_trail.h"

static tiku_work_job_t pending;
static int queued, closed, embedded, remote, cancelled;
static void board_key(char *out, size_t max, const char *prefix)
{ snprintf(out, max, "board:%s", prefix); }
int tiku_work_post(const tiku_work_job_t *job)
{ assert(!queued); pending = *job; queued = 1; return 0; }
int tiku_work_cancel(const char *key)
{ assert(!strcmp(key, "board:/devices/lab")); cancelled++; return 0; }
void desktop_board_tell(desktop_t *app, int session, int what, const char *name)
{
    (void)app; assert(session >= 0 && session < TIKU_REMOTE_SESSIONS);
    assert(closed && what == TIKU_APP_BOARD_RELEASED && !strcmp(name, "lab")); remote++;
}
static void said(void *state, int what, const char *name)
{
    (void)state; assert(closed && what == TIKU_APP_BOARD_RELEASED && !strcmp(name, "lab")); embedded++;
}
static void close_backend(tiku_backend_t *b)
{ assert(!closed); closed = 1; free(b); }
static const tiku_backend_ops_t ops = {.close = close_backend};
#include "../src/shell/desktop/tiku_trk_desktop_release.inl"

int main(void)
{
    desktop_t *app = calloc(1, sizeof *app); assert(app);
    const tiku_app_descriptor_t descriptor = {.board_said = said};
    app->embeds[0].running = 1; app->embeds[0].descriptor = &descriptor;
    app->backend = tiku_mount_open(); assert(app->backend);
    for (int release = 0; release < 2; release++) {
        closed = embedded = remote = 0;
        tiku_backend_t *board = calloc(1, sizeof *board); assert(board); board->ops = &ops;
        assert(!tiku_mount_add(app->backend, "/devices/lab", board, 1));
        assert(!(release ? desktop_board_unmount_released(app, "/devices/lab") :
                            desktop_board_unmount(app, "/devices/lab")));
        assert(queued && !closed && !embedded && !remote);
        assert(!strcmp(pending.key, "board:/devices/lab"));
        assert(!tiku_mount_count(app->backend));
        pending.run(pending.ctx); assert(closed && !embedded && !remote);
        pending.done(pending.ctx); pending.free(pending.ctx); queued = 0;
        assert(embedded == release && remote == release * TIKU_REMOTE_SESSIONS);
    }
    assert(cancelled == 2); tiku_backend_close(app->backend); free(app);
    puts("PASS: device release reaches embedded/remote apps only after the close worker finishes");
    return 0;
}
