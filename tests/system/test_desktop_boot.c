/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_desktop_boot.c - what the desktop's boot stages leave set up.
 *
 * A scripted run's boot stages up to the applications are called in their
 * order, in a home made for the test that holds a person's icon named
 * terminal.  Then the Open With window's store, the person's icon, the
 * launch of TikuTracker's own entry, and the window sizes after a resize
 * are checked.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/desktop/tiku_trk_desktop_lifecycle.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/** @brief Main's file is not linked; nothing here has folders to sync. */
void sync_folder_preferences(desktop_t *app) { (void)app; }

/** @brief Write @p icon's bytes to @p path.  @return 1 when written. */
static int
write_icon(const char *path, const tiku_icon_t *icon)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (f == NULL || icon == NULL) {
        if (f != NULL) {
            (void)fclose(f);
        }
        return 0;
    }
    ok = fwrite(icon->data, 1u, icon->len, f) == icon->len;
    return fclose(f) == 0 && ok;
}

int
main(void)
{
    static desktop_t app;
    static desktop_loop_t lp;
    static desktop_options_t o;
    char home[] = "/tmp/trk_boot_XXXXXX";
    char path[TIKU_PATH_MAX];
    const tiku_icon_t *folder, *terminal;
    const tiku_trk_app_t *self = NULL;
    int i;

    if (mkdtemp(home) == NULL) {
        return 1;
    }
    (void)setenv("HOME", home, 1);
    (void)unsetenv("TIKU_ICONS");
    (void)unsetenv("TIKU_SPLASH");
    (void)unsetenv("TIKU_MOUNT");
    (void)unsetenv("TIKU_EMBED_DEMO");
    snprintf(path, sizeof path, "%s/.config/tracker/icons", home);
    (void)tiku_state_mkparents(path);
    (void)mkdir(path, 0755);
    /* The person's terminal icon is the baked folder art. */
    folder = tiku_icons_find("folder");
    snprintf(path, sizeof path, "%s/.config/tracker/icons/terminal.hvif",
             home);
    if (!write_icon(path, folder)) {
        return 1;
    }
    o.script_path = "/dev/null";
    o.safe_mode = 1;
    o.mute = 1;
    o.baud = 115200;

    printf("the boot stages up to the applications\n");
    check(boot_namespace(&app, &o, &lp) < 0 &&
          boot_deskbar(&app, &o, &lp) < 0 &&
          boot_settings(&app, &o, &lp) < 0 &&
          boot_fonts(&app, &o, &lp) < 0 &&
          boot_applications(&app, &o, &lp) < 0,
          "every stage goes on");

    printf("\nthe Open With window's store\n");
    check(app.store != NULL &&
          app.open_with_window.store == app.store,
          "the Open With window keeps the session store, so make "
          "preferred is saved");

    printf("\nthe person's icon folder\n");
    terminal = tiku_icons_find("terminal");
    check(terminal != NULL && folder != NULL &&
          terminal->len == folder->len &&
          memcmp(terminal->data, folder->data, folder->len) == 0,
          "terminal.hvif in ~/.config/tracker/icons is the terminal icon "
          "drawn, not TikuOS's");

    printf("\nTikuTracker's own entry\n");
    for (i = 0; i < app.apps.count; i++) {
        if (app.apps.app[i].is_tracker) {
            self = &app.apps.app[i];
        }
    }
    check(self != NULL &&
          tiku_trk_launch_run(self, NULL, 0) == TIKU_TRK_LAUNCH_IS_TRACKER,
          "launching TikuTracker's entry is taken by this desktop, not "
          "refused for want of an application");

    printf("\na resize\n");
    check(resize_desktop(&app, 640, 400) == 1, "the surface takes 640x400");
    check(app.open_with_window.screen_w == 640 &&
          app.open_with_window.screen_h == 400,
          "the Open With window is told the new size");
    check(app.fonts_window.screen_w == 640 &&
          app.fonts_window.screen_h == 400,
          "the Fonts window is told the new size");
    check(app.sounds_window.screen_w == 640 &&
          app.sounds_window.screen_h == 400,
          "the Sounds window is told the new size");
    check(app.desktop_prefs_window.screen_w == 640 &&
          app.desktop_prefs_window.screen_h == 400,
          "the Desktop preferences window is told the new size");
    tiku_trk_fonts_window_open(&app.fonts_window);
    {
        tiku_window_t *w = tiku_trk_fonts_window_handle(&app.fonts_window);

        check(w != NULL && w->frame.x >= 0 &&
              w->frame.x + w->frame.w <= 640,
              "the Fonts window opens inside the new width");
    }

    snprintf(path, sizeof path, "rm -rf '%s'", home);
    if (system(path) != 0) {
        /* A leftover folder under /tmp fails nothing. */
    }
    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
