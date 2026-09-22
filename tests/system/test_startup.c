/* Startup preferences: real controls and persistence, mocked app launches. */
#include <assert.h>
#include "../../system/desktop/tiku_trk_desktop_startup.c"
/** @brief The painter is not linked: a full frame is the flag, as before. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }
#include "../../system/shared/shell/session/tiku_trk_session_controller.c"

static int loads, launches, folders;
static char last_path[TIKU_PATH_MAX];

const tiku_app_descriptor_t *tiku_app_load(const char *path,
                                         char *err, size_t max)
{
    static const tiku_app_descriptor_t descriptor = {0};
    (void)path; (void)err; (void)max;
    loads++;
    return &descriptor;
}

int embed_start(desktop_t *app, const tiku_app_descriptor_t *d)
{
    (void)app; (void)d;
    return 1;
}

void deskbar_pick_dispatch(desktop_t *app, tiku_trk_db_pick_t *pick,
                           int x, int y)
{
    (void)app; (void)x; (void)y;
    assert(pick->what == TIKU_TRK_DB_LAUNCH_APP);
    snprintf(last_path, sizeof last_path, "%s", pick->path);
    launches++;
}

tiku_trk_folder_window_t *tiku_trk_folder_lifecycle_open(
    tiku_trk_folder_lifecycle_t *controller, const char *path)
{
    static tiku_window_t window;
    static tiku_trk_folder_window_t folder;
    (void)controller;
    folder.win = &window;
    snprintf(last_path, sizeof last_path, "%.*s",
             (int)sizeof last_path - 1, path);
    folders++;
    return &folder;
}

int tiku_trk_desktop_view_shows_disks(const tiku_trk_desktop_view_t *v)
{
    (void)v;
    return 1;
}

/* Exercise complete pointer gestures, not press-only pseudo-clicks. */
static int send_input(tiku_window_t *w, const tiku_event_t *ev)
{
    int result = tiku_window_send(w, ev);
    if (ev->type == TIKU_EVENT_POINTER_DOWN && w->open) {
        tiku_event_t up = *ev;
        up.type = TIKU_EVENT_POINTER_UP;
        result |= tiku_window_send(w, &up);
    }
    return result;
}

