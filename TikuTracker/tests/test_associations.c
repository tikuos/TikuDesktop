/* File-association preferences: real controls, defaults and saved choices. */
#include <assert.h>
#include "../src/shell/desktop/tiku_trk_desktop_associations.c"

/** @brief Complete the same press/release gesture as the desktop router. */
static void click_control(tiku_window_t *w, tiku_rect_t r)
{
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                      .x = r.x + 8, .y = r.y + r.h / 2};
    assert(tiku_window_send(w, &e));
    e.type = TIKU_EVENT_POINTER_UP;
    assert(tiku_window_send(w, &e));
}

/** @brief Find types by identity, never by discovery or display order. */
static int type_index(const tiku_trk_associations_t *m, const char *type)
{
    for (int i = 0; i < m->count; i++)
        if (!strcmp(m->row[i].type, type)) return i;
    return -1;
}

/** @brief Read the default the actual launch registry would use. */
static const char *handler(desktop_t *app, const char *type)
{
    tiku_trk_cand_t c[TIKU_TRK_APP_MAX];
    assert(tiku_trk_launch_candidates(&app->apps, &type, 1, app->store,
                                      c, TIKU_TRK_APP_MAX));
    return c[0].app->name;
}

/** @brief Save survives a new store; cancel, restore and failure are safe. */
int main(void)
{
    char home[] = "/tmp/tiku-associations-XXXXXX";
    assert(mkdtemp(home) && !setenv("HOME", home, 1));
    desktop_t *app = calloc(1, sizeof *app); assert(app);
    app->store = tiku_store_sidecar_open("test"); assert(app->store);
    app->surf = tiku_surface_new(1100, 800, TIKU_C_PANEL);
    app->desk = tiku_workspace_new(app->surf);
    tiku_trk_launch_add_document(&app->apps, "TikuBasic", "/runner",
        "/Basic.so", "text/x-basic,application/x-tiku-project");
    tiku_trk_launch_add_document(&app->apps, "Editor", "/runner",
        "/Editor.so", "text/plain,text/");
    tiku_trk_launch_add_document(&app->apps, "TikuImage", "/runner",
        "/Image.so", "image/png,image/jpeg,image/bmp,image/x-ms-bmp");
    tiku_trk_types_add(&app->types, "text/x-custom", "Custom document", "", "");
    for (int i = 0; i < 10; i++) {
        char type[48], label[48];
        snprintf(type, sizeof type, "text/x-fixture%d", i);
        snprintf(label, sizeof label, "Fixture %d", i);
        tiku_trk_types_add(&app->types, type, label, "", "");
    }
    desktop_associations_open(app);
    struct desktop_associations *p = app->associations;
    assert(p && p->window && p->model.count > ASSOC_PAGE);
    assert(type_index(&p->model, "text/x-custom") >= 0);
    int at = type_index(&p->model, "text/x-basic"); assert(at >= 0);
    p->page = at / ASSOC_PAGE * ASSOC_PAGE;
    assoc_layout_t l = assoc_layout(tiku_window_content(p->window));
    click_control(p->window, assoc_row(l.types, at - p->page));
    assert(p->selected == at && p->count == 2);
    assert(!strcmp(p->candidates[0].app->name, "TikuBasic"));
    assert(!strcmp(p->candidates[1].app->name, "Editor"));
    click_control(p->window, assoc_row(l.apps, 2));
    assert(!strcmp(p->model.row[at].chosen, "Editor"));
    assert(!strcmp(handler(app, "text/x-basic"), "TikuBasic"));
    tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_ESCAPE};
    assert(tiku_window_send(p->window, &e));
    desktop_associations_open(app);
    assert(!p->model.row[at].chosen[0]);
    p->page = at / ASSOC_PAGE * ASSOC_PAGE;
    l = assoc_layout(tiku_window_content(p->window));
    click_control(p->window, assoc_row(l.types, at - p->page));
    click_control(p->window, assoc_row(l.apps, 2));
    e.key = 's'; e.modifiers = TIKU_MOD_CMD;
    assert(tiku_window_send(p->window, &e));
    assert(!desktop_associations_handle(app));
    assert(!strcmp(handler(app, "text/x-basic"), "Editor"));
    tiku_store_free(app->store);
    app->store = tiku_store_sidecar_open("test");
    assert(!strcmp(handler(app, "text/x-basic"), "Editor"));
    desktop_associations_open(app);
    p->page = at / ASSOC_PAGE * ASSOC_PAGE;
    l = assoc_layout(tiku_window_content(p->window));
    click_control(p->window, assoc_row(l.types, at - p->page));
    click_control(p->window, assoc_row(l.apps, 0));
    assert(!p->model.row[at].chosen[0]);
    assert(!tiku_trk_associations_save(&p->model, NULL));
    assert(!strcmp(p->model.row[at].saved, "Editor"));
    assert(tiku_trk_associations_save(&p->model, app->store));
    assert(!strcmp(handler(app, "text/x-basic"), "TikuBasic"));
    /* Untouched rows must not overwrite another preference editor's save. */
    tiku_trk_launch_set_preferred(app->store, "text/plain", "Missing editor");
    assert(tiku_trk_associations_save(&p->model, app->store));
    char pref[48];
    tiku_trk_launch_preferred(app->store, "text/plain", pref, sizeof pref);
    assert(!strcmp(pref, "Missing editor"));
    for (int size = 12; size <= 16; size += 4) {
        desktop_associations_close(app);
        tiku_font_set_size(size);
        desktop_associations_open(app);
        l = assoc_layout(tiku_window_content(p->window));
        assert(l.save.y + l.save.h <= p->window->frame.y + p->window->frame.h);
        assoc_draw(p->window, app->surf, tiku_window_content(p->window), p);
        click_control(p->window, l.next);
        assert(p->page == ASSOC_PAGE);
        click_control(p->window, l.previous);
        assert(p->page == 0);
    }
    desktop_associations_destroy(app);
    tiku_store_free(app->store);
    tiku_workspace_free(app->desk);
    tiku_surface_free(app->surf); free(app);
    puts("PASS: association controls, cancel/save/reset, persistence and failure");
    return 0;
}
