/* A frame that asks for the next one gets it: an animation drawn by the
 * desktop keeps drawing until it is done, with no other event to help it.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tiku_work.h"
#include "../../system/desktop/tiku_trk_desktop_draw.c"
#include "../../system/deskbar/bar/tiku_trk_deskbar_private.h"

/** @brief Main's file is not linked; nothing here has folders to sync. */
void sync_folder_preferences(desktop_t *app) { (void)app; }

/** @brief How many pixels of @p at on @p s differ from @p before. */
static int changed_pixels(const tiku_surface_t *s,
                          const tiku_surface_t *before, tiku_rect_t at)
{
    int n = 0;
    for (int y = 0; y < at.h; y++)
        for (int x = 0; x < at.w; x++)
            n += tiku_peek(s, at.x + x, at.y + y) !=
                 tiku_peek(before, x, y);
    return n;
}

/** @brief Press and release the first button at @p x, @p y. */
static void preference_click(tiku_window_t *window, int x, int y)
{
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN,
                       .button = 1, .x = x, .y = y};
    assert(tiku_window_send(window, &ev));
    ev.type = TIKU_EVENT_POINTER_UP;
    assert(tiku_window_send(window, &ev));
}

/** @brief Click the center of the control published under an exact native label. */
static void preference_control(tiku_workspace_t *workspace,tiku_window_t *window,const char *label)
{
    char facts[16384],needle[256];tiku_rect_t rectangle;const char *found;
    assert(tiku_workspace_controls(workspace,window,facts,sizeof facts));
    snprintf(needle,sizeof needle,"\"%s\"\t",label);found=strstr(facts,needle);assert(found);
    assert(sscanf(found+strlen(needle),"%d %d %d %d",&rectangle.x,&rectangle.y,&rectangle.w,&rectangle.h)==4);
    preference_click(window,rectangle.x+rectangle.w/2,rectangle.y+rectangle.h/2);
}

/** @brief Reduce motion: its tick box, Defaults, Revert and the file. */
static void motion_preference(desktop_t *app)
{
    tiku_trk_desktop_prefs_window_t controller;
    char dir[] = "/tmp/tiku-motion-prefs-XXXXXX", path[256];
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/TrackerSettings", dir);
    tiku_trk_prefs_set(prefs_model(app), "ReduceMotion", 0);
    tiku_trk_desktop_prefs_window_init(&controller, app->desk, NULL, NULL,
                                      1000, 680, &app->dirty);
    tiku_trk_desktop_prefs_window_bind_settings(&controller,
                                                prefs_model(app), path);
    /* Don't scan the user's Desktop or wallpapers in this unit fixture. */
    snprintf(controller.node, sizeof controller.node, "%s", dir);
    snprintf(controller.pictures_dir, sizeof controller.pictures_dir,
             "%s", dir);
    for (int size = 12; size <= 16; size += 4) {
        tiku_font_set_size(size);
        tiku_trk_desktop_prefs_window_open(&controller);
        assert(controller.window);
        tiku_rect_t frame = controller.window->frame;
        assert(frame.y >= 0 && frame.y + frame.h <= 680);
        assert(!controller.model.value.reduce_motion);
        preference_control(app->desk,controller.window,tiku_str("prefs.desktop.reduce_motion"));
        assert(controller.model.value.reduce_motion &&
               tiku_trk_prefs_get(prefs_model(app), "ReduceMotion"));
        tiku_trk_prefs_t saved;
        tiku_trk_prefs_init(&saved);
        assert(!tiku_trk_prefs_load(&saved, path));
        assert(tiku_trk_prefs_get(&saved, "ReduceMotion"));
        tiku_trk_prefs_free(&saved);
        /* A click that leaves Reduce motion as it was writes no settings:
         * the icon size's plus steps the size and nothing else. */
        assert(!unlink(path));
        preference_control(app->desk,controller.window,"+");
        assert(controller.model.value.icon_size > 32);
        assert(access(path, F_OK) != 0);
        /* Revert is the second button; actions happen on release. */
        preference_control(app->desk,controller.window,tiku_str("button.revert"));
        assert(!tiku_trk_prefs_get(prefs_model(app), "ReduceMotion"));
        assert(!access(path, F_OK));
        preference_control(app->desk,controller.window,tiku_str("prefs.desktop.reduce_motion"));
        tiku_trk_desktop_prefs_window_close(&controller);
        tiku_trk_desktop_prefs_window_open(&controller);
        /* Opening again reads the setting back. */
        assert(controller.model.value.reduce_motion);
        preference_control(app->desk,controller.window,tiku_str("button.defaults"));
        assert(!controller.model.value.reduce_motion &&
               !tiku_trk_prefs_get(prefs_model(app), "ReduceMotion"));
        tiku_trk_desktop_prefs_window_close(&controller);
    }
    tiku_font_set_size(12);
    tiku_trk_desktop_prefs_window_destroy(&controller);
    assert(!unlink(path));
    assert(!rmdir(dir));
}

