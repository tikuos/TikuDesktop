/* Local-only picks and session reuse across the app/desktop boundary. */
#include <assert.h>
#include "../../system/desktop/tiku_trk_desktop_panels.c"
/** @brief The painter is not linked: a full frame is the flag, as before. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }

static int panels, replies;
static char panel_start[TIKU_PATH_MAX], answer[TIKU_PATH_MAX];

void tiku_trk_panel_window_open_file(tiku_trk_panel_window_t *p, void *target,
    int command, const char *start, const char *filter)
{
    (void)p; (void)target; (void)filter;
    assert(command == TIKU_TRK_CMD_APP_PICK);
    panels++; snprintf(panel_start, sizeof panel_start, "%s", start);
}

void tiku_trk_panel_window_open_save(tiku_trk_panel_window_t *p, void *target,
    int command, const char *start, const char *name)
{ (void)p; (void)target; (void)command; (void)start; (void)name; assert(0); }

int tiku_remote_tell(tiku_remote_listener_t *l, int session, const tiku_msg_t *m)
{
    (void)l; assert(session == 0 && tiku_msg_what(m) == TIKU_MSG_PICKED);
    snprintf(answer, sizeof answer, "%s", tiku_msg_find_string(m, "path", 0));
    replies++; return 1;
}

int main(void)
{
    desktop_t *app = calloc(1, sizeof *app); assert(app);
    tiku_backend_t fake = {0};
    const tiku_backend_ops_t ops = {.name = "device"};
    fake.ops = &ops;
    app->backend = tiku_mount_open(); assert(app->backend);
    assert(!tiku_mount_add(app->backend, "/", &fake, 0));
    assert(!tiku_mount_add(app->backend, "/local", tiku_backend_posix_open(), 1));
    app->remote.session[0].used = 1;
    app->remote.session[0].generation = 7;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_PICK); assert(m);
    assert(tiku_msg_add_int32(m, "window", 1));
    assert(tiku_msg_add_int32(m, "mode", TIKU_APP_PICK_LOCAL_OPEN));
    assert(tiku_msg_add_string(m, "start", "/tmp"));
    desktop_remote_pick(app, 0, m);
    assert(panels == 1 && !strcmp(panel_start, "/local/tmp"));
    deliver_pick(app, "/local/tmp/document.md");
    assert(replies == 1 && !strcmp(answer, "/tmp/document.md"));

    desktop_remote_pick(app, 0, m);
    deliver_pick(app, "/data/device.md");
    assert(replies == 1 && strstr(app->status, "local files"));
    assert(app->pending_pick.kind == 0);
    desktop_remote_pick(app, 0, m);
    app->remote.session[0].generation++;
    deliver_pick(app, "/local/tmp/stale.md");
    assert(replies == 1 && app->pending_pick.kind == 0);
    desktop_remote_pick(app, 0, m);
    deliver_pick(app, NULL);
    assert(replies == 1 && app->pending_pick.kind == 0);
    assert(!tiku_mount_remove(app->backend, "/local"));
    desktop_remote_pick(app, 0, m);
    assert(app->pending_pick.kind == 0 && strstr(app->status, "No local"));
    tiku_msg_free(m); app->backend->ops->close(app->backend); free(app);
    puts("PASS: local picker namespace mapping, device refusal, cancellation, session reuse");
}
