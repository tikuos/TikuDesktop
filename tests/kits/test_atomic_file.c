/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_atomic_file.c - a failed producer or close leaves the old file and
 * no staging file; a replacement keeps the mode, a new file takes the umask.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static int fail_close, closes;
static int checked_close(FILE *f)
{
    int rc = fclose(f);
    closes++;
    if (fail_close) { errno = EIO; return EOF; }
    return rc;
}
#define fclose checked_close
#include <tiku_atomic_file.c>
#undef fclose

static int produce(FILE *f, void *ctx)
{
    if (fwrite("replacement", 1, 11, f) != 11) return -1;
    return ctx ? -1 : 0;
}
static void expect(const char *path, const char *text)
{
    char buf[32] = {0};
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(fread(buf, 1, sizeof buf, f) == strlen(text));
    assert(!strcmp(buf, text));
    fclose(f);
}
int main(void)
{
    char dir[] = "/tmp/tiku-atomic-XXXXXX", path[256], alias[256];
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/output", dir);
    snprintf(alias, sizeof alias, "%s/link", dir);
    FILE *f = fopen(path, "wb");
    assert(f && fputs("original", f) >= 0 && fclose(f) == 0);
    assert(chmod(path, 0640) == 0);
    assert(tiku_atomic_file(path, produce, (void *)1) == -1);
    assert(closes == 1); expect(path, "original");
    fail_close = 1; closes = 0;
    assert(tiku_atomic_file(path, produce, NULL) == -1);
    assert(closes == 1); expect(path, "original");
    fail_close = 0;
    assert(symlink(path, alias) == 0);
    assert(tiku_atomic_file(alias, produce, NULL) == -1);
    expect(path, "original");
    assert(tiku_atomic_file(path, produce, NULL) == 0);
    expect(path, "replacement");
    struct stat st;
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0640);
    snprintf(alias, sizeof alias, "%s/fresh", dir);
    umask(022);
    assert(tiku_atomic_file(alias, produce, NULL) == 0);
    expect(alias, "replacement");
    assert(stat(alias, &st) == 0 && (st.st_mode & 0777) == 0644);
    assert(unlink(alias) == 0);
    snprintf(alias, sizeof alias, "%s/link", dir);
    DIR *d = opendir(dir); struct dirent *e; int n = 0;
    assert(d);
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
    closedir(d); assert(n == 2);  /* no leaked staging files */
    assert(unlink(alias) == 0 && unlink(path) == 0 && rmdir(dir) == 0);
    puts("Atomic file: failed producer/close, single close, aliases, "
         "replacement, permissions PASS");
    return 0;
}
