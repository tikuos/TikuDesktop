/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs.h - the node and dynamic-children shapes the board library fills.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STUB_VFS_H_
#define TIKU_STUB_VFS_H_

#include <stddef.h>
#include <stdint.h>

typedef enum { TIKU_VFS_DIR = 0, TIKU_VFS_FILE } tiku_vfs_type_t;
typedef int (*tiku_vfs_read_fn)(char *buf, size_t max);
typedef int (*tiku_vfs_write_fn)(const char *buf, size_t len);
typedef void (*tiku_vfs_dyn_list_cb)(const char *name, void *ctx);
typedef struct tiku_vfs_desc tiku_vfs_desc_t;
typedef unsigned char tiku_vfs_cap_t;

typedef struct tiku_vfs_dynops {
    void (*list)(tiku_vfs_dyn_list_cb cb, void *ctx);
    int  (*read)(const char *name, char *buf, size_t max);
    int  (*write)(const char *name, const char *buf, size_t len);
    int  (*unlink)(const char *name);
    void (*list_dir)(const char *prefix, tiku_vfs_dyn_list_cb cb, void *ctx);
} tiku_vfs_dynops_t;

typedef struct tiku_vfs_node {
    const char                   *name;
    tiku_vfs_type_t               type;
    tiku_vfs_read_fn              read;
    tiku_vfs_write_fn             write;
    const struct tiku_vfs_node   *children;
    uint8_t                       child_count;
    const tiku_vfs_desc_t        *desc;
    const struct tiku_vfs_dynops *dyn;
    tiku_vfs_cap_t                req_cap;
} tiku_vfs_node_t;

/** @brief The test counts notifies and answers resolve with a node. */
void tiku_vfs_notify(const tiku_vfs_node_t *node);
const tiku_vfs_node_t *tiku_vfs_resolve(const char *path);

#endif /* TIKU_STUB_VFS_H_ */
