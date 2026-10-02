/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_input.c - the caret, the selection and the scroll moved by
 * keys, the pointer, the scrollbars and the wheel, through the same layout
 * the view paints with; editing keys go on to tiku_codeview_edit.c.
 *
 * Word moves follow the behaviour of Scintilla's Document::NextWordStart
 * (src/Document.cxx, read for reference; no code taken).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_ui.h"

/* A second press this soon after the last, and this near it, counts as
 * the same click: the desktop's double-click rule. */
#define CLICK_US 400000
#define CLICK_SLOP 6
/* A held scrollbar arrow waits this many ticks, then steps every few:
 * the desktop's scrollbar rule. */
#define ARROW_WAIT 12
#define ARROW_EVERY 4

/** @brief What a character is, for words: a line break, space (a blank
 *         or a control character), part of a word (letters, digits, '_'
 *         and everything past ASCII), or punctuation. */
enum { RUN_NONE, RUN_BREAK, RUN_SPACE, RUN_WORD, RUN_PUNCT };

/** @brief The run kind of the character starting at @p pos. */
static int
run_at(const tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    int b = tiku_editdoc_byte_at(v->doc, pos);

    if (b < 0)
        return RUN_NONE;
    if (b == '\r' || b == '\n')
        return RUN_BREAK;
    if (b <= ' ' || b == 0x7F)
        return RUN_SPACE;
    if (b >= 0x80 || b == '_' || (b >= '0' && b <= '9') ||
        (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z'))
        return RUN_WORD;
    return RUN_PUNCT;
}

/** @brief The run kind of the character ending at @p pos. */
static int
run_before(const tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    return pos > 0 ? run_at(v, tiku_editdoc_prev(v->doc, pos)) : RUN_NONE;
}

tiku_edit_pos_t
tiku_cv_word_right(const tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    tiku_edit_pos_t end = tiku_editdoc_length(v->doc);
    int kind = run_at(v, pos);

    while (pos < end && run_at(v, pos) == kind)
        pos = tiku_editdoc_next(v->doc, pos);
    while (pos < end && run_at(v, pos) == RUN_SPACE)
        pos = tiku_editdoc_next(v->doc, pos);
    return pos;
}

tiku_edit_pos_t
tiku_cv_word_left(const tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    int kind;

    while (pos > 0 && run_before(v, pos) == RUN_SPACE)
        pos = tiku_editdoc_prev(v->doc, pos);
    kind = run_before(v, pos);
    while (pos > 0 && run_before(v, pos) == kind)
        pos = tiku_editdoc_prev(v->doc, pos);
    return pos;
}

/** @brief The run holding the character at @p pos, within its line. */
static tiku_edit_range_t
run_around(const tiku_codeview_t *v, tiku_edit_pos_t pos)
{
    tiku_edit_range_t r;
    int kind = run_at(v, pos);

    r.start = pos;
    r.end = pos;
    if (kind == RUN_NONE || kind == RUN_BREAK)
        return r;
    while (r.start > 0 && run_before(v, r.start) == kind)
        r.start = tiku_editdoc_prev(v->doc, r.start);
    while (run_at(v, r.end) == kind)
        r.end = tiku_editdoc_next(v->doc, r.end);
    return r;
}

/** @brief The run under the line's own @p x on @p line: past the end,
 *         the line's last run; an empty line gives its start. */
static tiku_edit_range_t
run_under(tiku_codeview_t *v, int64_t line, int64_t x)
{
    tiku_edit_pos_t start = tiku_editdoc_line_start(v->doc, line);
    tiku_edit_pos_t end = tiku_editdoc_line_end(v->doc, line);
    tiku_edit_pos_t at = tiku_cv_char_at(v, line, x);
    tiku_edit_range_t r;

    if (at < 0 && end > start)
        at = tiku_editdoc_prev(v->doc, end);
    if (at < 0) {
        r.start = r.end = start;
        return r;
    }
    return run_around(v, at);
}

/** @brief A line with its line break, the last line to the end. */
static tiku_edit_range_t
line_around(const tiku_codeview_t *v, int64_t line)
{
    tiku_edit_range_t r;

    r.start = tiku_editdoc_line_start(v->doc, line);
    r.end = tiku_editdoc_line_start(v->doc, line + 1);
    return r;
}

/** @brief The line x of the caret on its line. */
static int64_t
caret_x(tiku_codeview_t *v, tiku_edit_pos_t caret)
{
    return tiku_cv_x_of(v, tiku_editdoc_line_of(v->doc, caret), caret);
}

void
tiku_codeview_reveal(tiku_codeview_t *view)
{
    tiku_edit_pos_t anchor, caret;
    int64_t line, rows, x, width;

    if (view == NULL ||
        tiku_codeview_selection(view, &anchor, &caret) != TIKU_EDIT_OK)
        return;
    line = tiku_editdoc_line_of(view->doc, caret);
    rows = tiku_codeview_rows_visible(view);
    if (line < view->top)
        view->top = line;
    else if (line >= view->top + rows)
        view->top = line - rows + 1;
    x = caret_x(view, caret);
    width = view->bounds.w - TIKU_CV_PAD_X;
    if (x < view->left)
        view->left = x;
    else if (x >= view->left + width - 4)
        view->left = x - width + 8;
    if (view->left > x)
        view->left = x;
    tiku_cv_clamp(view);
}

/** @brief The caret one row up or down by @p rows, at the kept x. */
static tiku_edit_pos_t
vertical(tiku_codeview_t *v, tiku_edit_pos_t caret, int64_t rows)
{
    int64_t line = tiku_editdoc_line_of(v->doc, caret) + rows;
    int64_t last = tiku_editdoc_lines(v->doc) - 1;

    if (v->goal < 0)
        v->goal = caret_x(v, caret);
    if (line < 0)
        line = 0;
    if (line > last)
        line = last;
    return tiku_cv_pos_at(v, line, v->goal);
}

tiku_edit_status_t
tiku_codeview_move(tiku_codeview_t *view, tiku_codeview_move_t move,
                   int extend)
{
    tiku_edit_pos_t anchor, caret, lo, hi;
    tiku_edit_status_t st;
    int64_t line, goal;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_codeview_selection(view, &anchor, &caret);
    if (st != TIKU_EDIT_OK)
        return st;
    lo = anchor < caret ? anchor : caret;
    hi = anchor < caret ? caret : anchor;
    line = tiku_editdoc_line_of(view->doc, caret);
    goal = -1;
    switch (move) {
    case TIKU_CODEVIEW_CHAR_LEFT:
        caret = !extend && lo < hi ? lo : tiku_editdoc_prev(view->doc, caret);
        break;
    case TIKU_CODEVIEW_CHAR_RIGHT:
        caret = !extend && lo < hi ? hi : tiku_editdoc_next(view->doc, caret);
        break;
    case TIKU_CODEVIEW_WORD_LEFT:
        caret = tiku_cv_word_left(view, caret);
        break;
    case TIKU_CODEVIEW_WORD_RIGHT:
        caret = tiku_cv_word_right(view, caret);
        break;
    case TIKU_CODEVIEW_LINE_UP:
    case TIKU_CODEVIEW_LINE_DOWN:
    case TIKU_CODEVIEW_PAGE_UP:
    case TIKU_CODEVIEW_PAGE_DOWN: {
        int64_t rows = move == TIKU_CODEVIEW_LINE_UP ||
                       move == TIKU_CODEVIEW_LINE_DOWN ? 1 :
                       tiku_codeview_rows_visible(view);

        caret = vertical(view, caret, move == TIKU_CODEVIEW_LINE_UP ||
                         move == TIKU_CODEVIEW_PAGE_UP ? -rows : rows);
        goal = view->goal;
        break;
    }
    case TIKU_CODEVIEW_LINE_START:
        caret = tiku_editdoc_line_start(view->doc, line);
        break;
    case TIKU_CODEVIEW_LINE_END:
        caret = tiku_editdoc_line_end(view->doc, line);
        break;
    case TIKU_CODEVIEW_DOC_START:
        caret = 0;
        break;
    case TIKU_CODEVIEW_DOC_END:
        caret = tiku_editdoc_length(view->doc);
        break;
    default:
        return TIKU_EDIT_INVALID;
    }
    st = tiku_editdoc_view_select(view->doc, view->view,
                                  extend ? anchor : caret, caret);
    view->goal = goal;
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    return st;
}

tiku_edit_status_t
tiku_codeview_select_all(tiku_codeview_t *view)
{
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_codeview_select(view, 0, tiku_editdoc_length(view->doc));
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    return st;
}

/** @brief The line under the pointer, one row past either end of the
 *         visible rows at most, so a drag outside the view scrolls a
 *         line at a time. */
static int64_t
line_under(tiku_codeview_t *v, int y)
{
    int64_t step = tiku_codeview_row_height(v);
    int64_t row = (int64_t)y - tiku_cv_text_y(v);
    int64_t rows = tiku_codeview_rows_visible(v);
    int64_t line, lines = tiku_editdoc_lines(v->doc);

    row = row >= 0 ? row / step : -1;
    if (row > rows)
        row = rows;
    line = v->top + row;
    if (line < 0)
        line = 0;
    if (line >= lines)
        line = lines - 1;
    return line;
}

/** @brief The selection a drag gives with the pointer at @p x, @p y:
 *         from the press to the character, word or line under it. */
static void
drag_to(tiku_codeview_t *v, int x, int y)
{
    int64_t line = line_under(v, y);
    int64_t lx = (int64_t)x - tiku_cv_text_x(v) + v->left;
    tiku_edit_pos_t anchor, caret;
    tiku_edit_range_t r;

    if (v->unit == 1) {
        if (tiku_codeview_selection(v, &anchor, &caret) != TIKU_EDIT_OK)
            return;
        caret = tiku_cv_pos_at(v, line, lx);
    } else {
        r = v->unit == 2 ? run_under(v, line, lx) : line_around(v, line);
        if (r.start < v->pressed.start) {
            anchor = v->pressed.end;
            caret = r.start;
        } else if (r.end > v->pressed.end) {
            anchor = v->pressed.start;
            caret = r.end;
        } else {
            anchor = v->pressed.start;
            caret = v->pressed.end;
        }
    }
    if (tiku_editdoc_view_select(v->doc, v->view, anchor, caret) ==
        TIKU_EDIT_OK)
        tiku_codeview_reveal(v);
}

/** @brief Button 1 went down at the event's point. */
static void
press(tiku_codeview_t *v, const tiku_event_t *e)
{
    int64_t line = line_under(v, e->y);
    int64_t lx = (int64_t)e->x - tiku_cv_text_x(v) + v->left;
    tiku_edit_pos_t anchor, caret, was;
    int shift = (e->modifiers & TIKU_MOD_SHIFT) != 0;

    if (!shift && v->clicks > 0 && v->clicks < 3 &&
        e->time_us - v->click_at < CLICK_US &&
        e->x - v->click_x > -CLICK_SLOP && e->x - v->click_x < CLICK_SLOP &&
        e->y - v->click_y > -CLICK_SLOP && e->y - v->click_y < CLICK_SLOP)
        v->clicks++;
    else
        v->clicks = 1;
    v->click_at = e->time_us;
    v->click_x = e->x;
    v->click_y = e->y;
    v->goal = -1;
    v->dragging = 1;
    v->unit = v->clicks;
    if (v->clicks == 1) {
        caret = tiku_cv_pos_at(v, line, lx);
        if (!shift ||
            tiku_codeview_selection(v, &anchor, &was) != TIKU_EDIT_OK)
            anchor = caret;
        v->pressed.start = v->pressed.end = caret;
    } else {
        v->pressed = v->clicks == 2 ? run_under(v, line, lx) :
                                      line_around(v, line);
        anchor = v->pressed.start;
        caret = v->pressed.end;
    }
    if (tiku_editdoc_view_select(v->doc, v->view, anchor, caret) ==
        TIKU_EDIT_OK)
        tiku_codeview_reveal(v);
}

/** @brief Whether @p x, @p y is inside @p r. */
static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/** @brief One column: a space's width in the view's face. */
static int64_t
column(const tiku_codeview_t *v)
{
    int w = tiku_text_advance(tiku_cv_font(v), ' ');

    return w > 0 ? w : 1;
}

/** @brief Scroll by @p delta lines down, or pixels across. */
static void
scroll_by(tiku_codeview_t *v, int across, int64_t delta)
{
    if (across)
        tiku_codeview_scroll_to(v, v->top, v->left + delta);
    else
        tiku_codeview_scroll_to(v, v->top + delta, v->left);
}

/** @brief One arrow's step: a line down, a column across. */
static int64_t
bar_step(const tiku_codeview_t *v, int across)
{
    return across ? column(v) : 1;
}

/** @brief A page: what is shown, less one step. */
static int64_t
bar_page(tiku_codeview_t *v, int across)
{
    int64_t page = across ? tiku_cv_text_w(v) - column(v)
                          : tiku_codeview_rows_visible(v) - 1;

    return page > bar_step(v, across) ? page : bar_step(v, across);
}

/** @brief Button 1 went down on a scrollbar: an arrow steps and holds,
 *         the track pages, and the thumb is taken. */
static void
bar_press(tiku_codeview_t *v, int across, int x, int y)
{
    tiku_rect_t bar = tiku_cv_bar(v, across), thumb;
    int pos, frac, at, ends, len, start, size;

    tiku_cv_bar_place(v, across, &pos, &frac);
    thumb = tiku_ui_scrollbar_thumb(bar, (float)pos / 1000.0f,
                                    (float)frac / 1000.0f, across);
    at = across ? x - bar.x : y - bar.y;
    ends = across ? bar.h : bar.w;
    len = across ? bar.w : bar.h;
    start = across ? thumb.x - bar.x : thumb.y - bar.y;
    size = across ? thumb.w : thumb.h;
    if (at < ends || at >= len - ends) {
        v->arrow_bar = across + 1;
        v->arrow_dir = at < ends ? -1 : 1;
        v->arrow_pulses = 0;
        scroll_by(v, across, v->arrow_dir * bar_step(v, across));
    } else if (at >= start && at < start + size) {
        v->held_bar = across + 1;
        v->grab = at - start;
    } else {
        scroll_by(v, across, (at < start ? -1 : 1) * bar_page(v, across));
    }
}

/** @brief The thumb held, the pointer at @p x, @p y: put the thumb's
 *         start where the pointer's grab says. */
static void
bar_drag(tiku_codeview_t *v, int x, int y)
{
    int across = v->held_bar - 1;
    tiku_rect_t bar = tiku_cv_bar(v, across), thumb;
    int pos, frac, ends, travel, at;
    int64_t total, visible, value;

    tiku_cv_bar_place(v, across, &pos, &frac);
    thumb = tiku_ui_scrollbar_thumb(bar, (float)pos / 1000.0f,
                                    (float)frac / 1000.0f, across);
    ends = across ? bar.h : bar.w;
    travel = (across ? bar.w : bar.h) - 2 * ends -
             (across ? thumb.w : thumb.h);
    if (travel <= 0)
        return;
    at = (across ? x - bar.x : y - bar.y) - ends - v->grab;
    if (at < 0)
        at = 0;
    if (at > travel)
        at = travel;
    tiku_codeview_extent(v, across, &total, &visible, &value);
    value = ((int64_t)at * (total - visible) + travel / 2) / travel;
    if (across)
        tiku_codeview_scroll_to(v, v->top, value);
    else
        tiku_codeview_scroll_to(v, value, v->left);
}

/** @brief A tick with an arrow held: step once the wait is over. */
static int
arrow_tick(tiku_codeview_t *v)
{
    if (v->arrow_bar == 0)
        return 0;
    v->arrow_pulses++;
    if (v->arrow_pulses < ARROW_WAIT ||
        (v->arrow_pulses - ARROW_WAIT) % ARROW_EVERY != 0)
        return 0;
    scroll_by(v, v->arrow_bar - 1,
              v->arrow_dir * bar_step(v, v->arrow_bar - 1));
    return 1;
}

/** @brief The wheel: rows down, or columns across with Shift or a
 *         sideways turn; the caret stays where it is. */
static void
wheel(tiku_codeview_t *v, const tiku_event_t *e)
{
    if (e->modifiers & TIKU_MOD_SHIFT)
        scroll_by(v, 1, (int64_t)e->dy * column(v));
    else if (e->dy != 0)
        scroll_by(v, 0, e->dy);
    if (e->dx != 0)
        scroll_by(v, 1, (int64_t)e->dx * column(v));
}

/** @brief A key the view moves by; 0 for any other. */
static int
key(tiku_codeview_t *v, const tiku_event_t *e)
{
    int extend = (e->modifiers & TIKU_MOD_SHIFT) != 0;
    int cmd = (e->modifiers & TIKU_MOD_CMD) != 0;
    int opt = (e->modifiers & TIKU_MOD_OPTION) != 0;
    tiku_codeview_move_t move;

    switch (e->key) {
    case TIKU_KEY_LEFT:
        move = cmd || opt ? TIKU_CODEVIEW_WORD_LEFT : TIKU_CODEVIEW_CHAR_LEFT;
        break;
    case TIKU_KEY_RIGHT:
        move = cmd || opt ? TIKU_CODEVIEW_WORD_RIGHT :
                            TIKU_CODEVIEW_CHAR_RIGHT;
        break;
    case TIKU_KEY_UP:
    case TIKU_KEY_DOWN:
    case TIKU_KEY_PAGE_UP:
    case TIKU_KEY_PAGE_DOWN:
        if (cmd || opt)
            return 0;
        move = e->key == TIKU_KEY_UP ? TIKU_CODEVIEW_LINE_UP :
               e->key == TIKU_KEY_DOWN ? TIKU_CODEVIEW_LINE_DOWN :
               e->key == TIKU_KEY_PAGE_UP ? TIKU_CODEVIEW_PAGE_UP :
                                            TIKU_CODEVIEW_PAGE_DOWN;
        break;
    case TIKU_KEY_HOME:
    case TIKU_KEY_END:
        if (opt)
            return 0;
        move = e->key == TIKU_KEY_HOME ?
               (cmd ? TIKU_CODEVIEW_DOC_START : TIKU_CODEVIEW_LINE_START) :
               (cmd ? TIKU_CODEVIEW_DOC_END : TIKU_CODEVIEW_LINE_END);
        break;
    default:
        return 0;
    }
    (void)tiku_codeview_move(v, move, extend);
    return 1;
}

/** @brief An event the view's text, scrollbars and caret take, with
 *         @p commit what staging made of it. */
static int
dispatch(tiku_codeview_t *view, const tiku_event_t *e, int commit)
{
    int across;

    switch (e->type) {
    case TIKU_EVENT_TEXT_INPUT:
        return tiku_cv_edit_commit(view, commit);
    case TIKU_EVENT_KEY_DOWN:
        return key(view, e) || tiku_cv_edit_key(view, e);
    case TIKU_EVENT_POINTER_DOWN:
        if (e->button != 1 || !inside(view->bounds, e->x, e->y))
            return 0;
        for (across = 0; across < 2; across++) {
            if (inside(tiku_cv_bar(view, across), e->x, e->y)) {
                bar_press(view, across, e->x, e->y);
                return 1;
            }
        }
        if (!inside(tiku_cv_area(view), e->x, e->y))
            return 0;
        press(view, e);
        return 1;
    case TIKU_EVENT_POINTER_MOVE:
    case TIKU_EVENT_POINTER_UP:
        if (e->type == TIKU_EVENT_POINTER_UP && e->button != 1)
            return 0;
        if (view->held_bar) {
            bar_drag(view, e->x, e->y);
            if (e->type == TIKU_EVENT_POINTER_UP)
                view->held_bar = 0;
            return 1;
        }
        if (view->arrow_bar) {
            if (e->type == TIKU_EVENT_POINTER_UP)
                view->arrow_bar = 0;
            return 1;
        }
        if (!view->dragging)
            return 0;
        drag_to(view, e->x, e->y);
        if (e->type == TIKU_EVENT_POINTER_UP)
            view->dragging = 0;
        return 1;
    case TIKU_EVENT_WHEEL:
        wheel(view, e);
        return 1;
    case TIKU_EVENT_TICK:
        return arrow_tick(view);
    case TIKU_EVENT_DEACTIVATED:
        view->dragging = 0;
        view->held_bar = 0;
        view->arrow_bar = 0;
        return 0;
    default:
        return 0;
    }
}

int
tiku_codeview_event(tiku_codeview_t *view, const tiku_event_t *e)
{
    int commit, taken;

    if (view == NULL || e == NULL)
        return 0;
    view->status = TIKU_EDIT_OK;
    commit = tiku_text_input_event(&view->input, e);
    taken = tiku_cv_assist_event(view, e) || dispatch(view, e, commit);
    tiku_cv_assist_follow(view);
    return taken;
}
