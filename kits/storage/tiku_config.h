/* TikuOS kits -- recoverable configuration snapshots over ordinary backends.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_CONFIG_H_
#define TIKU_CONFIG_H_
#include "tiku_model.h"

typedef struct {
    /* 1 managed, 0 absent/unsupported/unenrolled, -1 advertised but unreadable.
     * An error must NEVER downgrade a managed save to a legacy write. */
    int mode;
    int enrollable;                  /* advertised virgin storage, not legacy */
    uint32_t revision;
    char incarnation[33], state[12], desired[33];
} tiku_config_snapshot_t;

/* Read availability/incarnation without reading a resource or enrolling it. */
int tiku_config_probe(tiku_backend_t *, tiku_config_snapshot_t *);

/* Curated v1 resources: device name and CPU target frequency. Snapshot this
 * BEFORE displaying the value. No enrollment, writes or sensor acquisition. */
int tiku_config_read(tiku_backend_t *, const char *resource_path,
                     tiku_config_snapshot_t *);
/* Pure bounded wire codec, also used by protocol tests. */
int tiku_config_parse(const char *, tiku_config_snapshot_t *);
int tiku_config_request(const tiku_config_snapshot_t *, const uint8_t token[16],
                        const char *, size_t, char *out, size_t max);
/* Decode and verify an immutable checksummed request from the local outbox. */
int tiku_config_decode_request(const char *, tiku_config_snapshot_t *,
                               char token[33], char value[33]);
/* Obtain a nonzero OS-random token. Failure never substitutes a weak ID. */
int tiku_config_token(uint8_t out[16]);
/* Send exactly this already prepared request, without changing its revision. */
int tiku_config_send(tiku_backend_t *, const char *resource_path,
                     const char *request, char *error, size_t max);
/* Low-level, nonpersistent convenience API. Uses the ORIGINAL revision and
 * never downgrades. Interactive clients should use tiku_config_outbox_save()
 * to preserve the request token across a host crash and verify receipts. */
int tiku_config_write(tiku_backend_t *, const char *resource_path,
                      const tiku_config_snapshot_t *, const char *, size_t,
                      char *error, size_t max);
#endif
