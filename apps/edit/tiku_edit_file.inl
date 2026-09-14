/* Lossless, bounded text loading and same-directory atomic saves.
 * SPDX-License-Identifier: Apache-2.0 */

/** Compare the identity and modification stamp observed when loading. */
static int same_disk(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
        a->st_size == b->st_size && a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
        a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
        a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}

/** Build a separate document, retaining a final empty line for a final LF. */
static int read_document(edit_state_t *st, const char *path)
{
    int fd = -1, ok = 0, crlf = 0, lf = 0, bom = 0;
    struct stat info, after;
    char *bytes = NULL;
    edit_buffer_t *next = NULL;
    const char *error = "Cannot read file; the current document was kept.";
    if (strlen(path) >= sizeof st->path) goto done;
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &info) || !S_ISREG(info.st_mode)) goto done;
    error = "File exceeds this editor's addressable range. Nothing was truncated.";
    if (info.st_size < 0 || (uintmax_t)info.st_size > INT_MAX - 1u) goto done;
    size_t n = (size_t)info.st_size, used = 0;
    bytes = malloc(n + 1);
    next = calloc(1, sizeof *next);
    error = "Not enough memory; the current document was kept.";
    if (!bytes || !next) goto done;
    error = "Read failed or file changed while reading. Please reopen it.";
    while (used < n) {
        ssize_t got = read(fd, bytes + used, n - used);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) goto done;
        used += (size_t)got;
    }
    if (fstat(fd, &after) || !same_disk(&info, &after)) goto done;
    bytes[n] = 0;
    error = "Not a UTF-8 text file. Use TikuHex for binary data.";
    if (memchr(bytes, 0, n)) goto done;
    bom = n >= 3 && !memcmp(bytes, "\xef\xbb\xbf", 3);
    size_t out = 0;
    for (size_t i = bom ? 3 : 0; i < n; i++) {
        if (bytes[i] == '\r' && i + 1 < n && bytes[i + 1] == '\n') {
            crlf++; continue;
        }
        if (bytes[i] == '\n' && (!i || bytes[i - 1] != '\r')) lf++;
        bytes[out++] = bytes[i];
    }
    bytes[out] = 0;
    error = "Mixed LF/CRLF endings; normalize them before editing here.";
    if (crlf && lf) goto done;
    error = "Invalid UTF-8 or not enough memory; the current document was kept.";
    if (!edit_buffer_set(next, bytes)) goto done;
    edit_buffer_free(&st->tv);
    st->tv = *next;
    free(next); next = NULL;
    st->crlf = crlf != 0; st->bom = bom;
    st->disk = info; st->disk_known = 1; st->load_failed = 0;
    st->left = 0;
    st->width_dirty = 1;
    if (strcmp(st->path, path)) snprintf(st->path, sizeof st->path, "%s", path);
    st->lang = tiku_syntax_of_path(path);
    snprintf(st->note, sizeof st->note, "Opened %zu bytes", n);
    ok = 1;
done:
    if (!ok) snprintf(st->note, sizeof st->note, "%s", error);
    if (fd >= 0) close(fd);
    if (next) { edit_buffer_free(next); free(next); }
    free(bytes);
    return ok;
}

/** Empty launches stay empty; a failed launch cannot overwrite its source. */
static void load(edit_state_t *st)
{
    if (!st->path[0]) return;
    char path[sizeof st->path];
    snprintf(path, sizeof path, "%s", st->path);
    if (!read_document(st, path)) st->load_failed = 1;
}

/** Serialize exactly, including BOM, newline style and final-newline state. */
static int write_document(edit_state_t *st, FILE *f)
{
    if (st->bom && fwrite("\xef\xbb\xbf", 1, 3, f) != 3) return 0;
    for (int i = 0; i < st->tv.nline; i++) {
        if (i && fputs(st->crlf ? "\r\n" : "\n", f) < 0) return 0;
        size_t n = (size_t)st->tv.line[i].len;
        if (n && fwrite(st->tv.line[i].text, 1, n, f) != n) return 0;
    }
    return !ferror(f);
}

/** Replace only after a complete flushed write; never follow an output link. */
static int save_document(edit_state_t *st, const char *path)
{
    struct stat before, after;
    char temporary[560];
    int exists, fd, ok;
    if (!path[0] || strlen(path) >= sizeof st->path) return 0;
    int original = !strcmp(path, st->path);
    if (original && st->load_failed) {
        snprintf(st->note, sizeof st->note, "Source was not loaded. Use Save As to a different file.");
        return 0;
    }
    exists = lstat(path, &before) == 0;
    if ((!exists && errno != ENOENT) ||
        (exists && (!S_ISREG(before.st_mode) || before.st_nlink != 1))) {
        snprintf(st->note, sizeof st->note, "Save refused: use a regular file, not a symbolic/hard link.");
        return 0;
    }
    if (exists && (!(before.st_mode & 0222) || access(path, W_OK))) {
        snprintf(st->note, sizeof st->note, "File is read-only. Use Save As to save a copy.");
        return 0;
    }
    if (original && st->disk_known &&
        (!exists || !same_disk(&st->disk, &before))) {
        snprintf(st->note, sizeof st->note, "File changed on disk. Reopen it or Save As another file.");
        return 0;
    }
    snprintf(temporary, sizeof temporary, "%s.edit-XXXXXX", path);
    fd = mkstemp(temporary);
    if (fd < 0) goto failed;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(temporary); goto failed; }
    ok = (!exists || !fchmod(fd, before.st_mode & 0777)) &&
         write_document(st, f) && !fflush(f) && !fsync(fd);
    if (fclose(f)) ok = 0;
    int present = lstat(path, &after) == 0;
    if (present != exists || (present && !same_disk(&before, &after)) ||
        (!present && errno != ENOENT)) ok = 0;
    if (!ok || rename(temporary, path)) {
        unlink(temporary); goto failed;
    }
    if (!original) {
        snprintf(st->path, sizeof st->path, "%s", path);
        st->lang = tiku_syntax_of_path(path);
    }
    st->disk_known = !stat(path, &st->disk);
    st->load_failed = 0;
    edit_buffer_saved(&st->tv);
    snprintf(st->note, sizeof st->note, "Saved");
    return 1;
failed:
    snprintf(st->note, sizeof st->note, "Save failed; the existing file was not replaced.");
    return 0;
}

/** Save to the current document's name. */
static int save(edit_state_t *st)
{ return save_document(st, st->path); }
