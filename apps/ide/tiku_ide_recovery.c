/* tiku_ide_recovery.c -- recovery.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static int
recovery_dir(char *out, size_t max);

/** Reopen private snapshots; originals are never changed by recovery. */
void
tiku_ide_recovery_scan(ide_t *st)
{
    char dir[560], file[840], lock[860];
    DIR *d;
    struct dirent *entry;

    if (!recovery_dir(dir, sizeof dir) || !(d = opendir(dir))) { return; }
    while ((entry = readdir(d)) != NULL) {
        prow_t row = {0};
        FILE *f;
        const char *leaf;
        size_t n = strlen(entry->d_name);
        int i, fd, free_slot = 0;

        if (n < 6 || strcmp(entry->d_name + n - 6, ".draft")) { continue; }
        for (i = 0; i < IDE_EDITS; i++) { free_slot |= !st->edit[i].surface; }
        if (!free_slot) {
            snprintf(st->note, sizeof st->note,
                     "More recovery files: close a file, then Recover unsaved files");
            tiku_ide_project_paint(st);
            break;
        }
        snprintf(file, sizeof file, "%s/%s", dir, entry->d_name);
        snprintf(lock, sizeof lock, "%s.lock", file);
        fd = open(lock, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) { continue; }
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); continue; }
        close(fd);
        fd = open(file, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        f = fd < 0 ? NULL : fdopen(fd, "r");
        if (!f && fd >= 0) { close(fd); }
        if (!f) { continue; }
        if (!fgets(row.path, sizeof row.path, f)) { row.path[0] = '\0'; }
        fclose(f);
        row.path[strcspn(row.path, "\n")] = '\0';
        if (row.path[0] != '/' || tiku_ide_edit_of_path(st, row.path)) { continue; }
        leaf = strrchr(row.path, '/');
        snprintf(row.name, sizeof row.name, "%s", leaf + 1);
        tiku_ide_edit_open(st, &row);
    }
    closedir(d);
}

static int
recovery_dir(char *out, size_t max)
{
    const char *home_dir = getenv("HOME");
    const char *custom_dir = getenv("TIKU_IDE_RECOVERY_DIR");
    struct stat sb;
    int n;

    if (custom_dir && custom_dir[0] == '/') {
        n = snprintf(out, max, "%s", custom_dir);
    } else {
        if (!home_dir || home_dir[0] != '/') { return 0; }
        n = snprintf(out, max, "%s/.%s-recovery", home_dir, APP_NAME);
    }
    if (n < 0 || (size_t)n >= max) { return 0; }
    if (mkdir(out, 0700) != 0 && errno != EEXIST) { return 0; }
    return lstat(out, &sb) == 0 && S_ISDIR(sb.st_mode) &&
           sb.st_uid == getuid() && !(sb.st_mode & 0077);
}

/** @brief Write a run of the page's bytes to the FILE @p ctx. */
static int
draft_run(void *ctx, const char *bytes, size_t n)
{
    return fwrite(bytes, 1, n, ctx) != n;
}

/** Atomic private writes; a failed checkpoint leaves the last one intact.
 *  The draft is the page's path on a line, then every byte of its text. */
int
tiku_ide_recovery_write(edit_t *e)
{
    char temp[680];
    FILE *f;
    int fd, ok;
    uint64_t state;

    if (!e->recovery[0] || e->recovery_fd < 0 || e->recovery_blocked) { return 0; }
    if (!tiku_editdoc_modified(e->doc)) {
        e->recovery_current = 0;
        return unlink(e->recovery) == 0 || errno == ENOENT;
    }
    state = tiku_editdoc_state(e->doc);
    if (e->recovery_current && state == e->recovery_state) { return 1; }
    snprintf(temp, sizeof temp, "%s.XXXXXX", e->recovery);
    fd = mkstemp(temp);
    if (fd < 0) { return 0; }
    f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temp); return 0; }
    ok = fprintf(f, "%s\n", e->path) >= 0;
    {
        tiku_edit_range_t all = { 0, tiku_editdoc_length(e->doc) };

        if (tiku_editdoc_each(e->doc, all, draft_run, f) != TIKU_EDIT_OK ||
            ferror(f)) { ok = 0; }
    }
    if (fflush(f) != 0 || fsync(fd) != 0) { ok = 0; }
    if (fclose(f) != 0) { ok = 0; }
    if (ok && rename(temp, e->recovery) == 0) {
        e->recovery_state = state;
        e->recovery_current = 1;
        return 1;
    }
    unlink(temp);
    return 0;
}

