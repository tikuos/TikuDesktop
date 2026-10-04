/* Recordings route to TikuSDR: the type a host disk gives a .sdrlog in
 * either case of name, its manifest's type, its File Associations row, a
 * person's own choice, and a recording handed to the app.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <unistd.h>
#include "tiku_manifest.h"
#include "tiku_trk_associations.h"
#include "../../system/shared/ui/commands/tiku_trk_launch_controller.c"

static int opens;
static char opened[512];

/** @brief Record the recording the native controller was handed. */
static int recording_opened(void *ctx, const tiku_trk_app_t *app,
                            const char *path)
{
    assert(ctx == &opens && !strcmp(app->name, "TikuSDR"));
    opens++;
    snprintf(opened, sizeof opened, "%s", path);
    return 1;
}

/** @brief Offered to TikuSDR alone, and listed by what it is. */
static void offered(tiku_trk_apps_t *apps, tiku_store_t *store,
                    const char *type)
{
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    tiku_trk_associations_t rows;
    int found = 0;

    assert(tiku_trk_launch_candidates(apps, &type, 1, store, candidates,
                                      TIKU_TRK_APP_MAX) == 1);
    assert(candidates[0].app == &apps->app[0]);
    tiku_trk_associations_load(&rows, apps, NULL, store);
    for (int i = 0; i < rows.count; i++)
        if (!strcmp(rows.row[i].type, type)) {
            assert(!strcmp(rows.row[i].label, "TikuSDR recording (.sdrlog)"));
            found = 1;
        }
    assert(found);
}

/** @brief Typed, offered, listed, overridable, and handed a recording. */
int main(void)
{
    const char *type = "application/x-tikusdr";
    char dir[] = "/tmp/tiku-sdr-association-XXXXXX", path[512], status[256];
    tiku_trk_apps_t apps;
    tiku_manifest_t m;
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    tiku_trk_launch_controller_t controller;
    tiku_model_t model;
    FILE *f;

    tiku_trk_launch_clear(&apps);
    assert(tiku_manifest_read("apps/sdr/sdr.app", &m, NULL, 0) == 0);
    assert(!strcmp(m.id, "sdr") && !strcmp(m.name, "TikuSDR"));
    assert(tiku_trk_launch_add_document(&apps, m.name, "/runner",
                                        "/apps/sdr/TikuSDR.so",
                                        m.types) == 0);
    tiku_store_t *store = tiku_store_memory_open();
    tiku_backend_t *backend = tiku_backend_posix_open();
    assert(store && backend);

    /* A host disk has only the name to go on, in whichever case. */
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/Morning Scan.SDRLOG", dir);
    assert((f = fopen(path, "w")) && !fclose(f));
    assert(!backend->ops->stat(backend, path, &model));
    assert(!strcmp(model.type, type));
    offered(&apps, store, type);
    assert(tiku_trk_launch_relation(&apps.app[0], "application/pdf") ==
           TIKU_TRK_REL_NONE);

    /* A person's choice of another tool wins, and can be taken back. */
    tiku_trk_launch_add(&apps, "Spectrum viewer", "/bin/true", type);
    tiku_trk_launch_set_preferred(store, type, "Spectrum viewer");
    assert(tiku_trk_launch_candidates(&apps, &type, 1, store, candidates,
                                      TIKU_TRK_APP_MAX) == 2);
    assert(!strcmp(candidates[0].app->name, "Spectrum viewer"));
    tiku_trk_launch_set_preferred(store, type, "");

    tiku_trk_launch_controller_init(&controller, &apps, store, status,
                                    sizeof status, NULL, &opens);
    controller.open_document = recording_opened;
    assert(tiku_trk_launch_controller_handler(&controller, &model) ==
           &apps.app[0]);
    {
        const char *paths[] = {path};
        assert(tiku_trk_launch_controller_run(&controller, &apps.app[0],
                                              paths, 1));
    }
    assert(opens == 1 && !strcmp(opened, path));
    assert(!unlink(path) && !rmdir(dir));
    tiku_backend_close(backend);
    tiku_store_free(store);
    puts("PASS: .sdrlog recordings are typed by name, open in TikuSDR, "
         "listed by kind, overridable");
    return 0;
}
