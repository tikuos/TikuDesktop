/* tiku_ide_assist.c -- assist.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static int
assist_word(int ch);
static int
assist_context(edit_t *e, char *word, size_t max);
static void
assist_refresh(edit_t *e);
static int
assist_text(edit_t *e, int x, int y, int width, const char *text);

/* Editor assistance shares its completion vocabulary with syntax colouring. */
typedef struct { const char *word, *syntax, *detail, *example; } basic_help_t;
static const basic_help_t basic_help[] = {
    {"PRINT", "PRINT expression [; expression ...]", "Print values. A trailing semicolon keeps the line open.", "10 PRINT \"Value: \"; A"},
    {"LET", "[LET] variable = expression", "Assign an integer or string variable.", "10 LET A = 42"},
    {"IF", "IF condition THEN statement [ELSE statement]", "Choose a branch; multi-line IF ends with END IF.", "20 IF A > 0 THEN PRINT A"},
    {"FOR", "FOR variable = start TO end [STEP amount]", "A counted loop; close it with NEXT.", "10 FOR I = 1 TO 10"},
    {"NEXT", "NEXT [variable]", "Advance a FOR loop's counter.", "30 NEXT I"},
    {"WHILE", "WHILE condition ... WEND", "Repeat while the condition is nonzero.", "10 WHILE A < 10"},
    {"REPEAT", "REPEAT ... UNTIL condition", "Run the body at least once.", "30 UNTIL A >= 10"},
    {"GOTO", "GOTO line-number-or-label", "Continue execution at another program line.", "40 GOTO 10"},
    {"GOSUB", "GOSUB line-number-or-label", "Call a subroutine; RETURN returns to the caller.", "20 GOSUB 100"},
    {"RETURN", "RETURN", "Return from GOSUB.", "130 RETURN"},
    {"INPUT", "INPUT [\"prompt\";] variable", "Wait for a value in the program console.", "10 INPUT \"Name\"; A$"},
    {"END", "END", "Finish the program.", "100 END"},
    {"STOP", "STOP", "Finish the program; use debugger Step for pausing.", "100 STOP"},
    {"REM", "REM comment", "Everything after REM is a comment.", "10 REM Read the sensor"},
    {"DIM", "DIM A(size) [, B(rows, columns)]", "Allocate arrays; firmware memory limits apply.", "10 DIM A(10)"},
    {"DATA", "DATA value [, value ...]", "Declare values consumed by READ.", "10 DATA 5, 10, 15"},
    {"READ", "READ variable [, variable ...]", "Consume DATA values in order.", "20 READ A, B"},
    {"RESTORE", "RESTORE", "Reset the DATA read position.", "30 RESTORE"},
    {"DELAY", "DELAY milliseconds", "Wait; the shell-mode runtime yields between ticks.", "20 DELAY 100"},
    {"SLEEP", "SLEEP seconds", "Wait for the requested number of seconds.", "20 SLEEP 2"},
    {"SUB", "SUB name(parameters) ... ENDSUB", "Declare a named subroutine where supported.", "100 SUB SHOW(X)"},
    {"CALL", "CALL name(arguments)", "Invoke a named SUB where supported.", "20 CALL SHOW(42)"},
    {"LOCAL", "LOCAL variable", "Scope a variable to a SUB invocation.", "110 LOCAL TEMP"},
    {"IMPORT", "IMPORT \"/data/module\"", "Merge a stored module of SUBs into the program.", "IMPORT \"/data/helpers\""},
    {"LEN", "LEN(string)", "Return a string's length.", "20 PRINT LEN(A$)"},
    {"LEFT$", "LEFT$(string, count)", "Take characters from the start of a string.", "20 PRINT LEFT$(A$, 3)"},
    {"MID$", "MID$(string, start, count)", "Take a substring; positions start at one.", "20 PRINT MID$(A$, 2, 3)"},
    {"RIGHT$", "RIGHT$(string, count)", "Take characters from the end of a string.", "20 PRINT RIGHT$(A$, 3)"},
    {"ABS", "ABS(number)", "Absolute value.", "20 PRINT ABS(-7)"},
    {"MIN", "MIN(a, b)", "The smaller of two values.", "20 PRINT MIN(A, B)"},
    {"MAX", "MAX(a, b)", "The larger of two values.", "20 PRINT MAX(A, B)"},
    {"STR$", "STR$(number)", "Convert an integer to text.", "20 A$ = STR$(42)"},
    {"VAL", "VAL(string)", "Parse an integer from text.", "20 A = VAL(\"42\")"},
    {"LED", "LED index, value", "0: off, 1: on, other: toggle. Board support required.", "20 LED 0, 1"},
    {"PIN", "PIN port, pin, mode", "0: input; nonzero: output. Board support required.", "10 PIN 1, 0, 1"},
    {"DIGWRITE", "DIGWRITE port, pin, value", "0/1: low/high; other: toggle. Board support required.", "20 DIGWRITE 1, 0, 1"},
    {"DIGREAD", "DIGREAD(port, pin)", "Read a GPIO input. Board support required.", "20 A = DIGREAD(1, 0)"},
    {"I2CWRITE", "I2CWRITE address, register, value", "Write a register byte. Board support required.", "20 I2CWRITE 72, 1, 0"},
    {"VFSREAD", "VFSREAD(\"path\")", "Read a numeric VFS value. Firmware support required.", "20 A = VFSREAD(\"/sys/cpu/freq\")"},
    {"VFSREAD$", "VFSREAD$(\"path\")", "Read text from VFS. Firmware support required.", "20 A$ = VFSREAD$(\"/sys/name\")"},
    {"VFSWRITE", "VFSWRITE \"path\", value", "Write a numeric VFS value; node permissions apply.", "20 VFSWRITE \"/sys/cpu/freq_target\", 64000000"},
    {"VFSWRITE$", "VFSWRITE$ \"path\", string", "Write text to VFS; node permissions apply.", "20 VFSWRITE$ \"/sys/name\", \"Sensor\""},
    {"TRACE", "TRACE ON | OFF", "Print each executed line; graphical Debug adds stepping.", "TRACE ON"},
};

