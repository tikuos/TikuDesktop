/* The window: a toolbar of icon buttons and the find field, the outline
 * down the left, the pages, and a status line under them; presenting,
 * the one slide and nothing else.  An empty window shows nothing and
 * says nothing. */

#define OV_FIND_W 170               /* the find field, at most           */
#define OV_INDENT 14                /* an outline level's step in        */

/** @brief Where everything goes in the window's content. */
typedef struct {
    tiku_rect_t find, side, view, status;
} ov_layout_t;

/** @brief A toolbar button's icon, from the desk's set. */
static const char *ov_tool_icon(int id)
{
    switch (id) {
    case OV_OPEN: return "folder_open";
    case OV_EXPORT: return "pdf_viewer";
    case OV_ZOOM_OUT: return "zoom_out";
    case OV_ZOOM_IN: return "zoom_in";
    case OV_FIT_WIDTH: return "fit_width";
    case OV_FIT_PAGE: return "fit_page";
    case OV_PREV: return "page_up";
    case OV_NEXT: return "page_down";
    case OV_PRESENT: return "slides_viewer";
    case OV_OUTLINE: return "doc_outline";
    }
    return "file";
}

/** @brief What a toolbar button does, as its tip says it. */
static const char *ov_tool_tip(const tiku_officeview_t *c, int id)
{
    switch (id) {
    case OV_OPEN: return "Open (Ctrl+O)";
    case OV_EXPORT: return "Export as PDF (Ctrl+E)";
    case OV_ZOOM_OUT: return "Zoom Out (Ctrl+-)";
    case OV_ZOOM_IN: return "Zoom In (Ctrl++)";
    case OV_FIT_WIDTH: return "Fit Width";
    case OV_FIT_PAGE: return c->kind.slides ? "Fit Slide" : "Fit Page";
    case OV_PREV: return c->kind.slides ? "Previous Slide" : "Previous Page";
    case OV_NEXT: return c->kind.slides ? "Next Slide" : "Next Page";
    case OV_PRESENT: return "Present (Shift+Ctrl+P)";
    case OV_OUTLINE: return c->kind.slides ? "Slide Titles" : "Outline";
    }
    return "";
}

/** @brief A toolbar button's side: square, as the other viewers'. */
static int ov_tool_size(void)
{
    int bh = tiku_ui_button_h();

    return bh > 30 ? bh : 30;
}

/** @brief The toolbar's buttons and the find field laid along @p r's
 *         top.  @return the row below them. */
static int ov_tools_layout(tiku_officeview_t *c, tiku_rect_t r,
                           ov_layout_t *l)
{
    static const int ids[] = { OV_OPEN, OV_EXPORT, OV_OUTLINE, OV_ZOOM_OUT,
                               OV_ZOOM_IN, OV_FIT_WIDTH, OV_FIT_PAGE,
                               OV_PREV, OV_NEXT, OV_PRESENT };
    int bw = ov_tool_size(), bh = tiku_ui_button_h(), x = r.x + 6;
    int y = r.y + 4, fw;

    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        if (ids[i] == OV_PRESENT && !c->kind.slides) continue;
        if (ids[i] == OV_OUTLINE || ids[i] == OV_ZOOM_OUT ||
            ids[i] == OV_PREV || ids[i] == OV_PRESENT) x += 10;
        if (x + bw > r.x + r.w - 6) { x = r.x + 6; y += bw + 4; }
        (void)tiku_dialog_add(&c->dialog, ids[i], (tiku_rect_t){x, y, bw, bw},
                              TIKU_DIALOG_BUTTON, ov_enabled(c, ids[i]));
        x += bw + 2;
    }
    fw = r.x + r.w - 6 - x - 10;
    if (fw < 100) { x = r.x + 6; y += bw + 4; fw = r.w - 12; }
    if (fw > OV_FIND_W) fw = OV_FIND_W;
    if (fw < 1) fw = 1;
    l->find = (tiku_rect_t){r.x + r.w - 6 - fw, y + (bw - bh) / 2, fw, bh};
    (void)tiku_dialog_add(&c->dialog, OV_FIND_FIELD, l->find,
                          TIKU_DIALOG_INPUT, ov_open_now(c));
    return y + bw + 4;
}

/** @brief Where everything goes in content @p r: presenting, the slide
 *         takes all of it. */
