/* Persistent, bundle-scoped bookmarks. Files are written only on explicit edits. */

/** @brief Keep bookmarks for different documentation bundles separate. */
static int reader_bookmark_path(tiku_trk_help_window_t *c, char *out, size_t cap)
{
    const char *custom = getenv(c->document_mode ? "TIKU_READER_BOOKMARKS" : "TIKU_HELP_BOOKMARKS");
    if (custom) return *custom && snprintf(out, cap, "%s", custom) < (int)cap;
    char config[TIKU_PATH_MAX], root[TIKU_PATH_MAX];
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    int n = xdg && *xdg ? snprintf(config, sizeof config, "%s", xdg) :
        snprintf(config, sizeof config, "%s/.config", home ? home : "");
    if (n >= (int)sizeof config || ((!xdg || !*xdg) && (!home || !*home))) return 0;
    if (c->document_mode)
        return snprintf(out, cap, "%s/TikuReaderBookmarks", config) < (int)cap;
    if (!reader_realpath(c->root, root, sizeof root)) snprintf(root, sizeof root, "%s", c->root);
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)root; *p; p++) hash = (hash ^ *p) * 16777619u;
    return snprintf(out, cap, "%s/TikuHelpBookmarks-%08x", config, hash) < (int)cap;
}

/** @brief Load a bounded text file without changing any files on window startup. */
static void reader_bookmarks_load(tiku_trk_help_window_t *c)
{
    char path[TIKU_PATH_MAX], line[TIKU_PATH_MAX + 256];
    if (!reader_bookmark_path(c, path, sizeof path)) return;
    FILE *f = fopen(path, "r"); if (!f) return;
    for (int lines = 0; lines < 32 && fgets(line, sizeof line, f); lines++) {
        char *page = strchr(line, '\t'); if (!page) continue;
        *page++ = 0; char *title = strchr(page, '\t'); if (!title) continue;
        *title++ = 0; title[strcspn(title, "\r\n")] = 0;
        char *end; long block = strtol(line, &end, 10);
        if (*end || block < 0 || block >= 32 * 1024 * 1024 ||
            !*page || *page == '@' || strlen(page) >= TIKU_PATH_MAX) continue;
        if (c->document_mode ? (*page != '/' || !tiku_trk_document_file_supported(page)) :
            (*page == '/' || strstr(page, "..") != NULL)) continue;
        help_row_t *row = &c->reader->marks[c->reader->nmarks++];
        strcpy(row->page, page); row->block = (int)block;
        snprintf(row->title, sizeof row->title, "%.127s", title);
    }
    fclose(f);
}

/** @brief Replace atomically; a failed save leaves the previous file intact. */
static void reader_bookmarks_save(tiku_trk_help_window_t *c)
{
    char path[TIKU_PATH_MAX], temp[TIKU_PATH_MAX + 16];
    if (!reader_bookmark_path(c, path, sizeof path)) return;
    snprintf(temp, sizeof temp, "%s.XXXXXX", path);
    int fd = mkstemp(temp); if (fd < 0) { say(c, "Bookmark retained for this session; preferences folder is not writable."); return; }
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temp); return; }
    int ok = 1;
    for (int i = 0; i < c->reader->nmarks; i++) {
        help_row_t *row = &c->reader->marks[i]; char title[128];
        snprintf(title, sizeof title, "%s", row->title);
        for (char *p = title; *p; p++) if ((unsigned char)*p < 32) *p = ' ';
        if (fprintf(f, "%d\t%s\t%s\n", row->block, row->page, title) < 0) ok = 0;
    }
    if (fflush(f) || fsync(fd)) ok = 0;
    if (fclose(f)) ok = 0;
    if (!ok || rename(temp, path)) {
        unlink(temp); say(c, "Bookmark retained for this session; saving preferences failed.");
    }
}
