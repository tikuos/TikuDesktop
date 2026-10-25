/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_desktop_device_resources.c - generation-bound native board controls.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desktop_devices_vfs_private.h"
#include "tiku_desktop_jobs_private.h"
#include "tiku_desktop_device_config.h"
#include "tiku_trk_resource_arguments.h"
#include "tiku_resource_stream.h"
#include "tiku_crypto.h"
#include <stdatomic.h>
#include <time.h>

enum { DEVICE_FIELDS = 21, DISCOVERY_FIELDS = 5 };
static const char *const suffixes[DEVICE_FIELDS] = {"identity",
                                                    "connection",
                                                    "mounts",
                                                    "configuration",
                                                    "stats",
                                                    "build-jobs",
                                                    "debug-sessions",
                                                    "identity/value",
                                                    "connection/value",
                                                    "mounts/namespace",
                                                    "configuration/name",
                                                    "configuration/cpu-target",
                                                    "configuration/status",
                                                    "stats/sample",
                                                    "build-jobs/sources",
                                                    "debug-sessions/sources",
                                                    "actions/use-current",
                                                    "actions/preferences",
                                                    "actions/development",
                                                    "actions/disconnect",
                                                    "actions/refresh"};
static const char *const discovery_paths[DISCOVERY_FIELDS] = {
    "/devices/discovery/state", "/devices/discovery/candidates",
    "/devices/discovery/actions/scan", "/devices/discovery/actions/connect",
    "/devices/discovery/actions/use-host"};
static const char *const config_paths[] = {"/sys/device/name",
                                           "/sys/cpu/freq_target"};
static const char *const stat_paths[] = {"/sys/uptime", "/sys/mem/free",
                                         "/sys/mem/used", "/sys/cpu/freq"};
static const char *const stat_names[] = {"uptime", "memory_free",
                                         "memory_used", "cpu_frequency"};
