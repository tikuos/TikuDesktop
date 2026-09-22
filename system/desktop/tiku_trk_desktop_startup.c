/*
 * TikuDesktop -- explicit startup choices, without executing app discovery.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_desktop_private.h"
#include "tiku_dialog.h"
#include "tiku_preferences_layout.h"
#include "tiku_text_layout_paint.h"

#define START_MAX 32
#define START_PAGE 6
#define START_ATTR "Tiku:StartupV1"

typedef struct {
    char id[96];
    unsigned char enabled;
} start_choice_t;

typedef struct {
    unsigned char version, restore, fallback, count;
    start_choice_t choice[START_MAX];
} start_config_t;

typedef struct {
    char path[TIKU_PATH_MAX], label[96], id[96], legacy[96];
    int trusted, enabled;
} start_app_t;

struct desktop_startup {
    desktop_t *app;
    tiku_window_t *window;
    start_config_t saved;
    start_app_t items[START_MAX];
    int count, restore, fallback, page;
    tiku_dialog_t dialog;
    char status[160];
};

/** @brief Allocate the catalog even on installations with no apps. */
static struct desktop_startup *start_get(desktop_t *app)
{
    if (app->startup == NULL) {
        app->startup = calloc(1, sizeof *app->startup);
        if (app->startup != NULL) {
            app->startup->app = app;
            tiku_dialog_init(&app->startup->dialog);
        }
    }
    return app->startup;
}

/** @brief Locate a stable relative install identity, not a list position. */
static int start_index(const start_config_t *c, const char *id)
{
    int i;
    for (i = 0; i < c->count; i++) {
        if (!strcmp(c->choice[i].id, id)) return i;
    }
    return -1;
}

/** @brief Validate persisted data before allowing it to influence startup. */
static int start_valid(const start_config_t *c)
{
    int i, j;
    if (c->version != 1 || c->restore > 1 || c->fallback > 1 ||
        c->count > START_MAX) return 0;
    for (i = 0; i < c->count; i++) {
        if (!c->choice[i].id[0] || c->choice[i].enabled > 1 ||
            !memchr(c->choice[i].id, 0, sizeof c->choice[i].id)) return 0;
        for (j = 0; j < i; j++) {
            if (!strcmp(c->choice[i].id, c->choice[j].id)) return 0;
        }
    }
    return 1;
}

/** @brief Reload committed choices; opening or cancelling never writes. */
static void start_load(struct desktop_startup *p)
{
    int i;
    start_config_t c;
    memset(&c, 0, sizeof c);
    if (tiku_state_read(p->app->store, TIKU_KEY_SESSION, START_ATTR,
        &c, sizeof c) != sizeof c || !start_valid(&c)) {
        memset(&c, 0, sizeof c);
        c.version = c.restore = c.fallback = 1;
    }
    p->saved = c;
    p->restore = c.restore;
    p->fallback = c.fallback;
    for (i = 0; i < p->count; i++) {
        int at = start_index(&c, p->items[i].id);
        if (at < 0 && p->items[i].legacy[0])
            at = start_index(&c, p->items[i].legacy);
        p->items[i].enabled = at < 0 ? p->items[i].trusted :
                                      c.choice[at].enabled;
    }
}

/** @brief Register an installed app without executing its code. */
void desktop_startup_offer_id(desktop_t *app, const char *path,
                             const char *label, const char *id,
                             const char *legacy)
{
    struct desktop_startup *p = start_get(app);
    start_app_t item;
    int i;
    if (p == NULL || p->count == START_MAX) return;
    memset(&item, 0, sizeof item);
    if (strlen(path) >= sizeof item.path ||
        snprintf(item.id, sizeof item.id, "%s", id) >=
        (int)sizeof item.id) return;
    for (i = 0; i < p->count; i++) {
        if (!strcmp(item.id, p->items[i].id)) return;
    }
    snprintf(item.path, sizeof item.path, "%s", path);
    snprintf(item.label, sizeof item.label, "%s", label);
    i = (int)strlen(item.label);
    if (i > 3 && !strcmp(item.label + i - 3, ".so")) {
        item.label[i - 3] = 0;
    }
    snprintf(item.legacy, sizeof item.legacy, "%s", legacy ? legacy : "");
    item.trusted = !strncmp(id, "trusted/", 8);
    for (i = p->count; i > 0 &&
         strcmp(p->items[i - 1].id, item.id) > 0; i--) {
        p->items[i] = p->items[i - 1];
    }
    p->items[i] = item;
    p->count++;
}

/** @brief Compatibility identities for older installs and system panels. */
void desktop_startup_offer(desktop_t *app, const char *path,
                          const char *label, const char *group)
{
    char id[96];
    const char *leaf = strrchr(path, '/');
    if (snprintf(id, sizeof id, "%s/%s", group, leaf ? leaf + 1 : path) <
        (int)sizeof id)
        desktop_startup_offer_id(app, path, label, id, NULL);
}

