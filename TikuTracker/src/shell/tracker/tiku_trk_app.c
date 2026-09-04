/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app.c - the window: a real Tracker list view over a real backend.
 *
 * One X11 window drawn entirely by us, wired to the pose engine: the title bar
 * sorts, rows select with Tracker's own rules, the keyboard navigates, and a
 * device's values update live because the loop pumps the backend between
 * events instead of blocking on X.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/** @brief Real art for menu items, dimmed for a disabled one (IV-054). */
int
browser_paint_menu_icon(tiku_surface_t *s, const char *icon, int x, int y,
                int size, int dimmed)
{
    return dimmed
        ? tiku_icon_paint_dim(s, icon, x, y, size, 0.55f,
                                  TIKU_C_PANEL)
        : tiku_icon_paint(s, icon, x, y, size);
}

/*---------------------------------------------------------------------------*/
/* the run                                                                   */
/*---------------------------------------------------------------------------*/

static int
tracker_run(int argc, char **argv)
{
    app_t a;
    browser_options_t opt;
    browser_loop_t lp;
    int code;

    memset(&a, 0, sizeof a);
    memset(&opt, 0, sizeof opt);
    opt.baud = 115200;
    memset(&lp, 0, sizeof lp);
    (void)tiku_strings_init(NULL, NULL);
    tiku_trk_cwmenu_init(&a.menus);
    a.bar_open = -1;
    a.show_nav = 1;
    /* Zero is a valid row, so the "nothing to select" state has to be set
     * rather than zeroed: left at 0 it would re-select the first row after
     * every operation (FS-050). */
    a.select_next = -1;
    code = browser_parse_args(argc, argv, &opt);
    if (code >= 0) {
        return code;
    }

    code = browser_boot(&a, &opt, &lp);
    if (code >= 0) {
        return code;
    }

    while (a.running) {
        browser_sync_navigator_preference(&a, 0);
        /* Events first, then the device: the window stays responsive while a
         * slow link is being read, which is the whole point of pumping here
         * rather than blocking on either one. */
        browser_turn_begin(&a, &lp);
        browser_turn_end(&a, &opt, &lp);
    }

    return browser_teardown(&a, &lp);
}

const tiku_app_descriptor_t tiku_trk_application = {
    .id = "org.tikuos.tracker.browser",
    .name = "TikuTracker",
    .run = tracker_run
};
