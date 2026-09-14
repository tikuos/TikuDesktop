/* Isolated VFS access, without a board or a second serial owner.
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_vfs_rpc.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char value[64] = "alpha\n";
static int writes, readonly;
static atomic_int finished;
static tiku_backend_t *proxy;

static int stat_node(tiku_backend_t *b, const char *path, tiku_model_t *m)
{
    (void)b; (void)path; memset(m, 0, sizeof *m);
    strcpy(m->name, "name"); m->kind = TIKU_KIND_DEVICE_NODE;
    m->facts.perm = TIKU_P_READ | (readonly ? 0 : TIKU_P_WRITE);
    return 0;
}

static int read_node(tiku_backend_t *b, const char *path, void *out, size_t max)
{
    (void)b; (void)path;
    size_t n = strlen(value); assert(n < max);
    memcpy(out, value, n); return (int)n;
}

static int write_node(tiku_backend_t *b, const char *path, const void *data,
                      size_t n, char *error, size_t max)
{
    (void)b; (void)path; (void)error; (void)max;
    assert(n < sizeof value - 1); memcpy(value, data, n); value[n] = 0;
    writes++; return 0;
}

static int list_nodes(tiku_backend_t *b, const char *path, tiku_entry_fn fn, void *ctx)
{
    tiku_model_t m;
    for (int i = 0; i < 140; i++) {
        stat_node(b, path, &m); snprintf(m.name, sizeof m.name, "node-%d", i);
        if (fn(&m, ctx)) return i + 1;
    }
    return 140;
}

static const tiku_backend_ops_t ops = {
    .name = "fake", .stat = stat_node, .read = read_node,
    .write = write_node, .list = list_nodes
};
static tiku_backend_t board = {.ops = &ops, .devid = "uid-board-one"};

static tiku_msg_t *request(const char *op, const char *path, const char *key)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_VFS_REQUEST); assert(m);
    assert(tiku_msg_add_int32(m, "version", 1));
    assert(tiku_msg_add_int32(m, "sequence", 3));
    assert(tiku_msg_add_string(m, "op", op));
    assert(tiku_msg_add_string(m, "path", path));
    assert(tiku_msg_add_string(m, "key", key));
    return m;
}

static int status(tiku_msg_t *m)
{
    int32_t ok = 0; assert(m);
    assert(tiku_msg_find_int32(m, "ok", 0, &ok));
    tiku_msg_free(m); return ok;
}

static int send_request(void *ctx, const tiku_msg_t *m)
{
    (void)ctx;
    tiku_msg_t *reply = tiku_vfs_serve(&board, m); assert(reply);
    /* An old answer must not wake a newer call, even in the same process. */
    int32_t seq = 0; tiku_msg_find_int32(reply, "sequence", 0, &seq);
    tiku_msg_t *old = tiku_msg_new(TIKU_MSG_VFS_REPLY);
    tiku_msg_add_int32(old, "sequence", seq - 1);
    tiku_vfs_proxy_reply(proxy, old); tiku_msg_free(old);
    tiku_vfs_proxy_reply(proxy, reply); tiku_msg_free(reply);
    return 1;
}

static void *round_trip(void *unused)
{
    (void)unused;
    char bytes[128], error[128];
    tiku_model_t model;
    assert(!proxy->ops->stat(proxy, "/sys/device/name", &model));
    assert(model.facts.perm & TIKU_P_WRITE);
    assert(proxy->ops->read(proxy, "/sys/device/name", bytes, sizeof bytes) == 6);
    assert(!memcmp(bytes, "alpha\n", 6));
    assert(!proxy->ops->write(proxy, "/sys/device/name", "beta", 4, error, sizeof error));
    assert(proxy->ops->read(proxy, "/sys/device/name", bytes, sizeof bytes) == 4);
    assert(!memcmp(bytes, "beta", 4));
    atomic_store(&finished, 1); return NULL;
}

static void *cancelled_read(void *unused)
{
    (void)unused; char data[32];
    assert(proxy->ops->read(proxy, "/sys/uptime", data, sizeof data) < 0);
    atomic_store(&finished, 1); return NULL;
}

int main(void)
{
    tiku_msg_t *m = request("write", "/sys/device/name", board.devid);
    tiku_msg_add_blob(m, "data", "beta", 4);
    tiku_msg_add_blob(m, "expected", "stale", 5);
    assert(!status(tiku_vfs_serve(&board, m)) && !writes); tiku_msg_free(m);
    m = request("write", "/sys/device/name", "uid-other-board");
    tiku_msg_add_blob(m, "data", "beta", 4);
    tiku_msg_add_blob(m, "expected", value, strlen(value));
    assert(!status(tiku_vfs_serve(&board, m)) && !writes); tiku_msg_free(m);
    m = request("write", "/sys/device/name", board.devid);
    tiku_msg_add_blob(m, "data", "beta", 4);
    tiku_msg_add_blob(m, "expected", value, strlen(value));
    readonly = 1;
    assert(!status(tiku_vfs_serve(&board, m)) && !writes);
    readonly = 0; tiku_msg_free(m);
    m = request("read", "/sys/../data/file", "");
    assert(!status(tiku_vfs_serve(&board, m))); tiku_msg_free(m);
    m = request("read", "/sys/uptime", "");
    assert(!status(tiku_vfs_serve(NULL, m))); tiku_msg_free(m);
    m = request("list", "/sys", "");
    tiku_msg_t *r = tiku_vfs_serve(&board, m); int32_t count = 0;
    assert(tiku_msg_find_int32(r, "count", 0, &count) && count == 129);
    assert(status(r)); tiku_msg_free(m);
    proxy = tiku_vfs_proxy_new("board"); assert(proxy);
    pthread_t thread; assert(!pthread_create(&thread, NULL, round_trip, NULL));
    for (int i = 0; !atomic_load(&finished) && i < 10000; i++) {
        tiku_vfs_proxy_pump(proxy, send_request, NULL); usleep(1000);
    }
    assert(atomic_load(&finished)); pthread_join(thread, NULL);
    assert(writes == 1); tiku_backend_close(proxy);
    proxy = tiku_vfs_proxy_new("board"); assert(proxy);
    atomic_store(&finished, 0);
    assert(!pthread_create(&thread, NULL, cancelled_read, NULL));
    tiku_vfs_proxy_cancel(proxy); pthread_join(thread, NULL);
    assert(atomic_load(&finished)); tiku_backend_close(proxy);
    puts("PASS: VFS RPC, permissions, identity binding, atomic compare/write, bounds, cancellation");
    return 0;
}
