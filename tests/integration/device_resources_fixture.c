/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * device_resources_fixture.c - native resource endpoint with a simulated board.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../fixtures/device_resources_fixture.h"
#include <signal.h>
#include <errno.h>
static volatile sig_atomic_t stopping;
/** @brief Request owner-thread teardown after process termination. */
static void stop_fixture(int sig)
{
    (void)sig;
    stopping = 1;
}
/** @brief Publish observed effects for the independent SDK workflow. */
static void save_state(const char *directory)
{
    char path[1024], temporary[1024];
    snprintf(path, sizeof path, "%s/state", directory);
    snprintf(temporary, sizeof temporary, "%s/state.tmp", directory);
    FILE *file = fopen(temporary, "w");
    assert(file);
    fprintf(file, "%d\n%d\n%d\n%s\n%s\n", writes, plain_writes, reads, name,
            uid);
    assert(!fclose(file) && !rename(temporary, path));
}
/** @brief Keep the fixture's root namespace empty with explicit missing-node errors. */
static int missing(tiku_backend_t *b, const char *path, tiku_model_t *model)
{
    (void)b;
    (void)path;
    (void)model;
    errno = ENOENT;
    return -1;
}
static const tiku_backend_ops_t missing_ops = {.stat = missing};
static tiku_backend_t missing_backend = {.ops = &missing_ops};
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    signal(SIGTERM, stop_fixture);
    signal(SIGINT, stop_fixture);
    desktop_t *app = calloc(1, sizeof *app);
    assert(app);
    struct desktop_devices_vfs owner = setup(app);
    tiku_resource_registry_free(owner.registry);
    assert(!tiku_mount_add(app->backend, "/", &missing_backend, 0));
    app->resource_session = tiku_trk_resource_session_open(
        app->backend, argv[1], "/resources", "device-fixture", 0);
    assert(app->resource_session);
    owner.registry = tiku_trk_resource_session_registry(app->resource_session);
    assert(!desktop_device_resources_open(&owner));
    assert(!tiku_mount_add(app->backend, "/devices/sensor", &backend, 0));
    int held = 0;
    char command_path[1024];
    snprintf(command_path, sizeof command_path, "%s/command", argv[2]);
    for (int turn = 0; !stopping && turn < 60000; turn++) {
        FILE *file = fopen(command_path, "r");
        if (file) {
            char command[96];
            assert(fgets(command, sizeof command, file));
            fclose(file);
            unlink(command_path);
            command[strcspn(command, "\r\n")] = 0;
            if (!strcmp(command, "hold"))
                held = 1;
            else if (!strcmp(command, "release"))
                held = 0;
            else if (!strcmp(command, "swap-uid"))
                strcpy(uid, "1122334455667788");
            else if (!strcmp(command, "restore-uid"))
                strcpy(uid, identity.uid);
            else if (!strcmp(command, "replug")) {
                assert(tiku_mount_detach(app->backend, "/devices/sensor") ==
                       &backend);
                assert(!tiku_mount_add(app->backend, "/devices/sensor",
                                       &backend, 0));
            } else if (!strcmp(command, "managed"))
                managed = 1;
            else if (!strcmp(command, "offline"))
                offline = 1;
            else if (!strcmp(command, "online"))
                offline = 0;
            else if (!strcmp(command, "stop"))
                stopping = 1;
            else
                assert(0);
            if (!strcmp(command, "restore-uid") ||
                !strcmp(command, "managed") || !strcmp(command, "online"))
                for (int i = 0; i < TIKU_MOUNT_MAX; i++)
                    if (owner.native->slots[i])
                        owner.native->slots[i]->next_sample = 0;
        }
        now = monotonic_ms();
        desktop_device_resources_poll(&owner, now);
        (void)tiku_trk_resource_session_pump(app->resource_session);
        if (has_work && !held)
            drain(0);
        save_state(argv[2]);
        usleep(2000);
    }
    desktop_device_resources_close(&owner);
    if (has_work)
        drain(0);
    tiku_trk_resource_session_close(app->resource_session);
    app->backend->ops->close(app->backend);
    free(app);
    return 0;
}
