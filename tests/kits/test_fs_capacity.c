/* Oversized requests must not start a partial filesystem job. */
#include "tiku_fs_private.h"
#include <assert.h>
int main(void)
{
    tiku_backend_t *backend = tiku_backend_posix_open();
    tiku_fs_t *fs = tiku_fs_new(backend);
    const char *paths[FS_QUEUE_MAX + 1];
    assert(backend && fs);
    for (int i = 0; i <= FS_QUEUE_MAX; i++) paths[i] = "/missing-audit-file";
    int result = tiku_fs_delete(fs, paths, FS_QUEUE_MAX + 1);
    int ok = result < 0 && !tiku_fs_busy(fs) && fs->qcount == 0;
    tiku_fs_free(fs); tiku_backend_close(backend);
    puts(ok ? "PASS D-139" : "FAIL D-139");
    return !ok;
}
