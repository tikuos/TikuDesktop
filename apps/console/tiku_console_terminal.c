/* tiku_console_terminal.c -- the console's lines and the decoder that
 * fills them: UTF-8 text with ANSI colour, bold and line editing, decoded
 * across reads; and the command history.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_console_private.h"

static void next_line(console_t *st);
static void put_glyph(console_t *st, const char *value);
static void ansi_end(console_t *st, int final);

/** The line being written: the newest in the ring. */
line_t *tiku_console_last_line(console_t *st)
{ return &st->lines[(st->head + st->count - 1) % ROWS]; }

/** Start a new line, dropping the oldest when the ring of ROWS is full. */
static void next_line(console_t *st)
{
    if (st->count == ROWS) st->head = (st->head + 1) % ROWS;
    else st->count++;
    memset(tiku_console_last_line(st), 0, sizeof(line_t)); st->column = 0;
}

/** Put one glyph at the cursor in the current ink and weight, starting a
 *  new line at COLS. */
static void put_glyph(console_t *st, const char *value)
{
    if (st->column == COLS) next_line(st);
    line_t *l = tiku_console_last_line(st);
    glyph_t *g = &l->cell[st->column++];
    snprintf(g->glyph, sizeof g->glyph, "%s", value);
    g->ink = (unsigned char)st->ink; g->bold = (unsigned char)st->bold;
    if (l->length < st->column) l->length = st->column;
}

/** Carry out the CSI sequence ending in @p final: colours and bold (m),
 *  erase in line (K), and moving the cursor's column (G, D, C).  Any
 *  other sequence is ignored. */
static void ansi_end(console_t *st, int final)
{
    line_t *l = tiku_console_last_line(st);
    int p = st->param[0];
    if (final == 'm') for (int i = 0; i < st->nparam; i++) {
        int n = st->param[i];
        if (!n) { st->ink = 16; st->bold = 0; }
        else if (n == 1) st->bold = 1;
        else if (n == 22) st->bold = 0;
        else if (n == 39) st->ink = 16;
        else if (n >= 30 && n <= 37) st->ink = n - 30;
        else if (n >= 90 && n <= 97) st->ink = n - 90 + 8;
        else if (n == 38 || n == 48) {
            /* An extended colour (38 or 48) is skipped with its
             * arguments, so they are not read as other SGR codes. */
            if (i + 1 < st->nparam) i += st->param[i + 1] == 2 ? 4 : 2;
        }
    }
    if (final == 'K') {
        int first = p == 0 ? st->column : 0;
        int end = p == 1 ? st->column + 1 : COLS;
        if (end > COLS) end = COLS;
        for (int i = first; i < end; i++) memset(&l->cell[i], 0, sizeof(glyph_t));
        if (p == 0) l->length = st->column;
        if (p == 2) l->length = 0;
    }
    if (final == 'G') st->column = p > 0 ? (p > COLS ? COLS - 1 : p - 1) : 0;
    if (final == 'D') { st->column -= p ? p : 1; if (st->column < 0) st->column = 0; }
    if (final == 'C') { st->column += p ? p : 1; if (st->column >= COLS) st->column = COLS - 1; }
}

/** Feed one byte of text from the board to the decoder: CSI and OSC
 *  sequences, UTF-8, control characters and printable text. */