/** Restore the draft over the page, which then reads modified; a draft
 *  for another path, or one the page cannot hold, is kept unread. */
void
tiku_ide_recovery_restore(edit_t *e)
{
    char path[512];
    char *text = NULL;
    int fd = open(e->recovery, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    FILE *f = fd < 0 ? NULL : fdopen(fd, "r");
    size_t n = 0, cap = 0;

    if (!f) {
        if ((fd < 0 && errno != ENOENT) || (fd >= 0 && !f)) {
            e->recovery_blocked = 1;
        }
        if (fd >= 0) { close(fd); }
        return;
    }
    e->recovery_blocked = 1;
    if (!fgets(path, sizeof path, f)) { goto done; }
    path[strcspn(path, "\n")] = '\0';
    if (strcmp(path, e->path)) { goto done; }
    for (;;) {
        size_t got;

        if (n == cap) {
            char *grown;

            if (cap > (size_t)INT_MAX ||
                (grown = realloc(text, cap ? cap * 2 : 65536)) == NULL) {
                goto done;
            }
            text = grown;
            cap = cap ? cap * 2 : 65536;
        }
        got = fread(text + n, 1, cap - n, f);
        n += got;
        if (got == 0) { break; }
    }
    if (ferror(f) || (n > 0 && memchr(text, '\0', n))) { goto done; }
    if (tiku_ide_text_load(e, text != NULL ? text : "", n) != TIKU_EDIT_OK) {
        goto done;
    }
    tiku_editdoc_saved(e->doc, 0);
    e->recovery_blocked = 0;
    e->load_failed = 0;
    snprintf(e->feedback, sizeof e->feedback,
             "Recovered unsaved work — review, then Save. Original unchanged.");
done:
    if (e->recovery_blocked) {
        snprintf(e->feedback, sizeof e->feedback,
                 "Unreadable recovery snapshot preserved; original opened");
    }
    fclose(f);
    free(text);
}

void
tiku_ide_recovery_open(edit_t *e)
{
    char dir[560], lock[680], absolute[512];
    unsigned long long hash = 14695981039346656037ULL;
    const unsigned char *p;

    e->recovery_fd = -1;
    if (e->path[0] != '/') {
        if (!getcwd(absolute, sizeof absolute)) { return; }
        if (strlen(absolute) + strlen(e->path) + 2 > sizeof absolute) { return; }
        strcat(absolute, "/");
        strcat(absolute, e->path);
        snprintf(e->path, sizeof e->path, "%s", absolute);
    }
    if (!recovery_dir(dir, sizeof dir) || strchr(e->path, '\n')) { return; }
    for (p = (const unsigned char *)e->path; *p; p++) {
        hash = (hash ^ *p) * 1099511628211ULL;
    }
    snprintf(e->recovery, sizeof e->recovery, "%s/%016llx.draft", dir, hash);
    snprintf(lock, sizeof lock, "%s.lock", e->recovery);
    e->recovery_fd = open(lock, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (e->recovery_fd < 0 || flock(e->recovery_fd, LOCK_EX | LOCK_NB) != 0) {
        if (e->recovery_fd >= 0) { close(e->recovery_fd); }
        e->recovery_fd = -1;
        e->recovery[0] = '\0';
        snprintf(e->feedback, sizeof e->feedback,
                 "Recovery unavailable: file may be open in another instance");
        return;
    }
    tiku_ide_recovery_restore(e);
}

void
tiku_ide_recovery_release(edit_t *e, int discard)
{
    if (e->recovery[0] && e->recovery_fd >= 0) {
        char lock[680];

        if (discard) { unlink(e->recovery); }
        else { (void)tiku_ide_recovery_write(e); }
        /* The sentinel goes with the lock it held, or the directory
         * gathers one dead file per document ever opened. */
        snprintf(lock, sizeof lock, "%s.lock", e->recovery);
        unlink(lock);
        close(e->recovery_fd);
    }
    e->recovery_fd = -1;
    e->recovery[0] = '\0';
}
