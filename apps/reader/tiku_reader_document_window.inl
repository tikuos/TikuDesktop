/* TikuReader shares rendering and reading tools, but not Help's navigation policy. */

static void document_menus(tiku_reader_help_window_t *c);

/** @brief Document bookmarks have their own persistent namespace. */
void tiku_reader_document_window_init(tiku_reader_help_window_t *c,
    tiku_workspace_t *workspace, int screen_w, int screen_h, int *dirty,
    void (*choose_file)(void *ctx), void *ctx)
{
    tiku_reader_help_window_init(c, workspace, "", screen_w, screen_h, dirty);
    c->document_mode = 1;
    (void)tiku_icons_tiku_register();
    c->choose_file = choose_file; c->choose_ctx = ctx;
    if (c->reader) {
        c->reader->nmarks = 0;
        reader_bookmarks_load(c);
    }
}

/** @brief Maximize within the desk's usable area; second click restores exactly. */
int tiku_reader_document_window_zoom(tiku_reader_help_window_t *c, tiku_rect_t area)
{
    if (!c || !c->document_mode || !c->reader || !c->window ||
        !c->window->open || area.w < 180 || area.h < 120) return 0;
    tiku_window_t *w = c->window;
    tiku_rect_t z = c->reader->zoom_frame;
    if (w->zoom_valid && w->frame.x == z.x && w->frame.y == z.y &&
        w->frame.w == z.w && w->frame.h == z.h) {
        w->frame = w->saved_zoom; w->zoom_valid = 0;
    } else {
        w->saved_zoom = w->frame; w->zoom_valid = 1;
        w->frame = tiku_inset(area, 2);
        c->reader->zoom_frame = w->frame;
    }
    c->reader->hover_tool = 0;
    if (c->dirty) *c->dirty = 1;
    return 1;
}

/** @brief Open is transactional: a failed read leaves page and history intact. */
int tiku_reader_document_window_file(tiku_reader_help_window_t *c, const char *path)
{
    if (!c || !c->document_mode) return 0;
    char from[TIKU_PATH_MAX];
    snprintf(from, sizeof from, "%s", c->page);
    tiku_reader_help_position_t at = reader_position(c);
    c->status[0] = 0;
    if (!page_read(c, path)) {
        if (!c->status[0]) say(c, "Cannot read document. Choose an existing local .md, .markdown, .html or .htm file.");
        if (c->dirty) *c->dirty = 1;
        return 0;
    }
    if (strcmp(from, c->page)) back_push(c, from, at);
    c->nforward = 0;
    say(c, "");
    tiku_reader_help_window_open(c);
    if (c->reader && c->reader->side == 2) c->reader->side = 1;
    if (c->dirty) *c->dirty = 1;
    return 1;
}

/** @brief Reload restores a logical location without creating a history entry. */
int tiku_reader_document_window_reload(tiku_reader_help_window_t *c)
{
    if (!c || !c->document_mode || c->page[0] != '/') return 0;
    char path[TIKU_PATH_MAX]; snprintf(path, sizeof path, "%s", c->page);
    tiku_reader_help_position_t at = reader_position(c);
    c->status[0] = 0;
    if (!page_read(c, path)) {
        if (!c->status[0]) say(c, "Reload failed; the displayed document has been kept.");
        if (c->dirty) *c->dirty = 1;
        return 0;
    }
    tiku_scroll_to(&c->bar, tiku_doc_place_y(c->doc, at.place));
    say(c, "Reloaded from disk.");
    if (c->dirty) *c->dirty = 1;
    return 1;
}
