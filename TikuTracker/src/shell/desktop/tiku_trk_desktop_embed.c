/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_embed.c - the embedded descriptor host: the services
 * contract an application hosted in the desktop's own process is given, and
 * the windows it holds for it
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/**
 * @brief The About box's colour cycle, which only sometimes has news.
 *
 * Runs whether or not the window is open, because a task that has to be
 * registered and unregistered as windows come and go is a task that will
 * one day be left registered.  It costs a compare when there is nothing
 * to say, and it says nothing for the three seconds in four that the
 * mark is holding a colour -- the same discipline the clock keeps, and
 * for the same reason: a blind dirty on a timer repaints the world.
 */
int
tick_embeds(void *ctx)
{
    desktop_t *app = ctx;
    int e;

    for (e = 0; e < TIKU_TRK_EMBED_MAX; e++) {
        if (app->embeds[e].running &&
            app->embeds[e].descriptor->tick != NULL) {
            app->embeds[e].descriptor->tick(app->embeds[e].state,
                                            tiku_trk_now());
        }
    }
    return 0;                       /* periodic: never done */
}

/** @brief An in-process application's window shows its latest frame. */
static void
embed_window_draw(tiku_window_t *window, tiku_surface_t *surface,
                  tiku_rect_t content, void *context)
{
    struct embed_win *ew = context;
    int x, y, w, h;

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    if (ew == NULL || ew->frame == NULL) {
        return;
    }
    w = (ew->fw < content.w) ? ew->fw : content.w;
    h = (ew->fh < content.h) ? ew->fh : content.h;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            tiku_pixel(surface, content.x + x, content.y + y,
                            ew->frame[(size_t)y * (size_t)ew->fw + x]);
        }
    }
}

/** @brief The window @p w of a hosted application, or NULL. */
struct embed_win *
embed_win_of_window(desktop_t *app, const tiku_window_t *w)
{
    int i, j;

    if (w == NULL) {
        return NULL;
    }
    for (i = 0; i < TIKU_TRK_EMBED_MAX; i++) {
        if (!app->embeds[i].running) {
            continue;
        }
        for (j = 0; j < TIKU_TRK_EMBED_WINDOWS; j++) {
            if (app->embeds[i].win[j].window == w) {
                return &app->embeds[i].win[j];
            }
        }
    }
    return NULL;
}

/** @brief The window this application knows as @p id, or NULL. */
static struct embed_win *
embed_win_of_id(struct desktop_embed *slot, uint32_t id)
{
    int j;

    for (j = 0; slot != NULL && j < TIKU_TRK_EMBED_WINDOWS; j++) {
        if (slot->win[j].window != NULL && slot->win[j].id == id) {
            return &slot->win[j];
        }
    }
    return NULL;
}

/** @brief How many windows this application still holds. */
static int
embed_window_count(const struct desktop_embed *slot)
{
    int j, n = 0;

    for (j = 0; j < TIKU_TRK_EMBED_WINDOWS; j++) {
        if (slot->win[j].window != NULL) {
            n++;
        }
    }
    return n;
}

static uint32_t
embed_svc_open(void *ctx, const char *title, int w, int h)
{
    struct desktop_embed *slot = ctx;
    desktop_t *app = slot->app;
    struct embed_win *ew = NULL;
    tiku_rect_t frame;
    int j;

    for (j = 0; j < TIKU_TRK_EMBED_WINDOWS; j++) {
        if (slot->win[j].window == NULL) {
            ew = &slot->win[j];
            break;
        }
    }
    if (ew == NULL) {
        return 0;               /* no room: said with a 0, not a lie */
    }
    frame.w = w + 2 * TIKU_WINDOW_BORDER;
    frame.h = h + tiku_window_tab_h() + 2 * TIKU_WINDOW_BORDER;
    frame.x = (app->surf->w - frame.w) / 2;
    frame.y = (app->surf->h - frame.h) / 3;
    /* Applications are stepped past each other; the WINDOWS of one
     * application are set BESIDE each other instead.  A window laid
     * over another's tab is a window that cannot be reached by the one
     * gesture that raises it, and an application with several windows
     * is asking for them to be worked with, not stacked. */
    frame.x += (int)(slot - app->embeds) * 24;
    frame.y += (int)(slot - app->embeds) * 20;
    {
        int across = frame.w + 8;
        int down = frame.h + 8;
        int cols = (across > 0) ? (app->surf->w / across) : 1;

        if (cols < 1) {
            cols = 1;
        }
        frame.x += (j % cols) * across;
        frame.y += (j / cols) * down;
        /* Off the bottom is worse than over the top: a window nobody
         * can see is a window nobody can close. */
        if (frame.y + frame.h > app->surf->h) {
            frame.y = app->surf->h - frame.h;
        }
        if (frame.x + frame.w > app->surf->w) {
            frame.x = app->surf->w - frame.w;
        }
        if (frame.x < 0) {
            frame.x = 0;
        }
        if (frame.y < 0) {
            frame.y = 0;
        }
    }
    ew->slot = slot;
    ew->id = slot->next_id++;
    /* The WINDOW is the tag, not the application: a tag naming the
     * application would find the first window and hand it back, so the
     * second window would be the first one wearing a second name. */
    ew->window = tiku_workspace_open(app->desk, title, frame,
                                          embed_window_draw, ew, ew);
    if (ew->window == NULL) {
        return 0;
    }
    tiku_window_set_size_controls(ew->window, 0, 0);
    app->dirty = 1;
    return ew->id;
}