struct device_work;
/** @brief One observed mounting; its backend is borrowed until keyed close. */
struct device_record {
    struct desktop_device_resources *owner;
    tiku_backend_t *backend;
    int slot, retired, ready;
    unsigned generation;
    char id[33], prefix[TIKU_PATH_MAX], name[64], port[256];
    tiku_identity_t identity;
    tiku_resource_stamp_t stamp;
    desktop_device_config_t config[2];
    tiku_resource_error_t config_error[2], read_error;
    char stats[4][96];
    int stats_known[4];
    uint64_t sampled, next_sample;
    int schema_managed[2];
    struct device_work *work;
    char command[48], command_job[33];
    char tool_digest[2][65];
};
/** @brief Copied worker inputs and outputs; worker callbacks do not touch owners. */
struct device_work {
    struct device_record *record;
    tiku_backend_t *backend;
    char uid[40], job[33], value[96], error[256];
    desktop_device_config_t before, config[2];
    tiku_resource_error_t status, config_error[2], read_error;
    tiku_resource_effect_t effect;
    char stats[4][96];
    int stats_known[4], setting, completed;
    atomic_int phase; /**< 0 cancellable, 1 cancelled, 2 write accepted */
    uint64_t deadline;
};
/** @brief Product-selected controller; discovery uses the existing native cache. */
struct desktop_device_resources {
    struct desktop_devices_vfs *shared;
    struct device_record *slots[TIKU_MOUNT_MAX];
    tiku_resource_stamp_t discovery;
    uint64_t next_publish, now;
    char command[48], job[33], port[256];
    char candidate_digest[65];
};
/** @brief Return native monotonic milliseconds for the worker write deadline. */
static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}
/** @brief Require the original slot, generation, backend and namespace prefix. */
static int live(const struct device_record *r)
{
    if (!r || r->retired || !r->owner)
        return 0;
    tiku_backend_t *router = r->owner->shared->app->backend;
    const char *prefix = tiku_mount_prefix_at(router, r->slot);
    return r->backend == tiku_mount_at(router, r->slot) &&
           r->generation == tiku_mount_gen_at(router, r->slot) && prefix &&
           !strcmp(prefix, r->prefix);
}
/** @brief Build a resource path whose identity belongs to this mounting. */
static void path_at(const struct device_record *r, int field,
                    char out[TIKU_RESOURCE_PATH_MAX])
{
    snprintf(out, TIKU_RESOURCE_PATH_MAX, "/devices/%s/%s", r->id,
             suffixes[field]);
}
/** @brief Find an exact field without accepting arbitrary board-node paths. */
static int field_at(const struct device_record *r, const char *path)
{
    char expected[TIKU_RESOURCE_PATH_MAX];
    for (int i = 0; i < DEVICE_FIELDS; i++) {
        path_at(r, i, expected);
        if (!strcmp(expected, path))
            return i;
    }
    return -1;
}
/** @brief Read one bounded native node on its board's keyed worker. */
static int read_line(tiku_backend_t *b, const char *path, char *out,
                     size_t cap)
{
    if (!b->ops->read)
        return 0;
    int n = b->ops->read(b, path, out, cap - 1);
    if (n < 0 || (size_t)n >= cap - 1) {
        out[0] = 0;
        return 0;
    }
    out[n] = 0;
    char *end = strpbrk(out, "\r\n");
    if (end)
        *end = 0;
    if (!tiku_resource_utf8_valid(out, cap - 1)) {
        out[0] = 0;
        return 0;
    }
    return 1;
}
/** @brief Claim the write boundary atomically against individual cancellation. */
static int begin_write(void *context)
{
    struct device_work *w = context;
    int expected = 0;
    if (tiku_work_cancelled(w) || monotonic_ms() >= w->deadline)
        return 0;
    return atomic_compare_exchange_strong(&w->phase, &expected, 2);
}
/** @brief Collect native snapshots and optionally save a guarded proposal. */
static void work_run(void *context)
{
    struct device_work *w = context;
    if (atomic_load(&w->phase) == 1 || tiku_work_cancelled(w)) {
        w->status = TIKU_RESOURCE_CANCELLED;
        return;
    }
    if (w->setting >= 0) {
        w->status = desktop_device_config_save(
            w->backend, &w->before, w->value, begin_write, w, &w->effect,
            w->error, sizeof w->error);
    }
    char uid[40] = {0};
    if (!read_line(w->backend, "/sys/device/uid", uid, sizeof uid) ||
        !w->uid[0] || strcmp(uid, w->uid)) {
        w->read_error = TIKU_RESOURCE_OWNER_LOST;
        for (int i = 0; i < 2; i++)
            w->config_error[i] = TIKU_RESOURCE_OWNER_LOST;
        return;
    }
    for (int i = 0; i < 2; i++) {
        if (atomic_load(&w->phase) == 1 || tiku_work_cancelled(w)) {
            w->status = TIKU_RESOURCE_CANCELLED;
            return;
        }
        w->config_error[i] = desktop_device_config_read(
            w->backend, w->uid, config_paths[i], &w->config[i]);
    }
    for (int i = 0; i < 4; i++) {
        if (atomic_load(&w->phase) == 1 || tiku_work_cancelled(w))
            return;
        w->stats_known[i] = read_line(w->backend, stat_paths[i], w->stats[i],
                                      sizeof w->stats[i]);
    }
}
/** @brief Copy a complete value, keeping recovery metadata outside proposals. */
static tiku_msg_t *value_at(struct device_record *r, int field)
{
    tiku_msg_t *v = trk_value_container("object");
    int bad = 0;
    if (field < 7) {
        bad |= TRK_TEXT(v, "scope", suffixes[field]);
        bad |= TRK_TEXT(v, "native_namespace", r->prefix);
        bad |= TRK_BOOL(v, "mounted", live(r));
        if (field == 5 || field == 6) {
            bad |= TRK_TEXT(v, "authority",
                            "running_native_application_providers");
            bad |= TRK_BOOL(v, "firmware_checkout_required_for_desktop", 0);
        }
    } else if (field == 7) {
        bad |= TRK_TEXT(v, "resource_id", r->id);
        bad |= TRK_TEXT(v, "uid", r->identity.uid);
        bad |= TRK_TEXT(v, "mcu", r->identity.mcu);
        bad |= TRK_TEXT(v, "mount_name", r->name);
        bad |= TRK_TEXT(v, "bound_name", r->identity.name);
        bad |= TRK_TEXT(v, "identity_scope", "native_backend_open");
    } else if (field == 8) {
        bad |= TRK_BOOL(v, "mounted", live(r));
        bad |= TRK_TEXT(v, "port", r->port);
        bad |=
            TRK_BOOL(v, "current",
                     !strcmp(r->name, r->owner->shared->app->current_device));
        bad |= TRK_BOOL(v, "sampled", r->ready);
        bad |= TRK_BOOL(v, "uid_verified", r->ready && !r->read_error);
        bad |= TRK_TEXT(v, "last_read_error",
                        tiku_resource_error_name(r->read_error));
        bad |= trk_counter(v, "sampled_ms", r->sampled);
        bad |= TRK_BOOL(v, "wire_liveness_is_observation", 1);
    } else if (field == 9) {
        bad |= TRK_TEXT(v, "path", r->prefix);
        bad |= TRK_INT(v, "slot", r->slot);
        bad |= trk_counter(v, "generation", r->generation);
        bad |= TRK_TEXT(v, "access", "native_backend_vfs_owner");
    } else if (field == 10 || field == 11) {
        desktop_device_config_t *c = &r->config[field - 10];
        bad |= TRK_TEXT(v, "value", c->value);
        bad |= TRK_TEXT(v, "context", c->context);
    } else if (field == 12) {
        bad |= TRK_BOOL(v, "sampled", r->ready);
        bad |= trk_counter(v, "sampled_ms", r->sampled);
        for (int i = 0; i < 2; i++) {
            desktop_device_config_t *c = &r->config[i];
            char key[48];
#define CONFIG_TEXT(k, x)                                                     \
    do {                                                                      \
        snprintf(key, sizeof key, "%s_%s", i ? "cpu" : "name", k);            \
        bad |= TRK_TEXT(v, key, x);                                           \
    } while (0)
#define CONFIG_INT(k, x)                                                      \
    do {                                                                      \
        snprintf(key, sizeof key, "%s_%s", i ? "cpu" : "name", k);            \
        bad |= TRK_INT(v, key, x);                                            \
    } while (0)
            CONFIG_TEXT("error", tiku_resource_error_name(r->config_error[i]));
            CONFIG_INT("present", c->present);
            CONFIG_INT("writable", r->ready && !r->read_error &&
                                       !r->config_error[i] && c->writable);
            CONFIG_INT("managed", c->config.mode);
            CONFIG_INT("revision", c->config.revision);
            CONFIG_TEXT("incarnation", c->config.incarnation);
            CONFIG_TEXT("state", c->config.state);
            CONFIG_TEXT("desired", c->config.desired);
            CONFIG_INT("pending", c->pending.state);
            CONFIG_TEXT("recovery_action", "native_device_preferences");
            CONFIG_TEXT("durability",
                        c->config.mode == 1 ? "user" : "session");
#undef CONFIG_TEXT
#undef CONFIG_INT
        }
        bad |= TRK_TEXT(v, "cpu_mode", r->config[1].mode);
        bad |= TRK_TEXT(v, "cpu_rates", r->config[1].rates);
        bad |= TRK_BOOL(v, "legacy_retention_known", 0);
    } else if (field == 13) {
        bad |= TRK_BOOL(v, "sampled", r->ready);
        bad |= trk_counter(v, "sampled_ms", r->sampled);
        for (int i = 0; i < 4; i++) {
            char key[48];
            bad |= TRK_TEXT(v, stat_names[i], r->stats[i]);
            snprintf(key, sizeof key, "%s_known", stat_names[i]);
            bad |= TRK_BOOL(v, key, r->stats_known[i]);
        }
    } else if (field == 14 || field == 15) {
        tiku_msg_free(v);
        return tiku_resource_stream_metadata(0, "");
    } else
        bad |= TRK_TEXT(v, "dispatch", "native_owner_on_next_poll");
    if (bad) {
        tiku_msg_free(v);
        return NULL;
    }
    return v;
}
/** @brief Publish changed values through the current provider stamp. */
static void publish_record(struct device_record *r)
{
    if (!live(r))
        return;
    for (int i = 0; i < 16; i++) {
        if (i == 14 || i == 15)
            continue;
        char path[TIKU_RESOURCE_PATH_MAX];
        path_at(r, i, path);
        tiku_resource_snapshot_t next = {0};
        if (!trk_value_snapshot(&next, value_at(r, i),
                                r->ready ? "native_observation" : "loading"))
            trk_value_publish(r->owner->shared->registry, path, &next);
        tiku_resource_snapshot_clear(&next);
    }
}
/** @brief Finish on the owner thread only while the captured mounting survives. */
static void work_done(void *context)
{
    struct device_work *w = context;
    struct device_record *r = w->record;
    w->completed = 1;
    if (!live(r))
        return;
    if (atomic_load(&w->phase) != 1) {
        memcpy(r->config, w->config, sizeof r->config);
        memcpy(r->config_error, w->config_error, sizeof r->config_error);
        memcpy(r->stats, w->stats, sizeof r->stats);
        memcpy(r->stats_known, w->stats_known, sizeof r->stats_known);
        r->read_error = w->read_error;
        r->ready = 1;
        r->sampled = r->owner->now;
    }
    if (w->job[0]) {
        tiku_resource_snapshot_t result = {0};
        tiku_msg_t *v = value_at(r, 10 + w->setting);
        int bad = trk_value_snapshot(&result, v,
                                     w->error[0] ? "native_save_failed"
                                                 : "native_readback");
        (void)tiku_resource_job_complete(
            r->owner->shared->registry, w->job, bad ? NULL : &result,
            bad                           ? TIKU_RESOURCE_NO_MEMORY
            : atomic_load(&w->phase) == 1 ? TIKU_RESOURCE_CANCELLED
                                          : w->status,
            w->effect);
        tiku_resource_snapshot_clear(&result);
    }
}
/** @brief Complete queued cancellation and release detached owners without registry access. */
static void work_free(void *context)
{
    struct device_work *w = context;
    struct device_record *r = w->record;
    if (!w->completed && live(r) && w->job[0])
        (void)tiku_resource_job_complete(r->owner->shared->registry, w->job,
                                         NULL,
                                         w->effect == TIKU_RESOURCE_EFFECT_NONE
                                             ? TIKU_RESOURCE_CANCELLED
                                             : TIKU_RESOURCE_UNKNOWN_OUTCOME,
                                         w->effect);
    r->work = NULL;
    free(w);
    if (!r->owner)
        free(r);
}
/** @brief Serialize snapshot reads and saves with existing board traffic. */
static int post_work(struct device_record *r, int setting, const char *job,
                     const char *value)
{
    if (r->work || !live(r))
        return -1;
    struct device_work *w = calloc(1, sizeof *w);
    if (!w)
        return -1;
    w->record = r;
    w->backend = r->backend;
    w->setting = setting;
    atomic_init(&w->phase, 0);
    w->deadline = monotonic_ms() + 299000;
    snprintf(w->uid, sizeof w->uid, "%s", r->identity.uid);
    if (job)
        snprintf(w->job, sizeof w->job, "%s", job);
    if (value)
        snprintf(w->value, sizeof w->value, "%s", value);
    if (setting >= 0)
        w->before = r->config[setting];
    tiku_work_job_t work = {
        .run = work_run, .done = work_done, .free = work_free, .ctx = w};
    desktop_jobs_path_key(work.key, sizeof work.key, r->prefix);
    if (tiku_work_post_slow(&work)) {
        free(w);
        return -1;
    }
    r->work = w;
    return 0;
}
/** @brief Check exact proposal keys and the observed native configuration context. */
static tiku_resource_error_t validate(void *context, const char *path,
                                      const tiku_msg_t *value, int invoke)
{
    struct device_record *r = context;
    if (!live(r))
        return TIKU_RESOURCE_OWNER_LOST;
    int field = field_at(r, path);
    if (r->command_job[0])
        return TIKU_RESOURCE_BUSY;
    if (field >= 16) {
        if (!invoke)
            return TIKU_RESOURCE_INVALID;
        return trk_arguments_keys(value, NULL, 0) ? TIKU_RESOURCE_OK
                                                  : TIKU_RESOURCE_VALUE;
    }
    if (invoke || field < 10 || field > 11)
        return TIKU_RESOURCE_READ_ONLY;
    if (r->work)
        return TIKU_RESOURCE_BUSY;
    const char *const keys[] = {"value", "context"};
    char text[96], token[65];
    if (!trk_arguments_keys(value, keys, 2) ||
        !trk_arguments_text(value, "value", text, sizeof text) ||
        !trk_arguments_text(value, "context", token, sizeof token))
        return TIKU_RESOURCE_VALUE;
    int i = field - 10;
    if (!r->ready || r->read_error || r->config_error[i])
        return TIKU_RESOURCE_UNAVAILABLE;
    if (strcmp(token, r->config[i].context))
        return TIKU_RESOURCE_STALE;
    return r->config[i].writable ? TIKU_RESOURCE_OK : TIKU_RESOURCE_READ_ONLY;
}
/** @brief Queue copied work or a native UI command without reentrant mutation. */
static tiku_resource_error_t start(void *context, const char *job,
                                   const char *path, const tiku_msg_t *value,
                                   int invoke, tiku_resource_snapshot_t *out)
{
    struct device_record *r = context;
    (void)out;
    tiku_resource_error_t e = validate(context, path, value, invoke);
    if (e)
        return e;
    int field = field_at(r, path);
    if (field >= 16) {
        snprintf(r->command, sizeof r->command, "%s", suffixes[field] + 8);
        snprintf(r->command_job, sizeof r->command_job, "%s", job);
        return TIKU_RESOURCE_PENDING;
    }
    char text[96];
    if (!trk_arguments_text(value, "value", text, sizeof text))
        return TIKU_RESOURCE_VALUE;
    return post_work(r, field - 10, job, text) ? TIKU_RESOURCE_BUSY
                                               : TIKU_RESOURCE_PENDING;
}
/** @brief Cancel only this request; other jobs sharing the board key continue. */
static tiku_resource_cancel_t cancel(void *context, const char *job)
{
    struct device_record *r = context;
    if (!strcmp(r->command_job, job)) {
        r->command[0] = 0;
        return TIKU_RESOURCE_CANCEL_ACCEPTED;
    }
    if (r->work && !strcmp(r->work->job, job)) {
        int expected = 0;
        if (atomic_compare_exchange_strong(&r->work->phase, &expected, 1) ||
            expected == 1)
            return TIKU_RESOURCE_CANCEL_ACCEPTED;
    }
    return TIKU_RESOURCE_CANCEL_TOO_LATE;
}
static const tiku_resource_async_controller_t controller = {
    .validate = validate, .start = start, .cancel = cancel};