static int
assist_word(int ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_' || ch == '$';
}

/** Do not offer code completions inside a quoted string or comment. */
static int
assist_context(edit_t *e, char *word, size_t max)
{
    int y, x, a, b, i, pos = 0, n;
    const char *line;
    tiku_span_t spans[128];
    tiku_textview_caret(&e->tv, &y, &x);
    line = tiku_textview_line(&e->tv, y);
    n = tiku_syntax_spans_on(TIKU_SYNTAX_BASIC, line, TIKU_SYNTAX_OPEN,
                              NULL, spans, 128);
    for (i = 0; i < n; i++) {
        if (x > pos && x <= pos + spans[i].len &&
            (spans[i].ink == TIKU_INK_STR || spans[i].ink == TIKU_INK_REMARK)) {
            word[0] = '\0'; return 0;
        }
        pos += spans[i].len;
    }
    a = b = x;
    while (a > 0 && assist_word((unsigned char)line[a - 1])) { a--; }
    while (line[b] && assist_word((unsigned char)line[b])) { b++; }
    e->assist_y = y; e->assist_x0 = a; e->assist_x1 = b;
    n = e->assist_open == 2 ? x - a : b - a;
    if ((size_t)n >= max) { n = (int)max - 1; }
    memcpy(word, line + a, (size_t)n); word[n] = '\0';
    return 1;
}

static void
assist_refresh(edit_t *e)
{
    char prefix[32];
    const char *word;
    int i;
    if (!assist_context(e, prefix, sizeof prefix)) { e->assist_n = 0; return; }
    if (strcmp(prefix, e->assist_prefix)) { e->assist_at = 0; }
    snprintf(e->assist_prefix, sizeof e->assist_prefix, "%s", prefix);
    e->assist_n = 0;
    for (i = 0; (word = tiku_syntax_basic_keyword(i)) != NULL; i++) {
        if (!strncasecmp(word, prefix, strlen(prefix)) && e->assist_n < 192) {
            e->assist_match[e->assist_n++] = i;
        }
    }
    if (e->assist_at >= e->assist_n) { e->assist_at = 0; }
}

void
tiku_ide_assist_open(edit_t *e, int complete)
{
    if (e->lang != TIKU_SYNTAX_BASIC) { return; }
    e->finding = 0;
    e->assist_open = complete ? 2 : 1;
    e->assist_at = 0;
    assist_refresh(e);
}