static int
embed_svc_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    struct desktop_embed *slot = ctx;
    struct embed_win *ew = embed_win_of_id(slot, id);
    uint32_t *copy;

    if (ew == NULL || px == NULL || w < 1 || h < 1) {
        return -1;
    }
    copy = realloc(ew->frame, 4u * (size_t)w * (size_t)h);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, px, 4u * (size_t)w * (size_t)h);
    ew->frame = copy;
    ew->fw = w;
    ew->fh = h;
    slot->app->dirty = 1;
    return 0;
}

static void
embed_menu_pick(tiku_window_t *window, int command, void *context)
{
    struct embed_win *ew = context;
    struct desktop_embed *slot = (ew != NULL) ? ew->slot : NULL;

    (void)window;
    /* The window that PUBLISHED the row is the window the pick belongs
     * to; a host that said "1" here told every application with two
     * windows that both their menus were the first one's. */
    if (slot != NULL && slot->running && slot->descriptor->pick != NULL &&
        slot->descriptor->pick(slot->state, ew->id, command)) {
        embed_stop(slot);
    }
}

static int
embed_svc_menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    struct desktop_embed *slot = ctx;
    struct embed_win *ew = embed_win_of_id(slot, id);

    if (ew != NULL) {
        tiku_window_publish_menus(ew->window, set, embed_menu_pick, ew);
    }
    return 0;
}

/**
 * @brief The ANNOUNCE and PANEL roles: centred, upper third, fixed size.
 *
 * The application says what KIND of window it opened; where such a
 * window goes on THIS screen is the host's guideline, which is exactly
 * why the contract carries a role and not a coordinate.
 */
static int
embed_svc_place(void *ctx, uint32_t id, int role)
{
    struct desktop_embed *slot = ctx;
    desktop_t *app = slot->app;
    struct embed_win *ew = embed_win_of_id(slot, id);
    int plain = (role == TIKU_APP_PLACE_ANNOUNCE);

    if (ew == NULL || (role != TIKU_APP_PLACE_ANNOUNCE &&
                       role != TIKU_APP_PLACE_PANEL)) {
        return -1;
    }
    if (plain) {
        /* The frame was cut for a tab it will not wear: taken in, so the
         * hairline hugs the content exactly.  A PANEL wears the tab the
         * frame was cut for, so its frame stands as opened. */
        ew->window->frame.w -= 2 * TIKU_WINDOW_BORDER - 2;
        ew->window->frame.h -= tiku_window_tab_h() +
                               2 * TIKU_WINDOW_BORDER - 2;
    }
    ew->window->frame.x = (app->surf->w - ew->window->frame.w) / 2;
    /* The UPPER part of the screen, as the guideline places a panel
     * that announces something. */
    ew->window->frame.y = (app->surf->h - ew->window->frame.h) / 3;
    tiku_window_set_size_controls(ew->window, 0, 0);
    /* An announcement wears no tab and no close; a panel keeps both,
     * because the person who opened it is the one who puts it away. */
    tiku_window_set_plain(ew->window, plain);
    app->dirty = 1;
    return 0;
}

/**
 * @brief The window's content grew or shrank; the corner stays put.
 *
 * A window that re-placed itself on a refit would jump under the
 * pointer -- the About takes a new size when the face changes, and only
 * the size.
 */
