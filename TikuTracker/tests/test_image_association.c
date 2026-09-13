/* Image defaults, explicit preferences and native launch dispatch.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../src/ui/commands/tiku_trk_launch_controller.c"

static int opens;
static char opened[512];

/** @brief Record the literal filename delivered to the native controller. */
static int image_opened(void *ctx, const char *path)
{
    assert(ctx == &opens);
    opens++;
    snprintf(opened, sizeof opened, "%s", path);
    return 1;
}

/** @brief The built-in default is visible, routable and user-overridable. */
int main(void)
{
    tiku_trk_apps_t apps;
    tiku_trk_launch_clear(&apps);
    int at = tiku_trk_launch_add_image(&apps);
    assert(at == 0 && tiku_trk_launch_add_image(&apps) == at && apps.count == 1);
    assert(!strcmp(apps.app[at].name, "TikuImage"));
    tiku_store_t *store = tiku_store_memory_open();
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    const char *types[] = {"image/png", "image/jpeg", "image/bmp",
                          "image/x-ms-bmp"};
    for (int i = 0; i < 4; i++) {
        assert(tiku_trk_launch_candidates(&apps, &types[i], 1, store,
                                          candidates, TIKU_TRK_APP_MAX) == 1);
        assert(candidates[0].app->is_image);
    }
    assert(tiku_trk_launch_relation(&apps.app[0], "image/gif") ==
           TIKU_TRK_REL_NONE);
    tiku_trk_launch_add(&apps, "Another image viewer", "/bin/true", "image/");
    tiku_trk_launch_add(&apps, "A PNG viewer", "/bin/true", "image/png");
    assert(tiku_trk_launch_candidates(&apps, types, 1, store, candidates,
                                      TIKU_TRK_APP_MAX) == 3);
    assert(candidates[0].app->is_image);
    tiku_trk_launch_set_preferred(store, types[0], "Another image viewer");
    tiku_trk_launch_candidates(&apps, types, 1, store, candidates,
                               TIKU_TRK_APP_MAX);
    assert(!strcmp(candidates[0].app->name, "Another image viewer"));
    tiku_trk_launch_set_preferred(store, types[0], "");
    char status[256];
    tiku_trk_launch_controller_t controller;
    tiku_trk_launch_controller_init(&controller, &apps, store, status,
                                    sizeof status, NULL, &opens);
    tiku_trk_launch_controller_image(&controller, image_opened);
    const char *paths[] = {"/tmp/sensor image;literal.PNG", "/tmp/next.bmp"};
    assert(tiku_trk_launch_run(&apps.app[0], paths, 1) ==
           TIKU_TRK_LAUNCH_IS_IMAGE);
    assert(tiku_trk_launch_controller_run(&controller, &apps.app[0], paths, 1));
    assert(opens == 1 && !strcmp(opened, paths[0]));
    assert(!tiku_trk_launch_controller_run(&controller, &apps.app[0], paths, 2));
    assert(opens == 1 && strstr(status, "one image"));
    tiku_trk_launch_controller_image(&controller, NULL);
    assert(!tiku_trk_launch_controller_run(&controller, &apps.app[0], paths, 1));
    tiku_store_free(store);
    puts("PASS: image associations, preferred handlers and native dispatch");
    return 0;
}