/** @brief Read folder policy and launch selected apps after boot. */
void desktop_startup_run(desktop_t *app, int safe_mode,
                        int *restore, int *fallback)
{
    struct desktop_startup *p = start_get(app);
    int i;
    *restore = *fallback = 1;
    if (p == NULL) return;
    start_load(p);
    *restore = p->restore;
    *fallback = p->fallback;
    for (i = 0; !safe_mode && i < p->count; i++) {
        start_app_t *a = &p->items[i];
        if (!a->enabled) continue;
        tiku_trk_db_pick_t pick = { .what = TIKU_TRK_DB_LAUNCH_APP };
        snprintf(pick.path, sizeof pick.path, "%s", a->path);
        deskbar_pick_dispatch(app, &pick, 0, 0);
    }
}

/** @brief Save all choices together, preserving temporarily absent apps. */
static int start_save(struct desktop_startup *p)
{
    start_config_t c = p->saved;
    int i;
    c.restore = (unsigned char)p->restore;
    c.fallback = (unsigned char)p->fallback;
    for (i = 0; i < p->count; i++) {
        int at = start_index(&c, p->items[i].id);
        if (at < 0 && p->items[i].legacy[0])
            at = start_index(&c, p->items[i].legacy);
        if (at < 0) {
            if (c.count == START_MAX) {
                snprintf(p->status, sizeof p->status,
                         "Startup list is full; changes were not saved.");
                return 0;
            }
            at = c.count++;
        }
        snprintf(c.choice[at].id, sizeof c.choice[at].id, "%s",
                 p->items[i].id);
        c.choice[at].enabled = (unsigned char)p->items[i].enabled;
    }
    if (tiku_state_write(p->app->store, TIKU_KEY_SESSION, START_ATTR,
                        &c, sizeof c) < 0 ||
        tiku_store_flush(p->app->store) < 0) {
        (void)tiku_state_write(p->app->store, TIKU_KEY_SESSION,
                              START_ATTR, &p->saved, sizeof p->saved);
        snprintf(p->status, sizeof p->status,
                 "Could not save startup settings. Please try again.");
        return 0;
    }
    p->saved = c;
    return 1;
}

/** @brief Compact, font-scaled rows shared by drawing and input. */
static int start_row(void) { return tiku_font_plain()->height + 12; }

typedef struct {
    tiku_rect_t group, restore, fallback, list;
    tiku_rect_t previous, next, clear, cancel, save;
    int heading, note;
} start_layout_t;

/** @brief One geometry for the classic grouped panel and its hit targets. */
static start_layout_t start_layout(tiku_rect_t c)
{
    start_layout_t l;
    int row = start_row(), button = tiku_ui_button_h();
    int y = c.y + 2 * tiku_font_plain()->height + 32;
    l.group = (tiku_rect_t){c.x + 18, y, c.w - 36,
        2 * row + 2 * tiku_font_plain()->height + 10};
    l.restore = tiku_preferences_group_body(l.group);
    l.restore.h = row;
    l.fallback = l.restore;
    l.fallback.y += row;
    l.heading = y + l.group.h + 24;
    l.list = (tiku_rect_t){c.x + 18, l.heading + 10,
                         c.w - 36, START_PAGE * row + 8};
    y = l.list.y + l.list.h + 8;
    l.previous = (tiku_rect_t){c.x + c.w - 82, y, 28, button};
    l.next = (tiku_rect_t){c.x + c.w - 46, y, 28, button};
    l.note = y + button + 12 + tiku_font_plain()->ascent;
    y += button + tiku_font_plain()->height + 28;
    tiku_preferences_action_t utility = {"Open nothing", 112};
    tiku_preferences_action_t actions[] = {{"Cancel", 82}, {"Save", 82}};
    tiku_rect_t buttons[2];
    (void)tiku_preferences_actions((tiku_rect_t){c.x + 18, y, c.w - 36,
        button}, &utility, actions, 2, 12, &l.clear, buttons);
    l.cancel = buttons[0];
    l.save = buttons[1];
    return l;
}

/** @brief A list row stays inset from the well's two-pixel bevel. */
static tiku_rect_t start_app_row(start_layout_t l, int visible)
{
    return (tiku_rect_t){l.list.x + 8,
        l.list.y + 4 + visible * start_row(), l.list.w - 16, start_row()};
}

enum { START_PREVIOUS = 40, START_NEXT, START_CLEAR, START_CANCEL, START_SAVE };

