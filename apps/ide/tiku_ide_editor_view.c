/* tiku_ide_editor_view.c -- editor painting, toolbar and text geometry.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"
#include "tiku_host.h"

static void tb_arrow(tiku_surface_t *s, int x, int back, int on);
static void tb_glass(tiku_surface_t *s, int x);
static const char * tb_tool_name(const edit_t *e, int tool);
static void tb_stop(tiku_surface_t *s, int x, int on);
static void tb_mallet(tiku_surface_t *s, int x);
static void tb_renum(tiku_surface_t *s, int x);

/** @brief The language the file name @p name implies; the file's text is
 *         not looked at. */
tiku_syntax_lang_t
tiku_ide_lang_of(const char *name)
{
    return tiku_syntax_of_path(name);
}

/** @brief Draw the undo arrow (@p back) or the redo arrow at @p x, grey
 *         when @p on is 0. */
static void
tb_arrow(tiku_surface_t *s, int x, int back, int on)
{
    tiku_rgb_t c = on ? TIKU_RGB(64, 64, 64) : TIKU_RGB(178, 178, 178);
    int mid = 3 + TB_ICON / 2;          /* the toolbar y of the middle */
    int hx = back ? x + 7 : x + 16;     /* the arrowhead's tip */
    int dir = back ? 1 : -1;

    /* the shaft, with a hook rising at the tail */
    tiku_hline(s, x + 7, mid, 10, c);
    tiku_vline(s, back ? x + 16 : x + 7, mid - 4, 5, c);
    tiku_hline(s, back ? x + 13 : x + 7, mid - 4, 4, c);
    /* the arrowhead */
    tiku_pixel(s, hx + dir, mid - 2, c);
    tiku_pixel(s, hx + dir * 2, mid - 1, c);
    tiku_pixel(s, hx + dir, mid + 2, c);
    tiku_pixel(s, hx + dir * 2, mid + 1, c);
}

/** @brief Draw the Find tool's magnifying glass at @p x. */
static void
tb_glass(tiku_surface_t *s, int x)
{
    tiku_rgb_t c = TIKU_RGB(64, 64, 64);
    int i;

    /* the ring, an octagon of the lines available (toolbar y = 3 + n) */
    tiku_hline(s, x + 8, 3 + 6, 4, c);
    tiku_hline(s, x + 8, 3 + 13, 4, c);
    tiku_vline(s, x + 6, 3 + 8, 4, c);
    tiku_vline(s, x + 13, 3 + 8, 4, c);
    tiku_pixel(s, x + 7, 3 + 7, c);
    tiku_pixel(s, x + 12, 3 + 7, c);
    tiku_pixel(s, x + 7, 3 + 12, c);
    tiku_pixel(s, x + 12, 3 + 12, c);
    /* the handle, out of the lower right */
    for (i = 0; i < 4; i++) {
        tiku_pixel(s, x + 14 + i, 3 + 14 + i, c);
        tiku_pixel(s, x + 15 + i, 3 + 14 + i, c);
    }
}

/**
 * @brief Which toolbar tool sits at @p x,@p y on this page, or -1; a tool
 *        the page's language does not have is -1.
 *
 * @note R[] repeats the TB_* rectangles of tiku_ide_private.h, which the
 *       press handler also tests; the two must match.
 */
int
tiku_ide_tb_tool_at(const edit_t *e, int x, int y)
{
    static const tiku_rect_t R[] = {
        { 8, 3, TB_ICON, TB_ICON },     /* 0 Save */
        { 44, 3, TB_ICON, TB_ICON },    /* 1 Run / Build */
        { 72, 3, TB_ICON, TB_ICON },    /* 2 Stop */
        { 108, 3, TB_ICON, TB_ICON },   /* 3 Undo */
        { 136, 3, TB_ICON, TB_ICON },   /* 4 Redo */
        { 172, 3, TB_ICON, TB_ICON },   /* 5 Find */
        { 208, 3, TB_ICON, TB_ICON }    /* 6 Renumber */
    };
    int i;

    for (i = 0; i < (int)(sizeof R / sizeof R[0]); i++) {
        if (x >= R[i].x && x < R[i].x + R[i].w &&
            y >= R[i].y && y < R[i].y + R[i].h) {
            if ((i == 2 || i == 6) && e->lang != TIKU_SYNTAX_BASIC) {
                return -1;      /* stop and renumber are BASIC's */
            }
            if (i == 1 && e->lang != TIKU_SYNTAX_BASIC &&
                e->lang != TIKU_SYNTAX_C) {
                return -1;
            }
            return i;
        }
    }
    return -1;
}