static ov_layout_t ov_layout(tiku_officeview_t *c, tiku_rect_t r)
{
    ov_layout_t l;
    int line = tiku_font_plain()->height + 8, below;

    memset(&l, 0, sizeof l);
    tiku_dialog_begin(&c->dialog, -1, -1);
    if (c->presenting) {
        l.view = r;
    } else {
        below = ov_tools_layout(c, r, &l);
        l.status = (tiku_rect_t){r.x + 8, r.y + r.h - line, r.w - 16, line};
        l.view = (tiku_rect_t){r.x, below, r.w, l.status.y - below};
        if (l.view.h < 1) l.view.h = 1;
        if (c->outline_on && ov_enabled(c, OV_OUTLINE)) {
            int sw = r.w / 4 < 140 ? 140 : r.w / 4 > 260 ? 260 : r.w / 4;

            l.side = (tiku_rect_t){l.view.x, l.view.y, sw, l.view.h};
            l.view.x += sw + 1;
            l.view.w -= sw + 1;
            (void)tiku_dialog_add(&c->dialog, OV_SIDE, l.side,
                                  TIKU_DIALOG_INPUT, 1);
        }
    }
    (void)tiku_dialog_add(&c->dialog, OV_VIEW, l.view, TIKU_DIALOG_INPUT,
                          ov_open_now(c));
    tiku_dialog_end(&c->dialog);
    if (c->to_view && tiku_dialog_focus(&c->dialog, OV_VIEW)) c->to_view = 0;
    return l;
}

