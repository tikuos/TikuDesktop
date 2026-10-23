/*
 * TikuDesktop Storage Kit tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_atomic_guard.c - pre-replacement conflicts and post-rename uncertainty.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int sync_calls, fail_sync, check_calls, reject_check, produced;

/** @brief Fail a selected file/directory sync without changing other I/O. */
static int
checked_sync(int fd)
{
    if (++sync_calls == fail_sync) { errno = EIO; return -1; }
    return fsync(fd);
}
#define fsync checked_sync
#include <tiku_atomic_file.c>
#undef fsync

/** @brief Track guard calls and reject at an explicitly selected boundary. */
static int
guard(void *context)
{
    (void)context;
    return ++check_calls == reject_check;
}
/** @brief Produce complete replacement bytes or inject a producer refusal. */
static int
produce(FILE *file, void *context)
{
    produced++;
    assert(fputs("replacement", file) >= 0);
    return context ? -1 : 0;
}
/** @brief Compare the actual file contents without relying on a saved-state flag. */
static void
expect(const char *path, const char *text)
{
    char buffer[64] = {0};
    FILE *file = fopen(path, "rb");

    assert(file);
    assert(fread(buffer, 1, sizeof buffer, file) == strlen(text));
    assert(!strcmp(buffer, text));
    assert(!fclose(file));
}
/** @brief Reset injected operation outcomes between cases. */
static void
reset(void)
{
    sync_calls = fail_sync = check_calls = reject_check = produced = 0;
}
/** @brief Validate the two guard boundaries and truthful replacement outcomes. */
int
main(void)
{
    char path[] = "/tmp/tiku-guard-XXXXXX";
    int fd = mkstemp(path);
    struct stat st;

    assert(fd >= 0 && write(fd, "original", 8) == 8 && !close(fd));
    reject_check = 1;
    assert(tiku_atomic_file_guarded(path, produce, guard, NULL) ==
           TIKU_ATOMIC_CONFLICT);
    assert(!produced);
    expect(path, "original");
    reset();
    reject_check = 2;
    assert(tiku_atomic_file_guarded(path, produce, guard, NULL) ==
           TIKU_ATOMIC_CONFLICT);
    assert(produced == 1 && check_calls == 2);
    expect(path, "original");
    reset();
    fail_sync = 1;
    assert(tiku_atomic_file_guarded(path, produce, guard, NULL) ==
           TIKU_ATOMIC_FAILED);
    expect(path, "original");
    reset();
    fail_sync = 2;
    assert(tiku_atomic_file_guarded(path, produce, guard, NULL) ==
           TIKU_ATOMIC_UNCERTAIN);
    expect(path, "replacement");
    reset();
    assert(tiku_atomic_file_guarded(path, produce, guard, (void *)1) ==
           TIKU_ATOMIC_FAILED);
    expect(path, "replacement");
    assert(!unlink(path));
    reset();
    assert(tiku_atomic_file_guarded(path, produce, guard, NULL) == TIKU_ATOMIC_OK);
    assert(!stat(path, &st) && (st.st_mode & 0777) == 0600);
    expect(path, "replacement");
    assert(!unlink(path));
    puts("Atomic guard: before/after production conflicts, producer/sync "
         "refusals, post-rename uncertainty and private new files PASS");
    return 0;
}
