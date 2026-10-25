/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_bench_resources.c - native development queue observations.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_bench_private.h"
#include "tiku_app_resources.h"
/** @brief Copy build/run state owned by the native development application. */
static tiku_msg_t *value_at(bench_t *app, size_t index)
{
    if (index < 2)
        return tiku_resource_value_string(
            TIKU_RESOURCE_TEXT, index == 0 ? "build" : app->target_name);
    tiku_msg_t *out = tiku_app_resource_object(0);
    int bad = 0;
#define TEXT(k, x)                                                            \
    tiku_app_resource_member(                                                 \
        out, k, tiku_resource_value_string(TIKU_RESOURCE_TEXT, x))
#define INT(k, x)                                                             \
    tiku_app_resource_member(                                                 \
        out, k, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, x))
    bad |= TEXT("target", app->target_name);
    bad |= TEXT("mcu", app->mcu);
    bad |= TEXT("profile", app->profile);
    bad |= TEXT("active", app->active);
    bad |= TEXT("status", app->note);
    bad |= INT("queued", app->njob);
    bad |= INT("current", app->job);
    bad |= INT("busy", app->busy);
    bad |= INT("cancelled", app->cancelled);
    bad |= INT("waiting_release", app->waiting);
    bad |= INT("released", app->released);
    bad |= INT("reclaiming", app->reclaiming);
    bad |= INT("runner_pid", app->child);
    bad |= INT("in_build", app->in_build);
    bad |= INT("failed", app->batch_failed);
    bad |= INT("loading", app->loading);
    bad |= TEXT("commands", "commands/invoke");
    bad |= TEXT("command_inventory", "menus");
    bad |= TEXT("confirmation", "native_application_policy");
#undef TEXT
#undef INT
    if (bad) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
/** @brief The three state paths remain stable for the application lifetime. */
static int catalog(void *state, tiku_app_resources_t *owner, size_t index,
                   tiku_resource_initial_t *out)
{
    (void)owner;
    static const char *const paths[] = {
        "development/kind", "development/target", "development/state"};
    if (index >= 3)
        return 0;
    return tiku_app_resource_initial(out, paths[index], TIKU_RESOURCE_STATE,
                                     index < 2 ? TIKU_RESOURCE_TEXT
                                               : TIKU_RESOURCE_OBJECT,
                                     value_at(state, index));
}
/** @brief Refresh observations from the same model used by the native panels. */
static tiku_resource_error_t capture(void *state, tiku_app_resources_t *owner,
                                     const char *path,
                                     tiku_resource_snapshot_t *out)
{
    (void)owner;
    static const char *const paths[] = {
        "development/kind", "development/target", "development/state"};
    for (size_t i = 0; i < 3; i++)
        if (!strcmp(path, paths[i]))
            return tiku_app_resource_snapshot(out, value_at(state, i),
                                              "native_development_owner")
                       ? TIKU_RESOURCE_NO_MEMORY
                       : 0;
    return TIKU_RESOURCE_UNSUPPORTED;
}
/** @brief Keep development observations read-only; native menus own commands. */
static tiku_resource_error_t change(void *state, tiku_app_resources_t *owner,
                                    const char *path, const tiku_msg_t *value,
                                    int invoke, tiku_resource_snapshot_t *out,
                                    tiku_resource_effect_t *effect)
{
    (void)state;
    (void)owner;
    (void)path;
    (void)value;
    (void)invoke;
    (void)out;
    *effect = TIKU_RESOURCE_EFFECT_NONE;
    return TIKU_RESOURCE_UNSUPPORTED;
}
/** @brief Optional native observations; menu actions retain the runner's checks. */
__attribute__((visibility("default")))
const tiku_app_resource_export_t tiku_app_resources_v1 = {
    .version = 1,
    .size = sizeof(tiku_app_resource_export_t),
    .catalog = catalog,
    .capture = capture,
    .change = change};
