/* Application bundle metadata. No application code is executed to read it.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_MANIFEST_H
#define TIKU_MANIFEST_H
#include <stddef.h>

typedef struct {
    char id[64], name[96], module[64], category[32], icon[64];
    char types[256], startup[96], help[128];
} tiku_manifest_t;

/* Read a version-1 key=value manifest. Unknown, duplicate, overlong and
 * malformed fields fail closed; module is a leaf name, never a command. */
int tiku_manifest_read(const char *path, tiku_manifest_t *out,
                       char *error, size_t max);
#endif