/** @brief Whether point (@p x, @p y) is in @p r. */
static int ov_in(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* ------------------------------------------------------------- paint   */

/** @brief A toolbar button as the desk's toolbars draw one: its icon,
 *         raised under the pointer, sunk while pressed, dimmed when it can
 *         do nothing; its name crosses for narration. */
static void ov_button(tiku_officeview_t *c, tiku_surface_t *s,
                      const tiku_dialog_control_t *d)
{
    unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;
    int held = tiku_dialog_pressed(&c->dialog, d->id), size = d->rect.h - 6;

    if (held) flags |= TIKU_S_PRESSED;
    if (c->hover_tool == d->id) flags |= TIKU_S_HOT;
    if (d->id == OV_PRESENT && c->presenting) flags |= TIKU_S_PRESSED;
    tiku_ui_button(s, d->rect, ov_label(c, d->id), flags | TIKU_S_ICON);
    if (size > 24) size = 24;
    (void)tiku_icon_paint_dim(s, ov_tool_icon(d->id),
                              d->rect.x + (d->rect.w - size) / 2 + held,
                              d->rect.y + (d->rect.h - size) / 2 + held, size,
                              d->enabled ? 0.0f : 0.72f, TIKU_C_PANEL);
}

/** @brief The tip of the toolbar button under the pointer, below it. */
static void ov_tip_draw(tiku_officeview_t *c, tiku_surface_t *s,
                        tiku_rect_t r)
{
    for (int i = 0; i < c->dialog.count && c->hover_tool; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        const char *text = ov_tool_tip(c, d->id);
        tiku_rect_t tip;

        if (d->id != c->hover_tool || d->kind != TIKU_DIALOG_BUTTON ||
            !*text) continue;
        tip = tiku_ui_tip_size(text);
        tip.x = d->rect.x;
        tip.y = d->rect.y + d->rect.h + 4;
        if (tip.x + tip.w > r.x + r.w - 4) tip.x = r.x + r.w - 4 - tip.w;
        tiku_ui_tip(s, tip, text);
    }
}

/** @brief The find field, its hint shown while it is empty and idle. */
static void ov_find_draw(tiku_officeview_t *c, tiku_surface_t *s,
                         tiku_rect_t f)
{
    int focus = tiku_dialog_focused(&c->dialog, OV_FIND_FIELD);

    tiku_ui_textfield_scroll(s, f, c->find.text, focus ? c->find.caret : -1,
                             c->find.sel_a, c->find.sel_b,
                             focus ? TIKU_S_FOCUS : 0, c->find_in.scroll);
    if (!c->find.text[0] && !focus)
        tiku_text_line_draw(s, (tiku_rect_t){f.x + 6, f.y, f.w - 12, f.h},
                            TIKU_MEASURE_PLAIN, "Find",
                            tiku_ink(TIKU_INK_FAINT));
}

/** @brief The outline down the left: each entry indented by its level,
 *         the one last followed marked, a rule between it and the pages. */
static void ov_side_draw(tiku_officeview_t *c, tiku_surface_t *s,
                         tiku_rect_t r)
{
    const tiku_pdf_outline_t *items = NULL;
    int n = ov_entries(c, &items), top = c->outline.top;
    int seen = tiku_list_visible_rows(&c->outline, r);
    tiku_rect_t clip = s->clip;

    tiku_fill(s, r, TIKU_C_DOC);
    tiku_fill(s, (tiku_rect_t){r.x + r.w, r.y, 1, r.h},
              tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    tiku_clip_set(s, r);
    for (int row = top; row < n && row <= top + seen; row++) {
        tiku_rect_t at = tiku_list_row_rect(&c->outline, r, row);
        int sel = tiku_list_selected(&c->outline, row);
        int in = 8 + OV_INDENT * (items[row].level < 6 ? items[row].level
                                                       : 6);

        if (sel) tiku_fill(s, at, TIKU_C_SELECT);
        tiku_text_line_draw(s, (tiku_rect_t){at.x + in, at.y, at.w - in - 4,
                            at.h}, TIKU_MEASURE_PLAIN, items[row].title ?
                            items[row].title : "", sel ? TIKU_C_SELTEXT
                                                       : TIKU_C_TEXT);
    }
    tiku_clip_set(s, clip);
}

/** @brief The status line: what happened, then where the view stands;
 *         nothing while no document is shown or opening. */
static void ov_status(tiku_officeview_t *c, tiku_surface_t *s, tiku_rect_t r)
{
    const char *said = c->said[0] ? c->said : tiku_pdf_view_status(c->view);
    char where[64] = "";
    int w = 0;

    if (ov_open_now(c)) {
        snprintf(where, sizeof where, "%s %d of %d   %d%%",
                 c->kind.slides ? "Slide" : "Page",
                 tiku_pdf_view_page(c->view) + 1,
                 tiku_pdf_view_pages(c->view),
                 (int)(tiku_pdf_view_scale(c->view) * 100 + 0.5));
        w = tiku_measure_width(TIKU_MEASURE_PLAIN, where) + 8;
        tiku_text_line_draw(s, (tiku_rect_t){r.x + r.w - w, r.y, w, r.h},
                            TIKU_MEASURE_PLAIN, where, TIKU_C_TEXT);
    }
    tiku_text_line_draw(s, (tiku_rect_t){r.x, r.y, r.w - w - 8, r.h},
                        TIKU_MEASURE_PLAIN, said, tiku_ink(TIKU_INK_FAINT));
}

/** @brief The whole window, laid out afresh each time. */
static void ov_draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r,
                    void *ctx)
{
    tiku_officeview_t *c = ctx;
    tiku_rect_t clip = s->clip;
    ov_layout_t l;

    (void)w;
    tiku_clip_set(s, r);
    tiku_ui_panel(s, r);
    l = ov_layout(c, r);
    if (c->presenting) {
        tiku_fill(s, r, (tiku_rgb_t)0x000000u);
        tiku_pdf_view_draw(c->view, s, l.view);
        s->clip = clip;
        return;
    }
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        if (d->kind == TIKU_DIALOG_BUTTON) ov_button(c, s, d);
    }
    ov_find_draw(c, s, l.find);
    if (l.side.w > 0) ov_side_draw(c, s, l.side);
    if (ov_open_now(c)) tiku_pdf_view_draw(c->view, s, l.view);
    else tiku_fill(s, l.view, TIKU_C_DOC);  /* nothing open: blank */
    ov_status(c, s, l.status);
    ov_tip_draw(c, s, r);           /* last: nothing paints over it */
    s->clip = clip;
}

/* ------------------------------------------------------------- input   */

/** @brief The toolbar button under the pointer, lit and tipped; none once
 *         something is pressed or typed, or the window loses the focus. */
static void ov_hover(tiku_officeview_t *c, const tiku_event_t *ev)
{
    int tool = 0;

    if (ev->type == TIKU_EVENT_POINTER_MOVE &&
        c->dialog.capture == TIKU_DIALOG_NONE) {
        for (int i = 0; i < c->dialog.count; i++) {
            const tiku_dialog_control_t *d = &c->dialog.control[i];

            if (d->kind == TIKU_DIALOG_BUTTON &&
                ov_in(d->rect, ev->x, ev->y))
                tool = d->id;
        }
    }
    if (tool != c->hover_tool) {
        c->hover_tool = tool;
        ov_changed(c);
    }
}

/** @brief Keys while presenting: on and back through the slides, Escape
 *         to stop; a click steps on.  @return 1 when one was taken. */
