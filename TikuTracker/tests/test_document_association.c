/* Document defaults, MIME detection, overrides, and literal runner arguments.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../src/ui/commands/tiku_trk_launch_controller.c"

static int opens;
static char last_path[TIKU_PATH_MAX];

/** @brief Record each file delivered by Open With, without launching a GUI. */
static int document_open(void *ctx, const tiku_trk_app_t *app, const char *path)
{
    assert(ctx == &opens && app->module[0]);
    opens++;
    snprintf(last_path, sizeof last_path, "%s", path);
    return 1;
}

/** @brief Child mode captures argv; parent mode checks the complete registry. */
int main(int argc, char **argv)
{
    if (argc == 3) {
        FILE *f = fopen(argv[1], "w");
        assert(f && fputs(argv[2], f) >= 0 && !fclose(f));
        return 0;
    }
    char dir[] = "/tmp/tiku-document-association-XXXXXX", path[512];
    assert(mkdtemp(dir));
    tiku_trk_apps_t apps;
    tiku_trk_launch_clear(&apps);
    int basic = tiku_trk_launch_add_document(&apps, "TikuBasic", "/tiku-run",
        "/apps/programming/TikuBasic.so", "text/x-basic,application/x-tiku-project");
    int editor = tiku_trk_launch_add_document(&apps, "Editor", "/tiku-run",
        "/apps/editor.so", "text/plain");
    assert(basic == 0 && editor == 1);
    assert(tiku_trk_launch_add_document(&apps, "Editor", "/tiku-run",
        "/apps/editor.so", "text/plain") == editor && apps.count == 2);
    tiku_trk_launch_add(&apps, "Alternative", "/bin/true", "text/x-basic,text/plain");
    tiku_store_t *store = tiku_store_memory_open();
    tiku_backend_t *backend = tiku_backend_posix_open();
    assert(store && backend);
    char status[256];
    tiku_trk_launch_controller_t controller;
    tiku_trk_launch_controller_init(&controller, &apps, store, status,
                                    sizeof status, NULL, &opens);
    controller.open_document = document_open;
    const char *names[] = {"main.bas", "MAIN.BAS", "project.tiku", "project.TIKU",
                          "notes.txt", "NOTES.TXT"};
    for (int i = 0; i < 6; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        FILE *f = fopen(path, "w"); assert(f && !fclose(f));
        tiku_model_t model;
        assert(!backend->ops->stat(backend, path, &model));
        const tiku_trk_app_t *app = tiku_trk_launch_controller_handler(&controller, &model);
        assert(app == &apps.app[i < 4 ? basic : editor]);
        const char *paths[] = {path, "/tmp/another literal;name.bas"};
        assert(tiku_trk_launch_controller_run(&controller, app, paths, 2));
        assert(!strcmp(last_path, paths[1]));
        assert(tiku_trk_launch_run(app, paths, 2) == TIKU_TRK_LAUNCH_NOTHING);
        assert(!unlink(path));
    }
    assert(opens == 12);
    tiku_model_t model = {0};
    snprintf(model.type, sizeof model.type, "text/x-basic");
    tiku_trk_launch_set_preferred(store, model.type, "Alternative");
    assert(!strcmp(tiku_trk_launch_controller_handler(&controller, &model)->name,
                   "Alternative"));
    snprintf(model.type, sizeof model.type, "application/pdf");
    assert(!tiku_trk_launch_controller_handler(&controller, &model));
    tiku_trk_app_t capture = apps.app[basic];
    /* This binary again, in its child mode: a runner that writes down
     * what it was handed is the only way to see the argv a launch really
     * built.  By its own resolved path rather than through /proc, which
     * only Linux has. */
    char self[TIKU_PATH_MAX];
    assert(realpath(argv[0], self));
    snprintf(capture.cmd, sizeof capture.cmd, "%s", self);
    snprintf(capture.module, sizeof capture.module, "%s/captured args", dir);
    const char *literal[] = {"/tmp/literal file;$(not-a-command).BAS"};
    assert(tiku_trk_launch_run(&capture, literal, 1) == TIKU_TRK_LAUNCH_OK);
    (void)waitpid(-1, NULL, 0);
    FILE *f = fopen(capture.module, "r"); assert(f);
    char actual[512] = {0};
    assert(fread(actual, 1, sizeof actual - 1, f) == strlen(literal[0]));
    assert(!strcmp(actual, literal[0]) && !fclose(f));
    assert(!unlink(capture.module) && !rmdir(dir));
    tiku_store_free(store);
    tiku_backend_close(backend);
    puts("PASS: BASIC/project/text associations, overrides, batches and literal argv");
    return 0;
}
