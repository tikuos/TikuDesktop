/* A frame that asks for the next one gets it: an animation drawn by the
 * desktop keeps drawing until it is done, with no other event to help it.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <string.h>
#include <time.h>
#include "../src/shell/desktop/tiku_trk_desktop_draw.c"
#include "../src/deskbar/bar/tiku_trk_deskbar_private.h"

/** @brief Main's file is not linked; nothing here has folders to sync. */
void sync_folder_preferences(desktop_t *app) { (void)app; }

int main(void)
{
    desktop_t *app = calloc(1, sizeof *app); assert(app);
    app->surf = tiku_surface_new(640, 480, TIKU_C_PANEL); assert(app->surf);
    app->desk = tiku_workspace_new(app->surf); assert(app->desk);
    app->deskbar = tiku_trk_deskbar_new(NULL); assert(app->deskbar);
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
        assert(!app->dirty);
    }
    tiku_trk_deskbar_free(app->deskbar);
    tiku_workspace_free(app->desk); tiku_surface_free(app->surf); free(app);
    puts("PASS: a frame in the middle of an animation asks for the next one, "
         "and a hover for its own");
    return 0;
}
