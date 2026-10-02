/* tiku_ide_assist.c -- assist.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_private.h"

static int
assist_word(int ch);
static int
assist_quoted(const char *text, int x);
static int
assist_context(edit_t *e, tiku_edit_range_t *word, char *prefix,
               size_t max);
static void
assist_show(edit_t *e);

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

/** @brief Whether byte @p x of a BASIC line's @p text lies inside a
 *         quoted string or a remark. */
static int
assist_quoted(const char *text, int x)
{
    tiku_span_t spans[256];
    int i, n, pos = 0;

    n = tiku_syntax_spans_on(TIKU_SYNTAX_BASIC, text, TIKU_SYNTAX_OPEN,
                             NULL, spans, (int)(sizeof spans /
                                                sizeof spans[0]));
    for (i = 0; i < n; i++) {
        if (x > pos && x <= pos + spans[i].len &&
            (spans[i].ink == TIKU_INK_STR ||
             spans[i].ink == TIKU_INK_REMARK)) {
            return 1;
        }
        pos += spans[i].len;
    }
    return 0;
}

/**
 * @brief The word around the caret: letters, digits, "_" and "$".
 *
 * Its range goes in @p word and, in @p prefix, the part before the caret
 * for completion or the whole word for help.  Inside a quoted string or
 * a remark there is no word: @return 0 there, with an empty range at the
 * caret.
 */
static int
assist_context(edit_t *e, tiku_edit_range_t *word, char *prefix,
               size_t max)
{
    tiku_edit_pos_t start = 0;
    char text[IDE_CARET_TEXT];
    size_t got = 0;
    int n, x, a, b;

    word->start = word->end = tiku_ide_caret(e, NULL, NULL);
    prefix[0] = '\0';
    x = tiku_ide_caret_text(e, text, &start, &got);
    if (x < 0 || assist_quoted(text, x)) {
        return 0;
    }
    a = b = x;
    while (a > 0 && assist_word((unsigned char)text[a - 1])) { a--; }
    while (b < (int)got && assist_word((unsigned char)text[b])) { b++; }
    word->start = start + a;
    word->end = start + b;
    n = e->assist_open == 2 ? x - a : b - a;
    if ((size_t)n >= max) { n = (int)max - 1; }
    memcpy(prefix, text + a, (size_t)n);
    prefix[n] = '\0';
    return 1;
}

/**
 * @brief Offer the keywords that start with the caret's word, as a list
 *        at the word, with the chosen one's help in a tip beside it.
 *
 * The choice goes back to the first keyword when the word changes.  In
 * completion the list puts the chosen keyword over the whole word.
 */
static void
assist_show(edit_t *e)
{
    const char *words[192], *word;
    tiku_codeview_list_t list;
    tiku_edit_range_t range;
    char prefix[32], tip[512];
    const basic_help_t *help = NULL;
    size_t used;
    int i;

    e->assist_n = 0;
    if (assist_context(e, &range, prefix, sizeof prefix)) {
        for (i = 0; (word = tiku_syntax_basic_keyword(i)) != NULL; i++) {
            if (!strncasecmp(word, prefix, strlen(prefix)) &&
                e->assist_n < 192) {
                words[e->assist_n] = word;
                e->assist_match[e->assist_n++] = i;
            }
        }
    }
    if (strcmp(prefix, e->assist_prefix)) { e->assist_at = 0; }
    snprintf(e->assist_prefix, sizeof e->assist_prefix, "%s", prefix);
    if (e->assist_at >= (size_t)e->assist_n) { e->assist_at = 0; }
    memset(&list, 0, sizeof list);
    list.range = range;
    list.items = words;
    list.count = (size_t)e->assist_n;
    list.current = e->assist_at;
    list.inserts = e->assist_open == 2;
    (void)tiku_codeview_list_show(e->view, &list);
    word = e->assist_n ? words[e->assist_at] : NULL;
    for (i = 0; word && i < (int)(sizeof basic_help / sizeof basic_help[0]);
         i++) {
        if (!strcmp(word, basic_help[i].word)) { help = &basic_help[i]; break; }
    }
    if (help) {
        used = (size_t)snprintf(tip, sizeof tip, "%s\n%s\n%s", help->syntax,
                                help->detail, help->example);
    } else {
        used = (size_t)snprintf(tip, sizeof tip, "%s", word ?
            "BASIC keyword. Detailed help is not yet available for this word." :
            "Place the caret on a keyword, or type a prefix for completion.");
    }
    if (used < sizeof tip) {
        snprintf(tip + used, sizeof tip - used, "\n%s", e->assist_open == 2 ?
                 "Arrows: choose   Enter: insert   Esc: close   "
                 "Firmware features vary" :
                 "Click a keyword for help   Esc: close   "
                 "Firmware features vary");
    }
    (void)tiku_codeview_tip_show(e->view, range.start, tip);
}

void
tiku_ide_assist_open(edit_t *e, int complete)
{
    if (e->lang != TIKU_SYNTAX_BASIC) { return; }
    e->finding = 0;
    e->assist_open = complete ? 2 : 1;
    e->assist_at = 0;
    e->assist_prefix[0] = '\0';
    assist_show(e);
}

/** @brief Put the list and the tip away. */
void
tiku_ide_assist_close(edit_t *e)
{
    e->assist_open = 0;
    if (e->view != NULL) {
        tiku_codeview_list_hide(e->view);
        tiku_codeview_tip_hide(e->view);
    }
}

/**
 * @brief Offer again for where the caret is now.
 *
 * A list the view has put away, by putting its word in or on Escape, ends
 * the assistance.
 */
void
tiku_ide_assist_sync(edit_t *e)
{
    size_t current = 0;

    if (!e->assist_open) { return; }
    if (!tiku_codeview_list_shown(e->view, &current)) {
        tiku_ide_assist_close(e);
        return;
    }
    e->assist_at = current;
    assist_show(e);
}

/** @brief Put the chosen keyword over the word, as one undo step. */
void
tiku_ide_assist_accept(edit_t *e)
{
    if (!e->assist_n) { return; }
    tiku_ide_assist_sync(e);
    if (tiku_codeview_list_insert(e->view) == TIKU_EDIT_OK) {
        tiku_ide_assist_close(e);
    }
}

/** @brief Escape, and the list's own keys in completion; presses on the
 *         list and the tip reach the view with the page's. */
int
tiku_ide_assist_event(edit_t *e, const tiku_event_t *event)
{
    if (!e->assist_open || event->type != TIKU_EVENT_KEY_DOWN) { return 0; }
    if (event->key == TIKU_KEY_ESCAPE) { tiku_ide_assist_close(e); return 1; }
    if (e->assist_open != 2 || (event->modifiers & TIKU_MOD_CMD)) { return 0; }
    if (event->key == TIKU_KEY_UP || event->key == TIKU_KEY_DOWN ||
        event->key == TIKU_KEY_RETURN || event->key == TIKU_KEY_TAB) {
        tiku_ide_assist_sync(e);
        (void)tiku_codeview_event(e->view, event);
        tiku_ide_assist_sync(e);
        return 1;
    }
    return 0;
}
