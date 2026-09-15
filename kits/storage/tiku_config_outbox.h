/* Durable host requests for the experimental configuration protocol.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_CONFIG_OUTBOX_H
#define TIKU_CONFIG_OUTBOX_H
#include "tiku_config.h"

enum {
    TIKU_CONFIG_PENDING_NONE,
    TIKU_CONFIG_PENDING_RETRY,
    TIKU_CONFIG_PENDING_CONFIRMED,
    TIKU_CONFIG_PENDING_CONFLICT,
    TIKU_CONFIG_PENDING_OBSOLETE,
    TIKU_CONFIG_PENDING_UNKNOWN,
    TIKU_CONFIG_PENDING_ERROR
};
typedef struct {
    int state;                     /* Observation, not persisted proof. */
    int enrollment;
    char uid[33], path[64], request[160], summary[192];
} tiku_config_pending_t;

/* Private local directory; TIKU_CONFIG_OUTBOX overrides the default path.
 * Inspect never creates files or writes the device. Mutations require a live
 * hardware UID, not a name/port, and serialize writers across processes. */
int tiku_config_outbox_dir(char *out, size_t max);
int tiku_config_outbox_inspect(const char *dir, tiku_backend_t *backend,
                              tiku_config_pending_t *out);
int tiku_config_outbox_save(const char *dir, tiku_backend_t *backend,
    const char *path, const tiku_config_snapshot_t *snapshot,
    const char *value, size_t length, char *error, size_t max);
int tiku_config_outbox_enroll(const char *dir, tiku_backend_t *backend,
                             char *error, size_t max);
int tiku_config_outbox_retry(const char *dir, tiku_backend_t *backend,
    const tiku_config_pending_t *displayed, char *error, size_t max);
/* Forget removes only the matching local record. It cannot cancel or undo a
 * device operation. The UI must explain this before asking for confirmation. */
int tiku_config_outbox_forget(const char *dir, tiku_backend_t *backend,
    const tiku_config_pending_t *displayed, char *error, size_t max);
#endif
