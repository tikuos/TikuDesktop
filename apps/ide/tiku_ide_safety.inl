/* TikuBasic/TikuC: explicit destinations and separate crash snapshots. */

static void search_paint(edit_t *e);

static int
document_fits(const char *text)
{
    int lines = 1, col = 0;

    for (; *text; text++) {
        unsigned char ch = (unsigned char)*text;
        if ((ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') || ch == 127) {
            return 0;
        }
        if (*text == '\n') { col = 0; if (text[1]) { lines++; } }
        else { col++; }
        if (lines > TIKU_TEXTVIEW_LINES_MAX ||
            col >= TIKU_TEXTVIEW_LINE_MAX) { return 0; }
    }
    return 1;
}

static int
board_connected(ide_t *st, const char *board)
{
    char names[4096], *p;
    size_t n = strlen(board);

    if (st->services->board_names == NULL) {
        return 1; /* Older hosts do not expose connection state. */
    }
    names[0] = '\0';
    (void)st->services->board_names(st->services->ctx, names, sizeof names);
    names[sizeof names - 1] = '\0';
    for (p = names; *p; ) {
        char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if (len == n && memcmp(p, board, n) == 0) { return 1; }
        if (!end) { break; }
        p = end + 1;
    }
    return 0;
}

static void
target_text(ide_t *st, char *out, size_t max)
{
    char board[64];

    if (st->running) {
        snprintf(out, max, "%s: %s", st->debugging && st->debug_paused ? "Debug paused" : st->saving ? "Saving" :
                 st->asking ? "Waiting for input" : "Running",
                 st->board[0] ? st->board : "This computer");
    } else if (dest_board(st, board, sizeof board)) {
        snprintf(out, max, "Run on: %s — %s", board,
                 board_connected(st, board) ? "Ready" : "Disconnected");
    } else {
        snprintf(out, max, "Run on: This computer — no default device");
    }
}

static void
target_paint(ide_t *st, edit_t *e)
{
    char label[160];
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t r = { 0, TOOLBAR_H, e->w, TARGET_H };

    target_text(st, label, sizeof label);
    while (strlen(label) && tiku_text_width(f, label) > e->w - 12) {
        label[strlen(label) - 1] = '\0';
    }
    tiku_fill(e->surface, r, TIKU_C_PANEL);
    tiku_text(e->surface, f, 6, r.y + f->ascent + 4, label, TIKU_C_TEXT);
}

/** Reject, never truncate, data that the board service cannot carry. */
static int
upload_read(ide_t *st, const char *path, char *out, size_t max)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    int extra, bad;

    if (!f) {
        snprintf(st->note, sizeof st->note, "Cannot read the program");
        return 0;
    }
    n = fread(out, 1, max - 1, f);
    extra = fgetc(f);
    bad = ferror(f);
    if (fclose(f) != 0) { bad = 1; }
    if (bad || extra != EOF || memchr(out, '\0', n)) {
        snprintf(st->note, sizeof st->note, "%s", bad ? "Program read failed" :
                 extra != EOF ? "Upload too large (maximum 8191 bytes)" :
                 "Upload rejected: program contains a NUL byte");
        return 0;
    }
    out[n] = '\0';
    return 1;
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

/** Atomic private writes; a failed checkpoint leaves the last one intact. */
static int
recovery_write(edit_t *e)
{
    char temp[680];
    FILE *f;
    int fd, i, ok;
    uint64_t stamp = 14695981039346656037ULL;

    if (!e->recovery[0] || e->recovery_fd < 0 || e->recovery_blocked) { return 0; }
    if (!tiku_textview_modified(&e->tv)) {
        e->recovery_current = 0;
        return unlink(e->recovery) == 0 || errno == ENOENT;
    }
    for (i = 0; i < tiku_textview_lines(&e->tv); i++) {
        const unsigned char *p = (const unsigned char *)tiku_textview_line(&e->tv, i);
        for (; *p; p++) { stamp = (stamp ^ *p) * 1099511628211ULL; }
        stamp = (stamp ^ '\n') * 1099511628211ULL;
    }
    if (e->recovery_current && stamp == e->recovery_stamp) { return 1; }
    snprintf(temp, sizeof temp, "%s.XXXXXX", e->recovery);
    fd = mkstemp(temp);
    if (fd < 0) { return 0; }
    f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temp); return 0; }
    ok = fprintf(f, "%s\n", e->path) >= 0;
    for (i = 0; i < tiku_textview_lines(&e->tv); i++) {
        if (fprintf(f, "%s\n",
                    tiku_textview_line(&e->tv, i)) < 0) { ok = 0; }
    }
    if (fflush(f) != 0 || fsync(fd) != 0) { ok = 0; }
    if (fclose(f) != 0) { ok = 0; }
    if (ok && rename(temp, e->recovery) == 0) {
        e->recovery_stamp = stamp;
        e->recovery_current = 1;
        return 1;
    }
    unlink(temp);
    return 0;
}

static void
recovery_restore(edit_t *e)
{
    char path[512];
    size_t limit = TIKU_TEXTVIEW_LINES_MAX * TIKU_TEXTVIEW_LINE_MAX;
    char *text = malloc(limit + 1);
    int fd = open(e->recovery, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    FILE *f = fd < 0 ? NULL : fdopen(fd, "r");
    size_t n;

    if (!f || !text) {
        if (!text || (fd < 0 && errno != ENOENT) || (fd >= 0 && !f)) {
            e->recovery_blocked = 1;
        }
        if (f) { fclose(f); } else if (fd >= 0) { close(fd); }
        free(text);
        return;
    }
    e->recovery_blocked = 1;
    if (!fgets(path, sizeof path, f)) { goto done; }
    path[strcspn(path, "\n")] = '\0';
    if (strcmp(path, e->path)) { goto done; }
    n = fread(text, 1, limit, f);
    if (ferror(f) || n == limit || memchr(text, '\0', n)) { goto done; }
    text[n] = '\0';
    if (!document_fits(text)) { goto done; }
    tiku_textview_set(&e->tv, text);
    e->recovery_blocked = 0;
    e->load_failed = 0;
    e->tv.saved_at = -1;
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

static void
recovery_open(edit_t *e)
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
    recovery_restore(e);
}

static void
recovery_release(edit_t *e, int discard)
{
    if (e->recovery[0] && e->recovery_fd >= 0) {
        char lock[680];

        if (discard) { unlink(e->recovery); }
        else { (void)recovery_write(e); }
        /* The sentinel goes with the lock it held, or the directory
         * gathers one dead file per document ever opened. */
        snprintf(lock, sizeof lock, "%s.lock", e->recovery);
        unlink(lock);
        close(e->recovery_fd);
    }
    e->recovery_fd = -1;
    e->recovery[0] = '\0';
}