/** Draw wrapped reference text within the assistance panel, never over buttons. */
static int
assist_text(edit_t *e, int x, int y, int width, const char *text)
{
    const tiku_font_t *f = tiku_font_plain();
    int end = SEARCH_TOP + ASSIST_H - 18;
    while (*text && y < end) {
        char line[160];
        size_t n = 0, space = 0;
        while (text[n] && n + 1 < sizeof line) {
            line[n] = text[n]; line[n + 1] = '\0';
            if (tiku_text_width(f, line) > width) { break; }
            if (text[n] == ' ') { space = n; }
            n++;
        }
        if (!n) { break; }
        if (text[n] && space) { n = space; }
        line[n] = '\0';
        tiku_text(e->surface, f, x, y + f->ascent, line, TIKU_C_TEXT);
        y += f->height + 2;
        text += n;
        while (*text == ' ') { text++; }
    }
    return y;
}

void
tiku_ide_assist_paint(edit_t *e)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t panel = { 0, SEARCH_TOP, e->w, ASSIST_H };
    const char *word;
    int i, y = SEARCH_TOP + 5, first;
    const basic_help_t *help = NULL;

    assist_refresh(e);
    tiku_fill(e->surface, panel, TIKU_C_PANEL);
    first = e->assist_at / 5 * 5;
    for (i = first; i < e->assist_n && i < first + 5; i++) {
        tiku_rect_t row = { 6, y, 118, 19 };
        word = tiku_syntax_basic_keyword(e->assist_match[i]);
        tiku_ui_button(e->surface, row, word, i == e->assist_at ? TIKU_S_PRESSED : 0);
        y += 20;
    }
    word = e->assist_n ? tiku_syntax_basic_keyword(e->assist_match[e->assist_at]) : NULL;
    for (i = 0; word && i < (int)(sizeof basic_help / sizeof basic_help[0]); i++) {
        if (!strcmp(word, basic_help[i].word)) { help = &basic_help[i]; break; }
    }
    y = SEARCH_TOP + 5;
    if (help) {
        y = assist_text(e, 134, y, e->w - 142, help->syntax);
        y = assist_text(e, 134, y + 3, e->w - 142, help->detail);
        assist_text(e, 134, y + 3, e->w - 142, help->example);
    } else {
        assist_text(e, 134, y, e->w - 142, word ?
            "BASIC keyword. Detailed help is not yet available for this word." :
            "Place the caret on a keyword, or type a prefix for completion.");
    }
    tiku_text(e->surface, f, 6, SEARCH_TOP + ASSIST_H - 4,
              e->assist_open == 2 ?
              "Arrows: choose   Enter: insert   Esc: close   Firmware features vary" :
              "Click a keyword for help   Esc: close   Firmware features vary",
              TIKU_C_TEXT);
}

void
tiku_ide_assist_accept(edit_t *e)
{
    const char *word;
    if (!e->assist_n) { return; }
    word = tiku_syntax_basic_keyword(e->assist_match[e->assist_at]);
    if (e->tv.line[e->assist_y].len - (e->assist_x1 - e->assist_x0) +
        (int)strlen(word) >= TIKU_TEXTVIEW_LINE_MAX) { return; }
    tiku_textview_group_begin(&e->tv);
    tiku_ide_edit_select_span(e, e->assist_y, e->assist_x0, e->assist_x1);
    tiku_textview_insert_text(&e->tv, word);
    tiku_textview_group_end(&e->tv);
    e->assist_open = 0;
}

int
tiku_ide_assist_event(edit_t *e, const tiku_event_t *event)
{
    if (!e->assist_open) { return 0; }
    assist_refresh(e);
    if (event->type == TIKU_EVENT_POINTER_DOWN && event->y >= SEARCH_TOP &&
        event->y < SEARCH_TOP + ASSIST_H) {
        int at = e->assist_at / 5 * 5 + (event->y - SEARCH_TOP - 5) / 20;
        if (event->x >= 6 && event->x < 124 && at >= 0 && at < e->assist_n) {
            e->assist_at = at;
            if (e->assist_open == 2) { tiku_ide_assist_accept(e); }
        }
        return 1;
    }
    if (event->type != TIKU_EVENT_KEY_DOWN) { return 0; }
    if (event->key == TIKU_KEY_ESCAPE) { e->assist_open = 0; return 1; }
    if (e->assist_open != 2) { return 0; }
    if (event->key == TIKU_KEY_RETURN || event->key == TIKU_KEY_TAB) {
        tiku_ide_assist_accept(e); return 1;
    }
    if (event->key == TIKU_KEY_UP || event->key == TIKU_KEY_DOWN) {
        int step = event->key == TIKU_KEY_UP ? -1 : 1;
        if (e->assist_n) { e->assist_at = (e->assist_at + e->assist_n + step) % e->assist_n; }
        return 1;
    }
    return 0;
}
