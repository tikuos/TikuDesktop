/* Cache validity follows the source captured before decoding, and a
 * freshness check reads the recorded stamp without touching the file. */
#include "tiku_thumb.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __linux__
/* The linker routes stat() here (--wrap=stat in mk/tests.mk), so the test
 * can count the calls a freshness check makes. */
static int stat_calls;

int __real_stat(const char *path, struct stat *st);

int
__wrap_stat(const char *path, struct stat *st)
{
    stat_calls++;
    return __real_stat(path, st);
}
#endif

static int
check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    return !ok;
}

int main(void)
{
    char path[] = "/tmp/tiku-thumb-fresh-XXXXXX", active[TIKU_PATH_MAX];
    int fd = mkstemp(path), fails = 0;
    assert(fd >= 0 && write(fd, "a", 1) == 1);
    tiku_thumb_q_t *q = calloc(1, sizeof *q);
    assert(q);
    tiku_thumb_init(q);
    /* Each round lands into an empty store, so a stamp can only be the
     * one that round's landing wrote. */
    for (int changed = 0; changed < 2; changed++) {
        tiku_store_t *store = tiku_store_memory_open();
        tiku_image_t thumb = {0}; struct stat st;
        assert(store);
        thumb.w = thumb.h = TIKU_THUMB_SIZE;
        thumb.px = calloc(TIKU_THUMB_SIZE * TIKU_THUMB_SIZE, 4);
        assert(thumb.px);
        assert(tiku_thumb_queue(q, path, 1, 128) == 1);
        assert(tiku_thumb_take(q, active, sizeof active) == 1);
        if (changed) assert(write(fd, "b", 1) == 1);
        tiku_thumb_landed(q, store, path, 1, 1, &thumb);
        assert(stat(path, &st) == 0);
        if (!changed) {
            /* The stamp is the file's mtime: a model that is behind the
             * file still has a fresh thumbnail, a model ahead of the
             * stamp (the file was saved again) does not. */
            fails += check(tiku_thumb_fresh(store, path, st.st_mtime),
                           "an unchanged file keeps its thumbnail");
            fails += check(tiku_thumb_fresh(store, path, st.st_mtime - 1),
                           "a model whose mtime is behind the stamp is "
                           "still fresh");
            fails += check(!tiku_thumb_fresh(store, path,
                                             st.st_mtime + 1),
                           "a model past the stamp is stale");
#ifdef __linux__
            stat_calls = 0;
            (void)tiku_thumb_fresh(store, path, st.st_mtime);
            (void)tiku_thumb_fresh(store, path, st.st_mtime - 1);
            fails += check(stat_calls == 0,
                           "a freshness check makes no stat() call");
#endif
        } else {
            fails += check(!tiku_thumb_fresh(store, path, st.st_mtime) &&
                           !tiku_thumb_fresh(store, path, st.st_mtime - 1),
                           "a change between take and landing stores no "
                           "stamp");
        }
        tiku_store_free(store);
    }
    close(fd); unlink(path); free(q);
    puts(fails ? "FAIL D-090" : "PASS D-090");
    return !!fails;
}