/** @brief Register a mounting's collections, observations and typed controls. */
static int register_record(struct device_record *r)
{
    tiku_resource_initial_t initial[DEVICE_FIELDS] = {0};
    char paths[DEVICE_FIELDS][TIKU_RESOURCE_PATH_MAX];
    int bad = 0;
    for (int i = 0; i < DEVICE_FIELDS; i++) {
        path_at(r, i, paths[i]);
        int setting = i == 10 || i == 11, action = i >= 16;
        initial[i].spec = (tiku_resource_spec_t){
            .path = paths[i],
            .kind = i < 7                  ? TIKU_RESOURCE_COLLECTION
                    : setting              ? TIKU_RESOURCE_SETTING
                    : action               ? TIKU_RESOURCE_ACTION
                    : (i == 14 || i == 15) ? TIKU_RESOURCE_STREAM
                                           : TIKU_RESOURCE_STATE,
            .schema_version = 1,
            .schema = {.type = TIKU_RESOURCE_OBJECT},
            .read_scopes = TIKU_RESOURCE_READ,
            .sensitive = 1,
            .change_scopes =
                setting  ? TIKU_RESOURCE_WRITE | TIKU_RESOURCE_HOST_CONTROL
                : action ? TIKU_RESOURCE_INVOKE | TIKU_RESOURCE_HOST_CONTROL
                         : 0,
            .persistence =
                setting
                    ? (r->config[i - 10].config.mode == 1 ? "user" : "session")
                    : "none",
            .controller = "native-board"};
        bad |= trk_value_snapshot(&initial[i].initial, value_at(r, i),
                                  "native_observation");
    }
    char prefix[64], key[80];
    snprintf(prefix, sizeof prefix, "/devices/%s", r->id);
    snprintf(key, sizeof key, "device-%s", r->id);
    if (!bad)
        bad = tiku_resource_register_async(
            r->owner->shared->registry, key, prefix, NULL, initial,
            DEVICE_FIELDS, &controller, r, &r->stamp);
    for (int i = 0; i < DEVICE_FIELDS; i++)
        tiku_resource_snapshot_clear(&initial[i].initial);
    for (int i = 0; i < 2; i++)
        r->schema_managed[i] = r->config[i].config.mode == 1;
    memset(r->tool_digest, 0, sizeof r->tool_digest);
    return bad ? -1 : 0;
}
/** @brief Retire before slot reuse; an active worker retains its detached record. */
static void retire(struct device_record *r)
{
    if (r->stamp.incarnation[0]) {
        (void)tiku_resource_unregister(r->owner->shared->registry, &r->stamp);
        memset(&r->stamp, 0, sizeof r->stamp);
    }
    r->retired = 1;
    if (r->work) {
        int expected = 0;
        (void)atomic_compare_exchange_strong(&r->work->phase, &expected, 1);
    }
}
/** @brief Publish complete stream bytes when their retained content changes. */
static void publish_stream(tiku_resource_registry_t *registry,
                           const char *path, const char *bytes, size_t used,
                           char saved[65])
{
    unsigned char hash[32];
    char digest[65];
    if (tiku_hash(TIKU_HASH_SHA256, bytes, used, hash) != 32)
        return;
    for (size_t i = 0; i < 32; i++)
        snprintf(digest + 2 * i, 3, "%02x", hash[i]);
    if (!strcmp(digest, saved))
        return;
    tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
    strcpy(caller.principal, "00000000000000000000000000000001");
    tiku_resource_snapshot_t old = {0};
    if (!tiku_resource_read(registry, &caller, path, &old)) {
        tiku_resource_stamp_t stamp;
        if (!tiku_resource_stream_publish(registry, &old.stamp, path, bytes,
                                          used, &stamp))
            strcpy(saved, digest);
    }
    tiku_resource_snapshot_clear(&old);
}
/** @brief Publish source references from running native application providers. */
static void publish_tools(struct device_record *r, int debug)
{
    tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
    strcpy(caller.principal, "00000000000000000000000000000001");
    char bytes[32768], row[4096];
    size_t used = 0, count = 0;
    uint64_t epoch = 0, after = 0;
    int complete = 0;
    tiku_resource_registry_t *registry = r->owner->shared->registry;
    for (int page = 0; page < 16; page++) {
        tiku_resource_entry_t entries[16];
        size_t n = 0;
        if (tiku_resource_list(registry, &caller, "/apps", after, &epoch,
                               entries, 16, &n))
            break;
        for (size_t i = 0; i < n; i++) {
            after = entries[i].node_id;
            char path[TIKU_RESOURCE_PATH_MAX];
            if (strlen(entries[i].path) > TIKU_RESOURCE_PATH_MAX - 40)
                continue;
            snprintf(path, sizeof path, "%s/development/kind",
                     entries[i].path);
            tiku_resource_snapshot_t kind = {0}, target = {0}, state = {0};
            if (tiku_resource_read(registry, &caller, path, &kind))
                continue;
            const char *text = tiku_msg_find_string(kind.applied, "value", 0);
            int matches = text && !strcmp(text, debug ? "debug" : "build");
            tiku_resource_snapshot_clear(&kind);
            if (!matches)
                continue;
            snprintf(path, sizeof path, "%s/development/target",
                     entries[i].path);
            if (tiku_resource_read(registry, &caller, path, &target))
                continue;
            text = tiku_msg_find_string(target.applied, "value", 0);
            matches = text && !strcmp(text, r->name);
            tiku_resource_snapshot_clear(&target);
            if (!matches)
                continue;
            snprintf(path, sizeof path, "%s/development/state",
                     entries[i].path);
            if (tiku_resource_read(registry, &caller, path, &state))
                continue;
            tiku_msg_t *v = trk_value_container("object");
            int bad = TRK_TEXT(v, "application", entries[i].path) |
                      TRK_TEXT(v, "state", path) |
                      TRK_TEXT(v, "mount_name", r->name) |
                      TRK_TEXT(v, "device_uid", r->identity.uid);
            snprintf(path, sizeof path, "%s/commands/invoke", entries[i].path);
            bad |= TRK_TEXT(v, "commands", path);
            bad |= TRK_TEXT(v, "source_incarnation", state.stamp.incarnation) |
                   trk_counter(v, "source_revision", state.stamp.revision);
            bad |= TRK_TEXT(v, "binding",
                            "native_application_selected_mount_name");
            tiku_resource_snapshot_clear(&state);
            int length =
                bad ? -1 : tiku_resource_value_render(v, row, sizeof row);
            tiku_msg_free(v);
            if (length < 0 || (size_t)length > sizeof bytes - used)
                goto full;
            memcpy(bytes + used, row, (size_t)length);
            used += (size_t)length;
            count++;
        }
        if (n < 16) {
            complete = 1;
            break;
        }
    }
full:;
    tiku_msg_t *summary = trk_value_container("object");
    int bad = TRK_TEXT(summary, "kind", "catalog_status") |
              TRK_BOOL(summary, "complete", complete) |
              TRK_INT(summary, "references", count) |
              TRK_TEXT(summary, "authority", "native_application_providers");
    int length =
        bad ? -1 : tiku_resource_value_render(summary, row, sizeof row);
    tiku_msg_free(summary);
    if (length > 0 && (size_t)length <= sizeof bytes - used) {
        memcpy(bytes + used, row, (size_t)length);
        used += (size_t)length;
    } else
        return;
    char path[TIKU_RESOURCE_PATH_MAX];
    path_at(r, debug ? 15 : 14, path);
    publish_stream(registry, path, bytes, used, r->tool_digest[debug]);
}
/** @brief Dispatch deferred commands through their native owners. */
static void command_poll(struct device_record *r)
{
    if (!r->command_job[0])
        return;
    desktop_t *app = r->owner->shared->app;
    tiku_resource_error_t error = TIKU_RESOURCE_OK;
    int disconnect = 0;
    if (!r->command[0])
        error = TIKU_RESOURCE_CANCELLED;
    else if (!live(r))
        error = TIKU_RESOURCE_OWNER_LOST;
    else if (!strcmp(r->command, "use-current"))
        device_set_current(app, r->name);
    else if (!strcmp(r->command, "preferences"))
        desktop_deviceprefs_open_on(app, r->name);
    else if (!strcmp(r->command, "development")) {
        if (!desktop_bench_firmware(app, r->name))
            error = TIKU_RESOURCE_UNAVAILABLE;
    } else if (!strcmp(r->command, "refresh")) {
        r->next_sample = 0;
        if (r->work)
            error = TIKU_RESOURCE_BUSY;
    } else if (!strcmp(r->command, "disconnect")) {
        if (device_disconnect(app, r->name))
            error = TIKU_RESOURCE_APPLY_FAILED;
        else
            disconnect = 1;
    } else
        error = TIKU_RESOURCE_INVALID;
    tiku_resource_snapshot_t result = {0};
    tiku_msg_t *v = trk_value_container("object");
    if (TRK_TEXT(v, "command", r->command) |
        TRK_TEXT(v, "state",
                 disconnect ? "disconnect_dispatched"
                            : "native_owner_dispatched")) {
        tiku_msg_free(v);
        v = NULL;
    }
    if (!trk_value_snapshot(&result, v, "dispatched"))
        (void)tiku_resource_job_complete(
            r->owner->shared->registry, r->command_job, &result, error,
            error ? TIKU_RESOURCE_EFFECT_NONE : TIKU_RESOURCE_EFFECT_PARTIAL);
    tiku_resource_snapshot_clear(&result);
    r->command_job[0] = 0;
    r->command[0] = 0;
    if (disconnect) {
        retire(r);
    }
}
/** @brief Copy discovery facts without opening or enumerating host ports. */
static tiku_msg_t *discovery_value(struct desktop_device_resources *owner,
                                   int field)
{
    if (field == 1)
        return tiku_resource_stream_metadata(0, "");
    desktop_t *app = owner->shared->app;
    tiku_msg_t *v = trk_value_container("object");
    int bad = 0;
    if (field == 0) {
        bad |= TRK_BOOL(v, "scanning", app->look.going);
        bad |= TRK_INT(v, "pending", app->look.pending);
        bad |= TRK_INT(v, "candidates", app->ncandidates);
        bad |= TRK_TEXT(v, "source", "native_discovery_cache");
        bad |= TRK_BOOL(v, "automatic_port_probes", 0);
    } else
        bad |= TRK_TEXT(v, "required",
                        field == 3 ? "port_from_current_candidate_cache"
                                   : "empty_object");
    if (bad) {
        tiku_msg_free(v);
        return NULL;
    }
    return v;
}
/** @brief Accept discovery controls only for the native candidate cache. */
static tiku_resource_error_t discovery_validate(void *context,
                                                const char *path,
                                                const tiku_msg_t *value,
                                                int invoke)
{
    struct desktop_device_resources *owner = context;
    if (owner->job[0])
        return TIKU_RESOURCE_BUSY;
    if (!invoke)
        return TIKU_RESOURCE_READ_ONLY;
    if (!strcmp(path, discovery_paths[3])) {
        const char *const keys[] = {"port"};
        char port[256];
        if (!trk_arguments_keys(value, keys, 1) ||
            !trk_arguments_text(value, "port", port, sizeof port))
            return TIKU_RESOURCE_VALUE;
        desktop_t *app = owner->shared->app;
        for (int i = 0; i < app->ncandidates && i < TIKU_MOUNT_MAX; i++)
            if (!strcmp(port, app->candidates[i].port))
                return TIKU_RESOURCE_OK;
        return TIKU_RESOURCE_STALE;
    }
    if (strcmp(path, discovery_paths[2]) && strcmp(path, discovery_paths[4]))
        return TIKU_RESOURCE_READ_ONLY;
    return trk_arguments_keys(value, NULL, 0) ? TIKU_RESOURCE_OK
                                              : TIKU_RESOURCE_VALUE;
}
/** @brief Defer native discovery until the registry callback has returned. */
static tiku_resource_error_t discovery_start(void *context, const char *job,
                                             const char *path,
                                             const tiku_msg_t *value,
                                             int invoke,
                                             tiku_resource_snapshot_t *out)
{
    struct desktop_device_resources *owner = context;
    (void)out;
    tiku_resource_error_t e = discovery_validate(context, path, value, invoke);
    if (e)
        return e;
    snprintf(owner->command, sizeof owner->command, "%s",
             strrchr(path, '/') + 1);
    snprintf(owner->job, sizeof owner->job, "%s", job);
    if (!strcmp(path, discovery_paths[3]))
        (void)trk_arguments_text(value, "port", owner->port,
                                 sizeof owner->port);
    return TIKU_RESOURCE_PENDING;
}
/** @brief Cancel a native command before its owner-thread dispatch. */
static tiku_resource_cancel_t discovery_cancel(void *context, const char *job)
{
    struct desktop_device_resources *owner = context;
    if (strcmp(owner->job, job))
        return TIKU_RESOURCE_CANCEL_TOO_LATE;
    owner->command[0] = 0;
    return TIKU_RESOURCE_CANCEL_ACCEPTED;
}
/** @brief Publish copied candidates and current scan progress. */
static void discovery_poll(struct desktop_device_resources *owner)
{
    desktop_t *app = owner->shared->app;
    tiku_resource_registry_t *registry = owner->shared->registry;
    if (owner->job[0]) {
        tiku_resource_error_t e = TIKU_RESOURCE_OK;
        if (!owner->command[0])
            e = TIKU_RESOURCE_CANCELLED;
        else if (!strcmp(owner->command, "scan"))
            desktop_board_look_begin(app, -1, NULL);
        else if (!strcmp(owner->command, "use-host"))
            device_set_current(app, "");
        else if (!strcmp(owner->command, "connect")) {
            int found = 0;
            for (int i = 0; i < app->ncandidates && i < TIKU_MOUNT_MAX; i++)
                if (!strcmp(owner->port, app->candidates[i].port))
                    found = 1;
            if (!found)
                e = TIKU_RESOURCE_STALE;
            else if (device_connect(app, owner->port))
                e = TIKU_RESOURCE_BUSY;
        } else
            e = TIKU_RESOURCE_INVALID;
        tiku_resource_snapshot_t result = {0};
        tiku_msg_t *v = trk_value_container("object");
        if (TRK_TEXT(v, "dispatch", owner->command) |
            TRK_BOOL(v, "connection_confirmed", 0)) {
            tiku_msg_free(v);
            v = NULL;
        }
        if (!trk_value_snapshot(&result, v, "native_owner_dispatched"))
            (void)tiku_resource_job_complete(registry, owner->job, &result, e,
                                             e ? TIKU_RESOURCE_EFFECT_NONE
                                               : TIKU_RESOURCE_EFFECT_PARTIAL);
        tiku_resource_snapshot_clear(&result);
        owner->job[0] = owner->command[0] = owner->port[0] = 0;
    }
    tiku_resource_snapshot_t next = {0};
    if (!trk_value_snapshot(&next, discovery_value(owner, 0),
                            "native_discovery_cache"))
        trk_value_publish(registry, discovery_paths[0], &next);
    tiku_resource_snapshot_clear(&next);
    char bytes[16384], row[2048];
    size_t used = 0;
    for (int i = 0; i < app->ncandidates && i < TIKU_MOUNT_MAX; i++) {
        tiku_msg_t *v = trk_value_container("object");
        int bad = TRK_TEXT(v, "port", app->candidates[i].port) |
                  TRK_TEXT(v, "name", app->candidates[i].name) |
                  TRK_TEXT(v, "location", app->candidates[i].where);
        int n = bad ? -1 : tiku_resource_value_render(v, row, sizeof row);
        tiku_msg_free(v);
        if (n < 0 || (size_t)n > sizeof bytes - used)
            return;
        memcpy(bytes + used, row, (size_t)n);
        used += (size_t)n;
    }
    publish_stream(registry, discovery_paths[1], bytes, used,
                   owner->candidate_digest);
}
int desktop_device_resources_open(struct desktop_devices_vfs *shared)
{
    struct desktop_device_resources *owner = calloc(1, sizeof *owner);
    if (!owner)
        return -1;
    owner->shared = shared;
    shared->native = owner;
    tiku_resource_initial_t initial[DISCOVERY_FIELDS] = {0};
    int bad = 0;
    for (int i = 0; i < DISCOVERY_FIELDS; i++) {
        initial[i].spec = (tiku_resource_spec_t){
            .path = discovery_paths[i],
            .kind = i < 2
                        ? (i == 1 ? TIKU_RESOURCE_STREAM : TIKU_RESOURCE_STATE)
                        : TIKU_RESOURCE_ACTION,
            .schema_version = 1,
            .schema = {.type = TIKU_RESOURCE_OBJECT},
            .read_scopes = TIKU_RESOURCE_READ,
            .sensitive = 1,
            .change_scopes =
                i >= 2 ? TIKU_RESOURCE_INVOKE | TIKU_RESOURCE_HOST_CONTROL : 0,
            .persistence = "none",
            .controller = "native-board-discovery"};
        bad |=
            trk_value_snapshot(&initial[i].initial, discovery_value(owner, i),
                               "native_discovery_cache");
    }
    static const tiku_resource_async_controller_t actions = {
        .validate = discovery_validate,
        .start = discovery_start,
        .cancel = discovery_cancel};
    if (!bad)
        bad = tiku_resource_register_async(
            shared->registry, "native-board-discovery", "/devices/discovery",
            NULL, initial, DISCOVERY_FIELDS, &actions, owner,
            &owner->discovery);
    for (int i = 0; i < DISCOVERY_FIELDS; i++)
        tiku_resource_snapshot_clear(&initial[i].initial);
    if (bad) {
        desktop_device_resources_close(shared);
        return -1;
    }
    return 0;
}
void desktop_device_resources_poll(struct desktop_devices_vfs *shared,
                                   uint64_t now)
{
    struct desktop_device_resources *owner = shared->native;
    if (!owner)
        return;
    owner->now = now;
    for (int i = 0; i < TIKU_MOUNT_MAX; i++) {
        struct device_record *r = owner->slots[i];
        if (r && !live(r))
            retire(r);
        if (r && r->retired) {
            if (r->work)
                continue;
            free(r);
            owner->slots[i] = NULL;
            r = NULL;
        }
        if (!r) {
            tiku_backend_t *backend = tiku_mount_at(shared->app->backend, i);
            const char *prefix = tiku_mount_prefix_at(shared->app->backend, i);
            const tiku_identity_t *identity =
                tiku_backend_tiku_identity(backend);
            if (!identity || !prefix || strncmp(prefix, "/devices/", 9) ||
                !prefix[9] || strchr(prefix + 9, '/'))
                continue;
            r = calloc(1, sizeof *r);
            if (!r)
                continue;
            r->owner = owner;
            r->backend = backend;
            r->slot = i;
            r->generation = tiku_mount_gen_at(shared->app->backend, i);
            r->identity = *identity;
            snprintf(r->prefix, sizeof r->prefix, "%s", prefix);
            snprintf(r->name, sizeof r->name, "%s", prefix + 9);
            (void)tiku_backend_tiku_port(backend, r->port, sizeof r->port);
            r->read_error = TIKU_RESOURCE_UNAVAILABLE;
            if (tiku_resource_new_id(r->id) || register_record(r)) {
                free(r);
                continue;
            }
            owner->slots[i] = r;
        }
        command_poll(r);
        if (r->retired)
            continue;
        if (!r->work &&
            (r->schema_managed[0] != (r->config[0].config.mode == 1) ||
             r->schema_managed[1] != (r->config[1].config.mode == 1))) {
            (void)tiku_resource_unregister(shared->registry, &r->stamp);
            memset(&r->stamp, 0, sizeof r->stamp);
            if (register_record(r)) {
                r->retired = 1;
                continue;
            }
        }
        if (!r->work && now >= r->next_sample) {
            if (!post_work(r, -1, NULL, NULL))
                r->next_sample = now + 5000;
        }
        if (now >= owner->next_publish && !(r->work && r->work->job[0]) &&
            !r->command_job[0]) {
            publish_record(r);
            publish_tools(r, 0);
            publish_tools(r, 1);
        }
    }
    if (now >= owner->next_publish) {
        discovery_poll(owner);
        owner->next_publish = now + 250;
    } else if (owner->job[0])
        discovery_poll(owner);
}
int desktop_device_resources_close(struct desktop_devices_vfs *shared)
{
    struct desktop_device_resources *owner = shared->native;
    if (!owner)
        return 1;
    if (owner->discovery.incarnation[0])
        (void)tiku_resource_unregister(shared->registry, &owner->discovery);
    for (int i = 0; i < TIKU_MOUNT_MAX; i++)
        if (owner->slots[i])
            retire(owner->slots[i]);
    uint64_t until = monotonic_ms() + 5000;
    int pending;
    do {
        pending = 0;
        (void)tiku_work_collect();
        for (int i = 0; i < TIKU_MOUNT_MAX; i++)
            pending += owner->slots[i] && owner->slots[i]->work != NULL;
        if (pending)
            usleep(1000);
    } while (pending && monotonic_ms() < until);
    for (int i = 0; i < TIKU_MOUNT_MAX; i++)
        if (owner->slots[i]) {
            struct device_record *r = owner->slots[i];
            retire(r);
            r->owner = NULL;
            if (!r->work)
                free(r);
        }
    free(owner);
    shared->native = NULL;
    return !pending;
}