static int ov_present_key(tiku_officeview_t *c, const tiku_event_t *ev)
{
    int page = tiku_pdf_view_page(c->view);

    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        ov_go(c, page + 1);
        return 1;
    }
    if (ev->type != TIKU_EVENT_KEY_DOWN) return 0;
    switch (ev->key) {
    case TIKU_KEY_RIGHT: case TIKU_KEY_DOWN: case TIKU_KEY_PAGE_DOWN:
    case TIKU_KEY_RETURN: case ' ': case 'n':
        ov_go(c, page + 1); return 1;
    case TIKU_KEY_LEFT: case TIKU_KEY_UP: case TIKU_KEY_PAGE_UP:
    case TIKU_KEY_BACKSPACE: case 'p':
        ov_go(c, page - 1); return 1;
    case TIKU_KEY_HOME: ov_go(c, 0); return 1;
    case TIKU_KEY_END: ov_go(c, tiku_pdf_view_pages(c->view) - 1); return 1;
    case TIKU_KEY_ESCAPE: ov_action(c, OV_PRESENT); return 1;
    }
    return 0;
}

/** @brief Command keys, whatever has the focus; the field keeps its own
 *         editing keys.  @return 1 when one was taken. */
static int ov_command_key(tiku_officeview_t *c, const tiku_event_t *ev)
{
    int key = (int)ev->key, shift = (ev->modifiers & TIKU_MOD_SHIFT) != 0;

    if (ev->type != TIKU_EVENT_KEY_DOWN || !(ev->modifiers & TIKU_MOD_CMD))
        return 0;
    if (tiku_dialog_focused(&c->dialog, OV_FIND_FIELD) &&
        (key == 'c' || key == 'x' || key == 'v' || key == 'a' || key == 'z'))
        return 0;
    if (key == 'o') ov_action(c, OV_OPEN);
    else if (key == 'e') ov_action(c, OV_EXPORT);
    else if (key == 'w') ov_action(c, OV_CLOSE);
    else if (key == 'f') ov_action(c, OV_FIND);
    else if (key == 'g') ov_action(c, shift ? OV_FIND_PREV : OV_FIND_NEXT);
    else if (key == 'p' && shift) ov_action(c, OV_PRESENT);
    else return 0;
    return 1;
}

/** @brief A key or typed text for the find field: Return looks for it,
 *         Escape empties it and gives the keys back to the pages. */
static int ov_field_event(tiku_officeview_t *c, const tiku_event_t *ev,
                          const ov_layout_t *l)
{
    tiku_field_result_t r;
    char text[TIKU_FIELD_MAX];

    if (!tiku_dialog_focused(&c->dialog, OV_FIND_FIELD)) return 0;
    if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_TAB) return 0;
    r = ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_ESCAPE
        ? TIKU_FIELD_CANCELLED
        : tiku_field_event(&c->find, &c->find_in, ev, l->find, 1);
    if (r == TIKU_FIELD_COMMITTED) {
        ov_find(c, (ev->modifiers & TIKU_MOD_SHIFT) ? -1 : 1);
        snprintf(text, sizeof text, "%s", c->find.text);
        tiku_field_start(&c->find, text);   /* on, for the next */
    } else if (r == TIKU_FIELD_CANCELLED) {
        tiku_field_start(&c->find, "");
        c->to_view = !tiku_dialog_focus(&c->dialog, OV_VIEW);
    }
    ov_menus(c);
    ov_changed(c);
    return ev->type == TIKU_EVENT_KEY_DOWN ||
           ev->type == TIKU_EVENT_TEXT_INPUT || r != TIKU_FIELD_IGNORED;
}

/** @brief Pointer input for the find field: a press there gives it the
 *         keys, a drag selects in it. */
static int ov_field_pointer(tiku_officeview_t *c, const tiku_event_t *ev,
                            const ov_layout_t *l)
{
    tiku_rect_t f = l->find;

    if (ev->type == TIKU_EVENT_POINTER_DOWN && ov_in(f, ev->x, ev->y) &&
        ov_open_now(c)) {
        (void)tiku_dialog_focus(&c->dialog, OV_FIND_FIELD);
        (void)tiku_field_event(&c->find, &c->find_in, ev, f, 1);
        return 1;
    }
    if (c->find_in.dragging) {
        (void)tiku_field_event(&c->find, &c->find_in, ev, f, 1);
        return 1;
    }
    return 0;
}

/** @brief Follow the outline's chosen entry: the pages go to it, and the
 *         keys stay with the outline. */
static void ov_side_follow(tiku_officeview_t *c)
{
    const tiku_pdf_outline_t *items = NULL;
    int n = ov_entries(c, &items), row = tiku_list_chosen(&c->outline);

    if (row >= 0 && row < n) tiku_pdf_view_follow(c->view, &items[row].to);
}