void tiku_console_text_byte(console_t *st, unsigned char ch)
{
    if (st->scan == 3 || st->scan == 4) {
        if (ch == 7 || (st->scan == 4 && ch == '\\')) st->scan = 0;
        else st->scan = ch == 27 ? 4 : 3;
        return;
    }
    if (st->scan == 1) {
        st->scan = ch == '[' ? 2 : ch == ']' ? 3 : 0;
        memset(st->param, 0, sizeof st->param); st->nparam = 1; return;
    }
    if (st->scan == 2) {
        if (ch >= '0' && ch <= '9') {
            int *p = &st->param[st->nparam - 1];
            *p = *p < 1000 ? *p * 10 + ch - '0' : 10000;
        } else if (ch == ';' && st->nparam < 8) st->nparam++;
        else if (ch >= '@' && ch <= '~') { ansi_end(st, ch); st->scan = 0; }
        else if (ch == 27) st->scan = 1;
        return;
    }
    if (st->utf_need) {
        if ((ch & 0xc0) == 0x80) {
            st->utf[st->utf_n++] = (char)ch;
            if (--st->utf_need == 0) {
                st->utf[st->utf_n] = 0;
                tiku_text_input_t check;
                if (tiku_text_input_set(&check, st->utf, (size_t)st->utf_n)) put_glyph(st, st->utf);
                else put_glyph(st, "?");
            }
            return;
        }
        st->utf_need = 0; put_glyph(st, "?");
    }
    if (ch == 27) st->scan = 1;
    else if (ch == '\n') next_line(st);
    else if (ch == '\r') st->column = 0;
    else if (ch == '\b' || ch == 127) { if (st->column > 0) st->column--; }
    else if (ch == '\t') { int n = 8 - st->column % 8; while (n--) put_glyph(st, " "); }
    else if (ch >= 32 && ch < 127) { char s[2] = {(char)ch, 0}; put_glyph(st, s); }
    else if (ch >= 0xc2 && ch <= 0xf4) {
        st->utf[0] = (char)ch; st->utf_n = 1;
        st->utf_need = ch < 0xe0 ? 1 : ch < 0xf0 ? 2 : 3;
    } else if (ch >= 128) put_glyph(st, "?");
    st->dirty = 1;
}

/** The text of line @p l into @p out, a blank for each empty cell. */
void tiku_console_line_string(const line_t *l, char *out, size_t cap)
{
    size_t n = 0;
    for (int i = 0; i < l->length; i++) {
        const char *s = *l->cell[i].glyph ? l->cell[i].glyph : " ";
        size_t k = strlen(s); if (n + k >= cap) break;
        memcpy(out + n, s, k); n += k;
    }
    out[n] = 0;
}

/** Whether @p needle occurs in @p text, ignoring case; an empty needle
 *  always does. */
int tiku_console_contains(const char *text, const char *needle)
{
    if (!*needle) return 1;
    for (; *text; text++) {
        size_t n = 0;
        while (needle[n] && text[n] && tolower((unsigned char)needle[n]) ==
               tolower((unsigned char)text[n])) n++;
        if (!needle[n]) return 1;
    }
    return 0;
}

/** Rebuild the list of lines that pass the Find filter. */
void tiku_console_rebuild(console_t *st)
{
    st->visible = 0;
    for (int i = 0; i < st->count; i++) {
        int at = (st->head + i) % ROWS; char line[COLS * 4 + 1];
        tiku_console_line_string(&st->lines[at], line, sizeof line);
        if (tiku_console_contains(line, st->field[1].text)) st->index[st->visible++] = at;
    }
    tiku_list_set_count(&st->list, st->visible);
}

/** Add @p value to the console as a line of its own. */
void tiku_console_message(console_t *st, const char *value)
{
    if (tiku_console_last_line(st)->length) next_line(st);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) tiku_console_text_byte(st, *p);
    next_line(st); st->dirty = 1;
}

/** Add @p command to the history of the last 32 commands, unless it
 *  repeats the newest.  tiku_console_target_changed() clears the history
 *  when another board is chosen. */
void tiku_console_history_add(console_t *st, const char *command)
{
    if (st->nhistory && !strcmp(st->history[st->nhistory - 1], command)) return;
    if (st->nhistory == 32) {
        memmove(st->history, st->history + 1, 31 * sizeof st->history[0]); st->nhistory--;
    }
    snprintf(st->history[st->nhistory++], sizeof st->history[0], "%s", command);
    st->history_at = st->nhistory; st->draft[0] = 0;
}

/** Move through the command history by @p by; the line being typed is
 *  kept as a draft to come back to. */
void tiku_console_history_move(console_t *st, int by)
{
    if (st->history_at == st->nhistory)
        memcpy(st->draft, st->field[0].text, sizeof st->draft);
    st->history_at += by;
    if (st->history_at < 0) st->history_at = 0;
    if (st->history_at > st->nhistory) st->history_at = st->nhistory;
    tiku_field_start(&st->field[0], st->history_at == st->nhistory ?
                     st->draft : st->history[st->history_at]);
    tiku_field_input_init(&st->input[0]);
}
