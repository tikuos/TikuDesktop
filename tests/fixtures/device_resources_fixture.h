/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * device_resources_fixture.h - deterministic native board owner and backend.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEST_DEVICE_RESOURCES_FIXTURE_H_
#define TIKU_TEST_DEVICE_RESOURCES_FIXTURE_H_
#include <assert.h>
#include <unistd.h>
#define tiku_backend_tiku_identity test_identity
#define tiku_backend_tiku_port test_port
#define tiku_work_post_slow test_post
#define tiku_work_cancelled test_cancelled
#include "../../system/desktop/tiku_desktop_device_resources.c"
#undef tiku_backend_tiku_identity
#undef tiku_backend_tiku_port
#undef tiku_work_post_slow
#undef tiku_work_cancelled

static tiku_backend_t backend;
static tiku_identity_t identity = {
    .uid = "0102030405060708", .name = "Sensor", .mcu = "simulated"};
static char uid[40] = "0102030405060708", name[96] = "Sensor",
            cpu[96] = "64000000", history[768];
static const char *epoch = "0102030405060708090a0b0c0d0e0f10";
static unsigned revision = 7;
static int in_worker, reads, writes, plain_writes, managed, broken, readonly,
    offline, reject_write, lose_ack;
static int scans, connects, preferences, development_opened, disconnects,
    global_cancel, disconnect_failure;
static char rates[192] = "64000000 128000000", mode[16] = "live";
static tiku_work_job_t queued;
static int has_work;
static uint64_t now = 100;
static tiku_resource_caller_t caller = {
    .authenticated = 1,
    .scopes = 63,
    .principal = "00000000000000000000000000000001"};
/** @brief Bind the fake backend through the same identity query as production. */
const tiku_identity_t *test_identity(const tiku_backend_t *b)
{
    return b == &backend ? &identity : NULL;
}
/** @brief Return a simulated port without opening a host device. */
int test_port(const tiku_backend_t *b, char *out, size_t cap)
{
    return b == &backend ? snprintf(out, cap, "simulated-port") : 0;
}
/** @brief Capture serialized work for deterministic scheduling. */
int test_post(const tiku_work_job_t *job)
{
    assert(!has_work);
    assert(!strcmp(job->key, "board:/devices/sensor"));
    queued = *job;
    has_work = 1;
    return 0;
}
/** @brief Model native unmount cancellation independently of resource cancellation. */
int test_cancelled(const void *context)
{
    (void)context;
    return global_cancel;
}
/** @brief Match the native board worker key for the simulated mounting. */
void desktop_jobs_path_key(char *out, size_t cap, const char *path)
{
    assert(!strcmp(path, "/devices/sensor"));
    snprintf(out, cap, "board:%s", path);
}
/** @brief Count native actions without opening windows, ports or child processes. */
void desktop_board_look_begin(desktop_t *app, int session,
                              struct desktop_embed *slot)
{
    (void)session;
    (void)slot;
    scans++;
    app->look.going = 1;
}
int device_connect(desktop_t *app, const char *port)
{
    (void)app;
    assert(!strcmp(port, "simulated-port"));
    connects++;
    return 0;
}
void device_set_current(desktop_t *app, const char *value)
{
    snprintf(app->current_device, sizeof app->current_device, "%s", value);
}
void desktop_deviceprefs_open_on(desktop_t *app, const char *value)
{
    (void)app;
    assert(!strcmp(value, "sensor"));
    preferences++;
}
int desktop_bench_firmware(desktop_t *app, const char *value)
{
    (void)app;
    assert(!strcmp(value, "sensor"));
    development_opened++;
    return 1;
}
int device_disconnect(desktop_t *app, const char *value)
{
    assert(!strcmp(value, "sensor"));
    disconnects++;
    if (disconnect_failure)
        return -1;
    return tiku_mount_remove(app->backend, "/devices/sensor");
}
/** @brief Enforce worker ownership for node access. */
static int fake_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    (void)b;
    assert(in_worker);
    reads++;
    if (offline || (!managed && !strncmp(path, "/sys/config/", 12)))
        return -1;
    memset(out, 0, sizeof *out);
    out->facts.perm = TIKU_P_READ | (readonly ? 0 : TIKU_P_WRITE);
    return 0;
}
/** @brief Expose bounded native configuration and statistics nodes. */
static int fake_read(tiku_backend_t *b, const char *path, void *out,
                     size_t cap)
{
    (void)b;
    assert(in_worker);
    reads++;
    if (offline)
        return -1;
    if (!strcmp(path, "/sys/device/uid"))
        return snprintf(out, cap, "%s\n", uid);
    if (!strncmp(path, "/sys/config/", 12)) {
        if (!managed || broken)
            return -1;
        if (!strcmp(path, "/sys/config/status") ||
            !strcmp(path, "/sys/config/provision"))
            return snprintf(out, cap, "v1:ready:%s\n", epoch);
        if (!strcmp(path, "/sys/config/history"))
            return snprintf(out, cap, "%s", history);
        char hex[193];
        const char *value = strstr(path, "frequency") ? cpu : name;
        for (size_t i = 0; i <= strlen(value); i++)
            snprintf(hex + 2 * i, 3, "%02x", (unsigned char)value[i]);
        hex[2 * strlen(value)] = 0;
        return snprintf(out, cap, "v1:%s:%08x:applied:%s\n", epoch, revision,
                        hex);
    }
    if (!strcmp(path, "/sys/device/name"))
        return snprintf(out, cap, "%s\n", name);
    if (!strcmp(path, "/sys/cpu/freq_target"))
        return snprintf(out, cap, "%s\n", cpu);
    if (!strcmp(path, "/sys/cpu/freq_available"))
        return snprintf(out, cap, "%s\n", rates);
    if (!strcmp(path, "/sys/cpu/freq_change_mode"))
        return snprintf(out, cap, "%s\n", mode);
    for (size_t i = 0; i < 4; i++)
        if (!strcmp(path, stat_paths[i]))
            return snprintf(out, cap, "%zu\n", 100 + i);
    return -1;
}
/** @brief Model plain writes and managed CAS receipts without firmware. */
static int fake_write(tiku_backend_t *b, const char *path, const void *bytes,
                      size_t n, char *error, size_t cap)
{
    (void)b;
    assert(in_worker);
    writes++;
    if (reject_write) {
        snprintf(error, cap, "simulated write failure");
        return -1;
    }
    char text[256];
    assert(n < sizeof text);
    memcpy(text, bytes, n);
    text[n] = 0;
    if (!strncmp(path, "/sys/config/", 12)) {
        tiku_config_snapshot_t requested;
        char token[33], value[33];
        assert(!tiku_config_decode_request(text, &requested, token, value));
        if (requested.revision != revision ||
            strcmp(requested.incarnation, epoch)) {
            snprintf(error, cap, "ECONFLICT");
            return -1;
        }
        revision++;
        snprintf(strstr(path, "frequency") ? cpu : name, 96, "%s", value);
        snprintf(history, sizeof history, "%c:%08x:%s:applied:%.*s\n",
                 strstr(path, "frequency") ? '2' : '1', revision, token,
                 (int)(n - 84), text + 75);
        if (lose_ack) {
            snprintf(error, cap, "simulated lost acknowledgement");
            return -1;
        }
        return 0;
    }
    plain_writes++;
    assert(!managed);
    assert(!strcmp(path, "/sys/device/name") ||
           !strcmp(path, "/sys/cpu/freq_target"));
    snprintf(!strcmp(path, "/sys/device/name") ? name : cpu, 96, "%s", text);
    return 0;
}
static const tiku_backend_ops_t backend_ops = {
    .stat = fake_stat, .read = fake_read, .write = fake_write};