/** @brief The name on a hovered tool's tip. */
static const char *
tb_tool_name(const edit_t *e, int tool)
{
    static const char *const N[] = {
        "Save", "Run", "Stop", "Undo", "Redo", "Find", "Renumber"
    };

    if (tool == 1 && e->lang == TIKU_SYNTAX_C) {
        return "Build";
    }
    return (tool >= 0 && tool < 7) ? N[tool] : "";
}

/** @brief Draw the Stop tool's square at @p x, red when @p on. */
static void
tb_stop(tiku_surface_t *s, int x, int on)
{
    tiku_rect_t sq = { x + 6, 3 + 6, 12, 12 };

    tiku_fill(s, sq, on ? TIKU_RGB(178, 52, 52)
                        : TIKU_RGB(178, 178, 178));
}

/** @brief Draw the Build tool's mallet at @p x: a head and a handle. */
static void
tb_mallet(tiku_surface_t *s, int x)
{
    tiku_rgb_t c = TIKU_RGB(64, 64, 64);
    tiku_rect_t head = { x + 5, 3 + 5, 14, 5 };
    int i;

    tiku_fill(s, head, c);
    tiku_vline(s, x + 11, 3 + 10, 9, c);
    tiku_vline(s, x + 12, 3 + 10, 9, c);
    (void)i;
}

/** @brief Draw the Renumber tool at @p x: "10" and an arrow. */
static void
tb_renum(tiku_surface_t *s, int x)
{
    const tiku_font_t *f = tiku_font_at(11);
    tiku_rgb_t c = TIKU_RGB(64, 64, 64);
    int mid = 3 + TB_ICON / 2;

    tiku_text(s, f, x + 3, 3 + 6 + f->ascent, "10", c);
    tiku_hline(s, x + 15, mid + 1, 5, c);
    tiku_pixel(s, x + 18, mid, c);
    tiku_pixel(s, x + 18, mid + 2, c);
    tiku_pixel(s, x + 17, mid - 1, c);
    tiku_pixel(s, x + 17, mid + 3, c);
}

/** @brief The y the page begins at: below the toolbar and the target
 *         strip, and below the find bar when it is open. */
int
tiku_ide_edit_page_top(const edit_t *e)
{
    return SEARCH_TOP + (e->finding ? FIND_H * 3 : 0);
}

/** @brief The y the page ends at: above the status strip, and above the
 *         debug panel when it is shown. */
int
tiku_ide_edit_page_bottom(const edit_t *e)
{
    return e->h - STRIP_H - (e->debug_show ? DEBUG_H : 0);
}

/**
 * @brief Put the page's view where the page is: its text MARGIN pixels in
 *        from the window's left and the page's top, its right edge at the
 *        page's scrollbar.
 *
 * The view keeps 5 pixels before a line and 3 above the first row, so
 * its bounds start a pixel in and 3 down.
 */
void
tiku_ide_edit_bounds(edit_t *e)
{
    int top = tiku_ide_edit_page_top(e);
    tiku_rect_t r = { MARGIN - 5, top + MARGIN - 3, e->w - PAGE_SB - 1,
                      tiku_ide_edit_page_bottom(e) - top - (MARGIN - 3) };

    tiku_codeview_set_bounds(e->view, r);
}

/** @brief Scroll the page as little as shows the caret. */
void
tiku_ide_edit_reveal(edit_t *e)
{
    tiku_ide_edit_bounds(e);
    tiku_codeview_reveal(e->view);
}

