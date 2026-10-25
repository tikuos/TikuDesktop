/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_desktop_device_config.h - worker-owned curated device configuration.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESKTOP_DEVICE_CONFIG_H_
#define TIKU_DESKTOP_DEVICE_CONFIG_H_
#include "tiku_config_outbox.h"
#include "tiku_resource.h"
/** @brief One native setting, its configuration revision and current readback. */
typedef struct {
    char path[64], uid[40], value[96], mode[16], rates[192], context[65];
    tiku_config_snapshot_t config;
    tiku_config_pending_t pending;
    int present, writable;
} desktop_device_config_t;
/** @brief Claim a cancellable write; nonzero permits native effects. */
typedef int (*desktop_device_config_begin_t)(void *);
/** @brief Capture one curated node on the backend's keyed worker.
 * @note expected_uid is the identity bound to the mount. No enrollment occurs. */
tiku_resource_error_t desktop_device_config_read(tiku_backend_t *,
                                                 const char *expected_uid,
                                                 const char *path,
                                                 desktop_device_config_t *);
/** @brief Validate and save through the native outbox or legacy backend writer.
 * @note Called on the backend's keyed worker. The snapshot and live UID are
 * checked before writing; managed configuration never falls back to a plain write.
 * begin_write, when supplied, must claim the caller's cancellation boundary;
 * returning zero prevents effects. */
tiku_resource_error_t desktop_device_config_save(
                           tiku_backend_t *, const desktop_device_config_t *,
                           const char *, desktop_device_config_begin_t, void *,
                           tiku_resource_effect_t *, char *, size_t);
/** @brief Save the native GUI's already validated curated proposal.
 * @note Keeps its displayed managed revision and the existing recovery policy. */
int desktop_device_config_gui_save(tiku_backend_t *, const char *,
                                   const tiku_config_snapshot_t *,
                                   const char *, char *, size_t);
#endif