/** @brief Supply an isolated registry clock. */
static uint64_t registry_clock(void *context)
{
    (void)context;
    return now;
}
/** @brief Complete captured work through its native owner-thread callbacks. */
static void drain(int native_cancel)
{
    assert(has_work);
    tiku_work_job_t work = queued;
    has_work = 0;
    global_cancel = native_cancel;
    if (!native_cancel) {
        in_worker = 1;
        work.run(work.ctx);
        in_worker = 0;
        work.done(work.ctx);
    }
    work.free(work.ctx);
    global_cancel = 0;
}
/** @brief Open a disconnected resource owner before attaching a fake board. */
static struct desktop_devices_vfs setup(desktop_t *app)
{
    memset(app, 0, sizeof *app);
    app->backend = tiku_mount_open();
    assert(app->backend);
    tiku_resource_limits_t limits = {.providers = 32, .resources = 1024};
    struct desktop_devices_vfs owner = {
        .app = app, .registry = tiku_resource_registry_new(&limits)};
    assert(owner.registry && !tiku_resource_registry_clock(
                                 owner.registry, registry_clock, NULL));
    backend.ops = &backend_ops;
    return owner;
}
/** @brief Refresh an observed board without performing native discovery. */
static void refresh(struct desktop_devices_vfs *owner)
{
    now += 6000;
    desktop_device_resources_poll(owner, now);
    assert(has_work);
    drain(0);
    now += 300;
    desktop_device_resources_poll(owner, now);
}
/** @brief Return the single mounted board's registered record after discovery. */
static struct device_record *record(struct desktop_devices_vfs *owner)
{
    for (int i = 0; i < TIKU_MOUNT_MAX; i++)
        if (owner->native->slots[i])
            return owner->native->slots[i];
    assert(0);
    return NULL;
}
/** @brief Build a complete typed proposal from the published native context. */
static tiku_msg_t *proposal(struct desktop_devices_vfs *owner,
                            const char *path, const char *text)
{
    tiku_resource_snapshot_t snapshot = {0};
    assert(!tiku_resource_read(owner->registry, &caller, path, &snapshot));
    char token[65];
    assert(
        trk_arguments_text(snapshot.applied, "context", token, sizeof token));
    tiku_resource_snapshot_clear(&snapshot);
    tiku_msg_t *v = trk_value_container("object");
    assert(!TRK_TEXT(v, "value", text) && !TRK_TEXT(v, "context", token));
    return v;
}
/** @brief Submit a request with the current provider stamp and pump dispatch. */
static tiku_resource_job_info_t submit(struct desktop_devices_vfs *owner,
                                       const char *path, tiku_msg_t *value,
                                       int invoke)
{
    tiku_resource_snapshot_t before = {0};
    tiku_resource_job_info_t job = {0};
    char request[33];
    assert(!tiku_resource_read(owner->registry, &caller, path, &before));
    assert(!tiku_resource_new_id(request));
    tiku_resource_error_t error = tiku_resource_job_submit(
        owner->registry, &caller, "00000000000000000000000000000002", path,
        &before.stamp, request, value, invoke, 1000, &job);
    if (error)
        fprintf(stderr, "submit %s: %s\n", path,
                tiku_resource_error_name(error));
    assert(!error);
    tiku_resource_snapshot_clear(&before);
    assert(tiku_resource_jobs_pump(owner->registry) == 1);
    return job;
}
/** @brief Assert an exact terminal result and release the retained job copy. */
static void outcome(struct desktop_devices_vfs *owner,
                    tiku_resource_job_info_t *job, tiku_resource_error_t error)
{
    tiku_resource_job_info_t status = {0};
    assert(
        !tiku_resource_job_status(owner->registry, &caller, job->id, &status));
    if (status.error != error)
        fprintf(stderr, "expected %s, got %s (%s)\n",
                tiku_resource_error_name(error),
                tiku_resource_error_name(status.error),
                tiku_resource_job_state_name(status.state));
    assert(status.error == error);
    tiku_resource_job_info_clear(&status);
    tiku_resource_job_info_clear(job);
}
/** @brief Resolve build/debug references through another native owner's public catalog. */
static void tools(struct desktop_devices_vfs *owner, struct device_record *r)
{
    const char *base = "/apps/00000000000000000000000000000009";
    tiku_resource_initial_t initial[3] = {0};
    char paths[3][128];
    const char *fields[] = {"kind", "target", "state"};
    const char *values[] = {"build", "sensor", "native owner"};
    for (int i = 0; i < 3; i++) {
        snprintf(paths[i], sizeof paths[i], "%s/development/%s", base,
                 fields[i]);
        initial[i].spec = (tiku_resource_spec_t){
            .path = paths[i],
            .kind = TIKU_RESOURCE_STATE,
            .schema_version = 1,
            .schema = {.type = TIKU_RESOURCE_TEXT, .text_max = 96},
            .read_scopes = TIKU_RESOURCE_READ,
            .persistence = "none",
            .controller = "native-build-fixture"};
        assert(!trk_value_snapshot(
            &initial[i].initial,
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, values[i]),
            "native_fixture"));
    }
    tiku_resource_stamp_t stamp;
    assert(!tiku_resource_register(owner->registry, "native-build-fixture",
                                   base, NULL, initial, 3, NULL, NULL,
                                   &stamp));
    for (int i = 0; i < 3; i++)
        tiku_resource_snapshot_clear(&initial[i].initial);
    publish_tools(r, 0);
    char path[TIKU_RESOURCE_PATH_MAX];
    path_at(r, 14, path);
    tiku_resource_snapshot_t state = {0};
    assert(!tiku_resource_read(owner->registry, &caller, path, &state));
    tiku_resource_stream_t *stream = NULL;
    size_t bytes, n;
    char content[8192];
    assert(!tiku_resource_stream_open(owner->registry, &caller, path,
                                      &state.stamp, sizeof content - 1,
                                      &stream, &bytes));
    assert(!tiku_resource_stream_read(stream, &caller, 0, content,
                                      sizeof content - 1, &n));
    content[n] = 0;
    assert(bytes == n && strstr(content, base) &&
           strstr(content, "native_application_selected_mount_name"));
    tiku_resource_stream_close(stream);
    tiku_resource_snapshot_clear(&state);
    assert(!tiku_resource_unregister(owner->registry, &stamp));
    publish_tools(r, 0);
    assert(!tiku_resource_read(owner->registry, &caller, path, &state));
    assert(!tiku_resource_stream_open(owner->registry, &caller, path,
                                      &state.stamp, sizeof content - 1,
                                      &stream, &bytes));
    assert(!tiku_resource_stream_read(stream, &caller, 0, content,
                                      sizeof content - 1, &n));
    content[n] = 0;
    assert(!strstr(content, base) && strstr(content, "catalog_status"));
    tiku_resource_stream_close(stream);
    tiku_resource_snapshot_clear(&state);
}

#endif