/**
 * @brief Make page @p e read-only while it is the source of a debug run.
 *
 * The mark on the line the run is paused before is cleared unless that
 * run is paused.
 */
void
tiku_ide_edit_lock(ide_t *st, edit_t *e)
{
    int locked = tiku_ide_debug_locked(st, e);

    tiku_editdoc_set_read_only(e->doc, locked);
    if (!locked || !st->debug_paused) {
        tiku_editdoc_execution_clear(e->doc);
    }
}

/** @brief Paint page @p e, toolbar to status strip, with any tip, and
 *         hand the frame to the desktop. */
void
tiku_ide_edit_paint(ide_t *st, edit_t *e)
{
    const tiku_font_t *f = tiku_font_plain();
    int ptop = tiku_ide_edit_page_top(e);
    tiku_rect_t bar = { 0, 0, e->w, TOOLBAR_H };
    tiku_rect_t page = { 0, ptop, e->w, tiku_ide_edit_page_bottom(e) - ptop };
    tiku_rect_t strip = { 0, e->h - STRIP_H, e->w, STRIP_H };
    char where[256];
    int shape;

    if (e->surface == NULL || e->view == NULL) {
        return;
    }
    tiku_ide_edit_lock(st, e);
    tiku_ide_edit_bounds(e);
    if (e->assist_open) {
        tiku_ide_assist_sync(e);
    }
    /* The toolbar: Save, Run or Build, Stop, Undo, Redo, Find and
     * Renumber, drawn as icons, with the target strip under it. */
    tiku_fill(e->surface, bar, TIKU_C_PANEL);
    tiku_ide_target_paint(st, e);
    {
        tiku_rect_t sv = TB_SAVE;

        (void)tiku_icon_paint(e->surface, "floppy", sv.x, sv.y,
                                  TB_ICON);
    }
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* The run arrow: a green triangle drawn row by row. */
        tiku_rect_t rn = TB_RUN;
        tiku_rgb_t go = TIKU_RGB(52, 140, 62);
        int i;

        for (i = 0; i < TB_ICON - 6; i++) {
            int half = (i < (TB_ICON - 6) / 2)
                           ? i
                           : (TB_ICON - 7 - i);

            tiku_hline(e->surface, rn.x + 4,
                            rn.y + 3 + i, 2 + half * 2, go);
        }
    }
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* Stop, red while a run goes. */
        tb_stop(e->surface, TB_STOP.x, st->running);
    }
    if (e->lang == TIKU_SYNTAX_C) {
        /* On a C page the Run tool is Build, drawn as a mallet. */
        tb_mallet(e->surface, TB_RUN.x);
    }
    {
        /* Undo and redo, grey when there is nothing to undo or redo,
         * and Find. */
        tb_arrow(e->surface, TB_UNDO.x, 1, tiku_editdoc_can_undo(e->doc));
        tb_arrow(e->surface, TB_REDO.x, 0, tiku_editdoc_can_redo(e->doc));
        tb_glass(e->surface, TB_FIND.x);
    }
    if (e->lang == TIKU_SYNTAX_BASIC) {
        /* Renumber, for BASIC's numbered lines. */
        tb_renum(e->surface, TB_RENUM.x);
    }
    {
        /* A rule under the toolbar. */
        tiku_hline(e->surface, 0, TOOLBAR_H - 1, e->w,
                        tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    }
    if (e->finding) {
        /* The find bar, three rows between the target strip and the
         * page: Return goes to the next match, Escape closes it.  In a
         * BASIC file with replace closed, a number typed here goes to
         * that line, so the label reads "Find / line:". */
        tiku_rect_t fbar = { 0, SEARCH_TOP, e->w, FIND_H * 3 };
        tiku_rect_t fld;
        const char *label = (e->lang == TIKU_SYNTAX_BASIC && !e->replacing)
                                ? "Find / line:" : "Find:";
        int lw = tiku_text_width(f, label);

        tiku_fill(e->surface, fbar, TIKU_C_PANEL);
        tiku_hline(e->surface, 0, SEARCH_TOP + FIND_H * 3 - 1, e->w,
                        tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
        tiku_text(e->surface, f, MARGIN,
                       SEARCH_TOP + (FIND_H - f->height) / 2 + f->ascent,
                       label, TIKU_C_TEXT);
        fld = (tiku_rect_t){ MARGIN + lw + 6, SEARCH_TOP + 3,
                             e->w - (MARGIN + lw + 6) - MARGIN,
                             FIND_H - 6 };
        tiku_ui_textfield_sel(e->surface, fld, e->find.text,
                          e->find_focus == 0 ? e->find.caret : -1,
                          e->find.sel_a, e->find.sel_b, 0u);
        tiku_ide_search_paint(e);
    }
    /* The page: sunken, and the view inside its two-pixel rim, which
     * clips the view's fills and pop-ups to the rim's inside. */
    tiku_ui_sunken(e->surface, page, TIKU_C_DOC);
    if (e->debug_show) { tiku_ide_debug_paint(st, e); }
    tiku_clip_set(e->surface, (tiku_rect_t){ 2, ptop + 2, e->w - 4,
                                             page.h - 4 });
    tiku_codeview_paint(e->view, e->surface);
    tiku_clip_reset(e->surface);
    {
        /* The kit's scrollbar, in a gutter at the page's right, shown
         * when the listing is longer than the window. */
        int64_t total, rows, top;

        tiku_codeview_extent(e->view, 0, &total, &rows, &top);
        if (total > rows && rows > 0) {
            tiku_rect_t sb = { e->w - PAGE_SB, ptop, PAGE_SB, page.h };

            tiku_ui_scrollbar(e->surface, sb,
                (float)top / (float)(total - rows),
                (float)rows / (float)total, 0);
        }
    }
    tiku_fill(e->surface, strip, TIKU_C_PANEL);
    {
        /* The strip shows the caret's line and column and, on a
         * numbered BASIC line, its line number. */
        int64_t cy, cx;
        long num = 0;
        char bas[32];

        (void)tiku_ide_caret(e, &cy, &cx);
        bas[0] = '\0';
        if (e->lang == TIKU_SYNTAX_BASIC) {
            num = tiku_ide_line_number(e, cy, 1);
        }
        if (num > 0) {
            snprintf(bas, sizeof bas, "   [%ld]", num);
        }
        snprintf(where, sizeof where,
                 "%s%s   line %lld, col %lld%s   %lld lines", e->name,
                 tiku_editdoc_modified(e->doc) ? " (changed)" : "",
                 (long long)cy + 1, (long long)cx + 1, bas,
                 (long long)tiku_editdoc_lines(e->doc));
    }
    tiku_text(e->surface, f, MARGIN,
                   strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                   where, TIKU_C_TEXT);
    if (e->feedback[0]) {
        tiku_fill(e->surface, strip, TIKU_C_PANEL);
        tiku_text(e->surface, f, MARGIN, strip.y + f->ascent + 3,
                  e->feedback, TIKU_C_TEXT);
    }
    if (e->tip >= 0) {
        /* The hovered tool's name, on the kit's own tip, below the
         * button so the pointer does not sit on the words. */
        const char *name = tb_tool_name(e, e->tip);
        tiku_rect_t tr = tiku_ui_tip_size(name);

        tr.x = 8 + e->tip * 4 + e->tip * 28;   /* near its button */
        tr.y = TOOLBAR_H + 2;
        if (tr.x + tr.w > e->w) {
            tr.x = e->w - tr.w - 2;
        }
        tiku_ui_tip(e->surface, tr, name);
    }
    (void)st->services->frame(st->services->ctx, e->id, e->surface->px,
                              e->w, e->h);
    /* The pointer is a hand while it is over a jump target a click would
     * follow. */
    shape = tiku_codeview_hotspot(e->view, NULL) ? TIKU_CURSOR_HAND
                                                 : TIKU_CURSOR_ARROW;
    if (shape != e->cursor &&
        tiku_app_cursor_set(st->services, e->id, shape)) {
        e->cursor = shape;
    }
}
