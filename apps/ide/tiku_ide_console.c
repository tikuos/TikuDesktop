/* tiku_ide_console.c -- console.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static const char *
msg_name(void *ctx, int row);

static const char *
msg_name(void *ctx, int row)
{
    ide_t *st = ctx;

    return (row >= 0 && row < st->nmsg) ? st->msg[row].text : NULL;
}

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
        /* The program is waiting for a person: its own prompt as the
         * label, the answer typed beside it, Return to send it. */
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

/**
 * @brief The message window's menus.
 *
 * One row, and it is the gesture the window exists for: take me to
 * where this was about.
 */
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

/** @brief Make the message window when there is first something to say. */
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
 * @brief Take one line a run said.
 *
 * The interpreter names where an error was on the line after saying
 * what it was, "? syntax" then "at line 20"; the two become one row,
 * since a row that is only a number is one nobody can act on.  The
 * annotation is folded only onto a row that was an error, the one place
 * the interpreter puts it, so a program printing those words keeps them.
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
     * Under each complaint a compiler quotes the source and points at
     * it, as a numbered gutter and a row of carets:
     *
     *     bad.c:4:12: error: use of undeclared identifier 'nope'
     *         4 |     return nope;
     *           |            ^~~~
     *
     * Both of them do, in the same shape.  Neither is a thing SAID --
     * they are the line the row above already names, and following that
     * row opens the file there and shows it in full.  As rows they are
     * furniture a person can select and get nowhere from, so they are
     * folded away and the complaint stands alone.  Only for a compiler:
     * a BASIC program printing a bar of its own keeps it.
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
     * A compiler says where at the HEAD of what it says --
     * "file.c:12:5: error: ..." -- so the place and the words arrive
     * together and there is nothing to fold.  The file is taken as
     * written: a compiler asked about a path answers about that path.
     */
    {
        const char *colon = strchr(line, ':');
        unsigned at = 0u;

        if (colon != NULL && colon != line &&
            sscanf(colon + 1, "%u", &at) == 1 && at > 0u) {
            size_t len = (size_t)(colon - line);

            if (len < sizeof m->file) {
                memcpy(m->file, line, len);
                m->file[len] = '\0';
                m->line = (int)at;
            }
        }
    }
}

/**
 * @brief Which text line holds the program's line @p want.
 *
 * A BASIC line WEARS its number, so the file itself says where the
 * interpreter meant -- there is no table to keep in step.
 *
 * @return the line, or -1 when the file has no such number.
 */
int
tiku_ide_line_of_number(const tiku_textview_t *tv, int want)
{
    int i, found = -1;

    for (i = 0; i < tiku_textview_lines(tv); i++) {
        const char *p = tiku_textview_line(tv, i);
        long got;
        char *end;

        if (p == NULL) {
            break;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        got = strtol(p, &end, 10);
        if (end != p && got == (long)want) {
            /* The LAST of them: storing a line REPLACES one already
             * numbered the same, so a file with the number twice ran
             * the second and the first was never in the program. */
            found = i;
        }
    }
    return found;
}

/** @brief One line onto the console, the oldest dropped when full. */
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

/** @brief Paint the console and hand the frame over. */
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
        /* The program wants a line: its prompt, and the answer typed
         * after it, Return to send. */
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

/** @brief The console's one menu: stop the run. */
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
    int at;

    if (row < 0 || row >= st->nmsg) {
        return;
    }
    m = &st->msg[row];
    if (m->line <= 0 || m->file[0] == '\0') {
        return;             /* a message about nowhere leads nowhere */
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
    /* A compiler counted the lines of the file; BASIC named one. */
    at = m->numbered ? tiku_ide_line_of_number(&e->tv, m->line) : m->line - 1;
    if (at >= 0) {
        tiku_textview_place(&e->tv, at, 0);
        /* Placing the caret does not move the PAGE, and a caret below
         * the window is a jump that looks like nothing happening. */
        (void)tiku_textview_reveal(&e->tv, tiku_ide_edit_rows(e));
        tiku_ide_edit_paint(st, e);
    }
    if (st->services->raise_window != NULL) {
        (void)st->services->raise_window(st->services->ctx, e->id);
    }
}

void
tiku_ide_messages_show(ide_t *st)
{
    if (st->nmsg > 0) {
        tiku_ide_messages_open(st);      /* now there is something to put in it */
    }
    if (st->msg_surface == NULL) {
        return;
    }
    (void)tiku_list_set_count(&st->msg_list, st->nmsg);
    /*
     * Opened on the first row that names a place.  What a person wants
     * from a build that failed is the first error, ready to be gone to,
     * and it is never the first ROW: one compiler says which function it
     * was in before saying anything went wrong, another counts up the
     * errors after.  Choosing by what a row IS rather than where it sits
     * is also the only way to say this that holds for both.
     *
     * Only when nothing is chosen, so a second look at the same list
     * keeps whatever the person was reading.
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
}