int main(void)
{
    desktop_t *app = calloc(1, sizeof *app);
    struct desktop_startup *p;
    start_config_t saved;
    int restore, fallback;
    tiku_rect_t c;
    start_layout_t layout;
    tiku_event_t ev;
    tiku_trk_session_controller_t session = {0};
    const char *explicit_path[] = {"/explicit"};
    assert(app);
    app->store = tiku_store_memory_open();
    app->surf = tiku_surface_new(1100, 800, TIKU_C_PANEL);
    app->desk = tiku_workspace_new(app->surf);
    desktop_startup_offer(app, "/apps/trusted/Clock.so", "Clock.so", "trusted");
    desktop_startup_offer(app, "/apps/TikuBasic.so", "TikuBasic", "apps");
    desktop_startup_offer(app, "/apps/TikuBasic.so", "TikuBasic", "apps");
    assert(!loads && !launches && app->startup->count == 2);
    desktop_startup_run(app, 0, &restore, &fallback);
    assert(loads == 0 && launches == 1 && restore && fallback);
    desktop_startup_open(app);
    p = app->startup;
    assert(p->window);
    c = tiku_window_content(p->window);
    layout = start_layout(c);
    ev = (tiku_event_t){.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=layout.restore.x+4, .y=layout.restore.y+4};
    assert(send_input(p->window, &ev));
    assert(!p->restore);
    ev = (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN, .key=TIKU_KEY_ESCAPE};
    assert(send_input(p->window, &ev));
    desktop_startup_open(app);
    assert(p->restore); /* Cancel did not save. */
    ev.key = ' ';
    assert(send_input(p->window, &ev));
    ev.key = TIKU_KEY_TAB;
    assert(send_input(p->window, &ev));
    ev.key = ' ';
    assert(send_input(p->window, &ev));
    assert(!p->restore && !p->fallback);
    p->items[0].enabled = 1; /* TikuBasic, sorted before trusted/Clock. */
    p->items[1].enabled = 0;
    ev.key = 's'; ev.modifiers = TIKU_MOD_CMD;
    assert(send_input(p->window, &ev));
    assert(!desktop_startup_handle(app));
    desktop_startup_run(app, 0, &restore, &fallback);
    assert(!restore && !fallback && loads == 0 && launches == 2);
    assert(!strcmp(last_path, "/apps/TikuBasic.so"));
    desktop_startup_run(app, 1, &restore, &fallback);
    assert(loads == 0 && launches == 2); /* Safe mode runs no apps. */
    saved = p->saved;
    desktop_startup_destroy(app);
    /* A moved installation keeps its choices; old absolute paths aren't run. */
    desktop_startup_offer_id(app, "/new/apps/TikuBasic.so", "TikuBasic",
                             "app/basic", "apps/TikuBasic.so");
    desktop_startup_run(app, 0, &restore, &fallback);
    assert(launches == 3 && !strcmp(last_path, "/new/apps/TikuBasic.so"));
    p = app->startup;
    assert(start_save(p));
    assert(p->saved.count == 2); /* Temporarily absent Clock is retained. */
    tiku_store_free(app->store);
    app->store = NULL;
    assert(!start_save(p) && strstr(p->status, "Could not save"));
    app->store = session.store = tiku_store_memory_open();
    assert(tiku_state_write(app->store, TIKU_KEY_SESSION, START_ATTR,
                            &saved, sizeof saved) == 0);
    for (int i = 0; i < 10; i++) {
        char path[64], label[64];
        snprintf(path, sizeof path, "/apps/Example%d.so", i);
        snprintf(label, sizeof label, "Example %d", i);
        desktop_startup_offer(app, path, label, "apps");
    }
    desktop_startup_open(app);
    c = tiku_window_content(p->window);
    layout = start_layout(c);
    ev = (tiku_event_t){.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=layout.next.x+4, .y=layout.next.y+4};
    assert(send_input(p->window, &ev));
    assert(p->page == START_PAGE);
    /* The arrow greys itself at the last page: the focus goes to the
     * arrow beside it, never back to the folder settings at the top,
     * where the next Space would work a setting nobody aimed at. */
    {
        tiku_event_t key = {.type=TIKU_EVENT_KEY_DOWN, .key=' ',
                            .text=" "};
        int restore_was = p->restore, fallback_was = p->fallback;

        p->page = 0;
        start_controls(p, layout);
        p->dialog.focus = START_NEXT;
        p->dialog.keyboard = 1;
        assert(send_input(p->window, &key));       /* on to the last page */
        assert(p->page == START_PAGE);
        assert(p->dialog.focus != 0 && p->dialog.focus != 1);
        assert(send_input(p->window, &key));       /* and once more */
        assert(p->restore == restore_was && p->fallback == fallback_was);
        p->page = START_PAGE;
        p->dialog.keyboard = 0;
        start_controls(p, layout);
    }
    ev.x = start_app_row(layout, 0).x+4;
    ev.y = start_app_row(layout, 0).y+4;
    assert(send_input(p->window, &ev));
    assert(p->dialog.focus == START_PAGE+2 && p->items[START_PAGE].enabled);
    /* The list bevel and surrounding whitespace are not checkboxes. */
    ev.x = layout.list.x+1; ev.y = layout.list.y+1;
    assert(send_input(p->window, &ev));
    assert(p->items[START_PAGE].enabled);
    p->page = 0;
    tiku_workspace_draw(app->desk);
    assert(tiku_surface_png(app->surf, "/tmp/tiku-startup-preferences.png") == 0);
    {
        char label[96];
        snprintf(label, sizeof label, "%s", p->items[0].label);
        memset(p->items[0].label, 'W', sizeof p->items[0].label - 1);
        p->items[0].label[sizeof p->items[0].label - 1] = 0;
        tiku_theme_set(tiku_theme_dark());
        p->dialog.keyboard = 1; p->dialog.focus = 2;
        tiku_workspace_draw(app->desk);
        assert(tiku_surface_png(app->surf,
                                "/tmp/tiku-startup-dark.png") == 0);
        tiku_theme_set(NULL);
        snprintf(p->items[0].label, sizeof p->items[0].label, "%s", label);
    }
    ev.x = layout.clear.x+4; ev.y = layout.clear.y+4;
    assert(send_input(p->window, &ev));
    assert(!p->restore && !p->fallback);
    for (int i = 0; i < p->count; i++) assert(!p->items[i].enabled);
    ev.x = layout.save.x+4;
    assert(send_input(p->window, &ev)); /* Save by mouse. */
    assert(!desktop_startup_handle(app));
    desktop_startup_run(app, 0, &restore, &fallback);
    assert(!restore && !fallback && loads == 0 && launches == 3);
    desktop_startup_open(app);
    ev = (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN, .key=TIKU_KEY_RETURN};
    assert(send_input(p->window, &ev));
    assert(!desktop_startup_handle(app)); /* Return activates default Save. */
    saved.version = 99;
    assert(!start_valid(&saved));
    saved.version = 1; saved.count = START_MAX+1;
    assert(!start_valid(&saved));
    saved.count = 1;
    memset(saved.choice[0].id, 'x', sizeof saved.choice[0].id);
    assert(!start_valid(&saved));
    /* No restore + no fallback genuinely means no folder windows. */
    tiku_trk_session_restore_folders(&session, NULL, 0, 1, 0, 0);
    assert(!folders);
    tiku_trk_session_restore_folders(&session, explicit_path, 1, 1, 0, 0);
    assert(folders == 1 && !strcmp(last_path, "/explicit"));
    folders = 0;
    tiku_trk_session_restore_folders(&session, NULL, 0, 1, 1, 1);
    assert(folders == 2 && !strcmp(last_path, "/dev"));
    {
        tiku_trk_session_entry_t entry = {.path="/saved", .minimized=1};
        assert(tiku_trk_session_save_entries(session.store, &entry, 1) == 1);
        folders = 0;
        tiku_trk_session_restore_folders(&session, NULL, 0, 0, 0, 0);
        assert(folders == 1 && !strcmp(last_path, "/saved"));
        folders = 0;
        tiku_trk_session_restore_folders(&session, NULL, 0, 1, 0, 0);
        assert(!folders);
    }
    desktop_startup_destroy(app);
    tiku_store_free(app->store);
    tiku_workspace_free(app->desk);
    tiku_surface_free(app->surf);
    free(app);
    puts("PASS: startup selection, safe mode, persistence, controls and empty startup");
    return 0;
}