static int
embed_svc_resize(void *ctx, uint32_t id, int w, int h)
{
    struct desktop_embed *slot = ctx;
    desktop_t *app = slot->app;
    struct embed_win *ew = embed_win_of_id(slot, id);

    if (ew == NULL || w <= 0 || h <= 0) {
        return -1;
    }
    ew->window->frame.w = w + 2 * TIKU_WINDOW_BORDER;
    ew->window->frame.h = h + tiku_window_tab_h() +
                          2 * TIKU_WINDOW_BORDER;
    app->dirty = 1;
    return 0;
}

/**
 * @brief An application asks for the file panel; the shell puts up ITS
 *        panel and answers later.
 *
 * One panel, one question -- the rule the shell already keeps for its
 * own five askers.  A second ask while one is up is refused rather than
 * queued: two answers to one panel is what the rule exists to prevent,
 * and an application told no can ask again.
 */
static int
embed_svc_pick(void *ctx, uint32_t id, int mode, const char *start,
               const char *name)
{
    struct desktop_embed *slot = ctx;
    desktop_t *app = slot->app;

    if (app->pending_pick.kind != 0) {
        return 0;
    }
    app->pending_pick.kind = 1;
    app->pending_pick.slot = slot;
    app->pending_pick.window = id;
    if (mode == TIKU_APP_PICK_SAVE) {
        tiku_trk_panel_window_open_save(&app->file_panel,
            &app->pending_pick, TIKU_TRK_CMD_APP_PICK, start, name);
    } else {
        /* On the OPEN road the name is a FILTER: the one file name the
         * asker can accept, every other file greyed out of the offer. */
        tiku_trk_panel_window_open_file(&app->file_panel,
            &app->pending_pick, TIKU_TRK_CMD_APP_PICK, start, name);
    }
    app->dirty = 1;
    return 1;
}

/**
 * @brief Let go of one window of an application.
 *
 * @param tell nonzero when the RUNTIME took it and the application has
 *        yet to hear so; zero when the application asked.
 */
void
embed_drop_window(struct embed_win *ew, int tell)
{
    struct desktop_embed *slot = (ew != NULL) ? ew->slot : NULL;
    tiku_window_t *window;

    if (slot == NULL || ew->window == NULL) {
        return;
    }
    window = ew->window;
    ew->window = NULL;          /* before the close, so nothing re-enters */
    free(ew->frame);
    ew->frame = NULL;
    ew->fw = 0;
    ew->fh = 0;
    if (slot->app->embed_press == ew) {
        slot->app->embed_press = NULL;
    }
    tiku_workspace_close(slot->app->desk, window);
    if (tell && slot->running && slot->descriptor->closed != NULL) {
        slot->descriptor->closed(slot->state, ew->id);
    }
    slot->app->dirty = 1;
    /* An application with no windows left has nothing to be: the last
     * window closing is the application ending, which is exactly what
     * closing the ONE window always meant. */
    if (embed_window_count(slot) == 0) {
        embed_stop(slot);
    }
}

/** @brief From now on the person may resize this window. */
static int
embed_svc_sizeable(void *ctx, uint32_t id)
{
    struct desktop_embed *slot = ctx;
    struct embed_win *ew = embed_win_of_id(slot, id);

    if (ew == NULL || ew->window == NULL) {
        return -1;
    }
    tiku_window_set_size_controls(ew->window, 1, 0);
    return 0;
}

/** @brief The desk's clipboard, from an application in this process. */
static int
embed_svc_clip_set(void *ctx, const char *text)
{
    (void)ctx;
    tiku_clipboard_set(text);
    /* The sessions hear about it from the main loop's stamp watch --
     * the same road a field's copy takes, so there is one road. */
    return 1;
}

static int
embed_svc_clip_get(void *ctx, char *out, size_t max)
{
    (void)ctx;
    if (out == NULL || max == 0u) {
        return 0;
    }
    snprintf(out, max, "%s", tiku_clipboard_text());
    return (int)strlen(out);
}

/** @brief Bring one of this application's windows to the front. */
static int
embed_svc_raise(void *ctx, uint32_t id)
{
    struct desktop_embed *slot = ctx;
    struct embed_win *ew = embed_win_of_id(slot, id);

    if (ew == NULL) {
        return -1;
    }
    tiku_workspace_activate(slot->app->desk, ew->window);
    slot->app->dirty = 1;
    return 0;
}