/** @brief Publish the current page's complete focus order and hit targets. */
static void start_controls(struct desktop_startup *p, start_layout_t l)
{
    tiku_dialog_begin(&p->dialog, START_SAVE, START_CANCEL);
    tiku_dialog_add(&p->dialog, 0, l.restore, TIKU_DIALOG_TOGGLE, 1);
    tiku_dialog_add(&p->dialog, 1, l.fallback, TIKU_DIALOG_TOGGLE, 1);
    for (int i = p->page; i < p->count && i < p->page + START_PAGE; i++)
        tiku_dialog_add(&p->dialog, i + 2, start_app_row(l, i - p->page),
                        TIKU_DIALOG_TOGGLE, 1);
    tiku_dialog_add(&p->dialog, START_PREVIOUS, l.previous,
                    TIKU_DIALOG_BUTTON, p->page > 0);
    tiku_dialog_add(&p->dialog, START_NEXT, l.next, TIKU_DIALOG_BUTTON,
                    p->page + START_PAGE < p->count);
    tiku_dialog_add(&p->dialog, START_CLEAR, l.clear, TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_add(&p->dialog, START_CANCEL, l.cancel, TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_add(&p->dialog, START_SAVE, l.save, TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_end(&p->dialog);
}

/** @brief Native control decoration follows shared focus and capture state. */
static unsigned start_flags(struct desktop_startup *p, int id)
{
    return (tiku_dialog_focused(&p->dialog, id) ? TIKU_S_FOCUS : 0) |
           (tiku_dialog_pressed(&p->dialog, id) ? TIKU_S_PRESSED : 0);
}

/** @brief Paint one keyboard-accessible choice. */
static void start_check(tiku_surface_t *s, tiku_rect_t r,
                       const char *label, int on, int focused)
{
    tiku_ui_checkbox(s, r, label, (on ? TIKU_S_ON : 0) |
                                 (focused ? TIKU_S_FOCUS : 0));
}

/** @brief Clip long app names to the list, preserving the window's clip. */
static void start_app_label(tiku_surface_t *s, tiku_rect_t r,
                            const char *label)
{
    tiku_text_line_draw(s, (tiku_rect_t){r.x + 48, r.y, r.w - 48, r.h},
                       TIKU_MEASURE_PLAIN, label, TIKU_C_TEXT);
}

/** @brief Folder policy and a paged checklist of installed applications. */
static void start_draw(tiku_window_t *w, tiku_surface_t *s,
                       tiku_rect_t c, void *ctx)
{
    struct desktop_startup *p = ctx;
    const tiku_font_t *f = tiku_font_plain();
    start_layout_t l = start_layout(c);
    int i, enabled = 0;
    char count[64];
    tiku_rgb_t quiet = tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL);
    (void)w;
    start_controls(p, l);
    tiku_fill(s, c, TIKU_C_PANEL);
    tiku_icon_paint(s, "application", c.x + 20, c.y + 14, 32);
    tiku_text(s, tiku_font_bold(), c.x + 66, c.y + f->ascent + 12,
              "At startup", TIKU_C_TEXT);
    tiku_text(s, f, c.x + 66, c.y + f->height + f->ascent + 16,
              "Choose what opens with your desktop.", quiet);
    (void)tiku_ui_group(s, l.group, "Windows");
    start_check(s, l.restore, "Reopen my previous folders", p->restore,
                tiku_dialog_focused(&p->dialog, 0));
    start_check(s, l.fallback, "Open default folders if none reopen",
                p->fallback, tiku_dialog_focused(&p->dialog, 1));
    tiku_text(s, tiku_font_bold(), l.list.x, l.heading,
              "Open these applications", TIKU_C_TEXT);
    tiku_ui_sunken(s, l.list, TIKU_C_DOC);
    for (i = p->page; i < p->count && i < p->page + START_PAGE; i++) {
        tiku_rect_t r = start_app_row(l, i - p->page);
        start_check(s, r, NULL, p->items[i].enabled,
                    tiku_dialog_focused(&p->dialog, i + 2));
        const char *icon = "application";
        for (int k = 0; k < p->app->apps.count; k++) {
            const tiku_trk_app_t *a = &p->app->apps.app[k];
            if (!strcmp(a->module, p->items[i].path) && a->icon[0])
                icon = a->icon;
        }
        tiku_icon_paint(s, icon, r.x + 24,
                        r.y + (r.h - 16) / 2, 16);
        start_app_label(s, r, p->items[i].label);
    }
    if (!p->count) tiku_text(s, f, l.list.x + 16,
        l.list.y + 14 + f->ascent, "No applications installed.", quiet);
    for (i = 0; i < p->count; i++) enabled += p->items[i].enabled != 0;
    snprintf(count, sizeof count, "%d selected", enabled);
    tiku_text(s, f, l.list.x, l.next.y + f->ascent + 4, count, quiet);
    if (p->count > START_PAGE) {
        snprintf(count, sizeof count, "%d / %d", p->page / START_PAGE + 1,
                 (p->count + START_PAGE - 1) / START_PAGE);
        tiku_text(s, f, l.previous.x - tiku_text_width(f, count) - 12,
                  l.next.y + f->ascent + 4, count, quiet);
        tiku_ui_button(s, l.previous, "<", start_flags(p, START_PREVIOUS) |
                       (p->page ? 0 : TIKU_S_DISABLED));
        tiku_ui_button(s, l.next, ">",
            start_flags(p, START_NEXT) |
            (p->page + START_PAGE < p->count ? 0 : TIKU_S_DISABLED));
    }
    tiku_text_line_draw(s, (tiku_rect_t){c.x + 18, l.note - f->ascent,
        c.w - 36, f->height}, TIKU_MEASURE_PLAIN, p->status[0] ? p->status :
        "Changes take effect the next time you start TikuDesktop.", quiet);
    tiku_ui_button(s, l.cancel, "Cancel", start_flags(p, START_CANCEL));
    tiku_ui_button(s, l.clear, "Open nothing", start_flags(p, START_CLEAR));
    tiku_ui_button(s, l.save, "Save",
                   TIKU_S_DEFAULT | start_flags(p, START_SAVE));
}

/** @brief Toggle only the focused draft value. */
static void start_toggle(struct desktop_startup *p, int id)
{
    if (id == 0) p->restore = !p->restore;
    else if (id == 1) p->fallback = !p->fallback;
    else p->items[id - 2].enabled = !p->items[id - 2].enabled;
}

/** @brief Mouse and keyboard take the same draft-and-save path. */
static int start_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    struct desktop_startup *p = ctx;
    tiku_rect_t c = tiku_window_content(w);
    start_layout_t l = start_layout(c);
    int id;
    start_controls(p, l);
    id = tiku_dialog_event(&p->dialog, ev).activated;
    /* Last action's word only: a save that failed must not still be
     * reported under a row the user merely ticked. */
    if (id >= 0) p->status[0] = '\0';
    if (ev->type == TIKU_EVENT_KEY_DOWN && (ev->modifiers & TIKU_MOD_CMD)) {
        if (ev->key == 's') id = START_SAVE;
        if (ev->key == 'w') id = START_CANCEL;
    }
    if (id >= 0 && id < p->count + 2) start_toggle(p, id);
    if (id == START_PREVIOUS) p->page -= START_PAGE;
    if (id == START_NEXT) p->page += START_PAGE;
    if (id == START_CLEAR) {
        p->restore = p->fallback = 0;
        for (int i = 0; i < p->count; i++) p->items[i].enabled = 0;
    }
    if (id == START_CANCEL || (id == START_SAVE && start_save(p)))
        desktop_startup_close(p->app);
    desktop_damage_all(p->app);
    return 1;
}

/** @brief Return the current Startup Preferences window. */
tiku_window_t *desktop_startup_handle(desktop_t *app)
{
    return app->startup ? app->startup->window : NULL;
}

/** @brief Close without saving pending changes. */
void desktop_startup_close(desktop_t *app)
{
    tiku_window_t *w = desktop_startup_handle(app);
    if (app->prefs_track == w) app->prefs_track = NULL;
    if (w != NULL && w->open) tiku_workspace_close(app->desk, w);
    if (app->startup != NULL) app->startup->window = NULL;
    desktop_damage_all(app);
}

/** @brief Open or raise Startup Preferences. */
void desktop_startup_open(desktop_t *app)
{
    struct desktop_startup *p = start_get(app);
    tiku_rect_t r;
    start_layout_t l;
    if (p == NULL) return;
    if (p->window != NULL && p->window->open) {
        tiku_workspace_activate(app->desk, p->window);
        return;
    }
    start_load(p);
    p->page = 0;
    tiku_dialog_init(&p->dialog);
    p->status[0] = 0;
    r.w = tiku_text_width(tiku_font_plain(),
        "Changes take effect the next time you start TikuDesktop.") + 48;
    if (r.w < 480) r.w = 480;
    l = start_layout((tiku_rect_t){0, 0, r.w, 0});
    r.h = l.save.y + l.save.h + 44;
    r.x = (app->surf->w - r.w) / 2;
    r.y = (app->surf->h - r.h) / 3;
    p->window = tiku_workspace_open(app->desk, "Startup Preferences", r,
                                    start_draw, p, NULL);
    if (p->window != NULL) {
        tiku_window_set_size_controls(p->window, 0, 0);
        tiku_window_set_handlers(p->window, start_event, NULL, NULL);
    }
    desktop_damage_all(app);
}

/** @brief Release the startup catalog and its window. */
void desktop_startup_destroy(desktop_t *app)
{
    desktop_startup_close(app);
    free(app->startup);
    app->startup = NULL;
}
