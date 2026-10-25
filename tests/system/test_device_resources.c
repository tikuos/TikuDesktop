/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_device_resources.c - simulated board lifecycle and configuration requests.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../fixtures/device_resources_fixture.h"
/** @brief Run typed legacy/managed, cancellation and stale-mount workflows. */
int main(void)
{
    char home[] = "/tmp/tiku-device-resources-XXXXXX";
    assert(mkdtemp(home));
    assert(!setenv("HOME", home, 1));
    assert(!setenv("XDG_STATE_HOME", home, 1));
    desktop_t *app = calloc(1, sizeof *app);
    assert(app);
    struct desktop_devices_vfs owner = setup(app);
    assert(!desktop_device_resources_open(&owner));
    desktop_device_resources_poll(&owner, now);
    assert(!reads && !writes && !has_work && !scans && !connects);
    tiku_resource_snapshot_t observed = {0};
    assert(!tiku_resource_read(owner.registry, &caller, discovery_paths[0],
                               &observed));
    tiku_resource_snapshot_clear(&observed);
    assert(!tiku_mount_add(app->backend, "/devices/sensor", &backend, 0));
    refresh(&owner);
    struct device_record *r = record(&owner);
    assert(r->ready && r->config[0].writable && r->config[1].writable);
    tools(&owner, r);
    for (int i = 0; i < 7; i++) {
        char path[TIKU_RESOURCE_PATH_MAX];
        tiku_resource_entry_t entry;
        path_at(r, i, path);
        assert(!tiku_resource_entry(owner.registry, &caller, path, &entry) &&
               entry.kind == TIKU_RESOURCE_COLLECTION);
    }
    char path[TIKU_RESOURCE_PATH_MAX];
    path_at(r, 10, path);
    tiku_msg_t *v = proposal(&owner, path, "First sensor");
    tiku_resource_caller_t limited = caller;
    limited.scopes &= ~TIKU_RESOURCE_CONTENT;
    assert(tiku_resource_read(owner.registry, &limited, path, &observed) !=
           TIKU_RESOURCE_OK);
    limited = caller;
    limited.scopes &= ~TIKU_RESOURCE_HOST_CONTROL;
    assert(!tiku_resource_read(owner.registry, &caller, path, &observed));
    char request[33];
    assert(!tiku_resource_new_id(request));
    tiku_resource_job_info_t job = {0};
    assert(tiku_resource_job_submit(owner.registry, &limited,
                                    "00000000000000000000000000000002", path,
                                    &observed.stamp, request, v, 0, 1000,
                                    &job) == TIKU_RESOURCE_DENIED);
    tiku_resource_snapshot_clear(&observed);
    job = submit(&owner, path, v, 0);
    assert(has_work);
    drain(0);
    outcome(&owner, &job, TIKU_RESOURCE_OK);
    assert(!strcmp(name, "First sensor") && plain_writes == 1);
    tiku_msg_free(v);
    v = proposal(&owner, path, "Cancelled");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    tiku_resource_job_info_t status = {0};
    tiku_resource_cancel_t accepted;
    assert(
        !tiku_resource_job_status(owner.registry, &caller, job.id, &status));
    assert(!tiku_resource_job_cancel(owner.registry, &caller, job.id,
                                     &status.stamp, &accepted) &&
           accepted == TIKU_RESOURCE_CANCEL_ACCEPTED);
    tiku_resource_job_info_clear(&status);
    int before = writes;
    drain(0);
    outcome(&owner, &job, TIKU_RESOURCE_CANCELLED);
    assert(writes == before);
    v = proposal(&owner, path, "Changed context");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    strcpy(name, "Native GUI change");
    drain(0);
    outcome(&owner, &job, TIKU_RESOURCE_STALE);
    assert(writes == before);
    v = proposal(&owner, path, "Wrong device");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    strcpy(uid, "1122334455667788");
    drain(0);
    outcome(&owner, &job, TIKU_RESOURCE_OWNER_LOST);
    assert(writes == before);
    strcpy(uid, identity.uid);
    refresh(&owner);
    v = proposal(&owner, path, "Expired");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    r->work->deadline = 1;
    drain(0);
    outcome(&owner, &job, TIKU_RESOURCE_CANCELLED);
    assert(writes == before);
    path_at(r, 11, path);
    v = proposal(&owner, path, "12345");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    drain(0);
    outcome(&owner, &job, TIKU_RESOURCE_VALUE);
    assert(writes == before);
    v = proposal(&owner, path, "128000000");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    strcpy(rates, "64000000");
    drain(0);
    outcome(&owner, &job, TIKU_RESOURCE_STALE);
    assert(writes == before);
    strcpy(rates, "64000000 128000000");
    refresh(&owner);
    v = proposal(&owner, path, "128000000");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    drain(0);
    outcome(&owner, &job, TIKU_RESOURCE_OK);
    assert(!strcmp(cpu, "128000000"));
    managed = 1;
    refresh(&owner);
    r = record(&owner);
    path_at(r, 10, path);
    tiku_msg_t *description = NULL;
    assert(
        !tiku_resource_describe(owner.registry, &caller, path, &description));
    assert(
        !strcmp(tiku_msg_find_string(description, "persistence", 0), "user"));
    tiku_msg_free(description);
    v = proposal(&owner, path, "Managed sensor");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    before = plain_writes;
    lose_ack = 1;
    drain(0);
    outcome(&owner, &job, TIKU_RESOURCE_OK);
    lose_ack = 0;
    assert(plain_writes == before && revision == 8 &&
           !strcmp(name, "Managed sensor"));
    v = proposal(&owner, path, "Recovery failure");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    before = writes;
    broken = 1;
    drain(0);
    outcome(&owner, &job, TIKU_RESOURCE_STALE);
    assert(writes == before);
    broken = 0;
    refresh(&owner);
    tiku_msg_t *empty = trk_value_container("object");
    path_at(r, 17, path);
    job = submit(&owner, path, empty, 1);
    desktop_device_resources_poll(&owner, ++now);
    outcome(&owner, &job, 0);
    assert(preferences == 1);
    path_at(r, 18, path);
    job = submit(&owner, path, empty, 1);
    desktop_device_resources_poll(&owner, ++now);
    outcome(&owner, &job, 0);
    assert(development_opened == 1);
    path_at(r, 16, path);
    job = submit(&owner, path, empty, 1);
    desktop_device_resources_poll(&owner, ++now);
    outcome(&owner, &job, 0);
    assert(!strcmp(app->current_device, "sensor"));
    app->ncandidates = 1;
    strcpy(app->candidates[0].port, "simulated-port");
    strcpy(app->candidates[0].name, "sensor");
    tiku_msg_t *port = trk_value_container("object");
    assert(!TRK_TEXT(port, "port", "simulated-port"));
    job = submit(&owner, discovery_paths[3], port, 1);
    app->ncandidates = 0;
    desktop_device_resources_poll(&owner, ++now);
    outcome(&owner, &job, TIKU_RESOURCE_STALE);
    assert(!connects);
    tiku_msg_free(port);
    job = submit(&owner, discovery_paths[2], empty, 1);
    desktop_device_resources_poll(&owner, ++now);
    outcome(&owner, &job, 0);
    assert(scans == 1);
    path_at(r, 10, path);
    v = proposal(&owner, path, "Stale mounting");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    before = writes;
    assert(tiku_mount_detach(app->backend, "/devices/sensor") == &backend);
    assert(!tiku_mount_add(app->backend, "/devices/sensor", &backend, 0));
    desktop_device_resources_poll(&owner, ++now);
    assert(r->retired);
    drain(0);
    assert(writes == before);
    outcome(&owner, &job, TIKU_RESOURCE_UNKNOWN_OUTCOME);
    desktop_device_resources_poll(&owner, ++now);
    assert(tiku_resource_read(owner.registry, &caller, path, &observed) ==
           TIKU_RESOURCE_NOT_FOUND);
    drain(0);
    desktop_device_resources_poll(&owner, now += 300);
    r = record(&owner);
    path_at(r, 19, path);
    disconnect_failure = 1;
    job = submit(&owner, path, empty, 1);
    desktop_device_resources_poll(&owner, ++now);
    outcome(&owner, &job, TIKU_RESOURCE_APPLY_FAILED);
    assert(live(r) && disconnects == 1);
    disconnect_failure = 0;
    job = submit(&owner, path, empty, 1);
    desktop_device_resources_poll(&owner, ++now);
    outcome(&owner, &job, TIKU_RESOURCE_OK);
    assert(r->retired && disconnects == 2);
    assert(!tiku_mount_add(app->backend, "/devices/sensor", &backend, 0));
    desktop_device_resources_poll(&owner, ++now);
    assert(has_work);
    drain(0);
    desktop_device_resources_poll(&owner, now += 300);
    r = record(&owner);
    path_at(r, 10, path);
    v = proposal(&owner, path, "Close pending");
    job = submit(&owner, path, v, 0);
    tiku_msg_free(v);
    assert(!desktop_device_resources_close(&owner));
    drain(0);
    assert(writes == before);
    tiku_resource_job_info_clear(&job);
    tiku_msg_free(empty);
    app->backend->ops->close(app->backend);
    tiku_resource_registry_free(owner.registry);
    free(app);
    char command[256];
    snprintf(command, sizeof command, "rm -rf -- '%s'", home);
    assert(system(command) == 0);
    puts(
        "PASS device resources: disconnected, collections, authority, native actions, managed recovery, cancellation, identity and mount replacement");
    return 0;
}
