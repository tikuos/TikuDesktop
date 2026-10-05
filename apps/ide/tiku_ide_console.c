/* tiku_ide_console.c -- the Messages window, where a run's or build's
 * output becomes rows that can name a file and line, and the console
 * of a board run.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static const char * msg_name(void *ctx, int row);

/** @brief The text of message @p row, for the list painter; NULL past
 *         the last. */
static const char *
msg_name(void *ctx, int row)
{
    ide_t *st = ctx;

    return (row >= 0 && row < st->nmsg) ? st->msg[row].text : NULL;
}

/** @brief Paint the Messages window: the list, and under it the run's
 *         state or a waiting program's prompt and answer field. */
void
tiku_ide_messages_paint(ide_t *st)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t all = { 0, 0, st->msg_w, st->msg_h };
    tiku_rect_t strip = { 0, st->msg_h - STRIP_H, st->msg_w, STRIP_H };
    tiku_rect_t body = { MARGIN, MARGIN, st->msg_w - 2 * MARGIN,
                              st->msg_h - STRIP_H - 2 * MARGIN };
    char says[160];

    if (st->msg_surface == NULL) {
        return;
    }
    tiku_fill(st->msg_surface, all, TIKU_C_PANEL);
    tiku_ui_sunken(st->msg_surface, body, TIKU_C_DOC);
    tiku_list_draw(&st->msg_list, st->msg_surface, tiku_inset(body, 2),
                   msg_name, st);
        tiku_fill(st->msg_surface, strip, TIKU_C_PANEL);
    if (st->asking) {
        /* A program waiting at INPUT: its prompt as the label and the
         * answer field beside it; Return sends the answer. */
        int lw = tiku_text_width(f, st->ask_prompt);
        tiku_rect_t fld = { MARGIN + lw + 6, strip.y + 3,
                            st->msg_w - (MARGIN + lw + 6) - MARGIN,
                            STRIP_H - 6 };

        tiku_text(st->msg_surface, f, MARGIN,
                       strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                       st->ask_prompt, TIKU_C_TEXT);
        tiku_ui_textfield(st->msg_surface, fld, st->answer.text,
                          st->answer.caret, 0u);
    } else {
        snprintf(says, sizeof says, "%s",
                 st->running ? "running…" : "the run has finished");
        tiku_text(st->msg_surface, f, MARGIN,
                       strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                       says, TIKU_C_TEXT);
    }
    (void)st->services->frame(st->services->ctx, st->msg_id,
                              st->msg_surface->px, st->msg_w, st->msg_h);
}

/** @brief Publish the Messages window's one menu item: "Go to line N"
 *         when the chosen message names a line, otherwise a disabled
 *         row saying it names none. */
void
tiku_ide_messages_publish(ide_t *st)
{
    tiku_menuset_t menus;
    int row = tiku_list_chosen(&st->msg_list);
    int links = (row >= 0 && row < st->nmsg && st->msg[row].line > 0);

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Message");
    menus.menu[0].nitem = 1;
    if (links) {
        snprintf(menus.menu[0].item[0].label,
                 sizeof menus.menu[0].item[0].label, "Go to line %d",
                 st->msg[row].line);
    } else {
        snprintf(menus.menu[0].item[0].label,
                 sizeof menus.menu[0].item[0].label,
                 "This one is not about anywhere");
    }
    menus.menu[0].item[0].command = CMD_FOLLOW;
    menus.menu[0].item[0].enabled = (unsigned char)links;
    (void)st->services->menus(st->services->ctx, st->msg_id, &menus);
}

/** @brief Open the Messages window unless it is open; when the desktop
 *         refuses the window, the Project window's note says so. */
void
tiku_ide_messages_open(ide_t *st)
{
    if (st->msg_surface != NULL) {
        return;
    }
    st->msg_w = MSG_W;
    st->msg_h = MSG_H;
    st->msg_surface = tiku_surface_new(MSG_W, MSG_H, TIKU_C_PANEL);
    if (st->msg_surface == NULL) {
        return;
    }
    st->msg_id = st->services->open(st->services->ctx, "Messages",
                                    MSG_W, MSG_H);
    if (st->msg_id == 0u) {
        tiku_surface_free(st->msg_surface);
        st->msg_surface = NULL;
        snprintf(st->note, sizeof st->note, "no room for the messages");
        tiku_ide_project_paint(st);
        return;
    }
    tiku_ide_window_sizeable(st, st->msg_id);
    tiku_list_init(&st->msg_list, 1);
}

/**
 * @brief Add one line of a run's or build's output to the messages.
 *
 * BASIC reports an error's place on the next line: "? syntax", then
 * "at line 20".  An "at line" line is folded onto the row before it only
 * when that row holds "? ", so a program that prints those words keeps
 * them.  A compiler names the file and line at the head of its row.
 */