int main(void)
{
    desktop_t *app = calloc(1, sizeof *app); assert(app);
    app->surf = tiku_surface_new(640, 480, TIKU_C_PANEL); assert(app->surf);
    app->desk = tiku_workspace_new(app->surf); assert(app->desk);
    app->deskbar = tiku_trk_deskbar_new(NULL); assert(app->deskbar);
    tiku_trk_prefs_init(prefs_model(app));
    /* The bar answers through the responder list, as on the real desk. */
    desktop_responders_attach(app);
    app->remote.fd = -1;
    /* A host, so the workspace has a clock: draw_all() hands it the frame's
     * start only when one is open, and without a clock no ghost is taken. */
    app->host = (tiku_host_t *)app;
    /* No Info window is hovered, as the desktop's own setup says. */
    app->info_windows.drop_hot = -1;
    app->info_windows.hover_slot = -1;
    app->info_windows.hover_field = -1;
    tiku_window_t *w = tiku_workspace_open(app->desk, "Going",
        (tiku_rect_t){120, 100, 240, 160}, NULL, NULL, NULL);
    assert(w);
    app->dirty = 1; draw_all(app);
    assert(!app->dirty && !tiku_workspace_animating(app->desk));
    tiku_workspace_close(app->desk, w);
    app->dirty = 1; draw_all(app);
    /* The ghost is on its way, and this frame must have asked for the next:
     * a desktop that clears the request after drawing leaves the ghost
     * standing at this frame until some other event dirties the screen. */
    assert(tiku_workspace_animating(app->desk));
    /* And asked for a frame bounded to the ghost's way, not the desk. */
    assert(app->dirty == 2);
    assert(app->damage.x <= 120 && app->damage.y <= 100 &&
           app->damage.x + app->damage.w >= 360 &&
           app->damage.y + app->damage.h >= 260 &&
           app->damage.w < app->surf->w);
    struct timespec rest = {0, 200 * 1000000L};
    nanosleep(&rest, NULL);
    draw_all(app);
    assert(!tiku_workspace_animating(app->desk) && !app->dirty);
    {
        /* A hover across the Deskbar's leaf menu changes a row of it: the
         * frame it asks for is that menu's, not the whole desk's. */
        desktop_loop_t lp;
        tiku_event_t mv = { .type = TIKU_EVENT_POINTER_MOVE };
        int h = tiku_font_plain()->height + 6;
        tiku_rect_t menu, damage;

        memset(&lp, 0, sizeof lp);
        tiku_trk_deskbar_draw(app->deskbar, app->surf);
        tiku_trk_deskbar_open_menu(app->deskbar);
        menu = db_leaf_menu_rect(app->deskbar, h, app->deskbar->leaf_count);
        app->dirty = 0;
        mv.x = menu.x + menu.w / 2; mv.y = menu.y + 2 + h / 2;
        desktop_pointer_move(app, &mv, &lp);
        assert(app->dirty == 2);
        damage = app->damage;
        assert(damage.w > 0 && damage.w < app->surf->w / 2);
        assert(damage.x <= menu.x && damage.x + damage.w >= menu.x + menu.w);
        draw_all(app);
        assert(app->dirty == 2); /* the menu's entrance owns a bounded frame */
        assert(app->damage.w < app->surf->w);
        app->deskbar->menu_motion.since -= TIKU_MOTION_MENU_US;
        draw_all(app);
        assert(!app->dirty);
        /* A driver hears the row by its label, and hears nothing once
         * the pointer has left; the other chrome nouns answer too. */
        char said[256];
        assert(conduct_text(app, "hot", said, sizeof said));
        assert(strcmp(said, app->deskbar->leaf[0].label) == 0);
        assert(conduct_text(app, "menu-bottom", said, sizeof said) &&
               atoi(said) == menu.y + menu.h);
        mv.x = 5; mv.y = app->surf->h - 5;
        desktop_pointer_move(app, &mv, &lp);
        assert(conduct_text(app, "hot", said, sizeof said) && said[0] == 0);
        assert(conduct_text(app, "tip", said, sizeof said) && said[0] == 0);
        assert(conduct_text(app, "strip", said, sizeof said) &&
               strcmp(said, "off") == 0);
        assert(conduct_text(app, "minimized", said, sizeof said) &&
               said[0] == 0);
    }
    {
        /* The outline is drawn while it runs; its end leaves the desk as
         * it was and asks for no further frame. */
        tiku_rect_t icon = {100, 100, 32, 32};
        tiku_rect_t reach = tiku_motion_outline_bounds(icon);
        tiku_surface_t *before;

        tiku_trk_deskbar_close_menu(app->deskbar);
        app->dirty = 1; draw_all(app);
        before = tiku_grab(app->surf, reach); assert(before);
        app->activation_rect = icon;
        tiku_motion_start(&app->activation_motion, tiku_trk_now(),
                          TIKU_MOTION_ICON_US);
        desktop_damage(app, reach);
        draw_all(app);
        assert(app->activation_motion.active && app->dirty == 2);
        assert(app->damage.w < 100 && app->damage.h < 100);
        assert(changed_pixels(app->surf, before, reach) > 0);
        app->activation_motion.since -= TIKU_MOTION_ICON_US;
        draw_all(app);
        assert(!app->activation_motion.active && !app->dirty);
        assert(changed_pixels(app->surf, before, reach) == 0);
        tiku_surface_free(before);

        tiku_motion_start(&app->activation_motion, tiku_trk_now(),
                          TIKU_MOTION_ICON_US);
        tiku_trk_deskbar_open_menu(app->deskbar);
        app->dirty = 1; draw_all(app);
        assert(app->dirty);
        tiku_trk_prefs_set(prefs_model(app), "ReduceMotion", 1);
        app->dirty = 1; draw_all(app);
        assert(!app->activation_motion.active && !app->dirty);
        assert(!tiku_workspace_animating(app->desk));
        assert(!tiku_trk_deskbar_motion_damage(app->deskbar).w);
        w = tiku_workspace_open(app->desk, "Still",
                                (tiku_rect_t){30, 30, 200, 160},
                                NULL, NULL, NULL);
        assert(w);
        app->dirty = 1; draw_all(app);
        assert(!tiku_workspace_animating(app->desk) && !app->dirty);
        tiku_workspace_close(app->desk, w);
        app->dirty = 1; draw_all(app);
        assert(!tiku_workspace_animating(app->desk) && !app->dirty);
    }
    {
        /* Ordinary popups share the same bounded fade and preference. */
        tiku_menu_t menu = {0};
        tiku_rect_t frame = {80, 70, 180, 48};
        assert(tiku_menu_add(&menu, "Open", 1, 0, 0, 1) >= 0);
        tiku_trk_prefs_set(prefs_model(app), "ReduceMotion", 0);
        tiku_clip_damage(app->surf, NULL);
        app->dirty = 0;
        draw_popup_menu(app, &menu, frame, tiku_trk_now());
        assert(app->dirty == 2 && app->damage.w < app->surf->w);
        app->dirty = 0;
        draw_popup_menu(app, &menu, frame,
                        tiku_trk_now() - TIKU_MOTION_MENU_US);
        assert(!app->dirty);
        tiku_trk_prefs_set(prefs_model(app), "ReduceMotion", 1);
        draw_popup_menu(app, &menu, frame, tiku_trk_now());
        assert(!app->dirty);
    }
    /* The documents TikuTracker hands over, by name in either case. */
    assert(desktop_remote_document("/tmp/notes.txt") &&
           desktop_remote_document("/tmp/README.md") &&
           desktop_remote_document("/tmp/GUIDE.MARKDOWN") &&
           desktop_remote_document("/tmp/index.html") &&
           desktop_remote_document("/tmp/page.HTM") &&
           desktop_remote_document("/tmp/main.bas") &&
           desktop_remote_document("/tmp/demo.tiku"));
    assert(!desktop_remote_document("/tmp/run.sh") &&
           !desktop_remote_document("/tmp/markdown") &&
           !desktop_remote_document(NULL));
    motion_preference(app);
    {
        /* Hide replicants in the leaf menu: the bar flips the setting and
         * the desktop saves it with the Deskbar's other settings. */
        tiku_trk_db_pick_t pick = {.what = TIKU_TRK_DB_TOGGLE_REPLICANTS};
        tiku_trk_deskbar_prefs_t saved;

        app->store = tiku_store_memory_open();
        assert(app->store);
        tiku_trk_deskbar_prefs_init(&app->deskbar_prefs_window.model);
        tiku_trk_fontprefs_init(&app->fonts_window.model);
        app->record_settings=tiku_trk_record_settings_open(app->store,&app->deskbar_prefs_window.model,&app->fonts_window.model,NULL,NULL);
        assert(app->record_settings);
        tiku_trk_deskbar_set_show_replicants(app->deskbar, 0);
        deskbar_pick_dispatch(app, &pick, 0, 0);
        for (int i=0;i<5000 && tiku_work_busy();i++) {tiku_work_collect();usleep(1000);}
        assert(!tiku_work_busy());
        assert(!app->deskbar_prefs_window.model.value.show_replicants);
        tiku_trk_deskbar_prefs_init(&saved);
        assert(!tiku_trk_deskbar_prefs_load(&saved, app->store) &&
               !saved.value.show_replicants);
        tiku_trk_record_settings_close(app->record_settings);app->record_settings=NULL;
        tiku_store_free(app->store);
        app->store = NULL;
    }
    tiku_trk_prefs_free(prefs_model(app));
    tiku_trk_deskbar_free(app->deskbar);
    tiku_workspace_free(app->desk); tiku_surface_free(app->surf); free(app);
    puts("PASS: a frame in the middle of an animation asks for the next one, "
         "and a hover for its own");
    return 0;
}
