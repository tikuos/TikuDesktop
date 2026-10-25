/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_desktop_device_config.c - native configuration snapshots and revision guards.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desktop_device_config.h"
#include "tiku_crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** @brief Read a complete bounded first line without accepting truncated values. */
static int line(tiku_backend_t *backend, const char *path, char *out,
                size_t cap)
{
    if (!backend || !backend->ops || !backend->ops->read || cap < 2)
        return 0;
    int n = backend->ops->read(backend, path, out, cap - 1);
    if (n < 0 || (size_t)n >= cap - 1) {
        out[0] = 0;
        return 0;
    }
    out[n] = 0;
    char *end = strpbrk(out, "\r\n");
    if (end)
        *end = 0;
    return tiku_resource_utf8_valid(out, cap - 1);
}
/** @brief Match the two settings with native configuration-v1 support. */
static int curated(const char *path)
{
    return path && (!strcmp(path, "/sys/device/name") ||
                    !strcmp(path, "/sys/cpu/freq_target"));
}
/** @brief Recognize a clock from the native advertised rate list. */
static int rate(const char *rates, const char *value)
{
    if (!value || !*value)
        return 0;
    char *end;
    unsigned long requested = strtoul(value, &end, 10);
    if (*end || requested == 0 || requested > 1000000000UL || value[0] < '0' ||
        value[0] > '9')
        return 0;
    size_t count = 0;
    int found = 0;
    const char *p = rates;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (*p < '0' || *p > '9' || ++count > 8)
            return 0;
        unsigned long n = strtoul(p, &end, 10);
        if (!n || n > 1000000000UL || (*end && *end != ' '))
            return 0;
        if (n == requested)
            found = 1;
        p = end;
    }
    return found;
}
/** @brief Hash native identity, readback, revision and advertised clock constraints. */
static int context(desktop_device_config_t *out)
{
    char text[1024];
    int n = snprintf(text, sizeof text,
                     "%s\n%s\n%s\n%d:%u:%s:%s:%s\n%s\n%s\n%d\n%d:%s:%s\n",
                     out->uid, out->path, out->value, out->config.mode,
                     out->config.revision, out->config.incarnation,
                     out->config.state, out->config.desired, out->mode,
                     out->rates, out->writable, out->pending.state,
                     out->pending.uid, out->pending.request);
    if (n < 0 || (size_t)n >= sizeof text)
        return -1;
    tiku_hash_t hash;
    unsigned char bytes[32];
    tiku_hash_init(&hash, TIKU_HASH_SHA256);
    tiku_hash_update(&hash, text, (size_t)n);
    tiku_hash_final(&hash, bytes);
    for (size_t i = 0; i < 32; i++)
        snprintf(out->context + 2 * i, 3, "%02x", bytes[i]);
    return 0;
}
tiku_resource_error_t desktop_device_config_read(tiku_backend_t *backend,
                                                 const char *uid,
                                                 const char *path,
                                                 desktop_device_config_t *out)
{
    if (!out || !curated(path))
        return TIKU_RESOURCE_INVALID;
    memset(out, 0, sizeof *out);
    snprintf(out->path, sizeof out->path, "%s", path);
    if (!line(backend, "/sys/device/uid", out->uid, sizeof out->uid) || !uid ||
        !*uid || strcmp(uid, out->uid))
        return TIKU_RESOURCE_OWNER_LOST;
    tiku_model_t model = {0};
    tiku_config_read(backend, path, &out->config);
    out->present = backend->ops->stat &&
                   backend->ops->stat(backend, path, &model) == 0 &&
                   line(backend, path, out->value, sizeof out->value);
    out->writable = out->present && backend->ops->write &&
                    tiku_model_is_writable(&model) && out->config.mode >= 0;
    if (!strcmp(path, "/sys/cpu/freq_target")) {
        if (!line(backend, "/sys/cpu/freq_change_mode", out->mode,
                  sizeof out->mode) ||
            !line(backend, "/sys/cpu/freq_available", out->rates,
                  sizeof out->rates) ||
            (!strcmp(out->mode, "fixed")))
            out->writable = 0;
        if (strcmp(out->mode, "live") && strcmp(out->mode, "reboot"))
            out->writable = 0;
    }
    char directory[1024];
    if (!tiku_config_outbox_dir(directory, sizeof directory))
        tiku_config_outbox_inspect(directory, backend, &out->pending);
    else if (out->config.mode)
        out->pending.state = TIKU_CONFIG_PENDING_ERROR;
    if (out->pending.state && (out->pending.uid[0] || out->config.mode))
        out->writable = 0;
    if (context(out)) {
        out->writable = 0;
        return TIKU_RESOURCE_LIMIT;
    }
    return out->present ? TIKU_RESOURCE_OK : TIKU_RESOURCE_UNAVAILABLE;
}
int desktop_device_config_gui_save(tiku_backend_t *backend, const char *path,
                                   const tiku_config_snapshot_t *displayed,
                                   const char *value, char *error, size_t cap)
{
    if (!curated(path) || !displayed || !value)
        return -1;
    char directory[1024];
    int local = !tiku_config_outbox_dir(directory, sizeof directory);
    tiku_config_snapshot_t current = {0};
    tiku_config_pending_t pending = {0};
    tiku_config_read(backend, path, &current);
    if (local)
        tiku_config_outbox_inspect(directory, backend, &pending);
    int managed = current.mode || displayed->mode;
    if ((pending.state && pending.uid[0]) ||
        (managed && (!local || pending.state))) {
        snprintf(error, cap, "Resolve Configuration recovery before saving.");
        return -1;
    }
    if (managed) {
        if (current.mode != 1 || displayed->mode != 1) {
            snprintf(
                error, cap,
                "Recovery state changed or unavailable. Refresh before saving.");
            return -1;
        }
        return tiku_config_outbox_save(directory, backend, path, displayed,
                                       value, strlen(value), error, cap);
    }
    return backend->ops->write ? backend->ops->write(backend, path, value,
                                                     strlen(value), error, cap)
                               : -1;
}
tiku_resource_error_t desktop_device_config_save(
    tiku_backend_t *backend, const desktop_device_config_t *before,
    const char *value, desktop_device_config_begin_t begin_write, void *guard,
    tiku_resource_effect_t *effect, char *error, size_t cap)
{
    *effect = TIKU_RESOURCE_EFFECT_NONE;
    if (!before || !curated(before->path) || !value ||
        !tiku_resource_utf8_valid(value, 95) || strpbrk(value, "\r\n\t"))
        return TIKU_RESOURCE_VALUE;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if (*p < 32 || *p == 127)
            return TIKU_RESOURCE_VALUE;
    if (!strcmp(before->path, "/sys/device/name")) {
        if (!*value || strlen(value) > 31)
            return TIKU_RESOURCE_VALUE;
    } else if (!rate(before->rates, value))
        return TIKU_RESOURCE_VALUE;
    desktop_device_config_t live;
    tiku_resource_error_t status =
        desktop_device_config_read(backend, before->uid, before->path, &live);
    if (status)
        return status;
    if (strcmp(before->context, live.context))
        return TIKU_RESOURCE_STALE;
    if (!live.writable)
        return TIKU_RESOURCE_READ_ONLY;
    if (begin_write && !begin_write(guard))
        return TIKU_RESOURCE_CANCELLED;
    *effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
    if (desktop_device_config_gui_save(backend, before->path, &before->config,
                                       value, error, cap))
        return TIKU_RESOURCE_APPLY_FAILED;
    *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
    return TIKU_RESOURCE_OK;
}