void
tiku_ide_message_take(ide_t *st, const char *line)
{
    unsigned n = 0u;
    msg_t *m;

    if (st->nmsg >= IDE_MSGS) {
        return;
    }
    /* The marker is not always at the head of the row: a PRINT that
     * ended in a semicolon leaves the line open, and the error is
     * written onto the end of it -- "value: ? division by zero". */
    if (sscanf(line, "at line %u", &n) == 1 && st->nmsg > 0 &&
        strstr(st->msg[st->nmsg - 1].text, "? ") != NULL) {
        m = &st->msg[st->nmsg - 1];
        m->line = (int)n;
        m->numbered = 1;
        snprintf(m->file, sizeof m->file, "%s", st->child_file);
        {
            char whole[160];

            snprintf(whole, sizeof whole, "%s, at line %u", m->text, n);
            snprintf(m->text, sizeof m->text, "%s", whole);
        }
        return;
    }
    /*
     * Under each diagnostic, GCC and Clang quote the source line as a
     * numbered gutter and a row of carets:
     *
     *     bad.c:4:12: error: use of undeclared identifier 'nope'
     *         4 |     return nope;
     *           |            ^~~~
     *
     * The row above already names that line, so for a compiler these
     * rows are dropped.  A BASIC program's output that looks like them
     * is kept.
     */
    if (!st->child_numbered) {
        const char *p = line;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            p++;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '|' && p != line) {
            return;
        }
    }
    m = &st->msg[st->nmsg++];
    memset(m, 0, sizeof *m);
    snprintf(m->text, sizeof m->text, "%s", line);
    if (st->child_numbered) {
        return;                 /* BASIC says where on its own line */
    }
    /*
     * A compiler puts the place first, "file.c:12:5: error: ...", so the
     * file, line and column are read from this row.  The file is kept as
     * written, since the compiler names the path it was given.
     */
    {
        const char *colon = strchr(line, ':');
        unsigned at = 0u, col = 0u;

        if (colon != NULL && colon != line &&
            sscanf(colon + 1, "%u:%u", &at, &col) >= 1 && at > 0u) {
            size_t len = (size_t)(colon - line);

            if (len < sizeof m->file) {
                memcpy(m->file, line, len);
                m->file[len] = '\0';
                m->line = (int)at;
                m->col = (int)col;
            }
        }
    }
}

/**
 * @brief The text line that holds BASIC line number @p want, found by
 *        reading the number each line starts with.
 *
 * @return the line, or -1 when no line has that number.
 */
int64_t
tiku_ide_line_of_number(const edit_t *e, long want)
{
    int64_t i, found = -1, lines = tiku_editdoc_lines(e->doc);

    for (i = 0; i < lines; i++) {
        /* The last match: the interpreter keeps the last line stored
         * under a number, so when a file repeats a number, the later
         * line is the one that ran. */
        if (tiku_ide_line_number(e, i, 1) == want) {
            found = i;
        }
    }
    return found;
}

/** @brief Add one line to the console, dropping the oldest when it is
 *         full; @p dim marks the console's own remarks. */
void
tiku_ide_console_take(ide_t *st, const char *line, int dim)
{
    if (st->ncon >= CON_LINES) {
        memmove(st->con[0], st->con[1], (size_t)(CON_LINES - 1) * CON_COLS);
        memmove(st->con_dim, st->con_dim + 1, (size_t)(CON_LINES - 1));
        st->ncon = CON_LINES - 1;
    }
    snprintf(st->con[st->ncon], CON_COLS, "%s", line);
    st->con_dim[st->ncon] = (unsigned char)dim;
    st->ncon++;
    st->con_top = -1;               /* new output shows the tail */
}

/** @brief Paint the console and send its frame to the desktop. */
void
tiku_ide_console_paint(ide_t *st)
{
    const tiku_font_t *f = tiku_font_mono(0);
    tiku_rect_t all = { 0, 0, st->con_w, st->con_h };
    tiku_rect_t strip = { 0, st->con_h - STRIP_H, st->con_w, STRIP_H };
    int lh = f->height + 2;
    int rows = (st->con_h - STRIP_H - 2 * MARGIN) / lh;
    int first, i, y;

    if (st->con_surface == NULL) {
        return;
    }
    tiku_fill(st->con_surface, all, CON_BACK);
    first = (st->con_top >= 0) ? st->con_top : st->ncon - rows;
    if (first < 0) {
        first = 0;
    }
    y = MARGIN;
    for (i = first; i < st->ncon && i < first + rows; i++) {
        tiku_text(st->con_surface, f, MARGIN, y + f->ascent, st->con[i],
                  st->con_dim[i] ? CON_DIM : CON_INK);
        y += lh;
    }
    tiku_fill(st->con_surface, strip, 0x202020u);
    if (st->asking) {
        /* A program waiting at INPUT: its prompt and the answer after
         * it; Return sends the answer. */
        int lw = tiku_text_width(f, st->ask_prompt);
        tiku_rect_t fld = { MARGIN + lw + 6, strip.y + 3,
                            st->con_w - (MARGIN + lw + 6) - MARGIN,
                            STRIP_H - 6 };

        tiku_text(st->con_surface, f, MARGIN,
                  strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                  st->ask_prompt, CON_INK);
        tiku_ui_textfield(st->con_surface, fld, st->answer.text,
                          st->answer.caret, 0u);
    } else {
        tiku_text(st->con_surface, f, MARGIN,
                  strip.y + (STRIP_H - f->height) / 2 + f->ascent,
                  st->running ? "running   (Cmd+C stops it)"
                              : "the run has finished", CON_DIM);
    }
    (void)st->services->frame(st->services->ctx, st->con_id,
                              st->con_surface->px, st->con_w, st->con_h);
}