/** @brief Input for the outline: a press chooses and follows an entry,
 *         the wheel scrolls it, and with the keys the arrows move and
 *         Return follows.  @return 1 when it took @p ev. */
static int ov_side_event(tiku_officeview_t *c, const tiku_event_t *ev,
                         const ov_layout_t *l)
{
    int row;

    if (l->side.w <= 0) return 0;
    if (ev->type == TIKU_EVENT_POINTER_DOWN &&
        ov_in(l->side, ev->x, ev->y)) {
        (void)tiku_dialog_focus(&c->dialog, OV_SIDE);
        row = tiku_list_at(&c->outline, l->side, ev->x, ev->y);
        if (row >= 0) {
            tiku_list_select_only(&c->outline, row);
            ov_side_follow(c);
        }
        return 1;
    }
    if (ev->type == TIKU_EVENT_WHEEL && ov_in(l->side, ev->x, ev->y)) {
        (void)tiku_list_scroll_to(&c->outline, c->outline.top +
                                  (ev->dy > 0 ? 3 : ev->dy < 0 ? -3 : 0),
                                  l->side);
        return 1;
    }
    if (ev->type != TIKU_EVENT_KEY_DOWN ||
        !tiku_dialog_focused(&c->dialog, OV_SIDE)) return 0;
    if (ev->key == TIKU_KEY_RETURN) {
        ov_side_follow(c);
        return 1;
    }
    return tiku_list_key(&c->outline, ev->key, ev->modifiers, l->side);
}

/** @brief Whether @p ev is the pages': a press or the wheel over them, a
 *         move or release (a drag may have left them), a key while they
 *         have the focus. */
static int ov_for_view(tiku_officeview_t *c, const tiku_event_t *ev,
                       const ov_layout_t *l)
{
    if (!ov_open_now(c) || c->dialog.capture != TIKU_DIALOG_NONE) return 0;
    switch (ev->type) {
    case TIKU_EVENT_POINTER_DOWN:
    case TIKU_EVENT_WHEEL: return ov_in(l->view, ev->x, ev->y);
    case TIKU_EVENT_POINTER_MOVE:
    case TIKU_EVENT_POINTER_UP: return 1;
    case TIKU_EVENT_KEY_DOWN:
    case TIKU_EVENT_TEXT_INPUT:
        return tiku_dialog_focused(&c->dialog, OV_VIEW);
    default:
        return 0;
    }
}

/** @brief Every window event: presenting keys, command keys, the find
 *         field, the pages, then the toolbar and the focus's travels. */
static int ov_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    tiku_officeview_t *c = ctx;
    ov_layout_t l = ov_layout(c, tiku_window_content(w));
    tiku_dialog_result_t r;

    ov_hover(c, ev);
    if (c->presenting && (ov_present_key(c, ev) ||
                          ev->type == TIKU_EVENT_POINTER_DOWN ||
                          ev->type == TIKU_EVENT_POINTER_MOVE ||
                          ev->type == TIKU_EVENT_POINTER_UP)) {
        ov_title(c);                /* the slide's: nothing else to touch */
        ov_changed(c);
        return 1;
    }
    if (ov_command_key(c, ev) || ov_field_event(c, ev, &l) ||
        ov_field_pointer(c, ev, &l) || ov_side_event(c, ev, &l)) {
        ov_changed(c);
        return 1;
    }
    if (ov_for_view(c, ev, &l)) {
        if (ev->type == TIKU_EVENT_POINTER_DOWN)
            (void)tiku_dialog_focus(&c->dialog, OV_VIEW);
        if (tiku_pdf_view_event(c->view, l.view, ev)) {
            ov_menus(c);            /* what can be done may have changed */
            return 1;
        }
        if (ev->type == TIKU_EVENT_POINTER_DOWN) return 1;
    }
    r = tiku_dialog_event(&c->dialog, ev);
    if (r.activated >= 0 && r.activated != OV_FIND_FIELD &&
        r.activated != OV_VIEW && r.activated != OV_SIDE)
        ov_action(c, r.activated);
    if (r.taken) ov_changed(c);
    return r.taken;
}

/** @brief The native window went away: nothing may point at it. */
static void ov_window_gone(tiku_window_t *w, void *ctx)
{
    tiku_officeview_t *c = ctx;

    (void)w;
    c->window = NULL;
    c->presenting = 0;
    tiku_pdf_view_single(c->view, 0);
    ov_changed(c);
}