static void
embed_svc_close(void *ctx, uint32_t id)
{
    struct desktop_embed *slot = ctx;

    /* The application asked, so it already knows: told again it would
     * be hearing about a window it has just let go of. */
    embed_drop_window(embed_win_of_id(slot, id), 0);
}

void
embed_stop(struct desktop_embed *slot)
{
    desktop_t *app = slot->app;
    int j;

    if (!slot->running) {
        return;
    }
    slot->running = 0;
    if (slot->descriptor->stop != NULL) {
        slot->descriptor->stop(slot->state);
    }
    slot->state = NULL;
    for (j = 0; j < TIKU_TRK_EMBED_WINDOWS; j++) {
        struct embed_win *ew = &slot->win[j];

        if (ew->window != NULL) {
            tiku_workspace_close(app->desk, ew->window);
            ew->window = NULL;
        }
        free(ew->frame);
        ew->frame = NULL;
        if (app->embed_press == ew) {
            app->embed_press = NULL;
        }
    }
    app->dirty = 1;
}

/**
 * @brief Hand @p ev to the application, saying which window it landed in.
 *
 * An application built before windows could be told apart hears the
 * older call and is none the wiser, which is what lets one host carry
 * both.
 *
 * @return nonzero when the application says it is done.
 */
int
embed_deliver(struct embed_win *ew, const tiku_event_t *ev)
{
    struct desktop_embed *slot = (ew != NULL) ? ew->slot : NULL;

    if (slot == NULL || !slot->running) {
        return 0;
    }
    return tiku_app_deliver_event(slot->descriptor, slot->state,
                                       ew->id, ev);
}

/**
 * @brief Host @p descriptor in this process, over workspace services.
 *
 * @return nonzero when it started and took a slot.
 */
int
embed_start(desktop_t *app, const tiku_app_descriptor_t *descriptor)
{
    struct desktop_embed *slot = NULL;
    int i;

    for (i = 0; i < TIKU_TRK_EMBED_MAX; i++) {
        if (!app->embeds[i].running) {
            slot = &app->embeds[i];
            break;
        }
    }
    if (slot == NULL || descriptor == NULL || descriptor->start == NULL) {
        return 0;
    }
    memset(slot, 0, sizeof *slot);
    slot->app = app;
    slot->descriptor = descriptor;
    slot->next_id = 1;
    /* The services live in the SLOT: a descriptor keeps the pointer for
     * its lifetime, and one static set would give every application the
     * last one's window. */
    slot->services.ctx = slot;
    slot->services.open = embed_svc_open;
    slot->services.frame = embed_svc_frame;
    slot->services.menus = embed_svc_menus;
    slot->services.close = embed_svc_close;
    slot->services.place = embed_svc_place;
    slot->services.resize = embed_svc_resize;
    slot->services.pick = embed_svc_pick;
    slot->services.raise_window = embed_svc_raise;
    slot->services.sizeable = embed_svc_sizeable;
    slot->services.clip_set = embed_svc_clip_set;
    slot->services.clip_get = embed_svc_clip_get;
    if (descriptor->start(&slot->state, &slot->services) != 0) {
        return 0;
    }
    slot->running = 1;
    return 1;
}

/**
 * @brief Start @p descriptor, or bring its running window forward.
 *
 * By the descriptor's ID, which is what "the same application" means
 * across every way of reaching it: the second menu pick raises the
 * window the first one opened, rather than opening a twin.
 */
int
embed_open_or_raise(desktop_t *app, const tiku_app_descriptor_t *descriptor)
{
    int i;

    if (descriptor == NULL || descriptor->id == NULL) {
        return 0;
    }
    for (i = 0; i < TIKU_TRK_EMBED_MAX; i++) {
        struct desktop_embed *slot = &app->embeds[i];

        if (slot->running && slot->descriptor->id != NULL &&
            strcmp(slot->descriptor->id, descriptor->id) == 0) {
            int j;

            /* The first window it still holds: an application asked for
             * again is being asked to come forward, and it is the one
             * that can say which of its windows means "the application"
             * -- so the host raises the earliest rather than guessing. */
            for (j = 0; j < TIKU_TRK_EMBED_WINDOWS; j++) {
                if (slot->win[j].window != NULL) {
                    tiku_workspace_activate(app->desk,
                                                 slot->win[j].window);
                    break;
                }
            }
            app->dirty = 1;
            return 1;
        }
    }
    return embed_start(app, descriptor);
}