/** @brief Publish the console's one menu: Stop, enabled while the run
 *         goes. */
void
tiku_ide_console_publish(ide_t *st)
{
    tiku_menuset_t menus;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Console");
    tiku_ide_mrow(&menus.menu[0], "Stop", CMD_STOP, 'c', TIKU_MOD_CMD, st->running);
    (void)st->services->menus(st->services->ctx, st->con_id, &menus);
}

/**
 * @brief Open the console for a run of @p file on @p board, or bring it
 *        to the front cleared, and say what is being sent where.
 */
void
tiku_ide_console_open(ide_t *st, const char *file, const char *board)
{
    const char *leaf = strrchr(file, '/');
    char line[160];

    leaf = (leaf != NULL) ? leaf + 1 : file;
    snprintf(st->con_title, sizeof st->con_title, "%s on %s", leaf, board);
    st->ncon = 0;
    st->con_top = -1;
    if (st->con_surface == NULL) {
        st->con_w = CON_W;
        st->con_h = CON_H;
        st->con_surface = tiku_surface_new(CON_W, CON_H, CON_BACK);
        if (st->con_surface == NULL) {
            return;
        }
        st->con_id = st->services->open(st->services->ctx, st->con_title,
                                        CON_W, CON_H);
        if (st->con_id == 0u) {
            tiku_surface_free(st->con_surface);
            st->con_surface = NULL;
            return;
        }
        tiku_ide_window_sizeable(st, st->con_id);
    } else if (st->services->raise_window != NULL) {
        (void)st->services->raise_window(st->services->ctx, st->con_id);
    }
    snprintf(line, sizeof line, "sending %s to %s...", leaf, board);
    tiku_ide_console_take(st, line, 1);
    tiku_ide_console_paint(st);
    tiku_ide_console_publish(st);
}

/** @brief Open the file a message was about, at the line it named. */
void
tiku_ide_message_follow(ide_t *st)
{
    int row = tiku_list_chosen(&st->msg_list);
    const msg_t *m;
    edit_t *e;
    int64_t at;

    if (row < 0 || row >= st->nmsg) {
        return;
    }
    m = &st->msg[row];
    if (m->line <= 0 || m->file[0] == '\0') {
        return;
    }
    e = tiku_ide_edit_of_path(st, m->file);
    if (e == NULL) {
        prow_t want;

        memset(&want, 0, sizeof want);
        snprintf(want.path, sizeof want.path, "%s", m->file);
        {
            const char *slash = strrchr(m->file, '/');

            snprintf(want.name, sizeof want.name, "%s",
                     (slash != NULL) ? slash + 1 : m->file);
        }
        tiku_ide_edit_open(st, &want);
        e = tiku_ide_edit_of_path(st, m->file);
    }
    if (e == NULL) {
        return;
    }
    /* A compiler counts the file's lines; BASIC gives a line number. */
    at = m->numbered ? tiku_ide_line_of_number(e, m->line) : m->line - 1;
    if (at >= 0) {
        /* The caret goes to the line's start with nothing selected;
         * placing it does not scroll, so the page is then scrolled to
         * it. */
        tiku_ide_place(e, at, 0);
        tiku_ide_edit_reveal(e);
        tiku_ide_edit_paint(st, e);
    }
    if (st->services->raise_window != NULL) {
        (void)st->services->raise_window(st->services->ctx, e->id);
    }
}

/** @brief Show the messages: open the window once there are any, choose
 *         the first row that names a line when none is chosen, and mark
 *         the problems on the pages. */
void
tiku_ide_messages_show(ide_t *st)
{
    if (st->nmsg > 0) {
        tiku_ide_messages_open(st);
    }
    if (st->msg_surface == NULL) {
        return;
    }
    (void)tiku_list_set_count(&st->msg_list, st->nmsg);
    /*
     * When no row is chosen, the first row that names a line is chosen;
     * a build's output can open with a row that names none, such as
     * GCC's "In function" row.  A row already chosen stays chosen.
     */
    if (tiku_list_chosen(&st->msg_list) < 0) {
        int i;

        for (i = 0; i < st->nmsg; i++) {
            if (st->msg[i].line > 0) {
                tiku_list_select_only(&st->msg_list, i);
                break;
            }
        }
    }
    tiku_ide_messages_paint(st);
    tiku_ide_messages_publish(st);
    tiku_ide_problems_show(st);
}
